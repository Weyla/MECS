#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "master.h"
#include "mecs_protocol.h"
#include "mecs_io.h"
#include "mecs_can.h"
#include <stdbool.h>
#include <stdint.h>

#define MASTER_MAX_NODES 16
#define MASTER_LOG_COUNT 24
#define MASTER_LOG_WIDTH 144
#define MASTER_ONLINE_MS 1500u
#define MASTER_COMMAND_TIMEOUT_MS 300u
#define MASTER_COMMAND_QUEUE_LENGTH 32u

typedef struct {
  bool used, status_seen;
  mecs_identity_t identity;
  mecs_io_status_t status;
  mecs_pwm_measurement_t measurement[MECS_CHANNELS];
  uint32_t measurement_ms[MECS_CHANNELS];
  uint32_t seen_ms, status_ms;
  uint32_t values[MECS_CHANNELS + 1][MECS_PROP_COUNT];
  uint32_t valid[MECS_CHANNELS + 1];
  unsigned refresh_cursor;
  bool refreshing;
} node_view_t;

typedef struct {
  uint8_t node, channel, property;
  uint32_t value;
} command_t;

typedef struct {
  bool active;
  bool user_requested;
  command_t command;
  mecs_io_request_t request;
  uint32_t started_ms, sent_ms;
  unsigned attempts;
} master_pending_t;

extern node_view_t nodes[MASTER_MAX_NODES];
extern char logs[MASTER_LOG_COUNT][MASTER_LOG_WIDTH];
extern unsigned log_next, log_used;
extern uint32_t log_sequence;
extern SemaphoreHandle_t mutex;
extern QueueHandle_t commands;
extern mecs_t discovery;
extern uint16_t session, transaction;
extern bool discover_pending;
extern bool heartbeat_pending;
extern master_pending_t pending;
extern mecs_can_stats_t can_stats;

uint16_t master_next_session(void);
uint32_t master_now_ms(void);
node_view_t *master_lookup(uint8_t address);
bool master_online(const node_view_t *node, uint32_t now);
unsigned master_slot(uint8_t channel);
void master_discovered(void *context, const mecs_identity_t *identity,
                       mecs_announce_reason_t reason);
bool master_global_property(unsigned property);
uint8_t master_role_of(const node_view_t *node);
void master_accept_frame(const mecs_frame_t *frame, uint32_t now);
void master_transactions(uint32_t now);
void master_service_state(uint32_t now);
void master_transport_reset(void);
void master_console_poll(void);
