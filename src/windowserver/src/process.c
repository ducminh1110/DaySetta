/*
 * Front process (see process.h). The sequence follows what macOS 27 sends after SetFrontProcessWithInfo:
 * the activation event, then notifications 0x5e4, 0x52d { 1, 0, window }, 0x38a, 0x388 and 0x390 { 0, window }.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge.h"
#include "input.h"
#include "process.h"
#include "shell.h"
#include "window.h"

#define DL_NOTE_FRONT_PROCESS_CHANGED 0x5e4
#define DL_NOTE_WINDOW_FRONT 0x52d
#define DL_NOTE_ACTIVATION_A 0x38a
#define DL_NOTE_ACTIVATION_B 0x388

/* The front process (PSN as AppKit gave it in SetFrontProcessWithInfo), answered by GetKeyFocusProcess. */
static struct {
	int32_t pid;
	uint32_t psn_high, psn_low;
} front_process;

static uint32_t first_window_of(dl_server *server, int32_t pid)
{
	dl_windows *w = server->windows;
	for (uint32_t i = 0; w && i < w->count; i++)
		if (w->windows[i].owner_pid == pid)
			return w->windows[i].id;
	return 0;
}

void dl_process_activate(dl_server *server, dl_connection *conn, uint32_t psn_low, uint32_t psn_high)
{
	dl_activation a = {
		.pid = (uint32_t)conn->pid,
		.connection = conn->id,
		.psn_low = psn_low,
		.psn_high = psn_high,
		.screen_height = server->canvases ? server->canvases->screen_height : 1080,
		.timestamp = dl_now_ticks(),
	};
	uint8_t buf[512];
	size_t n = dl_event_encode_activation(buf, sizeof(buf), &a);
	uint32_t window = first_window_of(server, conn->pid);

	if (n)
		dl_session_post_event(server, conn, buf, n);
	const uint32_t zero = 0, front[4] = { 0, 1, 0, window }, win[2] = { 0, window };
	dl_session_post(server, conn, DL_NOTE_FRONT_PROCESS_CHANGED, &zero, sizeof(zero));
	if (window)
		dl_session_post(server, conn, DL_NOTE_WINDOW_FRONT, front, sizeof(front));
	dl_session_post(server, conn, DL_NOTE_ACTIVATION_A, &zero, sizeof(zero));
	dl_session_post(server, conn, DL_NOTE_ACTIVATION_B, &zero, sizeof(zero));
	if (window)
		dl_session_post(server, conn, DL_NOTE_WINDOW_BECAME_VISIBLE, win, sizeof(win));
	front_process.pid = conn->pid;
	front_process.psn_high = psn_high;
	front_process.psn_low = psn_low;
	if (server->input) {
		server->input->focus_pid = conn->pid;
		if (window && !server->input->focus_window)
			server->input->focus_window = window;
	}
	dl_shell_set_front(conn->pid);
	if (server->log)
		fprintf(server->log, "daylight: pid %d (PSN %u.%u) is the front process\n", conn->pid, psn_high, psn_low);
}

/* SetFrontProcessWithInfo(PSN high, PSN low, options..., OOL plist with timestamps) */
static void set_front_process_with_info(const dl_request *req, dl_reply *reply, void *ctx)
{
	dl_server *server = ctx;
	size_t size;
	const uint8_t *in = dl_request_inline_data(req, &size);
	uint32_t psn[2] = { 0, 0 };
	(void)reply;

	if (in && size >= sizeof(psn))
		memcpy(psn, in, sizeof(psn));
	dl_connection *conn = server->session ? dl_session_connection_for_pid(server->session, req->client_pid) : NULL;
	if (conn && !getenv("DAYLIGHT_NO_ACTIVATE"))
		dl_process_activate(server, conn, psn[1], psn[0]);
}

/*
 * GetKeyFocusProcess() -> PSN high, PSN low, then a word that was 1 on macOS 27 (the recorded reply named the
 * Mac's front app). AppKit asks right after activating itself; given another process's PSN it stayed inactive (no
 * key window, no keyboard focus) and did not draw its window until an event came in.
 */
static void get_key_focus_process(const dl_request *req, dl_reply *reply, void *ctx)
{
	(void)req;
	(void)ctx;
	const uint32_t out[3] = { front_process.psn_high, front_process.psn_low, front_process.pid ? 1 : 0 };
	dl_reply_put(reply, out, sizeof(out));
}

void dl_processes_init(dl_server *server)
{
	dl_register(server, DL_MSG_SetFrontProcessWithInfo, set_front_process_with_info, server);
	dl_register(server, DL_MSG_GetKeyFocusProcess, get_key_focus_process, server);
}
