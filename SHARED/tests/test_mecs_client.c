#include "mecs_client.h"

#include <assert.h>
#include <string.h>

typedef struct {
  mecs_frame_t sent[128];
  unsigned sent_count;
  unsigned confirmed;
  unsigned timeouts;
} test_context_t;

static bool send_frame(void *context, const mecs_frame_t *frame) {
  test_context_t *test = context;
  assert(test->sent_count < 128u);
  test->sent[test->sent_count++] = *frame;
  return true;
}

static void on_event(void *context, mecs_client_event_t event,
                     uint8_t node_id, uint8_t channel, uint8_t property,
                     uint32_t value, mecs_error_t error) {
  (void)node_id;
  (void)channel;
  (void)property;
  (void)value;
  test_context_t *test = context;
  if (event == MECS_CLIENT_EVENT_REQUEST_CONFIRMED && error == MECS_OK) {
    ++test->confirmed;
  } else if (event == MECS_CLIENT_EVENT_REQUEST_TIMEOUT) {
    ++test->timeouts;
  }
}

static mecs_frame_t output_announce(void) {
  return (mecs_frame_t){.id = MECS_ANNOUNCE_BASE + 1u,
                       .length = 8u,
                       .data = {MECS_PROTOCOL_VERSION,
                                MECS_BOARD_DO4,
                                0,
                                1,
                                2,
                                3,
                                MECS_ANNOUNCE_BOOT,
                                0}};
}

/* Reproduce the observed error-6 race even if an old status says "alive".
 * The first heartbeat is lost; a session rejection must repair the heartbeat
 * and preserve the transaction and every subsequent setup request. */
static void session_race(void) {
  test_context_t test = {0};
  mecs_client_t client;
  const mecs_client_config_t cfg = {send_frame, &test, 7};
  assert(mecs_client_begin(&client, &cfg, on_event, &test));
  mecs_frame_t f = output_announce();
  mecs_client_receive(&client, &f, 0);
  const mecs_io_status_t status = {.master_alive = true, .boot_id = 1};
  mecs_io_encode_status(1, &status, &f);
  mecs_client_receive(&client, &f, 1);
  assert(mecs_client_set(&client, 1, 3, MECS_PROP_POLARITY, 0));
  assert(mecs_client_set(&client, 1, 3, MECS_PROP_MODE, MECS_OUTPUT_PWM));
  assert(mecs_client_set(&client, 1, 3, MECS_PROP_FREQUENCY, 50));
  assert(mecs_client_set(&client, 1, 3, MECS_PROP_DUTY, 750));
  assert(mecs_client_set(&client, 1, 3, MECS_PROP_VALUE, 1));
  const uint8_t pins[4] = {0, 1, 2, 3};
  mecs_io_node_t node;
  mecs_io_node_init(&node, MECS_BOARD_DO4, pins, false, NULL, NULL);
  mecs_client_loop(&client, 2);
  mecs_io_request_t request;
  assert(mecs_io_decode_request(1, &test.sent[test.sent_count - 1], &request));
  mecs_io_reply_t reply = mecs_io_node_request(&node, &request, 3);
  assert(reply.error == MECS_ERR_SESSION);
  mecs_io_encode_reply(1, &reply, &f);
  mecs_client_receive(&client, &f, 3);
  assert(client.request_active && client.queue_count == 4);
  const unsigned count = test.sent_count;
  mecs_client_loop(&client, 4);
  assert(test.sent_count == count + 1);
  uint16_t session;
  assert(mecs_io_decode_heartbeat(&test.sent[count], &session));
  mecs_io_node_heartbeat(&node, session, 4);
  for (unsigned i = 0; i < 5; ++i) {
    mecs_client_loop(&client, 302 + i * 2);
    assert(mecs_io_decode_request(1, &test.sent[test.sent_count - 1], &request));
    reply = mecs_io_node_request(&node, &request, 303 + i * 2);
    assert(reply.error == MECS_OK);
    mecs_io_encode_reply(1, &reply, &f);
    mecs_client_receive(&client, &f, 303 + i * 2);
  }
  assert(test.confirmed == 5 && node.config[3].value);
  assert(node.config[3].mode == MECS_OUTPUT_PWM);
  assert(node.config[3].frequency_hz == 50);
  assert(node.config[3].duty_percent_x1000 == 7500);
  // A duplicate enable after lease loss must not falsely ACK an inactive pin.
  mecs_io_node_tick(&node, 0, 2000);
  mecs_io_node_heartbeat(&node, session, 2001);
  reply = mecs_io_node_request(&node, &request, 2002);
  assert(reply.error != MECS_OK && !node.config[3].value);
}

