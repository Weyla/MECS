/* ESP-IDF transport adapter for the portable MECS frame type.
 * TWAI is Espressif's name for its CAN controller. This file moves frames and
 * handles controller faults; it does not interpret discovery message payloads.
 *
 * Receive: hardware -> ISR callback -> copied queue item -> owner task.
 * Transmit: owner task -> persistent buffer -> hardware -> completion callback.
 * All public calls belong to one owner task. Only the driver callbacks run
 * in interrupt context; they use FreeRTOS FromISR APIs and never log or block.
 */
#include "mecs_can.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include "sdkconfig.h"

/* Cache-safe interrupts require an additional audit of code/data placement
 * in internal RAM. Reject that configuration rather than imply it is supported. */
#if CONFIG_TWAI_ISR_CACHE_SAFE
#error "The MECS example transport does not support cache-safe TWAI interrupts."
#endif

static const char *TAG = "mecs_can";
/* One controller at a time. Queues exist before interrupts are enabled and
 * remain allocated while a recovered controller is recreated. */
static twai_node_handle_t controller;
static QueueHandle_t rx_queue;
enum { RX_QUEUE_LENGTH = 32, RX_MAX_AGE_MS = 100, DIAGNOSTIC_INTERVAL_MS = 1000 };
typedef struct {
    mecs_frame_t frame;
    uint32_t received_ms;
} rx_item_t;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static mecs_can_stats_t stats;
static uint32_t last_diagnostic_ms;
static mecs_can_stats_t reported;
static esp_err_t last_tx_error;
static uint32_t error_flags;

