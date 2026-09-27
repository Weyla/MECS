/* ESP-IDF transport adapter for the portable MIO frame type.
 * TWAI is Espressif's name for its CAN controller. This file moves frames and
 * handles controller faults; it does not interpret discovery message payloads.
 *
 * Receive: hardware -> ISR callback -> copied queue item -> owner task.
 * Transmit: owner task -> persistent buffer -> hardware -> completion callback.
 * All public calls belong to one owner task. Only the two driver callbacks run
 * in interrupt context; they use FreeRTOS FromISR APIs and never log or block.
 */
#include "mio_can.h"
#include "esp_log.h"
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
#error "The MIO example transport does not support cache-safe TWAI interrupts."
#endif

static const char *TAG = "mio_can";
/* One controller instance for the lifetime of this example. These resources
 * are allocated before enabling interrupts and shared with the ISR callbacks. */
static twai_node_handle_t controller;
static QueueHandle_t rx_queue;
static QueueHandle_t fault_queue;
/* A binary semaphore represents ownership of the single transmit buffer:
 * available = the owner task may write it; taken = the driver still owns it.
 * A mutex cannot be used here because completion releases the token in an ISR. */
static SemaphoreHandle_t tx_available;
static twai_frame_t tx_frame;
static uint8_t tx_data[8];
/* Recovery bookkeeping is accessed only from the owner task. */
static bool recovering;
static twai_error_state_t previous_state = TWAI_ERROR_ACTIVE;

enum { FAULT_RX_FULL = 1, FAULT_TX_FAILED = 2 };

/* Called by the driver when a received hardware frame is available. The
 * driver's receive_from_isr API is only legal inside this RX callback. */
static bool on_rx(twai_node_handle_t node, const twai_rx_done_event_data_t *event, void *context)
{
    (void)event;
    (void)context;
    /* Temporary storage is sufficient here: xQueueSendFromISR copies the
     * complete mio_frame_t, including its inline payload, before we return. */
    mio_frame_t frame = {0};
    twai_frame_t received = {.buffer = frame.data, .buffer_len = sizeof(frame.data)};
    BaseType_t wake = pdFALSE;
    if (twai_node_receive_from_isr(node, &received) == ESP_OK) {
        frame.id = received.header.id;
        /* For Classic CAN, DLC 0..8 is the payload byte count. Preserve DLC
         * for the core to reject lengths that do not match its message format. */
        frame.length = received.header.dlc;
        frame.extended = received.header.ide;
        frame.remote = received.header.rtr;
        if (xQueueSendFromISR(rx_queue, &frame, &wake) != pdTRUE) {
            /* Drop rather than block inside an interrupt. Fault reporting is
             * best-effort too: a full diagnostic queue can discard this notice. */
            const uint8_t fault = FAULT_RX_FULL;
            xQueueSendFromISR(fault_queue, &fault, &wake);
        }
    }
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
    if (!event->is_tx_success) {
        const uint8_t fault = FAULT_TX_FAILED;
        xQueueSendFromISR(fault_queue, &fault, &wake);
    }
    /* The frame and its buffer may only be reused after this callback. */
    xSemaphoreGiveFromISR(tx_available, &wake);
    return wake == pdTRUE;
}

/* Allocate the queues and driver, register callbacks, then enable CAN.
 * No interrupt may use a queue before that queue has been created. */
