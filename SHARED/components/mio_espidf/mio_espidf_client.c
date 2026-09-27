#include "mio_espidf_client.h"

unsigned mio_espidf_client_poll(mio_client_t *client, uint32_t now_ms,
                               unsigned max_frames) {
  mio_frame_t frame;
  unsigned received = 0;
  while (received < max_frames && mio_can_receive(&frame)) {
    mio_client_receive(client, &frame, now_ms);
    ++received;
  }
  mio_client_loop(client, now_ms);
  return received;
}
