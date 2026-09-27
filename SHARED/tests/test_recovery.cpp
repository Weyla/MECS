/* A deterministic CAN network runs the real master, wire codec and node model.
 * Assert node-side output state, not just log messages or queued requests. */
#include "MECS.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>

struct Network {
  MECSController master;
  mecs_io_node_t output{}, input{};
  uint32_t now = 0;
  uint16_t boot = 1;
  bool connected = true, accept = true, dropHeartbeat = false;
  bool loseReplies = false, failHardware = false, rejectPreciseDuty = false;
  unsigned commands = 0, applied = 0, enableCalls = 0;
  uint32_t random = 1234567;
  bool noisy = false;
  struct Delivery { mecs_frame_t frame; uint32_t due; bool toMaster; };
  std::deque<Delivery> messages;
  static bool apply(void *context, uint8_t, const mecs_channel_config_t *c, bool active) {
    auto &n = *static_cast<Network *>(context);
    ++n.applied;
    if (active && c->value) {
      ++n.enableCalls;
      if (n.failHardware) return false;
    }
    return true;
  }
  static bool send(void *context, const mecs_frame_t *frame) {
    auto &n = *static_cast<Network *>(context);
    if (!n.accept) return false;
    n.push(*frame, false);
    return true;
  }
  void push(const mecs_frame_t &frame, bool toMaster) {
    random = random * 1664525u + 1013904223u;
    if (noisy && (random % 11 == 0)) return;
    Delivery d{frame, now + (noisy ? 1 + random % 17 : 1), toMaster};
    messages.push_back(d);
    if (noisy && random % 13 == 0) { d.due += 2; messages.push_back(d); }
  }
  Network() {
    const mecs_client_config_t config{send, this, 7};
    assert(master.start(config));
    const uint8_t pins[4] = {0, 1, 2, 3};
    mecs_io_node_init(&input, MECS_BOARD_DI4, pins, false, nullptr, nullptr);
    reboot();
    announce(2, MECS_BOARD_DI4);
  }
  void announce(uint8_t address, uint16_t board) {
    mecs_frame_t frame{};
    frame.id = MECS_ANNOUNCE_BASE + address;
    frame.length = 8;
    frame.data[0] = MECS_PROTOCOL_VERSION;
    frame.data[1] = board;
    frame.data[6] = MECS_ANNOUNCE_BOOT;
    master.receive(frame, now);
  }
  void status(uint8_t address, const mecs_io_node_t &node, uint16_t bootId) {
    mecs_io_status_t s{};
    s.logical = mecs_io_node_logical(&node);
    s.master_alive = node.master_alive;
    s.fault = node.fault;
    s.boot_id = bootId;
    s.sequence = now / 50;
    mecs_frame_t f;
    mecs_io_encode_status(address, &s, &f);
    push(f, true);
  }
  void reboot(bool sendAnnounce = true) {
    const uint8_t pins[4] = {0, 1, 2, 3};
    mecs_io_node_init(&output, MECS_BOARD_DO4, pins, false, apply, this);
    ++boot;
    if (sendAnnounce) announce(1, MECS_BOARD_DO4);
    status(1, output, boot); // Before the first heartbeat: original failure.
  }
  void run(unsigned ms) {
    for (unsigned i = 0; i < ms; ++i) {
      ++now;
      mecs_io_node_tick(&output, 0, now);
      mecs_io_node_tick(&input, 1, now);
      for (auto it = messages.begin(); it != messages.end();) {
        if (it->due > now) { ++it; continue; }
        const auto d = *it;
        it = messages.erase(it);
        if (!connected) continue;
        if (d.toMaster) {
          if (!loseReplies || d.frame.id != MECS_REPLY_BASE + 1)
            master.receive(d.frame, now);
          continue;
        }
        uint16_t session;
        if (mecs_io_decode_heartbeat(&d.frame, &session)) {
          if (!dropHeartbeat) {
            mecs_io_node_heartbeat(&output, session, now);
            mecs_io_node_heartbeat(&input, session, now);
          }
        } else {
          for (uint8_t address = 1; address <= 2; ++address) {
            mecs_io_request_t request;
            if (mecs_io_decode_request(address, &d.frame, &request)) {
              ++commands;
              auto &node = address == 1 ? output : input;
              const auto reply = rejectPreciseDuty && address == 1 &&
                                         request.property == MECS_PROP_DUTY_PRECISE
                                     ? mecs_io_reply_t{request.property,
                                                       MECS_ERR_PROPERTY,
                                                       request.transaction,
                                                       request.session,
                                                       request.value}
                                     : mecs_io_node_request(&node, &request, now);
              mecs_frame_t response;
              mecs_io_encode_reply(address, &reply, &response);
              // deque append invalidates iterators; restart after enqueue.
              push(response, true);
              it = messages.begin();
              break;
            }
          }
        }
      }
      if (now % 50 == 0 && connected) {
        status(1, output, boot);
        status(2, input, 1);
      }
      master.service(now);
    }
  }
};

static void check_servo(const Network &n, uint32_t duty) {
  assert(n.output.config[3].mode == MECS_OUTPUT_PWM);
  assert(n.output.config[3].frequency_hz == 50);
  assert(n.output.config[3].duty_percent_x1000 == duty);
  assert(n.output.config[3].value);
  assert(!n.output.fault);
}

