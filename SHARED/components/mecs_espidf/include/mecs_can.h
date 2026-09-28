#pragma once
#include "esp_err.h"
#include "mecs_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cumulative counters wrap at UINT32_MAX. Busy means normal backpressure;
 * submitted means accepted by the driver, completed means a link-level ACK.
 * Snapshot from the same owner task as send/receive/poll; never from an ISR. */
typedef struct {
    uint32_t rx_received, rx_dropped, rx_rejected, rx_stale;
    uint32_t tx_submitted, tx_completed, tx_failed, tx_rejected, tx_busy;
    uint32_t bus_errors, recoveries;
    /* Controller state: 0 active, 1 warning, 2 passive, 3 bus off. */
    uint32_t state, tx_error_count, rx_error_count;
} mecs_can_stats_t;
void mecs_can_get_stats(mecs_can_stats_t *out);

/* Single controller adapter; call start once, then use from one owner task.
 * RX is copied from ISR into a bounded queue. TX storage remains owned by the
 * driver until its completion callback, even if no peer acknowledges it. */
esp_err_t mecs_can_start(int tx_gpio, int rx_gpio);
/* Start the same single-controller transport at an explicit bitrate. */
esp_err_t mecs_can_start_with_bitrate(int tx_gpio, int rx_gpio,
                                      uint32_t bitrate);
bool mecs_can_send(void *context, const mecs_frame_t *frame);
bool mecs_can_receive(mecs_frame_t *frame);
/* Poll from the owner task. Rate-limited fault logs; true after recovery and
 * controller recreation. RX is quarantined during recovery; frames queued for
 * 100 ms or longer are dropped. Call frequently to preserve lease timing. */
bool mecs_can_poll(void);

#ifdef __cplusplus
}
#endif
