#include "mecs_can.h"
#include "fake_idf.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct fake_queue {
    unsigned length, size, head, count;
    uint8_t data[];
};
static unsigned allocations, logs, recoveries;
static int64_t now_us;
static bool reject_tx, reject_create;
static twai_event_callbacks_t callbacks;
static twai_node_status_t status;
static const twai_frame_t *in_flight;
static twai_frame_t incoming;

QueueHandle_t xQueueCreate(unsigned length, unsigned size) {
    QueueHandle_t q = calloc(1, sizeof(*q) + length * size);
    assert(q);
    q->length = length;
    q->size = size;
    ++allocations;
    return q;
}
int xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *wake) {
    (void)wake;
    if (q->count == q->length) return pdFALSE;
    if (q->size) memcpy(q->data + ((q->head + q->count) % q->length) * q->size, item, q->size);
    ++q->count;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *item, unsigned ticks) {
    assert(ticks == 0);
    if (!q->count) return pdFALSE;
    if (q->size) memcpy(item, q->data + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->length;
    --q->count;
    return pdTRUE;
}
void xQueueReset(QueueHandle_t q) { q->head = q->count = 0; }
void vQueueDelete(QueueHandle_t q) { free(q); --allocations; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xQueueCreate(1, 0); }
int xSemaphoreGive(SemaphoreHandle_t s) { return xQueueSendFromISR(s, NULL, NULL); }
int xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *wake) { return xQueueSendFromISR(s, NULL, wake); }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned ticks) { return xQueueReceive(s, NULL, ticks); }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "fake"; }
int64_t esp_timer_get_time(void) { return now_us; }
void fake_log(const char *tag, const char *format, ...) { (void)tag; (void)format; ++logs; }
esp_err_t twai_new_node_onchip(const twai_onchip_node_config_t *config, twai_node_handle_t *node) {
    assert(config->fail_retry_cnt == 0 && config->tx_queue_depth == 1);
    if (reject_create) return ESP_ERR_NO_MEM;
    *node = &status;
    return ESP_OK;
}
esp_err_t twai_node_register_event_callbacks(twai_node_handle_t n, const twai_event_callbacks_t *c, void *context) {
    (void)n; (void)context; callbacks = *c; return ESP_OK;
}
esp_err_t twai_node_config_mask_filter(twai_node_handle_t n, unsigned index, const twai_mask_filter_config_t *filter) {
    (void)n; (void)index; assert(!filter->is_ext); return ESP_OK;
}
esp_err_t twai_node_enable(twai_node_handle_t n) { (void)n; return ESP_OK; }
esp_err_t twai_node_disable(twai_node_handle_t n) { (void)n; return ESP_OK; }
esp_err_t twai_node_delete(twai_node_handle_t n) { (void)n; in_flight = NULL; return ESP_OK; }
esp_err_t twai_node_transmit(twai_node_handle_t n, const twai_frame_t *frame, int timeout) {
    (void)n; assert(timeout == 0 && !in_flight);
    if (reject_tx) return ESP_ERR_INVALID_STATE;
    in_flight = frame; return ESP_OK;
}
esp_err_t twai_node_receive_from_isr(twai_node_handle_t n, twai_frame_t *frame) {
    (void)n;
    frame->header = incoming.header;
    const size_t count = incoming.buffer_len < frame->buffer_len ? incoming.buffer_len : frame->buffer_len;
    memcpy(frame->buffer, incoming.buffer, count);
    return ESP_OK;
}
esp_err_t twai_node_get_info(twai_node_handle_t n, twai_node_status_t *out, void *record) {
    (void)n; (void)record; *out = status; return ESP_OK;
}
esp_err_t twai_node_recover(twai_node_handle_t n) { (void)n; ++recoveries; return ESP_OK; }
static void complete(bool success) {
    const twai_tx_done_event_data_t event = {.is_tx_success = success};
    assert(in_flight);
    in_flight = NULL;
    callbacks.on_tx_done(&status, &event, NULL);
}
static void receive(void) { callbacks.on_rx_done(&status, NULL, NULL); }

int main(void) {
    assert(mecs_can_start(4, 4) == ESP_ERR_INVALID_ARG);
    reject_create = true;
    assert(mecs_can_start(4, 5) == ESP_ERR_NO_MEM && allocations == 0);
    reject_create = false;
    assert(mecs_can_start(4, 5) == ESP_OK);
    assert(mecs_can_start(4, 5) == ESP_ERR_INVALID_STATE);
    mecs_frame_t frame = {.id = 0x201, .length = 8, .data = {1,2,3}};
    assert(mecs_can_send(NULL, &frame));
    frame.data[0] = 99;
    assert(in_flight->buffer[0] == 1); /* Caller may reuse stack immediately. */
    assert(!mecs_can_send(NULL, &frame));
    assert(in_flight->buffer[0] == 1); /* Busy send must not overwrite TX. */
    complete(false);
    reject_tx = true;
    assert(!mecs_can_send(NULL, &frame));
    reject_tx = false;
    assert(mecs_can_send(NULL, &frame)); /* Rejection returned the token. */
    complete(true);

    incoming = (twai_frame_t){.header = {.id = 0x080, .dlc = 1},
                              .buffer = frame.data, .buffer_len = 1};
    for (unsigned i = 0; i < 40; ++i) receive();
    mecs_can_stats_t stats;
    mecs_can_get_stats(&stats);
    assert(stats.rx_received == 40 && stats.rx_dropped == 8);
    for (unsigned i = 0; i < 32; ++i) assert(mecs_can_receive(&frame));
    assert(!mecs_can_receive(&frame));
    incoming.header.fdf = true;
    receive();
    assert(!mecs_can_receive(&frame)); /* Short FD frame is not Classic CAN. */
    incoming.header.fdf = false;
    receive();
    now_us += 100000;
    assert(!mecs_can_receive(&frame)); /* Do not renew leases from old RX. */
    mecs_can_get_stats(&stats);
    assert(stats.rx_stale == 1 && stats.rx_rejected == 1);

    const twai_error_event_data_t error = {.err_flags = {.val = 16}};
    unsigned before = logs;
    for (unsigned i = 0; i < 100; ++i) {
        callbacks.on_error(&status, &error, NULL);
        assert(!mecs_can_poll());
    }
    assert(logs == before); /* Fault storm does not flood the console. */
    now_us = 1000000;
    assert(!mecs_can_poll() && logs == before + 2);
    assert(!mecs_can_poll() && logs == before + 2);

    assert(mecs_can_send(NULL, &frame));
    status.state = TWAI_ERROR_BUS_OFF;
    assert(!mecs_can_poll());
    assert(!mecs_can_poll() && recoveries == 1);
    assert(!mecs_can_send(NULL, &frame));
    receive();
    assert(!mecs_can_receive(&frame)); /* Quarantine RX during recovery. */
    status.state = TWAI_ERROR_ACTIVE;
    /* Recovery must delete the old driver before reclaiming an aborted TX. */
    reject_create = true;
    assert(!mecs_can_poll());
    assert(!mecs_can_send(NULL, &frame));
    reject_create = false;
    assert(!mecs_can_poll()); /* Restart failures are rate limited. */
    now_us += 1000000;
    assert(mecs_can_poll());
    assert(!mecs_can_receive(&frame));
    assert(mecs_can_send(NULL, &frame));
    complete(true);
    assert(!mecs_can_poll());
    mecs_can_get_stats(&stats);
    assert(stats.recoveries == 1 && stats.bus_errors == 100);
    return 0;
}
