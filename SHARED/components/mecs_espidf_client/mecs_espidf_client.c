#include "mecs_espidf_client.h"

unsigned mecs_espidf_client_poll(mecs_client_t *client, uint32_t now_ms,
                               unsigned max_frames) {
  if (!client) {
    return 0;
  }
  if (mecs_can_poll()) {
    mecs_client_transport_reset(client);
  }
  mecs_frame_t frame;
  unsigned received = 0;
  while (received < max_frames && mecs_can_receive(&frame)) {
    mecs_client_receive(client, &frame, now_ms);
    ++received;
  }
  mecs_client_loop(client, now_ms);
  return received;
}