static void wire_precision() {
  for (uint32_t value : {0u, 1u, 56012u, 65535u, 65536u, 99999u, 100000u}) {
    for (uint8_t channel = 0; channel < 4; ++channel) {
      mecs_io_request_t request{MECS_PROP_DUTY_PRECISE, channel, 17, 7, value};
      mecs_frame_t frame;
      assert(mecs_io_encode_request(1, &request, &frame));
      mecs_io_request_t decoded{};
      assert(mecs_io_decode_request(1, &frame, &decoded));
      assert(decoded.value == value && decoded.channel == channel);
      assert(!mecs_io_decode_request(2, &frame, &decoded));
      mecs_io_reply_t reply{MECS_PROP_DUTY_PRECISE, MECS_OK, 17, 7, value};
      mecs_io_encode_reply(1, &reply, &frame);
      uint8_t node;
      mecs_io_reply_t received{};
      assert(mecs_io_decode_reply(&frame, &node, &received));
      assert(node == 1 && received.value == value && received.error == MECS_OK);
    }
  }
}

int main() {
  wire_precision();
  Network n;
  auto output = n.master.node(1);
  auto servo = output.pin(3);
  auto inputs = n.master.node(2);
  auto button = inputs.pin(0);
  // Registered in setup before discovery; no callback, boot ID or retry code.
  assert(servo.setPwm(50, 7.5));
  assert(servo.turnOn());
  assert(button.setMode(MECS::DEBOUNCE));
  assert(button.setDebounce(20));
  assert(button.setResistor(MECS::PullUp));
  assert(inputs.setReporting(20, 500, true));
  assert(!servo.ready());
  n.run(1000);
  assert(servo.ready() && button.valid() && button.value());
  check_servo(n, 7500);
  assert(n.input.report_min_ms == 20 && n.input.report_ms == 500);

  // Calling the same setter every loop produces no additional commands.
  const unsigned commands = n.commands;
  for (unsigned i = 0; i < 1000; ++i) { assert(servo.setPwm(50, 7.5)); n.run(1); }
  assert(n.commands == commands);

  // Reboot at every phase within the 250 ms heartbeat interval. Also exercise
  // a lost boot announcement, using only the boot identity in status.
  for (unsigned i = 0; i < 250; ++i) {
    n.run(i % 7);
    n.reboot(i % 2 == 0);
    assert(!n.output.config[3].value);
    n.run(1000);
    assert(servo.ready());
    check_servo(n, 7500);
  }
  // The latest setter wins even when an old value is still awaiting its ACK.
  assert(servo.setPwm(50, 5.501));
  n.run(2);
  assert(servo.setPwm(50, 12.501));
  n.run(1000);
  check_servo(n, 12501);

  // Dropped ACKs must not mark configuration ready or lose desired settings.
  n.loseReplies = true;
  n.reboot();
  n.run(2500);
  assert(!servo.ready());
  n.loseReplies = false;
  n.run(3000);
  assert(servo.ready());
  check_servo(n, 12501);

  // No usable link: outputs stop locally, cached input is invalid.
  n.connected = false;
  n.run(2000);
  assert(!n.output.config[3].value && !button.valid() && !servo.ready());
  n.connected = true;
  n.master.transportRecovered();
  n.run(2000);
  check_servo(n, 12501);

  // An explicit OFF written while disconnected must not restore the old ON.
  n.connected = false;
  n.run(2000);
  assert(servo.turnOff());
  n.connected = true;
  n.master.transportRecovered();
  n.run(1500);
  assert(servo.ready() && !n.output.config[3].value);
  assert(servo.turnOn());
  n.run(1000);
  check_servo(n, 12501);

  // Master session rotation must eventually finish and reapply configuration.
  assert(n.master.setSession(8));
  n.run(1500);
  assert(servo.ready());
  check_servo(n, 12501);

  // Transport queue permanently refuses TX. Once repaired, the reconciler
  // must recover rather than remain stuck in a request with zero attempts.
  n.accept = false;
  assert(servo.setPwm(50, 56.012));
  n.run(2200);
  assert(!servo.ready());
  n.accept = true;
  n.run(2500);
  check_servo(n, 56012);

  // Loss, duplication and variable latency around repeated resets.
  n.noisy = true;
  for (unsigned i = 0; i < 50; ++i) {
    n.reboot(i % 3 != 0);
    n.run(5000);
    assert(servo.ready());
    check_servo(n, 56012);
  }
  n.noisy = false;
  // A hardware rejection must never turn into a false-ready success.
  n.failHardware = true;
  n.reboot();
  n.run(2000);
  assert(n.output.fault && !n.output.config[3].value && !servo.ready());
  assert(servo.lastError() == MECS_ERR_HARDWARE);
  n.failHardware = false;
  n.reboot();
  n.run(1500);
  check_servo(n, 56012);

  // A confirmed update to a different setting must not conceal a rejected
  // setting on the same channel.
  n.rejectPreciseDuty = true;
  assert(servo.setPwm(50, 10.0));
  n.run(1000);
  assert(!servo.ready());
  assert(servo.lastError() == MECS_ERR_PROPERTY);
  assert(servo.setActiveLow(true));
  n.run(1000);
  assert(!servo.ready());
  assert(servo.lastError() == MECS_ERR_PROPERTY);
  n.rejectPreciseDuty = false;
  n.reboot();
  n.run(1500);
  check_servo(n, 10000);
  assert(n.output.config[3].active_low);

  assert(!servo.setPwm(50, NAN));
  assert(!servo.setPwm(50, 100.001));
  assert(!output.pin(4).setPwm(50, 7.5));
  assert(!output.pin(255).setMode(MECS::PWM));
  assert(!inputs.setReporting(501, 500, true));
  std::puts("MECS: 300 reboot/recovery scenarios, precision, losses and hardware faults passed");
}
