/*
 * Input events (see input.h): encoding, hit testing against the window canvases, pointer grab and keyboard focus.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge.h"
#include "input.h"
#include "process.h"
#include "session.h"
#include "shell.h"

/* Field tags: count << 16 | kind << 14 | field */
#define TAG_U32(field) (0x00014000u | (field))
#define TAG_FLOAT2(field) (0x0002c000u | (field))
#define TAG_U64(field) (0x00010000u | (field))

typedef struct writer {
	uint8_t *p, *end;
	int overflow;
	int big_endian;
} writer;

static void put32(writer *w, uint32_t v)
{
	if (w->p + 4 > w->end) {
		w->overflow = 1;
		return;
	}
	for (int i = 0; i < 4; i++)
		w->p[i] = (uint8_t)(v >> (w->big_endian ? 24 - 8 * i : 8 * i));
	w->p += 4;
}

static void field32(writer *w, uint32_t field, uint32_t v)
{
	put32(w, TAG_U32(field));
	put32(w, v);
}

static void field_point(writer *w, uint32_t field, float x, float y)
{
	uint32_t ux, uy;
	memcpy(&ux, &x, 4);
	memcpy(&uy, &y, 4);
	put32(w, TAG_FLOAT2(field));
	put32(w, ux);
	put32(w, uy);
}

static void field64(writer *w, uint32_t field, uint64_t v)
{
	put32(w, TAG_U64(field));
	put32(w, (uint32_t)(w->big_endian ? v >> 32 : v));
	put32(w, (uint32_t)(w->big_endian ? v : v >> 32));
}

static int is_mouse(uint32_t type)
{
	return (type >= DL_EV_LEFT_MOUSE_DOWN && type <= DL_EV_RIGHT_MOUSE_DRAGGED) || type == 25 || type == 26 ||
		type == 27;
}

/* Field order and the unnamed fields follow what CGEventCreateData writes on macOS 26/27. */
static size_t encode(uint8_t *out, size_t cap, const dl_event *ev, int big_endian)
{
	writer w = { out, out + cap, 0, big_endian };

	/* bytes 00 00 00 02: big-endian fields (CGEventCreateData); 00 00 00 03: little-endian (the window server) */
	if (cap >= 4) {
		out[0] = out[1] = out[2] = 0;
		out[3] = big_endian ? 2 : 3;
	}
	w.p += 4;
	field32(&w, 0x35, 0);
	field32(&w, 0x36, 0);
	field32(&w, 0x37, ev->type);
	field_point(&w, 0x38, ev->x, ev->y);
	field_point(&w, 0x39, ev->window_x, ev->window_y);
	field64(&w, 0x3a, ev->timestamp);
	field32(&w, 0x3b, ev->flags | 0x20000000u);
	field32(&w, 0x33, ev->window); /* the event's window: NSEvent.windowNumber (checked on macOS) */
	field32(&w, 0x34, 0);
	field32(&w, 0x6a, 0);
	field32(&w, 0x6b, 0);
	if (is_mouse(ev->type)) {
		int pressed = ev->type != DL_EV_MOUSE_MOVED;
		field32(&w, 7, 0);                         /* kCGMouseEventSubtype */
		field32(&w, 0, ev->event_number);          /* kCGMouseEventNumber */
		field32(&w, 1, ev->click_state);           /* kCGMouseEventClickState */
		field32(&w, 2, pressed ? 255 : 0);         /* kCGMouseEventPressure */
		field32(&w, 3, ev->button);                /* kCGMouseEventButtonNumber */
		field32(&w, 4, (uint32_t)ev->delta_x);     /* kCGMouseEventDeltaX */
		field32(&w, 5, (uint32_t)ev->delta_y);     /* kCGMouseEventDeltaY */
		field32(&w, 6, 0);                         /* kCGMouseEventInstantMouser */
		field32(&w, 0x59, 0);
		field32(&w, 0x5a, 0);
		field32(&w, 0x5b, ev->window);             /* kCGMouseEventWindowUnderMousePointer */
		field32(&w, 0x5c, ev->window);             /* ...ThatCanHandleThisEvent */
		field32(&w, 0x6c, pressed ? 1 : 0);
		field32(&w, 0x3c, 0);
		field64(&w, 0x3d, 0);
		field64(&w, 0x3e, 0);
		field32(&w, 0x3f, 0);
	} else if (ev->type == DL_EV_KEY_DOWN || ev->type == DL_EV_KEY_UP || ev->type == DL_EV_FLAGS_CHANGED) {
		field32(&w, 9, ev->keycode);               /* kCGKeyboardEventKeycode */
		field32(&w, 8, ev->autorepeat);            /* kCGKeyboardEventAutorepeat */
		field32(&w, 10, 0xc6);                     /* kCGKeyboardEventKeyboardType */
		field32(&w, 0x4c, 0xfc);
		field32(&w, 0x4d, ev->character);
		field32(&w, 0x4e, ev->character);
		field32(&w, 0x4f, 0);
		field32(&w, 0x50, 0x0a);
		field32(&w, 0x51, 0);
		field32(&w, 0x7f, 0);
		field32(&w, 0x80, 0);
		field32(&w, 0x52, ev->character);
	} else if (ev->type == DL_EV_SCROLL_WHEEL) {
		field32(&w, 11, (uint32_t)ev->scroll_y);   /* kCGScrollWheelEventDeltaAxis1 */
		field32(&w, 12, (uint32_t)ev->scroll_x);   /* kCGScrollWheelEventDeltaAxis2 */
		field32(&w, 0x5b, ev->window);
		field32(&w, 0x5c, ev->window);
	}
	return w.overflow || cap < 4 ? 0 : (size_t)(w.p - out);
}

