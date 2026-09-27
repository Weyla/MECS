#pragma once

/* Portable digital-I/O model and fixed-size CAN extension. No ESP-IDF types
 * appear here: a hardware port supplies one apply callback and sampled levels.
 */
#include "mio.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIO_CHANNELS 4u
#define MIO_BOARD_DI4 2u
#define MIO_BOARD_DO4 3u
#define MIO_HEARTBEAT_ID 0x081u
#define MIO_COMMAND_BASE 0x200u
#define MIO_REPLY_BASE 0x280u
#define MIO_STATUS_BASE 0x300u
#define MIO_LEASE_MS 1500u
#define MIO_HEARTBEAT_MS 250u
#define MIO_STATUS_MS 500u
#define MIO_GLOBAL_CHANNEL 255u
#define MIO_READ_FLAG 0x80u

typedef enum {
  MIO_FILTER_NONE,
  MIO_FILTER_STABLE,
  MIO_FILTER_PWM
} mio_filter_t;
typedef enum {
  MIO_OUTPUT_DIGITAL,
  MIO_OUTPUT_PWM,
  MIO_OUTPUT_PULSE,
  MIO_OUTPUT_SLOW_PWM
} mio_output_mode_t;
typedef enum { MIO_PULL_NONE, MIO_PULL_UP, MIO_PULL_DOWN } mio_pull_t;
typedef enum {
  MIO_REPORT_PERIODIC,
  MIO_REPORT_CHANGE,
  MIO_REPORT_BOTH
} mio_report_t;

/* Properties are shared by the serial parser, UI schema and node validation.
 * Unsupported properties are rejected rather than silently ignored. */
typedef enum {
  MIO_PROP_POLARITY = 1,
  MIO_PROP_FILTER,
  MIO_PROP_FILTER_MS,
  MIO_PROP_PULL,
  MIO_PROP_REPORT,
  MIO_PROP_REPORT_MS,
  MIO_PROP_MODE,
  MIO_PROP_FREQUENCY,
  MIO_PROP_DUTY,
  MIO_PROP_VALUE,
  MIO_PROP_PULSE_MS,
  MIO_PROP_TRIGGER,
  MIO_PROP_PIN,
  MIO_PROP_PERIOD,
  MIO_PROP_COUNT
} mio_property_t;
typedef struct {
  const char *name;
  const char *label;
  const char *unit;
  const char *choices; /* Pipe-separated names for consecutive values from 0. */
  uint16_t minimum, maximum;
  uint8_t roles; /* 1 = DI4, 2 = DO4, 3 = both. */
  bool readonly;
  uint16_t scale; /* Encoded units per displayed unit: duty 100, seconds 10. */
} mio_property_info_t;
extern const mio_property_info_t mio_properties[MIO_PROP_COUNT];

typedef enum {
  MIO_OK,
  MIO_ERR_PROPERTY,
  MIO_ERR_CHANNEL,
  MIO_ERR_RANGE,
  MIO_ERR_OFFLINE,
  MIO_ERR_HARDWARE,
  MIO_ERR_SESSION,
  MIO_ERR_SEQUENCE
} mio_error_t;
const char *mio_error_name(mio_error_t error);

typedef struct {
  uint8_t property; /* Read flag may be ORed into this byte. */
  uint8_t channel;
  uint16_t transaction;
  uint16_t session;
  uint16_t value;
} mio_io_request_t;
typedef struct {
  uint8_t property;
  mio_error_t error;
  uint16_t transaction, session, value;
} mio_io_reply_t;
typedef struct {
  uint8_t raw, logical;
  bool master_alive, fault;
  uint16_t boot_id, sequence;
} mio_io_status_t;

bool mio_io_encode_request(uint8_t node_id, const mio_io_request_t *request,
                           mio_frame_t *frame);
bool mio_io_decode_request(uint8_t node_id, const mio_frame_t *frame,
                           mio_io_request_t *request);
void mio_io_encode_reply(uint8_t node_id, const mio_io_reply_t *reply,
                         mio_frame_t *frame);
bool mio_io_decode_reply(const mio_frame_t *frame, uint8_t *node_id,
                         mio_io_reply_t *reply);
void mio_io_encode_heartbeat(uint16_t session, mio_frame_t *frame);
bool mio_io_decode_heartbeat(const mio_frame_t *frame, uint16_t *session);
void mio_io_encode_status(uint8_t node_id, const mio_io_status_t *status,
                          mio_frame_t *frame);
bool mio_io_decode_status(const mio_frame_t *frame, uint8_t *node_id,
                          mio_io_status_t *status);

typedef struct {
  bool active_low;
  mio_filter_t filter;
  uint16_t filter_ms;
  mio_pull_t pull;
  mio_output_mode_t mode;
  uint16_t frequency_hz, duty_percent_x100, pulse_ms;
  uint16_t period_deciseconds; /* Slow PWM: 0.1-second units, 1..36000. */
  bool value; /* Output gate: logical active for digital/pulse; PWM enabled. */
  uint8_t pin;
} mio_channel_config_t;

typedef struct {
  bool raw, candidate, filtered;
  uint32_t candidate_since_ms, pulse_until_ms, cycle_since_ms;
  bool output_active; /* Local slow-PWM phase, separate from its enable gate. */
} mio_channel_state_t;

/* Called synchronously in the node owner task. Return false if hardware could
 * not apply the requested state. No CAN operations are allowed in this
 * callback. */
typedef bool (*mio_io_apply_fn)(void *context, uint8_t channel,
                                const mio_channel_config_t *config,
                                bool output_active);
typedef struct {
  uint16_t board_type, session;
  bool master_alive, fault;
  uint32_t heartbeat_ms;
  mio_report_t report;
  uint16_t report_ms;
  mio_channel_config_t config[MIO_CHANNELS];
  mio_channel_state_t state[MIO_CHANNELS];
  mio_io_apply_fn apply;
  void *context;
  bool cached;
  mio_io_request_t last_request;
  mio_io_reply_t last_reply;
} mio_io_node_t;

void mio_io_node_init(mio_io_node_t *node, uint16_t board_type,
                      const uint8_t pins[MIO_CHANNELS], bool output_active_low,
                      mio_io_apply_fn apply, void *context);
void mio_io_node_heartbeat(mio_io_node_t *node, uint16_t session,
                           uint32_t now_ms);
void mio_io_node_tick(mio_io_node_t *node, uint8_t raw_levels, uint32_t now_ms);
void mio_io_node_stop(mio_io_node_t *node);
mio_io_reply_t mio_io_node_request(mio_io_node_t *node,
                                   const mio_io_request_t *request,
                                   uint32_t now_ms);
uint8_t mio_io_node_logical(const mio_io_node_t *node);

#define MIO_MEASUREMENT_BASE 0x380u
#define MIO_PWM_MIN_PERIOD_US 90u
#define MIO_PWM_MAX_PERIOD_US 1000000u
#define MIO_PWM_MIN_PULSE_US 4u

typedef struct {
  bool valid;
  uint32_t period_us;
  uint16_t duty_percent_x100;
} mio_pwm_measurement_t;
void mio_io_encode_measurement(uint8_t node, uint8_t channel,
                               const mio_pwm_measurement_t *measurement,
                               mio_frame_t *frame);
bool mio_io_decode_measurement(const mio_frame_t *frame, uint8_t *node,
                               uint8_t *channel,
                               mio_pwm_measurement_t *measurement);

#ifdef __cplusplus
}
#endif
