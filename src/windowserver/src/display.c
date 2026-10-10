/*
 * Display and event shared state: GetEventShmem, GetDisplayShmem, GetDisplaySystemState and
 * GetRealtimeDisplayDataShmemPort.
 *
 * Shared regions are seeded from a real window server; the display state is Daylight's own (one display, in the
 * layout SkyLight 26.6 reads, see tools/display/make_state.py).
 */
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "dl_canned.h"
#include "dl_display_state.h"
#include "dl_shm_seed.h"

static uint32_t shared(dl_server *server, const uint8_t *init, size_t size, void **mapped)
{
	if (!server->host.new_shared_memory)
		return 0;
	return server->host.new_shared_memory(server->host.ctx, init, size, mapped);
}

/* GetEventShmem() -> (scoreboard entry, event source entry); see CGSScoreboard in SkyLight. */
static void get_event_shmem(const dl_request *req, dl_reply *reply, void *ctx)
{
	(void)req;
	dl_display *d = ((dl_server *)ctx)->display;
	dl_reply_add_port(reply, d->event_scoreboard_port, DL_PORT_COPY_SEND);
	dl_reply_add_port(reply, d->event_source_port, DL_PORT_COPY_SEND);
}

/* GetDisplayShmem() -> three entries, mapped by SkyLight's initDisplayState in this order. */
static void get_display_shmem(const dl_request *req, dl_reply *reply, void *ctx)
{
	(void)req;
	dl_display *d = ((dl_server *)ctx)->display;
	for (int i = 0; i < 3; i++)
		dl_reply_add_port(reply, d->display_ports[i], DL_PORT_COPY_SEND);
}

/*
 * GetDisplaySystemState(seed) -> (OOL state, size). The client polls with the seed of the state it holds; the
 * blob carries its own seed at offset 4, and a client already holding it gets an empty buffer.
 */
static void get_display_system_state(const dl_request *req, dl_reply *reply, void *ctx)
{
	dl_display *d = ((dl_server *)ctx)->display;
	size_t size;
	const uint8_t *in = dl_request_data(req, &size);
	uint32_t seed = 0, count;

	if (in && size >= 4)
		memcpy(&seed, in, 4);
	if (seed == d->state_seed || getenv("DAYLIGHT_EMPTY_DISPLAY_STATE")) {
		dl_reply_set_ool(reply, NULL, 0);
		count = 0;
	} else {
		dl_reply_set_ool(reply, d->state, d->state_size);
		count = d->state_size;
	}
	dl_reply_put(reply, &count, sizeof(count));
}

/*
 * Realtime display data, one region per display, read by SkyLight's display_get_timing_common
 * (SLSDisplayGetTiming, which AppKit's NSDisplayTiming calls for every event): u64 refresh period at 0x08
 * (mach time, nanoseconds on Darling), vsync base time at 0x28, period again at 0x30, 0x38 must not be negative
 * (else SkyLight falls back to the legacy DisplayGetRefreshTiming interface), valid flag (u8 1) at 0x88. Without it
 * SkyLight asks again on every call, and AppKit's event handling spins.
 */
#define DL_REALTIME_SIZE 4096
#define DL_REFRESH_PERIOD_NS 16666667ull

static void get_realtime_display_data(const dl_request *req, dl_reply *reply, void *ctx)
{
	(void)req;
	dl_display *d = ((dl_server *)ctx)->display;
	dl_reply_add_port(reply, d->realtime_port, DL_PORT_COPY_SEND);
}

static void init_realtime(dl_display *display, dl_server *server)
{
	uint8_t *init = calloc(1, DL_REALTIME_SIZE);
	uint64_t period = DL_REFRESH_PERIOD_NS, base = dl_now_ticks();
	if (!init || getenv("DAYLIGHT_NO_REALTIME")) {
		free(init);
		return;
	}
	memcpy(init + 0x08, &period, 8);
	memcpy(init + 0x28, &base, 8);
	memcpy(init + 0x30, &period, 8);
	init[0x88] = 1;
	display->realtime_port = shared(server, init, DL_REALTIME_SIZE, &display->realtime);
	free(init);
	if (display->realtime_port)
		dl_register(server, DL_MSG_GetRealtimeDisplayDataShmemPort, get_realtime_display_data, server);
}

int dl_display_init(dl_display *display, dl_server *server)
{
	memset(display, 0, sizeof(*display));
	server->display = display;

	display->event_scoreboard_port =
		shared(server, dl_shm_event_scoreboard, dl_shm_event_scoreboard_size, &display->event_scoreboard);
	display->event_source_port = shared(server, dl_shm_event_source, dl_shm_event_source_size, &display->event_source);
	display->display_ports[0] = shared(server, dl_shm_display_a, dl_shm_display_a_size, &display->display[0]);
	display->display_ports[1] = shared(server, dl_shm_display_b, dl_shm_display_b_size, &display->display[1]);
	display->display_ports[2] = shared(server, dl_shm_display_c, dl_shm_display_c_size, &display->display[2]);

	display->state = dl_display_state;
	display->state_size = dl_display_state_size;
	display->state_seed = dl_display_state_seed;

	if (display->event_scoreboard_port) {
		dl_register(server, DL_MSG_GetEventShmem, get_event_shmem, server);
		dl_register(server, DL_MSG_GetDisplayShmem, get_display_shmem, server);
	}
	if (display->state)
		dl_register(server, DL_MSG_GetDisplaySystemState, get_display_system_state, server);
	init_realtime(display, server);
	return 0;
}