/*
 * The event the window server sends a process when it becomes the front process (event type 20 in the port
 * stream, recorded on macOS 27 after SetFrontProcessWithInfo and at app launch): pid, connection, PSN, and a
 * nested record describing the sender. AppKit becomes active on it.
 */
size_t dl_event_encode_activation(uint8_t *out, size_t cap, const dl_activation *a)
{
	uint8_t nested[128];
	writer n = { nested, nested + sizeof(nested), 0, 1 };
	writer w = { out, out + cap, 0, 1 };

	put32(&n, 2);
	field64(&n, 0, a->timestamp);
	field32(&n, 3, 0x40);
	field32(&n, 4, a->connection);
	field32(&n, 0xf, 20);
	field64(&n, 0x11, a->pid);
	field64(&n, 0x12, a->pid);

	put32(&w, 2);
	field32(&w, 0x35, 3);
	field32(&w, 0x36, 0);
	field32(&w, 0x37, 20);
	field_point(&w, 0x38, 0, 0);
	field_point(&w, 0x39, 0, 0);
	field64(&w, 0x3a, a->timestamp);
	field32(&w, 0x3b, 0x40);
	field32(&w, 0x34, a->connection);
	field32(&w, 0x28, a->pid);
	put32(&w, 0x00024027);                     /* two u32: PSN low, high */
	put32(&w, a->psn_low);
	put32(&w, a->psn_high);
	field32(&w, 0x55, a->connection);
	field32(&w, 0x6b, a->screen_height);
	size_t nsize = (size_t)(n.p - nested);
	put32(&w, (uint32_t)(nsize << 16) | 0x10ae); /* blob: the nested record */
	for (size_t i = 0; i < nsize && !w.overflow; i++) {
		if (w.p >= w.end)
			w.overflow = 1;
		else
			*w.p++ = nested[i];
	}
	field32(&w, 0x41, 0x40);
	field32(&w, 0x43, 0xc00c);
	field32(&w, 0x45, 0);
	return w.overflow || n.overflow ? 0 : (size_t)(w.p - out);
}

