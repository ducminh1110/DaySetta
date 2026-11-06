#ifndef DAYLIGHT_WINDOW_H
#define DAYLIGHT_WINDOW_H

#include <stdint.h>

#include "daylight.h"

#define DL_MAX_WINDOWS 1024

typedef struct dl_window {
	uint32_t id;
	uint32_t port;        /* memory entry for the window's shared state (0xb0 bytes), handed to the owner */
	uint8_t *shm;         /* the server's mapping of it */
	int32_t owner_pid;
	uint32_t context_id;  /* Core Animation context drawing the window (SetWindowLayerContext) */
	uint8_t region[64];   /* shape region as sent by the client (opaque for now) */
	uint32_t region_size;
} dl_window;

typedef struct dl_windows {
	dl_window windows[DL_MAX_WINDOWS];
	uint32_t count;
	uint32_t next_id;
} dl_windows;

void dl_windows_init(dl_windows *windows, dl_server *server);

/* Where the window table is published for the renderer (one line per window: id pid context). */
#define DL_WINDOWS_FILE "/private/tmp/daylight-windows.txt"
dl_window *dl_window_find(dl_windows *windows, uint32_t id);

#endif
