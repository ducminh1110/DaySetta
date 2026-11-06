/*
 * Windows: creation and the window table.
 *
 * CreateWindow(connection, OOL shape region, flags...) -> (window shmem entry, window id, tag, three words).
 * The port is a memory entry SkyLight maps as the window's shared state (CGSWindowConstructInternal, 0xb0 bytes);
 * the tag also appears three times at offset 0x98 of that region. The last three words are returned as a real
 * window server returned them (0x00030007, 1, 0xdf7d28d9 on macOS 27); their meaning is still to be worked out.
 */
#include <string.h>

#include "session.h"
#include "window.h"

#define DL_FIRST_WINDOW_ID 0x1f6
#define DL_WINDOW_SHM_SIZE 0xb0
#define DL_WINDOW_TAG 0x000924a3

/* Window shared state as a fresh window has it (recorded from macOS 27): seeds, identity transforms. */
static const uint8_t window_shm_template[DL_WINDOW_SHM_SIZE] = {
	0x01, 0, 0, 0, 0x01, 0, 0, 0, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x3f, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0x80, 0x3f, 0, 0, 0, 0x80, 0, 0, 0, 0x80, 0, 0, 0x80, 0x3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0x80, 0x3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x3f, 0, 0, 0, 0, 0, 0, 0, 0x80,
	0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 0x80, 0x3f, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa3, 0x24, 0x09, 0, 0xa3, 0x24, 0x09, 0,
	0xa3, 0x24, 0x09, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static void create_window(const dl_request *req, dl_reply *reply, void *ctx)
{
	dl_server *server = ctx;
	dl_windows *windows = server->windows;
	const uint8_t *body = req->body;

	if (windows->count >= DL_MAX_WINDOWS || !server->host.new_shared_memory) {
		reply->retcode = DL_KERN_RESOURCE_SHORTAGE;
		return;
	}
	dl_window *w = &windows->windows[windows->count];
	memset(w, 0, sizeof(*w));
	void *shm = NULL;
	w->port = server->host.new_shared_memory(server->host.ctx, window_shm_template, sizeof(window_shm_template), &shm);
	w->shm = shm;
	if (!w->port) {
		reply->retcode = DL_KERN_RESOURCE_SHORTAGE;
		return;
	}
	w->id = windows->next_id++;
	w->owner_pid = req->client_pid;
	/* body: descriptor count, one OOL descriptor (16 bytes), NDR, flags... */
	if (req->complex && req->body_size >= 4 + 16) {
		uint32_t size;
		memcpy(&size, body + 4 + 12, 4);
		const void *addr;
		memcpy(&addr, body + 4, sizeof(addr));
		if (addr && size <= sizeof(w->region)) {
			memcpy(w->region, addr, size);
			w->region_size = size;
		}
	}
	if (server->log) {
		fprintf(server->log, "daylight: window region (%u bytes):", w->region_size);
		for (unsigned i = 0; i < w->region_size; i++)
			fprintf(server->log, " %02x", w->region[i]);
		fprintf(server->log, "\n");
	}
	windows->count++;
	if (server->log)
		fprintf(server->log, "daylight: window 0x%x created for pid %d\n", w->id, w->owner_pid);

	const uint32_t out[5] = { w->id, DL_WINDOW_TAG, 0x00030007, 1, 0xdf7d28d9 };
	dl_reply_add_port(reply, w->port, DL_PORT_COPY_SEND);
	dl_reply_put(reply, out, sizeof(out));
}

/* Rewrites the window table file atomically (write + rename). */
static void publish(dl_windows *windows)
{
	FILE *f = fopen(DL_WINDOWS_FILE ".new", "w");
	if (!f)
		return;
	for (uint32_t i = 0; i < windows->count; i++) {
		const dl_window *w = &windows->windows[i];
		fprintf(f, "%u %d %u", w->id, w->owner_pid, w->context_id);
		if (w->shm)
			for (int k = 0; k < 0xb0 / 4; k++)
				fprintf(f, " %08x", ((const uint32_t *)w->shm)[k]);
		fprintf(f, "\n");
	}
	fclose(f);
	rename(DL_WINDOWS_FILE ".new", DL_WINDOWS_FILE);
}

/* SetWindowLayerContext(window, context id): simpleroutine. */
static void set_window_layer_context(const dl_request *req, dl_reply *reply, void *ctx)
{
	dl_server *server = ctx;
	size_t size;
	const uint8_t *in = dl_request_data(req, &size);
	uint32_t wid, cid;

	(void)reply;
	if (!in || size < 8)
		return;
	memcpy(&wid, in, 4);
	memcpy(&cid, in + 4, 4);
	dl_window *w = dl_window_find(server->windows, wid);
	if (!w)
		return;
	w->context_id = cid;
	if (server->log)
		fprintf(server->log, "daylight: window 0x%x drawn by CA context 0x%x\n", wid, cid);
	publish(server->windows);

	/*
	 * Every window is on screen and unobscured for now: AppKit only draws the views of windows whose occlusion
	 * state is visible, and that state comes from these notifications.
	 * FIXME: send them when the window is ordered in (inside the transactions, not decoded yet).
	 */
	dl_connection *conn = server->session ? dl_session_connection_for_pid(server->session, w->owner_pid) : NULL;
	if (conn) {
		const uint32_t note[2] = { 0, wid };
		dl_session_post(server, conn, DL_NOTE_WINDOW_SPACE_MEMBERSHIP_CHANGED, note, sizeof(note));
		dl_session_post(server, conn, DL_NOTE_WINDOW_BECAME_VISIBLE, note, sizeof(note));
	}
}

/*
 * SetTransactionCompletion(port [make-send], ...): sent right after TransactionCommitAsync. The window server sends
 * an empty-id message carrying a timestamp (8 bytes) to the port once the transaction is applied; SkyLight
 * (_SLSTransactionWaitSource) waits for it, and AppKit commits nothing more to Core Animation until it arrives.
 * Transactions are not decoded yet, so they complete at once.
 */
static void set_transaction_completion(const dl_request *req, dl_reply *reply, void *ctx)
{
	dl_server *server = ctx;
	dl_request_ports in;

	(void)reply;
	if (dl_request_parse_ports(req, &in) != 0 || in.count < 1 || !server->host.send_once) {
		if (server->log)
			fprintf(server->log, "daylight: SetTransactionCompletion without a port (complex=%d, %zu bytes)\n",
				req->complex, req->body_size);
		return;
	}
	uint64_t when = dl_now_ticks();
	server->host.send_once(server->host.ctx, in.names[0], 0, &when, sizeof(when));
}

/*
 * WindowIsOrderedIn(window) -> (ordered in). Ordering travels inside transactions, which are not decoded yet: a
 * window counts as on screen once Core Animation draws it (it has a layer context).
 */
static void window_is_ordered_in(const dl_request *req, dl_reply *reply, void *ctx)
{
	dl_server *server = ctx;
	size_t size;
	const uint8_t *in = dl_request_data(req, &size);
	uint32_t wid = 0, ordered = 0;

	if (in && size >= 4)
		memcpy(&wid, in, 4);
	dl_window *w = dl_window_find(server->windows, wid);
	if (w && w->context_id)
		ordered = 1;
	dl_reply_put(reply, &ordered, sizeof(ordered));
}

/*
 * CopySpacesForWindows(OOL window ids, mask...) -> (OOL space ids as u64, count). There is one space, id 1 (what
 * macOS reports for the first desktop); every window known to the server is on it.
 */
static void copy_spaces_for_windows(const dl_request *req, dl_reply *reply, void *ctx)
{
	dl_server *server = ctx;
	static const uint64_t space = 1;
	uint32_t count = 0;
	const uint8_t *body = req->body;

	if (req->complex && req->body_size >= 4 + 16) {
		const uint32_t *ids;
		uint32_t size;
		memcpy(&ids, body + 4, sizeof(ids));
		memcpy(&size, body + 4 + 12, 4);
		for (uint32_t i = 0; ids && i < size / 4; i++)
			if (dl_window_find(server->windows, ids[i]))
				count = 1;
	}
	dl_reply_set_ool(reply, count ? &space : NULL, count ? sizeof(space) : 0);
	dl_reply_put(reply, &count, sizeof(count));
}

dl_window *dl_window_find(dl_windows *windows, uint32_t id)
{
	for (uint32_t i = 0; i < windows->count; i++)
		if (windows->windows[i].id == id)
			return &windows->windows[i];
	return NULL;
}

void dl_windows_init(dl_windows *windows, dl_server *server)
{
	memset(windows, 0, sizeof(*windows));
	windows->next_id = DL_FIRST_WINDOW_ID;
	server->windows = windows;
	dl_register(server, DL_MSG_CreateWindow, create_window, server);
	dl_register(server, DL_MSG_SetWindowLayerContext, set_window_layer_context, server);
	dl_register(server, DL_MSG_SetTransactionCompletion, set_transaction_completion, server);
	dl_register(server, DL_MSG_WindowIsOrderedIn, window_is_ordered_in, server);
	dl_register(server, DL_MSG_CopySpacesForWindows, copy_spaces_for_windows, server);
}
