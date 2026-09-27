#pragma once
#include "esp_err.h"
#include "mio.h"

/* Single controller adapter; call start once, then use from one owner task.
 * RX is copied from ISR into a bounded queue. TX storage remains owned by the
 * driver until its completion callback, even if no peer acknowledges it. */
esp_err_t mio_can_start(int tx_gpio, int rx_gpio);
bool mio_can_send(void *context, const mio_frame_t *frame);
bool mio_can_receive(mio_frame_t *frame);
/* Poll from the owner task. Logs transport faults; true after bus recovery. */
bool mio_can_poll(void);