size_t dl_event_encode(uint8_t *out, size_t cap, const dl_event *ev)
{
	return encode(out, cap, ev, 0);
}

size_t dl_event_encode_data(uint8_t *out, size_t cap, const dl_event *ev)
{
	return encode(out, cap, ev, 1);
}

void dl_input_init(dl_input *input, dl_server *server)
{
	memset(input, 0, sizeof(*input));
	server->input = input;
}

/* Topmost visible canvas containing the point (most recently flushed is on top, as in dl_canvases_composite). */
static const dl_canvas *canvas_at(const dl_canvases *canvases, float x, float y)
{
	const dl_canvas *best = NULL;
	for (uint32_t i = 0; canvases && i < canvases->count; i++) {
		const dl_canvas *c = &canvases->canvas[i];
		if (!c->visible || x < c->x || y < c->y || x >= c->x + (float)c->width || y >= c->y + (float)c->height)
			continue;
		if (!best || c->stamp > best->stamp)
			best = c;
	}
	return best;
}

static const dl_canvas *canvas_for_window(const dl_canvases *canvases, uint32_t window)
{
	for (uint32_t i = 0; canvases && i < canvases->count; i++)
		if (canvases->canvas[i].window == window)
			return &canvases->canvas[i];
	return NULL;
}

static void deliver(dl_server *server, int32_t pid, dl_event *ev)
{
	dl_connection *conn = server->session ? dl_session_connection_for_pid(server->session, pid) : NULL;
	uint8_t buf[512];
	size_t n;

	if (!conn)
		return;
	ev->timestamp = dl_now_ticks();
	/* The big-endian form: SkyLight decodes both, but loses the location of little-endian records built here
	 * (checked with CGEventCreateFromData on macOS 27). */
	n = dl_event_encode_data(buf, sizeof(buf), ev);
	if (n)
		dl_session_post_event(server, conn, buf, n);
	if (server->log)
		fprintf(server->log, "daylight: event %u at %.0f,%.0f (window 0x%x, local %.0f,%.0f) to pid %d\n", ev->type,
			ev->x, ev->y, ev->window, ev->window_x, ev->window_y, pid);
}

static void mouse(dl_server *server, uint32_t type, float x, float y, uint32_t button)
{
	dl_input *in = server->input;
	const dl_canvas *target;
	dl_event ev = { 0 };

	in->x = x;
	in->y = y;
	if (type == DL_EV_MOUSE_MOVED)
		dl_shell_motion(x, y);
	/* The menu bar and the Dock float above the windows: a press there belongs to the shell, not the app. */
	if (type == DL_EV_LEFT_MOUSE_DOWN || type == DL_EV_RIGHT_MOUSE_DOWN) {
		int32_t focus = 0;
		if (dl_shell_press(x, y, button, &focus)) {
			if (focus) {
				dl_connection *conn = dl_session_connection_for_pid(server->session, focus);
				if (conn)
					dl_process_activate(server, conn, 0, 0);
				/* bring the app's windows to the front of the composite (most recent flush on top) */
				for (uint32_t i = 0; server->canvases && i < server->canvases->count; i++)
					if (server->canvases->canvas[i].pid == focus)
						server->canvases->canvas[i].stamp = ++server->canvases->clock;
				if (server->canvases)
					server->canvases->dirty = 1;
			}
			return;
		}
	}
	if (in->grab_window && type != DL_EV_LEFT_MOUSE_DOWN && type != DL_EV_RIGHT_MOUSE_DOWN)
		target = canvas_for_window(server->canvases, in->grab_window);
	else
		target = canvas_at(server->canvases, x, y);
	if (!target)
		return;

	if (type == DL_EV_LEFT_MOUSE_DOWN || type == DL_EV_RIGHT_MOUSE_DOWN) {
		uint64_t now = dl_now_ticks();
		int near = (x - in->last_click_x) * (x - in->last_click_x) + (y - in->last_click_y) * (y - in->last_click_y) < 25;
		in->click_count = (near && now - in->last_click_time < 500000000ull) ? in->click_count + 1 : 1;
		in->last_click_time = now;
		in->last_click_x = x;
		in->last_click_y = y;
		in->event_number++;
		in->buttons |= 1u << button;
		in->grab_window = target->window;
		in->grab_pid = target->pid;
		in->focus_window = target->window;
		in->focus_pid = target->pid;
		dl_shell_set_front(target->pid);
	} else if (type == DL_EV_LEFT_MOUSE_UP || type == DL_EV_RIGHT_MOUSE_UP) {
		in->buttons &= ~(1u << button);
		if (!in->buttons)
			in->grab_window = 0;
	} else if (type == DL_EV_MOUSE_MOVED && in->buttons) {
		type = (in->buttons & 1) ? DL_EV_LEFT_MOUSE_DRAGGED : DL_EV_RIGHT_MOUSE_DRAGGED;
	}

	ev.type = type;
	ev.x = x;
	ev.y = y;
	ev.window_x = x - (float)target->x;
	ev.window_y = y - (float)target->y;
	ev.flags = in->flags;
	ev.window = target->window;
	ev.event_number = in->event_number;
	ev.click_state = type == DL_EV_MOUSE_MOVED ? 0 : in->click_count;
	ev.button = button;
	deliver(server, target->pid, &ev);
}