static uint32_t milliseconds(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void mecs_can_get_stats(mecs_can_stats_t *out)
{
    if (out) {
        portENTER_CRITICAL(&stats_lock);
        *out = stats;
        portEXIT_CRITICAL(&stats_lock);
    }
}
/* A binary semaphore represents ownership of the single transmit buffer:
 * available = the owner task may write it; taken = the driver still owns it.
 * A mutex cannot be used here because completion releases the token in an ISR. */
static SemaphoreHandle_t tx_available;
static twai_frame_t tx_frame;
static uint8_t tx_data[8];
/* Recovery bookkeeping is accessed only from the owner task. */
static bool recovering;
static bool rebuilding;
static bool controller_enabled;
static twai_onchip_node_config_t controller_config;
static uint32_t next_recovery_attempt_ms;
static bool recovery_attempted;
static twai_error_state_t previous_state = TWAI_ERROR_ACTIVE;

static bool on_error(twai_node_handle_t node, const twai_error_event_data_t *event,
                     void *context)
{
    (void)node;
    (void)context;
    portENTER_CRITICAL_ISR(&stats_lock);
    ++stats.bus_errors;
    error_flags |= event->err_flags.val;
    portEXIT_CRITICAL_ISR(&stats_lock);
    return false;
}

/* Called by the driver when a received hardware frame is available. The
 * driver's receive_from_isr API is only legal inside this RX callback. */
static bool on_rx(twai_node_handle_t node, const twai_rx_done_event_data_t *event, void *context)
{
    (void)event;
    (void)context;
    /* Temporary storage is sufficient here: xQueueSendFromISR copies the
     * complete mecs_frame_t, including its inline payload, before we return. */
    rx_item_t item = {.received_ms = milliseconds()};
    twai_frame_t received = {.buffer = item.frame.data,
                            .buffer_len = sizeof(item.frame.data)};
    BaseType_t wake = pdFALSE;
    const esp_err_t error = twai_node_receive_from_isr(node, &received);
    if (error != ESP_OK || received.header.ide || received.header.rtr ||
        received.header.fdf || received.header.dlc > 8) {
        portENTER_CRITICAL_ISR(&stats_lock);
        ++stats.rx_rejected;
        portEXIT_CRITICAL_ISR(&stats_lock);
        return false;
    }
    item.frame.id = received.header.id;
    item.frame.length = received.header.dlc;
    const bool queued = xQueueSendFromISR(rx_queue, &item, &wake) == pdTRUE;
    portENTER_CRITICAL_ISR(&stats_lock);
    ++stats.rx_received;
    if (!queued) ++stats.rx_dropped;
    portEXIT_CRITICAL_ISR(&stats_lock);
    /* Tell the driver whether a queue operation woke a higher-priority task
     * so it can request a scheduler switch when leaving the interrupt. */
    return wake == pdTRUE;
}

/* Completion means the driver no longer needs this submission's buffer,
 * whether transmission succeeded or failed. A CAN success is link-level ACK,
 * not proof that the master's or node's application processed the message. */
static bool on_tx(twai_node_handle_t node, const twai_tx_done_event_data_t *event, void *context)
{
    (void)node;
    (void)context;
    BaseType_t wake = pdFALSE;
    portENTER_CRITICAL_ISR(&stats_lock);
    if (event->is_tx_success) ++stats.tx_completed;
    else ++stats.tx_failed;
    portEXIT_CRITICAL_ISR(&stats_lock);
    /* The frame and its buffer may only be reused after this callback. */
    xSemaphoreGiveFromISR(tx_available, &wake);
    return wake == pdTRUE;
}

/* Reused after recovery: queues and diagnostics live for the whole adapter
 * lifetime; the controller alone owns references to submitted TX storage. */
static esp_err_t create_controller(void)
{
    esp_err_t error;
    error = twai_new_node_onchip(&controller_config, &controller);
    if (error != ESP_OK) {
        goto cleanup;
    }
    const twai_event_callbacks_t callbacks = {
        .on_rx_done = on_rx, .on_tx_done = on_tx, .on_error = on_error};
    error = twai_node_register_event_callbacks(controller, &callbacks, NULL);
    if (error != ESP_OK) {
        goto cleanup;
    }
    /* A zero mask accepts every standard identifier. The portable core then
     * validates the protocol IDs and frame type. Keeping this filter broad
     * avoids duplicating the protocol's identifier map in the transport. */
    const twai_mask_filter_config_t filter = {.id = 0, .mask = 0, .is_ext = false};
    error = twai_node_config_mask_filter(controller, 0, &filter);
    if (error != ESP_OK) {
        goto cleanup;
    }
    error = twai_node_enable(controller);
    if (error == ESP_OK) {
        controller_enabled = true;
        return ESP_OK;
    }
cleanup:
    if (controller) {
        twai_node_delete(controller);
        controller = NULL;
    }
    return error;
}

/* Allocate the queues and driver, register callbacks, then enable CAN.
 * No interrupt may use a queue before that queue has been created. */
esp_err_t mecs_can_start(int tx_gpio, int rx_gpio)
{
    return mecs_can_start_with_bitrate(tx_gpio, rx_gpio, 500000);
}

esp_err_t mecs_can_start_with_bitrate(int tx_gpio, int rx_gpio, uint32_t bitrate)
{
    if (rx_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (tx_gpio == rx_gpio || !bitrate) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Fixed queue capacities bound RAM use. The RX queue absorbs short bursts;
     * it is not an unlimited backlog if the application cannot keep up. */
    rx_queue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(rx_item_t));
    tx_available = xSemaphoreCreateBinary();
    esp_err_t error = ESP_ERR_NO_MEM;
    if (!rx_queue || !tx_available) {
        goto fail;
    }
    /* Binary semaphores start empty; initially no transmission owns the slot. */
    xSemaphoreGive(tx_available);
    controller_config = (twai_onchip_node_config_t){
        .io_cfg = {.tx = tx_gpio, .rx = rx_gpio, .quanta_clk_out = -1, .bus_off_indicator = -1},
        .bit_timing = {.bitrate = bitrate},
        /* Protocol retries own the deadline; no unlimited hardware retries. */
        .fail_retry_cnt = 0,
        .tx_queue_depth = 1,
    };
    error = create_controller();
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "CAN started: %lu bit/s, TX GPIO%d, RX GPIO%d",
                 (unsigned long)bitrate, tx_gpio, rx_gpio);
        return ESP_OK;
    }
