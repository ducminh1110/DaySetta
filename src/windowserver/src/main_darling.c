/*
 * Mach front end: runs inside Darling as the com.apple.windowserver services.
 *
 * Usage (inside a Darling shell, launched by launchd or by hand):
 *   DAYLIGHT_TRACE=/tmp/daylight-trace.json daylight
 * then start an app; the trace lists every WindowServer routine it used, most-called first.
 */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <servers/bootstrap.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <bsm/libbsm.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include "daylight.h"
#include "bridge.h"
#include "input.h"
#include "process.h"
#include "framebuffer.h"
#include "display.h"
#include "window.h"
#include "dl_canned.h"
#include "dl_reply_sizes.h"
#include "session.h"
#include "shell.h"

/* Darling's memory entries (memfd-backed fileports, see libsystem_kernel memory_entry.c). */
extern kern_return_t darling_memory_entry_create(mach_vm_size_t size, mach_port_t *entry) __attribute__((weak_import));

#define DL_MAX_MSG (64 * 1024)

static const char *const services[] = {
	"com.apple.windowserver",
	"com.apple.windowserver.active",
};

/*
 * com.apple.CARenderServer is served by daylight-ca (QuartzCore's own render server on the macOS userland) when
 * it runs; DAYLIGHT_SERVE_CA=1 makes this process answer it with recorded replies instead.
 */
static const char *const ca_service = "com.apple.CARenderServer";

static volatile sig_atomic_t stop_requested;

static void on_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

typedef struct {
	mach_msg_header_t head;
	NDR_record_t ndr;
	kern_return_t retcode;
	char body[DL_MAX_MSG];
} dl_mig_reply;

typedef struct {
	mach_msg_header_t head;
	mach_msg_body_t body;
	mach_msg_port_descriptor_t ports[DL_MAX_PORTS];
	mach_msg_ool_descriptor_t ool;
	/* NDR record and data follow the used descriptors. */
	char rest[sizeof(NDR_record_t) + DL_MAX_MSG];
} dl_mig_complex_reply;

static mach_port_t g_port_set;

/* dl_host.new_port: a receive right the server listens on, with a send right to hand out. */
static uint32_t new_server_port(void *ctx)
{
	(void)ctx;
	mach_port_t port;
	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) != KERN_SUCCESS)
		return 0;
	if (mach_port_move_member(mach_task_self(), port, g_port_set) != KERN_SUCCESS) {
		mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
		return 0;
	}
	return port;
}