esp_err_t mio_can_start(int tx_gpio, int rx_gpio)
{
    if (controller) {
        return ESP_ERR_INVALID_STATE;
    }
    if (tx_gpio == rx_gpio) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Fixed queue capacities bound RAM use. The RX queue absorbs short bursts;
     * it is not an unlimited backlog if the application cannot keep up. */
    rx_queue = xQueueCreate(32, sizeof(mio_frame_t));
    fault_queue = xQueueCreate(8, sizeof(uint8_t));
    tx_available = xSemaphoreCreateBinary();
    esp_err_t error = ESP_ERR_NO_MEM;
    if (!rx_queue || !fault_queue || !tx_available) {
        goto fail;
    }
    /* Binary semaphores start empty; initially no transmission owns the slot. */
    xSemaphoreGive(tx_available);
    /* -1 disables the optional clock-output and bus-off-indicator GPIOs.
     * Self-test/listen-only/loopback flags remain zero, so normal CAN ACK and
     * arbitration rules apply. The configured bitrate is bits per second. */
    const twai_onchip_node_config_t config = {
        .io_cfg = {.tx = tx_gpio, .rx = rx_gpio, .quanta_clk_out = -1, .bus_off_indicator = -1},
        .bit_timing = {.bitrate = 500000},
        /* C3 supports single-shot or unlimited retries. Keep arbitration retries. */
        .fail_retry_cnt = -1,
        .tx_queue_depth = 1,
    };
    error = twai_new_node_onchip(&config, &controller);
    if (error != ESP_OK) {
        goto fail;
    }
    const twai_event_callbacks_t callbacks = {.on_rx_done = on_rx, .on_tx_done = on_tx};
    error = twai_node_register_event_callbacks(controller, &callbacks, NULL);
    if (error != ESP_OK) {
        goto fail;
    }
    /* A zero mask accepts every standard identifier. The portable core then
     * validates the protocol IDs and frame type. Keeping this filter broad
     * avoids duplicating the protocol's identifier map in the transport. */
    const twai_mask_filter_config_t filter = {.id = 0, .mask = 0, .is_ext = false};
    error = twai_node_config_mask_filter(controller, 0, &filter);
    if (error != ESP_OK) {
        goto fail;
    }
    error = twai_node_enable(controller);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "CAN started: 500000 bit/s, TX GPIO%d, RX GPIO%d", tx_gpio, rx_gpio);
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
    if (fault_queue) {
        vQueueDelete(fault_queue);
        fault_queue = NULL;
    }
    if (tx_available) {
        vSemaphoreDelete(tx_available);
        tx_available = NULL;
    }
    return error;
}

/* Send callback supplied to mio_init(). Copy the caller's temporary frame
 * into persistent storage because the TWAI driver transmits asynchronously.
 * True means submitted; completion or a CAN fault may happen later. */
bool mio_can_send(void *context, const mio_frame_t *frame)
{
    (void)context;
    /* Never wait for buffer ownership: local I/O sampling must keep running. A missing peer
     * can leave an earlier frame retransmitting indefinitely; never overwrite
     * that frame just because the application wants to send another one. */
    if (!controller || !frame || frame->length > 8 || frame->extended || frame->remote ||
        frame->id > 0x7ff || xSemaphoreTake(tx_available, 0) != pdTRUE) {
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
        ESP_LOGW(TAG, "TX rejected: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

/* Nonblocking task-side receive. FreeRTOS copies the next queued frame into
 * caller storage; false means no frame is available (or arguments are invalid).
 * The application, not this adapter, passes it to mio_receive() for decoding. */
bool mio_can_receive(mio_frame_t *frame)
{
    return rx_queue && frame && xQueueReceive(rx_queue, frame, 0) == pdTRUE;
}

/* Service diagnostics and recovery from the owner task on each loop pass.
 * Return true only when a recovery we started has reached error-active state,
 * allowing the application to request discovery or announce itself again. */
bool mio_can_poll(void)
{
    if (!controller) {
        return false;
    }
    uint8_t fault;
    /* Coalesce at most eight notices per call into one log per fault type.
     * This keeps work bounded even if more interrupt events arrive while we
     * drain the queue. Notices are not an exact cumulative error counter. */
    bool rx_full = false, tx_failed = false;
    for (unsigned i = 0; i < 8 && xQueueReceive(fault_queue, &fault, 0) == pdTRUE; ++i) {
        rx_full |= fault == FAULT_RX_FULL;
        tx_failed |= fault == FAULT_TX_FAILED;
    }
    if (rx_full) {
        ESP_LOGW(TAG, "RX queue overflow; discovery may need repeating");
    }
    if (tx_failed) {
        ESP_LOGW(TAG, "CAN TX failed; check peer power, bitrate and wiring");
    }
    twai_node_status_t status;
    if (twai_node_get_info(controller, &status, NULL) != ESP_OK) {
        return false;
    }
    /* Log transitions rather than every poll of the same error state. */
    if (status.state != previous_state) {
        ESP_LOGW(TAG, "CAN error state=%d, TX errors=%u, RX errors=%u", (int)status.state,
                 status.tx_error_count, status.rx_error_count);
        previous_state = status.state;
    }
    /* Bus-off means the controller stopped participating after excessive
     * transmit errors. Start the driver's asynchronous bus recovery once;
     * repeated polling must not repeatedly initiate the same recovery. */
    if (status.state == TWAI_ERROR_BUS_OFF && !recovering) {
        ESP_LOGW(TAG, "Bus off; starting recovery");
        recovering = twai_node_recover(controller) == ESP_OK;
    } else if (recovering && status.state == TWAI_ERROR_ACTIVE) {
        /* Controller recovery is not application discovery. The caller uses
         * our true result to submit the appropriate protocol message. */
        recovering = false;
        ESP_LOGI(TAG, "CAN recovered");
        return true;
    }
    return false;
}
