#pragma once

/* Portable digital-I/O model and fixed-size CAN extension. No ESP-IDF types
 * appear here: a hardware port supplies one apply callback and sampled levels.
 */
#include "mecs_protocol.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MECS_CHANNELS 4u
#define MECS_BOARD_DI4 2u
#define MECS_BOARD_DO4 3u
#define MECS_HEARTBEAT_ID 0x081u
#define MECS_COMMAND_BASE 0x200u
#define MECS_REPLY_BASE 0x280u
#define MECS_STATUS_BASE 0x300u
#define MECS_LEASE_MS 1500u
#define MECS_HEARTBEAT_MS 250u
#define MECS_STATUS_MS 500u
#define MECS_GLOBAL_CHANNEL 255u
#define MECS_READ_FLAG 0x80u

typedef enum {
  MECS_FILTER_NONE,
  MECS_FILTER_STABLE,
  MECS_FILTER_PWM
} mecs_filter_t;
typedef enum {
  MECS_OUTPUT_DIGITAL,
  MECS_OUTPUT_PWM,
  MECS_OUTPUT_PULSE,
  MECS_OUTPUT_SLOW_PWM
} mecs_output_mode_t;
typedef enum { MECS_PULL_NONE, MECS_PULL_UP, MECS_PULL_DOWN } mecs_pull_t;
typedef enum {
  MECS_REPORT_PERIODIC,
  MECS_REPORT_CHANGE,
  MECS_REPORT_BOTH
} mecs_report_t;

/* Properties are shared by the serial parser, UI schema and node validation.
 * Unsupported properties are rejected rather than silently ignored. */
typedef enum {
  MECS_PROP_POLARITY = 1,
  MECS_PROP_FILTER,
  MECS_PROP_FILTER_MS,
  MECS_PROP_PULL,
  MECS_PROP_REPORT,
  MECS_PROP_REPORT_MS,
  MECS_PROP_MODE,
  MECS_PROP_FREQUENCY,
  MECS_PROP_DUTY,
  MECS_PROP_VALUE,
  MECS_PROP_PULSE_MS,
  MECS_PROP_TRIGGER,
  MECS_PROP_PIN,
  MECS_PROP_PERIOD,
  MECS_PROP_DUTY_PRECISE, /* Percent x1000; one atomic 17-bit command. */
  MECS_PROP_REPORT_MIN_MS,
  MECS_PROP_COUNT
} mecs_property_t;
typedef struct {
  const char *name;
  const char *label;
  const char *unit;
  const char *choices; /* Pipe-separated names for consecutive values from 0. */
  uint32_t minimum, maximum;
  uint8_t roles; /* 1 = DI4, 2 = DO4, 3 = both. */
  bool readonly;
  uint16_t scale; /* Encoded units per displayed unit: duty 100, seconds 10. */
} mecs_property_info_t;
extern const mecs_property_info_t mecs_properties[MECS_PROP_COUNT];

typedef enum {
  MECS_OK,
  MECS_ERR_PROPERTY,
  MECS_ERR_CHANNEL,
  MECS_ERR_RANGE,
  MECS_ERR_OFFLINE,
  MECS_ERR_HARDWARE,
  MECS_ERR_SESSION,
  MECS_ERR_SEQUENCE
} mecs_error_t;
const char *mecs_error_name(mecs_error_t error);

typedef struct {
  uint8_t property; /* Read flag may be ORed into this byte. */
  uint8_t channel;
  uint16_t transaction;
  uint16_t session;
  uint32_t value;
} mecs_io_request_t;
typedef struct {
  uint8_t property;
  mecs_error_t error;
  uint16_t transaction, session;
  uint32_t value;
} mecs_io_reply_t;
typedef struct {
  uint8_t raw, logical;
  bool master_alive, fault;
  uint16_t boot_id, sequence;
} mecs_io_status_t;

bool mecs_io_encode_request(uint8_t node_id, const mecs_io_request_t *request,
                           mecs_frame_t *frame);