fail:
    /* Unwind partial initialization in reverse dependency order: remove the
     * controller before deleting queues its callbacks would otherwise use. */
    if (controller) {
        twai_node_delete(controller);
        controller = NULL;
    }
    if (rx_queue) {
        vQueueDelete(rx_queue);
        rx_queue = NULL;
    }
    if (tx_available) {
        vSemaphoreDelete(tx_available);
        tx_available = NULL;
    }
    return error;
}

/* Send callback supplied to mecs_init(). Copy the caller's temporary frame
 * into persistent storage because the TWAI driver transmits asynchronously.
 * True means submitted; completion or a CAN fault may happen later. */
bool mecs_can_send(void *context, const mecs_frame_t *frame)
{
    (void)context;
    /* Never wait for buffer ownership: local I/O sampling must keep running.
     * Even single-shot transmission is asynchronous; completion must release
     * its buffer before the next caller may write it. */
    if (!controller || !frame || frame->length > 8 || frame->extended || frame->remote ||
        frame->id > 0x7ff || recovering || previous_state == TWAI_ERROR_BUS_OFF) {
        return false;
    }
    if (xSemaphoreTake(tx_available, 0) != pdTRUE) {
        ++stats.tx_busy;
        return false;
    }
    memcpy(tx_data, frame->data, frame->length);
    tx_frame = (twai_frame_t){
        .header = {.id = frame->id, .dlc = frame->length},
        .buffer = tx_data,
        .buffer_len = frame->length,
    };
    /* We already hold the only application TX slot. Do not additionally wait
     * for driver queue space. On success only on_tx() returns buffer ownership. */
    esp_err_t error = twai_node_transmit(controller, &tx_frame, 0);
    if (error != ESP_OK) {
        /* Rejected submissions produce no completion for this frame, so we
         * must return the token ourselves or all later sends would stay busy. */
        xSemaphoreGive(tx_available);
        ++stats.tx_rejected;
        last_tx_error = error;
        return false;
    }
    ++stats.tx_submitted;
    ESP_LOGD(TAG, "TX id=0x%03lx dlc=%u", (unsigned long)frame->id, frame->length);
    return true;
}

/* Nonblocking task-side receive. FreeRTOS copies the next queued frame into
 * caller storage; false means no frame is available (or arguments are invalid).
 * The application, not this adapter, passes it to mecs_receive() for decoding. */
bool mecs_can_receive(mecs_frame_t *frame)
{
    if (!rx_queue || !frame || recovering || rebuilding) return false;
    rx_item_t item;
    /* Bound draining even when a busy bus continuously fills the queue. Old
     * heartbeats must not renew a lease after a stalled owner task resumes. */
    for (unsigned i = 0; i < RX_QUEUE_LENGTH; ++i) {
        if (xQueueReceive(rx_queue, &item, 0) != pdTRUE) return false;
        if ((uint32_t)(milliseconds() - item.received_ms) >= RX_MAX_AGE_MS) {
            ++stats.rx_stale;
            continue;
        }
        *frame = item.frame;
        ESP_LOGD(TAG, "RX id=0x%03lx dlc=%u", (unsigned long)frame->id, frame->length);
        return true;
    }
    return false;
}

/* After bus recovery, remove every old driver reference before reusing TX.
 * ESP-IDF 5.5 has no public idle/cancel API. Disable/delete/recreate works on
 * that release too, including an aborted TX with no completion callback. */
static bool rebuild_controller(uint32_t now)
{
    if ((int32_t)(now - next_recovery_attempt_ms) < 0) return false;
    next_recovery_attempt_ms = now + DIAGNOSTIC_INTERVAL_MS;
    stats.state = TWAI_ERROR_BUS_OFF;
    esp_err_t error = ESP_OK;
    if (controller_enabled) {
        error = twai_node_disable(controller);
        if (error != ESP_OK) goto failed;
        controller_enabled = false;
    }
    if (controller) {
        error = twai_node_delete(controller);
        if (error != ESP_OK) goto failed;
        controller = NULL;
    }
    xQueueReset(rx_queue);
    error = create_controller();
    if (error != ESP_OK) goto failed;
    xSemaphoreGive(tx_available);
    recovering = rebuilding = recovery_attempted = false;
    previous_state = TWAI_ERROR_ACTIVE;
    stats.state = TWAI_ERROR_ACTIVE;
    stats.tx_error_count = stats.rx_error_count = 0;
    ++stats.recoveries;
    ESP_LOGI(TAG, "CAN recovered; count=%lu", (unsigned long)stats.recoveries);
    return true;
failed:
    ESP_LOGE(TAG, "Controller restart failed: %s; retry in 1 s", esp_err_to_name(error));
    return false;
}

