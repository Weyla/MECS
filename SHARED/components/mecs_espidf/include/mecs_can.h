#pragma once
#include "esp_err.h"
#include "mecs_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single controller adapter; call start once, then use from one owner task.
 * RX is copied from ISR into a bounded queue. TX storage remains owned by the
 * driver until its completion callback, even if no peer acknowledges it. */
esp_err_t mecs_can_start(int tx_gpio, int rx_gpio);
/* Start the same single-controller transport at an explicit bitrate. */
esp_err_t mecs_can_start_with_bitrate(int tx_gpio, int rx_gpio,
                                      uint32_t bitrate);
bool mecs_can_send(void *context, const mecs_frame_t *frame);
bool mecs_can_receive(mecs_frame_t *frame);
/* Poll from the owner task. Logs transport faults; true after bus recovery. */
bool mecs_can_poll(void);

#ifdef __cplusplus
}
#endif
