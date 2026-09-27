#include "mio_client.h"

#include <assert.h>
#include <string.h>

typedef struct {
  mio_frame_t sent[16];
  unsigned sent_count;
  unsigned confirmed;
  unsigned timeouts;
} test_context_t;

static bool send_frame(void *context, const mio_frame_t *frame) {
  test_context_t *test = context;
  assert(test->sent_count < 16u);
  test->sent[test->sent_count++] = *frame;
  return true;
}

static void on_event(void *context, mio_client_event_t event,
                     uint8_t node_id, uint8_t channel, uint8_t property,
                     uint16_t value, mio_error_t error) {
  (void)node_id;
  (void)channel;
  (void)property;
  (void)value;
  test_context_t *test = context;
  if (event == MIO_CLIENT_EVENT_REQUEST_CONFIRMED && error == MIO_OK) {
    ++test->confirmed;
  } else if (event == MIO_CLIENT_EVENT_REQUEST_TIMEOUT) {
    ++test->timeouts;
  }
}

static mio_frame_t output_announce(void) {
  return (mio_frame_t){.id = MIO_ANNOUNCE_BASE + 1u,
                       .length = 8u,
                       .data = {MIO_PROTOCOL_VERSION,
                                MIO_BOARD_DO4,
                                0,
                                1,
                                2,
                                3,
                                MIO_ANNOUNCE_BOOT,
                                0}};
}

int main(void) {
  test_context_t test = {0};
  mio_client_t client;
  const mio_client_config_t config = {
      .send_frame = send_frame,
      .transport_context = &test,
      .session = 7,
  };
  assert(mio_client_begin(&client, &config, on_event, &test));

  mio_client_loop(&client, 10);
  assert(test.sent_count == 2u); /* First heartbeat and discovery. */
  assert(test.sent[0].id == MIO_HEARTBEAT_ID);
  assert(test.sent[1].id == MIO_DISCOVER_ID);

  const mio_frame_t announce = output_announce();
  mio_client_receive(&client, &announce, 20);
  mio_io_status_t status = {.raw = 0, .logical = 0, .master_alive = true,
                            .boot_id = 4, .sequence = 1};
  mio_frame_t status_frame;
  mio_io_encode_status(1, &status, &status_frame);
  mio_client_receive(&client, &status_frame, 30);
  const mio_client_node_t *node = mio_client_node(&client, 1);
  assert(node && node->identity.board_type == MIO_BOARD_DO4);
  assert(mio_client_node_online(node, 30));

  assert(mio_client_set(&client, 1, 0, MIO_PROP_VALUE, 1));
  assert(mio_client_set(&client, 1, 0, MIO_PROP_MODE, MIO_OUTPUT_PWM));
  assert(mio_client_request_pending(&client));
  mio_client_loop(&client, 40);
  mio_io_request_t request;
  assert(mio_io_decode_request(1, &test.sent[test.sent_count - 1u], &request));
  assert(request.property == MIO_PROP_VALUE);
  assert(request.channel == 0u && request.value == 1u);

  mio_io_reply_t reply = {.property = request.property,
                          .error = MIO_OK,
                          .transaction = request.transaction,
                          .session = request.session,
                          .value = 1};
  mio_frame_t reply_frame;
  mio_io_encode_reply(1, &reply, &reply_frame);
  mio_client_receive(&client, &reply_frame, 45);
  assert(mio_client_request_pending(&client)); /* Second write is queued. */
  assert(test.confirmed == 1u);
  node = mio_client_node(&client, 1);
  assert(node->value[0][MIO_PROP_VALUE] == 1u);
  assert(node->valid[0] & (1u << MIO_PROP_VALUE));

  /* A second property waits in the client FIFO and is sent after the first
   * acknowledgement, without the application constructing wire frames. */
  mio_client_loop(&client, 46);
  assert(mio_io_decode_request(1, &test.sent[test.sent_count - 1u], &request));
  assert(request.property == MIO_PROP_MODE);
  reply = (mio_io_reply_t){.property = request.property,
                           .error = MIO_OK,
                           .transaction = request.transaction,
                           .session = request.session,
                           .value = MIO_OUTPUT_PWM};
  mio_io_encode_reply(1, &reply, &reply_frame);
  mio_client_receive(&client, &reply_frame, 48);
  assert(test.confirmed == 2u);
  assert(!mio_client_request_pending(&client));

  assert(mio_client_get(&client, 1, 0, MIO_PROP_VALUE));
  mio_client_loop(&client, 50);
  assert(mio_client_request_pending(&client));
  mio_client_loop(&client, 1551);
  assert(!mio_client_request_pending(&client));
  assert(test.timeouts == 1u);

  /* Cached nodes stop being usable after the status lease expires. */
  assert(!mio_client_node_online(node, 1531));

  client.next_transaction = UINT16_MAX;
  assert(mio_client_needs_new_session(&client));
  const unsigned sent_before_session_change = test.sent_count;
  assert(mio_client_set_session(&client, 8));
  mio_client_loop(&client, 1600);
  assert(test.sent_count == sent_before_session_change + 1u);
  assert(test.sent[test.sent_count - 1u].id == MIO_HEARTBEAT_ID);
  uint16_t new_session = 0;
  assert(mio_io_decode_heartbeat(&test.sent[test.sent_count - 1u],
                                 &new_session));
  assert(new_session == 8u);
  mio_io_encode_status(1, &status, &status_frame);
  mio_client_receive(&client, &status_frame, 1601);
  /* Do not send property commands until nodes have had a chance to see the
   * session-changing heartbeat. */
  assert(mio_client_set(&client, 1, 0, MIO_PROP_VALUE, 0));
  mio_client_loop(&client, 1849);
  assert(mio_client_request_pending(&client));
  assert(test.sent_count == sent_before_session_change + 1u);
  return 0;
}
