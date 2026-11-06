/*
 * Processes: which one is in front. AppKit asks with SetFrontProcessWithInfo (when it activates itself, e.g. on a
 * click into one of its windows); the server answers the new front process with an activation event and window
 * notifications, after which AppKit is active and has a key window.
 */
#ifndef DAYLIGHT_PROCESS_H
#define DAYLIGHT_PROCESS_H

#include <stdint.h>

#include "daylight.h"
#include "session.h"

void dl_processes_init(dl_server *server);

/* Makes the connection's process the front process (also used when an app shows its first window). */
void dl_process_activate(dl_server *server, dl_connection *conn, uint32_t psn_low, uint32_t psn_high);

#endif