/* dl_host.new_shared_memory */
static uint32_t new_shared_memory(void *ctx, const void *init, size_t size, void **mapped)
{
	(void)ctx;
	mach_port_t entry = MACH_PORT_NULL;
	mach_vm_address_t addr = 0;
	mach_vm_size_t rounded = (size + vm_page_size - 1) & ~((mach_vm_size_t)vm_page_size - 1);

	if (!darling_memory_entry_create) {
		/*
		 * DaySetta fallback (ESC-G1-1): this Darling has neither memory entries nor working fileports /
		 * mach_make_memory_entry (darlingserver RPCs are down), so no memory can cross a process boundary as a
		 * Mach right. Back the region by a host file (shm-N in DSWS_SHM_DIR, "shm-last" -> newest) and hand out
		 * a placeholder send right; the probe client maps the file by path. Genuine SkyLight needs the real entry.
		 */
		static unsigned seq;
		const char *dir = getenv("DSWS_SHM_DIR");
		char path[1100], last[1100];
		snprintf(path, sizeof(path), "%s/shm-%u", dir ? dir : "/Volumes/SystemRoot/tmp/dsws", ++seq);
		snprintf(last, sizeof(last), "%s/shm-last", dir ? dir : "/Volumes/SystemRoot/tmp/dsws");
		mkdir(dir ? dir : "/Volumes/SystemRoot/tmp/dsws", 0777);
		int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
		void *p = fd >= 0 && ftruncate(fd, (off_t)rounded) == 0 ?
			mmap(NULL, rounded, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
		if (fd >= 0)
			close(fd);
		if (p == MAP_FAILED || mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &entry) != KERN_SUCCESS ||
			mach_port_insert_right(mach_task_self(), entry, entry, MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
			fprintf(stderr, "dsws: shared memory fallback failed: %s\n", strerror(errno));
			return 0;
		}
		unlink(last);
		symlink(path, last);
		memcpy(p, init, size);
		*mapped = p;
		return entry;
	}
	if (darling_memory_entry_create(rounded, &entry) != KERN_SUCCESS)
		return 0;
	if (mach_vm_map(mach_task_self(), &addr, rounded, 0, VM_FLAGS_ANYWHERE, entry, 0, FALSE,
			VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE) != KERN_SUCCESS) {
		mach_port_deallocate(mach_task_self(), entry);
		return 0;
	}
	memcpy((void *)(uintptr_t)addr, init, size);
	*mapped = (void *)(uintptr_t)addr;
	return entry;
}

/* dl_host.notify: an empty message to a client's notification port; never blocks the server. */
static void notify_client(void *ctx, uint32_t port)
{
	(void)ctx;
	mach_msg_header_t m = { 0 };
	m.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
	m.msgh_size = sizeof(m);
	m.msgh_remote_port = port;
	m.msgh_id = 0;
	mach_msg(&m, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(m), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
}

/* dl_host.send_once */
static void send_once(void *ctx, uint32_t port, int32_t id, const void *body, size_t size)
{
	(void)ctx;
	struct {
		mach_msg_header_t head;
		char body[256];
	} m;
	if (size > sizeof(m.body))
		size = sizeof(m.body);
	memset(&m.head, 0, sizeof(m.head));
	m.head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND, 0);
	m.head.msgh_size = (mach_msg_size_t)(sizeof(m.head) + ((size + 3) & ~(size_t)3));
	m.head.msgh_remote_port = port;
	m.head.msgh_id = id;
	memcpy(m.body, body, size);
	kern_return_t kr = mach_msg(&m.head, MACH_SEND_MSG | MACH_SEND_TIMEOUT, m.head.msgh_size, 0, MACH_PORT_NULL, 0,
		MACH_PORT_NULL);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "daylight: message %d to port 0x%x failed: 0x%x\n", id, port, kr);
		mach_port_deallocate(mach_task_self(), port);
	}
}

/*
 * Files shared with the display front end (daylight-x11, a Linux program) live on the host's file system, by
 * default /tmp/daylight there: inside the prefix's overlay a FIFO opened from Darling and the same FIFO opened from
 * the host are different pipes. DAYLIGHT_RUNTIME_DIR (a Darling path) overrides it.
 */
static char runtime_dir[1024] = "/Volumes/SystemRoot/tmp/daylight";

static void runtime_path(char *out, size_t size, const char *name)
{
	snprintf(out, size, "%s/%s", runtime_dir, name);
}

/*
 * The composited screen goes to screen.fb in the runtime directory (framebuffer.h) for the display front end, at
 * most every 16 ms after canvas updates, and to desktop.png (tests, screenshots) at most every
 * DAYLIGHT_PNG_INTERVAL_MS (default 1000; 0 disables it).
 */
static dl_fb_header *fb;

static void open_framebuffer(const dl_canvases *c)
{
	char path[1100];
	runtime_path(path, sizeof(path), "screen.fb");
	size_t stride = (size_t)c->screen_width * 4, size = DL_FB_HEADER_SIZE + stride * c->screen_height;
	int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
		fprintf(stderr, "daylight: cannot create %s: %s\n", path, strerror(errno));
		if (fd >= 0)
			close(fd);
		return;
	}
	fchmod(fd, 0666);
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (p == MAP_FAILED)
		return;
	fb = p;
	fb->width = c->screen_width;
	fb->height = c->screen_height;
	fb->stride = (uint32_t)stride;
	fb->version = DL_FB_VERSION;
	fb->frame = 0;
	fb->magic = DL_FB_MAGIC;
}

