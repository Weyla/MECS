/* Serial and web command validation/queueing. */
#include "master_internal.h"
#include "mecs_command.h"
#include <stdio.h>

/* Parsing has no hardware side effects. Requests are rechecked for liveness
 * by the owner before sending, and independently validated by the node. */
bool master_command(const char *line, char *result, size_t size) {
  mecs_command_t parsed;
  bool success = false;
  if (!mecs_command_parse(line, &parsed)) {
    snprintf(result, size, "Rejected: invalid command syntax or value");
    return false;
  }
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  switch (parsed.kind) {
  case MECS_COMMAND_DISCOVER:
    discover_pending = true;
    success = true;
    break;
  case MECS_COMMAND_NODES:
    for (unsigned i = 0; i < MASTER_MAX_NODES; ++i) {
      if (nodes[i].used) {
        master_note("Node %u board=%u %s raw=0x%X logical=0x%X",
                    nodes[i].identity.node_id, nodes[i].identity.board_type,
                    master_online(&nodes[i], master_now_ms()) ? "online"
                                                              : "offline",
                    nodes[i].status.raw, nodes[i].status.logical);
      }
    }
    success = true;
    break;
  case MECS_COMMAND_HELP:
    master_note("discover | nodes | refresh N | get N CH PROPERTY | set N CH "
                "PROPERTY VALUE");
    master_note("on N CH | off N CH | pulse N CH. Global channel: 255");
    success = true;
    break;
  case MECS_COMMAND_REFRESH: {
    node_view_t *n = master_lookup(parsed.node);
    if (n) {
      n->refresh_cursor = 0;
      n->refreshing = true;
      success = true;
    }
    break;
  }
  case MECS_COMMAND_PROPERTY: {
    node_view_t *n = master_lookup(parsed.node);
    unsigned p = parsed.property & ~MECS_READ_FLAG;
    if (n && master_online(n, master_now_ms()) &&
        (mecs_properties[p].roles & master_role_of(n))) {
      const command_t command = {parsed.node, parsed.channel, parsed.property,
                                 parsed.value};
      success = xQueueSend(commands, &command, 0) == pdTRUE;
    }
    break;
  }
  }
  snprintf(result, size, "%s",
           success ? "Accepted; inspect confirmed values and event log"
                   : "Rejected: node offline, unsupported property, or command "
                     "queue full");
  xSemaphoreGiveRecursive(mutex);
  return success;
}
