#pragma once
/* Minimal host model of the API boundary used by mecs_can.c. This tests the
 * adapter's ownership and queue policy, not the real TWAI driver or scheduler. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, ESP_ERR_TIMEOUT };
const char *esp_err_to_name(esp_err_t error);
int64_t esp_timer_get_time(void);
void fake_log(const char *tag, const char *format, ...);
#define ESP_LOGI fake_log
#define ESP_LOGW fake_log
#define ESP_LOGE fake_log
#define ESP_LOGD(...) ((void)0)
#define CONFIG_TWAI_ISR_CACHE_SAFE 0

typedef int BaseType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define portENTER_CRITICAL_ISR(lock) ((void)(lock))
#define portEXIT_CRITICAL_ISR(lock) ((void)(lock))
enum { pdFALSE, pdTRUE };
typedef struct fake_queue *QueueHandle_t;
typedef QueueHandle_t SemaphoreHandle_t;
QueueHandle_t xQueueCreate(unsigned length, unsigned size);
int xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *wake);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned ticks);
void xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
int xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *wake);
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned ticks);
#define vSemaphoreDelete vQueueDelete

typedef void *twai_node_handle_t;
typedef enum { TWAI_ERROR_ACTIVE, TWAI_ERROR_WARNING, TWAI_ERROR_PASSIVE, TWAI_ERROR_BUS_OFF } twai_error_state_t;
typedef struct {
    uint32_t id;
    uint16_t dlc;
    bool ide, rtr, fdf;
} twai_frame_header_t;
typedef struct {
    twai_frame_header_t header;
    uint8_t *buffer;
    size_t buffer_len;
} twai_frame_t;
typedef struct { bool is_tx_success; } twai_tx_done_event_data_t;
typedef struct { int unused; } twai_rx_done_event_data_t;
typedef union {
    struct { unsigned arb_lost:1, bit_err:1, form_err:1, stuff_err:1, ack_err:1; };
    uint32_t val;
} twai_error_flags_t;
typedef struct { twai_error_flags_t err_flags; } twai_error_event_data_t;
typedef struct {
    bool (*on_rx_done)(twai_node_handle_t, const twai_rx_done_event_data_t *, void *);
    bool (*on_tx_done)(twai_node_handle_t, const twai_tx_done_event_data_t *, void *);
    bool (*on_error)(twai_node_handle_t, const twai_error_event_data_t *, void *);
} twai_event_callbacks_t;
typedef struct {
    struct { int tx, rx, quanta_clk_out, bus_off_indicator; } io_cfg;
    struct { uint32_t bitrate; } bit_timing;
    int fail_retry_cnt, tx_queue_depth;
} twai_onchip_node_config_t;
typedef struct { uint32_t id, mask; bool is_ext; } twai_mask_filter_config_t;
typedef struct { twai_error_state_t state; unsigned tx_error_count, rx_error_count; } twai_node_status_t;
esp_err_t twai_new_node_onchip(const twai_onchip_node_config_t *, twai_node_handle_t *);
esp_err_t twai_node_register_event_callbacks(twai_node_handle_t, const twai_event_callbacks_t *, void *);
esp_err_t twai_node_config_mask_filter(twai_node_handle_t, unsigned, const twai_mask_filter_config_t *);
esp_err_t twai_node_enable(twai_node_handle_t);
esp_err_t twai_node_delete(twai_node_handle_t);
esp_err_t twai_node_transmit(twai_node_handle_t, const twai_frame_t *, int);
esp_err_t twai_node_receive_from_isr(twai_node_handle_t, twai_frame_t *);
esp_err_t twai_node_get_info(twai_node_handle_t, twai_node_status_t *, void *);
esp_err_t twai_node_recover(twai_node_handle_t);
esp_err_t twai_node_disable(twai_node_handle_t);