static void composite_if_due(dl_server *server)
{
	static uint8_t *screen;
	static uint64_t last, last_png;
	static int png_pending;
	static long png_interval = -1;
	dl_canvases *c = server->canvases;

	if (!c)
		return;
	if (png_interval < 0) {
		const char *e = getenv("DAYLIGHT_PNG_INTERVAL_MS");
		png_interval = e ? atol(e) : 1000;
	}
	uint64_t now = clock_gettime_nsec_np(CLOCK_MONOTONIC);
	if (dl_shell_needs_redraw())
		c->dirty = 1;
	if (c->dirty && now - last >= 16000000ull) {
		if (!screen)
			screen = malloc((size_t)c->screen_width * c->screen_height * 4);
		if (!screen)
			return;
		dl_canvases_composite(c, screen);
		if (fb) {
			uint8_t *dst = (uint8_t *)fb + DL_FB_HEADER_SIZE;
			const uint8_t *src = screen;
			fb->frame++;
			__sync_synchronize();
			for (size_t i = 0, n = (size_t)c->screen_width * c->screen_height; i < n; i++, src += 4, dst += 4) {
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
				dst[3] = 0;
			}
			__sync_synchronize();
			fb->frame++;
		}
		c->dirty = 0;
		last = now;
		png_pending = 1;
	}
	if (png_pending && png_interval > 0 && c->screen_path && screen &&
		now - last_png >= (uint64_t)png_interval * 1000000ull) {
		dl_write_png(c->screen_path, screen, c->screen_width, c->screen_height);
		png_pending = 0;
		last_png = now;
	}
}

/* Input commands (input.h) arrive as text lines on the FIFO "input" there, from the front end or a test. */
static int input_fd = -1;

static void open_input(void)
{
	char path[1100];
	runtime_path(path, sizeof(path), "input");
	unlink(path);
	if (mkfifo(path, 0666) != 0) {
		fprintf(stderr, "daylight: cannot create %s: %s\n", path, strerror(errno));
		return;
	}
	chmod(path, 0666);
	input_fd = open(path, O_RDONLY | O_NONBLOCK);
	/* keep a writer open so that reads return EAGAIN rather than end-of-file between front-end sessions */
	if (input_fd >= 0)
		open(path, O_WRONLY | O_NONBLOCK);
}

static void poll_input(dl_server *server)
{
	static char buf[4096];
	static size_t len;

	if (input_fd < 0)
		return;
	for (;;) {
		ssize_t n = read(input_fd, buf + len, sizeof(buf) - 1 - len);
		if (n <= 0)
			break;
		len += (size_t)n;
		buf[len] = 0;
		char *line = buf, *nl;
		while ((nl = strchr(line, '\n'))) {
			*nl = 0;
			dl_input_command(server, line);
			line = nl + 1;
		}
		len -= (size_t)(line - buf);
		memmove(buf, line, len);
		if (len == sizeof(buf) - 1)
			len = 0; /* overlong line */
	}
}

/* Builds the reply for a dispatched request; returns its size. */
static mach_msg_size_t build_reply(const mach_msg_header_t *request, const dl_reply *out, void *buf)
{
	if (out->retcode == KERN_SUCCESS && (out->nports > 0 || out->has_ool)) {
		dl_mig_complex_reply *reply = buf;
		char *p;

		reply->head.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(request->msgh_bits), 0) | MACH_MSGH_BITS_COMPLEX;
		reply->body.msgh_descriptor_count = (mach_msg_size_t)(out->nports + (out->has_ool ? 1 : 0));
		for (size_t i = 0; i < out->nports; i++) {
			memset(&reply->ports[i], 0, sizeof(reply->ports[i]));
			reply->ports[i].name = out->ports[i];
			reply->ports[i].disposition = out->port_dispositions[i];
			reply->ports[i].type = MACH_MSG_PORT_DESCRIPTOR;
		}
		p = (char *)&reply->ports[out->nports];
		if (out->has_ool) {
			mach_msg_ool_descriptor_t *d = (mach_msg_ool_descriptor_t *)p;
			memset(d, 0, sizeof(*d));
			d->address = (void *)out->ool;
			d->size = (mach_msg_size_t)out->ool_size;
			d->deallocate = FALSE;
			d->copy = MACH_MSG_VIRTUAL_COPY;
			d->type = MACH_MSG_OOL_DESCRIPTOR;
			p += sizeof(*d);
		}
		if (out->body_size > 0) {
			memcpy(p, &NDR_record, sizeof(NDR_record));
			p += sizeof(NDR_record);
			memcpy(p, out->body, out->body_size);
			p += out->body_size;
		}
		return (mach_msg_size_t)(p - (char *)reply);
	}

	dl_mig_reply *reply = buf;
	reply->head.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(request->msgh_bits), 0);
	reply->ndr = NDR_record;
	reply->retcode = out->retcode;
	if (out->retcode != KERN_SUCCESS)
		return (mach_msg_size_t)offsetof(dl_mig_reply, body);
	memmove(reply->body, out->body, out->body_size);
	return (mach_msg_size_t)(offsetof(dl_mig_reply, body) + out->body_size);
}

