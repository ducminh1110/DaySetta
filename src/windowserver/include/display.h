#ifndef DAYLIGHT_DISPLAY_H
#define DAYLIGHT_DISPLAY_H

#include <stdint.h>

#include "daylight.h"

/*
 * Display and event state shared with every client: the regions SkyLight maps at start-up (event scoreboard and
 * event source, three display-state regions) and the display-system-state blob it fetches by seed.
 */
typedef struct dl_display {
	uint32_t event_scoreboard_port, event_source_port;
	uint32_t display_ports[3];
	void *event_scoreboard, *event_source, *display[3];
	const uint8_t *state;      /* serialized display system state (GetDisplaySystemState) */
	uint32_t state_size;
	uint32_t state_seed;       /* the seed stored in the blob; a client holding it gets no data */
	uint32_t realtime_port;    /* realtime display data (refresh timing), GetRealtimeDisplayDataShmemPort */
	void *realtime;
} dl_display;

/* Creates the shared regions (seeded from the recorded ones) and registers the display routines. */
int dl_display_init(dl_display *display, dl_server *server);

#endif
