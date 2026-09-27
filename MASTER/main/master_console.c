/* USB serial input. Wi-Fi credentials are accepted only on this console. */
#include "master.h"
#include <stdio.h>
#include <string.h>

void master_console_poll(void) {
  static char line[192];
  static unsigned used;
  static bool overflow;
  for (unsigned budget = 0; budget < 64; ++budget) {
    int c = getchar();
    if (c == EOF) {
      clearerr(stdin);
      break;
    }
    if (c == '\r' || c == '\n') {
      if (overflow) {
        master_note("Console line too long; discarded");
      } else if (used) {
        line[used] = 0;
        /* Credentials are accepted only on USB serial and are never
         * echoed into the web event log. Split at the first | so a
         * password can itself contain | characters. */
        if (!strncmp(line, "wifi ", 5)) {
          char *separator = strchr(line + 5, '|');
          if (separator) {
            *separator = 0;
            master_note("Wi-Fi settings %s",
                        network_credentials(line + 5, separator + 1)
                            ? "saved; connecting"
                            : "rejected");
          } else {
            master_note("Wi-Fi syntax: wifi SSID|PASSWORD (USB serial only)");
          }
        } else {
          char result[128];
          master_command(line, result, sizeof(result));
          master_note("%s", result);
        }
      }
      memset(line, 0, sizeof(line));
      used = 0;
      overflow = false;
    } else if (c == 8 || c == 127) {
      if (used) {
        --used;
      }
    } else if (c >= 32 && c <= 126 && !overflow) {
      if (used + 1 < sizeof(line)) {
        line[used++] = c;
      } else {
        overflow = true;
      }
    }
  }
}