/*
 * Replays a reply recorded on a real Mac for a routine Daylight does not implement yet: same body bytes, fresh
 * Daylight ports in place of the recorded port descriptors, recorded bytes for out-of-line data.
 */
static mach_msg_size_t build_canned_reply(const mach_msg_header_t *request, const dl_canned_reply *c, void *buf,
	size_t capacity)
{
	mach_msg_header_t *head = buf;
	char *p = (char *)(head + 1);
	char *end = (char *)buf + capacity;

	head->msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(request->msgh_bits), 0);
	if (c->complex) {
		head->msgh_bits |= MACH_MSGH_BITS_COMPLEX;
		((mach_msg_body_t *)p)->msgh_descriptor_count = c->ndesc;
		p += sizeof(mach_msg_body_t);
		for (unsigned i = 0; i < c->ndesc; i++) {
			if (c->desc[i].type == DL_CANNED_PORT) {
				mach_msg_port_descriptor_t *d = (void *)p;
				memset(d, 0, sizeof(*d));
				d->name = new_server_port(NULL);
				d->disposition = MACH_MSG_TYPE_MAKE_SEND;
				d->type = MACH_MSG_PORT_DESCRIPTOR;
				p += sizeof(*d);
			} else {
				mach_msg_ool_descriptor_t *d = (void *)p;
				static char ool[16][64 * 1024];
				static unsigned next;
				char *data = ool[next++ % 16];
				size_t size = c->desc[i].size < sizeof(ool[0]) ? c->desc[i].size : sizeof(ool[0]);
				memset(data, 0, size);
				memcpy(data, c->desc[i].data, c->desc[i].size < 4096 ? c->desc[i].size : 4096);
				memset(d, 0, sizeof(*d));
				d->address = data;
				d->size = (mach_msg_size_t)size;
				d->deallocate = 0;
				d->copy = MACH_MSG_VIRTUAL_COPY;
				d->type = MACH_MSG_OOL_DESCRIPTOR;
				p += sizeof(*d);
			}
		}
	}
	if (p + c->body_size > end)
		return 0;
	memcpy(p, c->body, c->body_size);
	p += c->body_size;
	return (mach_msg_size_t)(p - (char *)buf);
}

static int32_t sender_pid(mach_msg_header_t *msg)
{
	mach_msg_audit_trailer_t *trailer = (mach_msg_audit_trailer_t *)((char *)msg + round_msg(msg->msgh_size));
	if (trailer->msgh_trailer_size < sizeof(*trailer))
		return -1;
	return audit_token_to_pid(trailer->msgh_audit);
}

/*
 * The serve loop wakes up every 15 ms (input from the front end, compositing) without receiving with a timeout:
 * a thread sends an empty "tick" message to a port in the set instead. On Darling a receive with a timeout
 * (MACH_RCV_TIMEOUT) on the port set, repeated every 15 ms, now and then lost a client's request (the client
 * waited forever for the reply; Daylight never saw the message). DAYLIGHT_TIMED_RECEIVE=1 restores the timeout.
 */
#define DL_TICK_ID 0x44594cff
#define DL_TICK_MS 15

static void *tick_thread(void *arg)
{
	mach_port_t port = (mach_port_t)(uintptr_t)arg;
	for (;;) {
		struct timespec ts = { 0, DL_TICK_MS * 1000000L };
		nanosleep(&ts, NULL);
		mach_msg_header_t m = { 0 };
		m.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
		m.msgh_size = sizeof(m);
		m.msgh_remote_port = port;
		m.msgh_id = DL_TICK_ID;
		/* never queue up ticks: the port holds one message at most */
		mach_msg(&m, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(m), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
	}
	return NULL;
}

static int start_ticks(mach_port_t port_set)
{
	mach_port_t port;
	mach_port_limits_t limits = { .mpl_qlimit = 1 };
	pthread_t thread;
	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) != KERN_SUCCESS)
		return 0;
	mach_port_set_attributes(mach_task_self(), port, MACH_PORT_LIMITS_INFO, (mach_port_info_t)&limits,
		MACH_PORT_LIMITS_INFO_COUNT);
	if (mach_port_move_member(mach_task_self(), port, port_set) != KERN_SUCCESS)
		return 0;
	return pthread_create(&thread, NULL, tick_thread, (void *)(uintptr_t)port) == 0;
}