int main(void) {
  session_race();
  test_context_t test = {0};
  mecs_client_t client;
  const mecs_client_config_t config = {
      .send_frame = send_frame,
      .transport_context = &test,
      .session = 7,
  };
  assert(mecs_client_begin(&client, &config, on_event, &test));

  mecs_client_loop(&client, 10);
  assert(test.sent_count == 2u); /* First heartbeat and discovery. */
  assert(test.sent[0].id == MECS_HEARTBEAT_ID);
  assert(test.sent[1].id == MECS_DISCOVER_ID);

  const mecs_frame_t announce = output_announce();
  mecs_client_receive(&client, &announce, 20);
  mecs_io_status_t status = {.raw = 0, .logical = 0, .master_alive = true,
                            .boot_id = 4, .sequence = 1};
  mecs_frame_t status_frame;
  mecs_io_encode_status(1, &status, &status_frame);
  mecs_client_receive(&client, &status_frame, 30);
  const mecs_client_node_t *node = mecs_client_node(&client, 1);
  assert(node && node->identity.board_type == MECS_BOARD_DO4);
  assert(mecs_client_node_online(node, 30));

  assert(mecs_client_set(&client, 1, 0, MECS_PROP_VALUE, 1));
  assert(mecs_client_set(&client, 1, 0, MECS_PROP_MODE, MECS_OUTPUT_PWM));
  assert(mecs_client_request_pending(&client));
  mecs_client_loop(&client, 40);
  mecs_io_request_t request;
  assert(mecs_io_decode_request(1, &test.sent[test.sent_count - 1u], &request));
  assert(request.property == MECS_PROP_VALUE);
  assert(request.channel == 0u && request.value == 1u);

  mecs_io_reply_t reply = {.property = request.property,
                          .error = MECS_OK,
                          .transaction = request.transaction,
                          .session = request.session,
                          .value = 1};
  mecs_frame_t reply_frame;
  mecs_io_encode_reply(1, &reply, &reply_frame);
  mecs_client_receive(&client, &reply_frame, 45);
  assert(mecs_client_request_pending(&client)); /* Second write is queued. */
  assert(test.confirmed == 1u);
  node = mecs_client_node(&client, 1);
  assert(node->value[0][MECS_PROP_VALUE] == 1u);
  assert(node->valid[0] & (1u << MECS_PROP_VALUE));

  /* A second property waits in the client FIFO and is sent after the first
   * acknowledgement, without the application constructing wire frames. */
  mecs_client_loop(&client, 46);
  assert(mecs_io_decode_request(1, &test.sent[test.sent_count - 1u], &request));
  assert(request.property == MECS_PROP_MODE);
  reply = (mecs_io_reply_t){.property = request.property,
                           .error = MECS_OK,
                           .transaction = request.transaction,
                           .session = request.session,
                           .value = MECS_OUTPUT_PWM};
  mecs_io_encode_reply(1, &reply, &reply_frame);
  mecs_client_receive(&client, &reply_frame, 48);
  assert(test.confirmed == 2u);
  assert(!mecs_client_request_pending(&client));

  assert(mecs_client_get(&client, 1, 0, MECS_PROP_VALUE));
  mecs_client_loop(&client, 50);
  assert(mecs_client_request_pending(&client));
  mecs_client_loop(&client, 1551);
  assert(!mecs_client_request_pending(&client));
  assert(test.timeouts == 1u);

  /* Cached nodes stop being usable after the status lease expires. */
  assert(!mecs_client_node_online(node, 1531));

  client.next_transaction = UINT16_MAX;
  assert(mecs_client_needs_new_session(&client));
  const unsigned sent_before_session_change = test.sent_count;
  assert(mecs_client_set_session(&client, 8));
  mecs_client_loop(&client, 1600);
  assert(test.sent_count == sent_before_session_change + 1u);
  assert(test.sent[test.sent_count - 1u].id == MECS_HEARTBEAT_ID);
  uint16_t new_session = 0;
  assert(mecs_io_decode_heartbeat(&test.sent[test.sent_count - 1u],
                                 &new_session));
  assert(new_session == 8u);
  mecs_io_encode_status(1, &status, &status_frame);
  mecs_client_receive(&client, &status_frame, 1601);
  /* Do not send property commands until nodes have had a chance to see the
   * session-changing heartbeat. */
  assert(mecs_client_set(&client, 1, 0, MECS_PROP_VALUE, 0));
  mecs_client_loop(&client, 1849);
  assert(mecs_client_request_pending(&client));
  // The fresh status after an offline gap requests another heartbeat.
  assert(test.sent[test.sent_count - 1u].id == MECS_HEARTBEAT_ID);
  mecs_client_loop(&client, 2100);
  assert(!client.session_settling); /* Periodic heartbeat must not extend it. */
  assert(mecs_io_decode_request(1, &test.sent[test.sent_count - 1u], &request));
  return 0;
}
