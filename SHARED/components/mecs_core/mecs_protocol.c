/* Portable discovery protocol: this file knows CAN message layouts, but has no
 * dependency on a CAN driver or operating system. The application feeds
 * received frames into mecs_receive(); outgoing frames go through the supplied
 * send callback.
 *
 * Request path: master request -> node receive -> node announce.
 * Response path: master receive -> application discovery callback.
 * See docs/PROTOCOL.md for the normative byte layouts and identifier
 * allocation.
 */
#include "mecs_protocol.h"
#include <stddef.h>

/* Bind a caller-owned instance to its identity and application callbacks.
 * Address 0 selects the master role; it needs no expansion board type. A node
 * must have a nonzero board type so its announcement describes a real type.
 * Initialization does not start hardware or send anything on the bus.
 */
bool mecs_init(mecs_t *mecs, mecs_identity_t identity, mecs_send_fn send,
              mecs_discovered_fn discovered, void *context) {
  if (!mecs || !send || identity.node_id > MECS_MAX_NODE_ID ||
      (identity.node_id != 0 && identity.board_type == 0)) {
    return false;
  }
  /* Copy configuration; context is passed back unchanged to both callbacks.
   * The application owns whatever context points to and must keep it alive. */
  *mecs = (mecs_t){identity, send, discovered, context};
  return true;
}

/* Keep the simple request check in the shared library so every board handles
 * identifier, frame type, length and protocol revision in the same way. */
bool mecs_is_discovery_request(const mecs_frame_t *frame) {
  return frame && !frame->extended && !frame->remote &&
         frame->id == MECS_DISCOVER_ID && frame->length == 1 &&
         frame->data[0] == MECS_PROTOCOL_VERSION;
}

/* Ask all expansion nodes to identify themselves. This is a broadcast data
 * frame, not a CAN remote frame (RTR), and only the master may originate it.
 */
bool mecs_request_discovery(mecs_t *mecs) {
  if (!mecs || !mecs->send || mecs->identity.node_id != 0) {
    return false;
  }
  /* The ID identifies the operation; the one-byte payload identifies the
   * wire revision. Omitted flags are zero: standard identifier, data frame. */
  const mecs_frame_t frame = {
      .id = MECS_DISCOVER_ID,
      .length = 1,
      .data = {MECS_PROTOCOL_VERSION},
  };
  /* frame lives on this stack. The transport must copy it before returning.
   * A true result means accepted for sending, not acknowledged by any node. */
  return mecs->send(mecs->context, &frame);
}

/* Submit the same identity format for boot, requested and recovery events.
 * The caller supplies the reason; the core does not detect boot or bus
 * recovery.
 */
bool mecs_announce(mecs_t *mecs, mecs_announce_reason_t reason) {
  if (!mecs || !mecs->send || mecs->identity.node_id == 0 ||
      mecs->identity.node_id > MECS_MAX_NODE_ID || reason < MECS_ANNOUNCE_BOOT ||
      reason > MECS_ANNOUNCE_RECOVERY) {
    return false;
  }
  const mecs_identity_t *id = &mecs->identity;
  /* Each node has its own CAN ID (0x100 + address). Simultaneous responses
   * can therefore arbitrate instead of sending different data under one ID.
   * Addresses must be unique; this mechanism does not detect duplicates.
   *
   * Payload: revision, board low byte, board high byte, firmware major/minor/
   * patch, reason, reserved zero. Explicit bytes avoid struct padding and
   * CPU byte-order differences when this core is later used on STM32. */
  const mecs_frame_t frame = {
      .id = MECS_ANNOUNCE_BASE + id->node_id,
      .length = 8,
      .data = {MECS_PROTOCOL_VERSION, (uint8_t)id->board_type,
               (uint8_t)(id->board_type >> 8), id->firmware_major,
               id->firmware_minor, id->firmware_patch, (uint8_t)reason, 0},
  };
  return mecs->send(mecs->context, &frame);
}

/* Dispatch one received frame in the application task, never in an ISR.
 * IGNORED means the message is outside this role/protocol; INVALID means an
 * assigned message has an unsupported layout/value, or arguments are invalid.
 * No callback or reply is generated until the relevant fields are validated.
 */
mecs_rx_result_t mecs_receive(mecs_t *mecs, const mecs_frame_t *frame) {
  if (!mecs || !mecs->send || !frame) {
    return MECS_RX_INVALID;
  }
  /* The protocol uses 11-bit identifiers and actual payloads only. Extended
   * (29-bit) identifiers and RTR frames are not alternate discovery formats. */
  if (frame->extended || frame->remote) {
    return MECS_RX_IGNORED;
  }
  /* First dispatch case: master broadcast. Check the length first so a
   * truncated payload is rejected before any payload field is interpreted. */
  if (frame->id == MECS_DISCOVER_ID) {
    if (!mecs_is_discovery_request(frame)) {
      return MECS_RX_INVALID;
    }
    if (mecs->identity.node_id == 0) {
      return MECS_RX_IGNORED;
    }
    /* A valid request generates one response submission. Failure is
     * returned to the caller; there is no hidden application retry loop. */
    return mecs_announce(mecs, MECS_ANNOUNCE_REQUEST) ? MECS_RX_HANDLED
                                                   : MECS_RX_SEND_FAILED;
  }
  /* Second dispatch case: announcements from addresses 1..127. The base
   * identifier itself would mean address 0 and is intentionally excluded. */
  if (frame->id <= MECS_ANNOUNCE_BASE ||
      frame->id > MECS_ANNOUNCE_BASE + MECS_MAX_NODE_ID) {
    return MECS_RX_IGNORED;
  }
  /* A valid CAN CRC only establishes link-level integrity. Still validate
   * our wire revision, reason, reserved byte and nonzero board type before
   * exposing the identity. Unknown nonzero board types remain discoverable. */
  if (frame->length != 8 || frame->data[0] != MECS_PROTOCOL_VERSION ||
      frame->data[6] > MECS_ANNOUNCE_RECOVERY || frame->data[7] != 0 ||
      (frame->data[1] == 0 && frame->data[2] == 0)) {
    return MECS_RX_INVALID;
  }
  /* Expansion nodes do not build a registry of other expansion nodes. */
  if (mecs->identity.node_id != 0) {
    return MECS_RX_IGNORED;
  }
  /* Decode the source address from the identifier and reconstruct the
   * little-endian board type. The application receives typed fields, not bytes.
   */
  const mecs_identity_t identity = {
      .node_id = (uint8_t)(frame->id - MECS_ANNOUNCE_BASE),
      .board_type =
          (uint16_t)(frame->data[1] | ((uint16_t)frame->data[2] << 8)),
      .firmware_major = frame->data[3],
      .firmware_minor = frame->data[4],
      .firmware_patch = frame->data[5],
  };
  /* The identity pointer is valid only during this synchronous callback.
   * The application copies it into its registry and decides how to handle
   * repeats. An announcement alone does not prove continued connectivity. */
  if (mecs->discovered) {
    mecs->discovered(mecs->context, &identity,
                    (mecs_announce_reason_t)frame->data[6]);
  }
  return MECS_RX_HANDLED;
}