static void serve(dl_server *server, mach_port_t port_set)
{
	static union {
		mach_msg_header_t head;
		char bytes[DL_MAX_MSG + MAX_TRAILER_SIZE];
	} request;
	static char reply_buf[sizeof(dl_mig_complex_reply)];
	static char reply_data[DL_MAX_MSG];
	mach_msg_header_t *reply = (mach_msg_header_t *)reply_buf;
	mach_msg_option_t rcv = MACH_RCV_MSG | MACH_RCV_LARGE |
		MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) | MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT);
	if (getenv("DAYLIGHT_TIMED_RECEIVE") || !start_ticks(port_set))
		rcv |= MACH_RCV_TIMEOUT;

	while (!stop_requested) {
		kern_return_t kr = mach_msg(&request.head, rcv, 0, sizeof(request), port_set, DL_TICK_MS, MACH_PORT_NULL);
		poll_input(server);
		if (kr == KERN_SUCCESS && request.head.msgh_id == DL_TICK_ID) {
			mach_msg_destroy(&request.head);
			kr = MACH_RCV_TIMED_OUT;
		}
		if (kr == MACH_RCV_TIMED_OUT) {
			composite_if_due(server);
			continue;
		}
		if (kr != KERN_SUCCESS) {
			fprintf(stderr, "daylight: mach_msg receive failed: %s\n", mach_error_string(kr));
			continue;
		}

		dl_request req = {
			.id = request.head.msgh_id,
			.body = &request.head + 1,
			.body_size = request.head.msgh_size - sizeof(mach_msg_header_t),
			.client_pid = sender_pid(&request.head),
			.local_port = request.head.msgh_local_port,
			.complex = (request.head.msgh_bits & MACH_MSGH_BITS_COMPLEX) != 0,
		};
		/* DAYLIGHT_DUMP_PID=<pid>|all: hex-dump every request body from that client (wire format work). */
		const char *dump_pid = getenv("DAYLIGHT_DUMP_PID");
		if (dump_pid && server->log && (strcmp(dump_pid, "all") == 0 || atoi(dump_pid) == req.client_pid) &&
			req.id != DL_TICK_ID) {
			fprintf(server->log, "dump %u c=%d n=%u:", req.id, req.complex, req.body_size);
			const uint8_t *b = req.body;
			for (unsigned i = 0; i < req.body_size && i < 256; i++)
				fprintf(server->log, " %02x", b[i]);
			fprintf(server->log, "\n");
		}
		dl_reply out = { .body = reply_data, .body_capacity = sizeof(reply_data) };
		int bridged = dl_bridge_handle(server, &req, &out);
		if (!bridged)
			dl_dispatch(server, &req, &out);
		composite_if_due(server);

		if (request.head.msgh_remote_port == MACH_PORT_NULL) {
			/* simpleroutine: no reply expected. */
			mach_msg_destroy(&request.head);
			continue;
		}

		const dl_canned_reply *canned = !bridged && out.retcode == DL_MIG_BAD_ID ? dl_canned_lookup(req.id) : NULL;
		if (canned) {
			if (server->log)
				fprintf(server->log, "daylight:   -> canned reply (%u bytes%s)\n", canned->body_size,
					canned->complex ? ", complex" : "");
			reply->msgh_size = build_canned_reply(&request.head, canned, reply_buf, sizeof(reply_buf));
		} else {
			/*
			 * Unimplemented and nothing recorded: succeed with zeroed out-parameters of the size the client
			 * expects (its receive buffer minus the minimal trailer), assuming a simple reply.
			 */
			uint32_t expected = out.retcode == DL_MIG_BAD_ID ? dl_reply_size_lookup(req.id) : 0;
			if (expected >= sizeof(dl_mig_reply) - DL_MAX_MSG + 8 && expected - 8 - offsetof(dl_mig_reply, body) <= 256) {
				size_t data = expected - 8 - offsetof(dl_mig_reply, body);
				out.retcode = KERN_SUCCESS;
				memset(reply_data, 0, data);
				out.body_size = data;
				if (server->log)
					fprintf(server->log, "daylight:   -> default success reply (%zu zero bytes)\n", data);
			}
			reply->msgh_size = build_reply(&request.head, &out, reply_buf);
		}
		reply->msgh_remote_port = request.head.msgh_remote_port;
		reply->msgh_local_port = MACH_PORT_NULL;
		reply->msgh_voucher_port = MACH_PORT_NULL;
		reply->msgh_id = request.head.msgh_id + DL_MIG_REPLY_ID_OFFSET;

		kr = mach_msg(reply, MACH_SEND_MSG | MACH_SEND_TIMEOUT, reply->msgh_size, 0, MACH_PORT_NULL, 100,
			MACH_PORT_NULL);
		if (kr != KERN_SUCCESS)
			mach_msg_destroy(reply);
	}
}