bool mecs_io_decode_request(uint8_t node_id, const mecs_frame_t *frame,
                           mecs_io_request_t *request);
void mecs_io_encode_reply(uint8_t node_id, const mecs_io_reply_t *reply,
                         mecs_frame_t *frame);
bool mecs_io_decode_reply(const mecs_frame_t *frame, uint8_t *node_id,
                         mecs_io_reply_t *reply);
void mecs_io_encode_heartbeat(uint16_t session, mecs_frame_t *frame);
bool mecs_io_decode_heartbeat(const mecs_frame_t *frame, uint16_t *session);
void mecs_io_encode_status(uint8_t node_id, const mecs_io_status_t *status,
                          mecs_frame_t *frame);
bool mecs_io_decode_status(const mecs_frame_t *frame, uint8_t *node_id,
                          mecs_io_status_t *status);

typedef struct {
  bool active_low;
  mecs_filter_t filter;
  uint16_t filter_ms;
  mecs_pull_t pull;
  mecs_output_mode_t mode;
  uint16_t frequency_hz, pulse_ms;
  uint32_t duty_percent_x1000;
  uint16_t period_deciseconds; /* Slow PWM: 0.1-second units, 1..36000. */
  bool value; /* Output gate: logical active for digital/pulse; PWM enabled. */
  uint8_t pin;
} mecs_channel_config_t;

typedef struct {
  bool raw, candidate, filtered;
  uint32_t candidate_since_ms, pulse_until_ms, cycle_since_ms;
  bool output_active; /* Local slow-PWM phase, separate from its enable gate. */
} mecs_channel_state_t;

/* Called synchronously in the node owner task. Return false if hardware could
 * not apply the requested state. No CAN operations are allowed in this
 * callback. */
typedef bool (*mecs_io_apply_fn)(void *context, uint8_t channel,
                                const mecs_channel_config_t *config,
                                bool output_active);
typedef struct {
  uint16_t board_type, session;
  bool master_alive, fault;
  uint32_t heartbeat_ms;
  mecs_report_t report;
  uint16_t report_ms, report_min_ms;
  mecs_channel_config_t config[MECS_CHANNELS];
  mecs_channel_state_t state[MECS_CHANNELS];
  mecs_io_apply_fn apply;
  void *context;
  bool cached;
  mecs_io_request_t last_request;
  mecs_io_reply_t last_reply;
} mecs_io_node_t;

void mecs_io_node_init(mecs_io_node_t *node, uint16_t board_type,
                      const uint8_t pins[MECS_CHANNELS], bool output_active_low,
                      mecs_io_apply_fn apply, void *context);
void mecs_io_node_heartbeat(mecs_io_node_t *node, uint16_t session,
                           uint32_t now_ms);
void mecs_io_node_tick(mecs_io_node_t *node, uint8_t raw_levels, uint32_t now_ms);
void mecs_io_node_stop(mecs_io_node_t *node);
mecs_io_reply_t mecs_io_node_request(mecs_io_node_t *node,
                                   const mecs_io_request_t *request,
                                   uint32_t now_ms);
uint8_t mecs_io_node_logical(const mecs_io_node_t *node);

#define MECS_MEASUREMENT_BASE 0x380u
#define MECS_PWM_MIN_PERIOD_US 90u
#define MECS_PWM_MAX_PERIOD_US 1000000u
#define MECS_PWM_MIN_PULSE_US 4u

typedef struct {
  bool valid;
  uint32_t period_us;
  uint16_t duty_percent_x100;
} mecs_pwm_measurement_t;
void mecs_io_encode_measurement(uint8_t node, uint8_t channel,
                               const mecs_pwm_measurement_t *measurement,
                               mecs_frame_t *frame);
bool mecs_io_decode_measurement(const mecs_frame_t *frame, uint8_t *node,
                               uint8_t *channel,
                               mecs_pwm_measurement_t *measurement);

#ifdef __cplusplus
}
#endif