static void key(dl_server *server, uint32_t keycode, uint32_t character, int down)
{
	dl_input *in = server->input;
	dl_event ev = { 0 };

	if (!in->focus_pid)
		return;
	ev.type = down ? DL_EV_KEY_DOWN : DL_EV_KEY_UP;
	ev.x = in->x;
	ev.y = in->y;
	ev.flags = in->flags;
	ev.window = in->focus_window;
	ev.keycode = keycode;
	ev.character = character;
	deliver(server, in->focus_pid, &ev);
}

void dl_input_command(dl_server *server, const char *line)
{
	dl_input *in = server->input;
	char cmd[16];
	float x = 0, y = 0;
	unsigned a = 0, b = 0;
	char updown[8] = "";

	if (!in || sscanf(line, "%15s", cmd) != 1)
		return;
	if (!strcmp(cmd, "move") && sscanf(line, "%*s %f %f", &x, &y) == 2) {
		mouse(server, DL_EV_MOUSE_MOVED, x, y, 0);
	} else if (!strcmp(cmd, "down") && sscanf(line, "%*s %f %f %u", &x, &y, &a) >= 2) {
		mouse(server, a ? DL_EV_RIGHT_MOUSE_DOWN : DL_EV_LEFT_MOUSE_DOWN, x, y, a);
	} else if (!strcmp(cmd, "up") && sscanf(line, "%*s %f %f %u", &x, &y, &a) >= 2) {
		mouse(server, a ? DL_EV_RIGHT_MOUSE_UP : DL_EV_LEFT_MOUSE_UP, x, y, a);
	} else if (!strcmp(cmd, "scroll")) {
		int dy = 0, dx = 0;
		const dl_canvas *target = canvas_at(server->canvases, in->x, in->y);
		if (sscanf(line, "%*s %d %d", &dy, &dx) >= 1 && target) {
			dl_event ev = { .type = DL_EV_SCROLL_WHEEL, .x = in->x, .y = in->y, .window = target->window,
				.window_x = in->x - (float)target->x, .window_y = in->y - (float)target->y,
				.flags = in->flags, .scroll_y = dy, .scroll_x = dx };
			deliver(server, target->pid, &ev);
		}
	} else if (!strcmp(cmd, "key") && sscanf(line, "%*s %u %u %7s", &a, &b, updown) == 3) {
		key(server, a, b, !strcmp(updown, "down"));
	} else if (!strcmp(cmd, "flags") && sscanf(line, "%*s %x", &a) == 1) {
		in->flags = a;
	}
}