static void log_diagnostics(uint32_t now)
{
    if ((uint32_t)(now - last_diagnostic_ms) >= DIAGNOSTIC_INTERVAL_MS) {
        mecs_can_stats_t current;
        mecs_can_get_stats(&current);
        portENTER_CRITICAL(&stats_lock);
        const twai_error_flags_t errors = {.val = error_flags};
        error_flags = 0;
        portEXIT_CRITICAL(&stats_lock);
        if (current.rx_dropped != reported.rx_dropped ||
            current.rx_stale != reported.rx_stale ||
            current.rx_rejected != reported.rx_rejected ||
            current.tx_failed != reported.tx_failed ||
            current.tx_rejected != reported.tx_rejected || (errors.val & ~1u)) {
            ESP_LOGW(TAG, "CAN totals: rx=%lu dropped=%lu stale=%lu invalid=%lu "
                     "tx=%lu failed=%lu rejected=%lu bus_errors=%lu; last_submit=%s",
                     (unsigned long)current.rx_received, (unsigned long)current.rx_dropped,
                     (unsigned long)current.rx_stale, (unsigned long)current.rx_rejected,
                     (unsigned long)current.tx_completed, (unsigned long)current.tx_failed,
                     (unsigned long)current.tx_rejected, (unsigned long)current.bus_errors,
                     esp_err_to_name(last_tx_error));
            ESP_LOGW(TAG, "CAN interval: ack=%u bit=%u form=%u stuff=%u arbitration=%u",
                     errors.ack_err, errors.bit_err, errors.form_err,
                     errors.stuff_err, errors.arb_lost);
        }
        reported = current;
        last_diagnostic_ms = now;
    }
}

/* Service diagnostics and recovery from the owner task on each loop pass.
 * Return true only when a recovery we started has reached error-active state,
 * allowing the application to request discovery or announce itself again. */
bool mecs_can_poll(void)
{
    const uint32_t now = milliseconds();
    if (rebuilding) return rebuild_controller(now);
    if (!controller) return false;
    log_diagnostics(now);
    twai_node_status_t status;
    if (twai_node_get_info(controller, &status, NULL) != ESP_OK) {
        return false;
    }
    stats.state = status.state;
    stats.tx_error_count = status.tx_error_count;
    stats.rx_error_count = status.rx_error_count;
    /* Log transitions rather than every poll of the same error state. */
    if (status.state != previous_state) {
        ESP_LOGW(TAG, "CAN error state=%d, TX errors=%u, RX errors=%u", (int)status.state,
                 status.tx_error_count, status.rx_error_count);
        previous_state = status.state;
    }
    /* Bus-off means the controller stopped participating after excessive
     * transmit errors. Start the driver's asynchronous bus recovery once;
     * repeated polling must not repeatedly initiate the same recovery. */
    if (status.state == TWAI_ERROR_BUS_OFF && !recovering &&
        (!recovery_attempted || (int32_t)(now - next_recovery_attempt_ms) >= 0)) {
        recovery_attempted = true;
        next_recovery_attempt_ms = now + DIAGNOSTIC_INTERVAL_MS;
        ESP_LOGW(TAG, "Bus off; starting recovery");
        const esp_err_t error = twai_node_recover(controller);
        recovering = error == ESP_OK;
        if (!recovering) ESP_LOGE(TAG, "Recovery failed: %s", esp_err_to_name(error));
    } else if (recovering && status.state == TWAI_ERROR_ACTIVE) {
        rebuilding = true;
        next_recovery_attempt_ms = now;
        return rebuild_controller(now);
    }
    return false;
}