int main(void)
{
	static dl_server server;
	static dl_session session;
	mach_port_t port_set;

	dl_server_init(&server, getenv("DAYLIGHT_QUIET") ? NULL : stderr);

	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &port_set) != KERN_SUCCESS)
		return 1;
	g_port_set = port_set;
	server.host = (dl_host){ .new_port = new_server_port, .new_shared_memory = new_shared_memory,
		.notify = notify_client, .send_once = send_once };
	dl_session_init(&session, &server);
	static dl_display display;
	dl_display_init(&display, &server);
	static dl_windows windows;
	dl_windows_init(&windows, &server);
	static dl_canvases canvases;
	dl_canvases_init(&canvases, &server, 1920, 1080);
	mkdir("/private/tmp/daylight", 0755);
	canvases.screen_path = "/private/tmp/daylight/desktop.png";
	static dl_input input;
	dl_input_init(&input, &server);
	if (getenv("DAYLIGHT_RUNTIME_DIR"))
		snprintf(runtime_dir, sizeof(runtime_dir), "%s", getenv("DAYLIGHT_RUNTIME_DIR"));
	mkdir(runtime_dir, 0777);
	chmod(runtime_dir, 0777);
	open_input();
	open_framebuffer(&canvases);
	dl_processes_init(&server);
	if (!display.event_scoreboard_port)
		fprintf(stderr, "daylight: no shared memory support (needs Darling memory entries)\n");

	for (size_t i = 0; i < sizeof(services) / sizeof(services[0]); i++) {
		mach_port_t port;
		kern_return_t kr = bootstrap_check_in(bootstrap_port, services[i], &port);
		if (kr != KERN_SUCCESS) {
			/* Not launched by launchd: register the name ourselves. */
			kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
			if (kr == KERN_SUCCESS)
				kr = mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND);
			if (kr == KERN_SUCCESS)
				kr = bootstrap_register(bootstrap_port, (char *)services[i], port);
		}
		if (kr != KERN_SUCCESS) {
			fprintf(stderr, "daylight: cannot publish %s: %s\n", services[i], mach_error_string(kr));
			return 1;
		}
		mach_port_move_member(mach_task_self(), port, port_set);
		fprintf(stderr, "daylight: serving %s\n", services[i]);
	}

	if (getenv("DAYLIGHT_SERVE_CA")) {
		mach_port_t port;
		if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) == KERN_SUCCESS &&
			mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS &&
			bootstrap_register(bootstrap_port, (char *)ca_service, port) == KERN_SUCCESS) {
			mach_port_move_member(mach_task_self(), port, port_set);
			fprintf(stderr, "daylight: serving %s (recorded replies)\n", ca_service);
		}
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	serve(&server, port_set);

	const char *trace = getenv("DAYLIGHT_TRACE");
	FILE *out = trace ? fopen(trace, "w") : NULL;
	if (out) {
		dl_trace_write_json(&server, out);
		fclose(out);
		fprintf(stderr, "daylight: trace written to %s\n", trace);
	}
	return 0;
}
