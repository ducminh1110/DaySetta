/*
 * Input: mouse and keyboard events delivered to apps the way the window server does it, as event datagrams in the
 * connection's port stream (record { u32 0, u32 length, encoded event }, decoded by SkyLight's
 * CGSDecodeEventRecord into the connection's event queue, from which AppKit's event loop takes them).
 *
 * The encoded event is SkyLight's tokenized event record, the format CGEventCreateData also uses: bytes
 * 00 00 00 03 (little-endian fields, as the window server sends them) or 00 00 00 02 (big-endian, CGEventCreateData),
 * then fields, each a u32 tag { count:16, kind:2, field:14 } followed by its values (kind 1: u32 each, kind 3:
 * float32 each, kind 0: one u64). Field numbers are CGEventField values where public (91 = window under the pointer).
 */
#ifndef DAYLIGHT_INPUT_H
#define DAYLIGHT_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "daylight.h"

/* CGEventType */
enum {
	DL_EV_LEFT_MOUSE_DOWN = 1,
	DL_EV_LEFT_MOUSE_UP = 2,
	DL_EV_RIGHT_MOUSE_DOWN = 3,
	DL_EV_RIGHT_MOUSE_UP = 4,
	DL_EV_MOUSE_MOVED = 5,
	DL_EV_LEFT_MOUSE_DRAGGED = 6,
	DL_EV_RIGHT_MOUSE_DRAGGED = 7,
	DL_EV_KEY_DOWN = 10,
	DL_EV_KEY_UP = 11,
	DL_EV_FLAGS_CHANGED = 12,
	DL_EV_SCROLL_WHEEL = 22,
};

typedef struct dl_event {
	uint32_t type;
	float x, y;               /* global location, points, top-left origin */
	float window_x, window_y; /* location in the window */
	uint64_t timestamp;       /* nanoseconds (mach_absolute_time on Darling) */
	uint32_t flags;           /* CGEventFlags */
	uint32_t window;          /* window under the pointer */
	/* mouse */
	uint32_t event_number, click_state, button;
	int32_t delta_x, delta_y;
	/* keyboard */
	uint32_t keycode, character, autorepeat;
	/* scroll wheel */
	int32_t scroll_y, scroll_x;
} dl_event;

/* Encodes `ev` as the window server does (little endian) into out; returns the size used (0 if cap is too small). */
size_t dl_event_encode(uint8_t *out, size_t cap, const dl_event *ev);
/* The same in CGEventCreateData's big-endian form. */
size_t dl_event_encode_data(uint8_t *out, size_t cap, const dl_event *ev);

typedef struct dl_activation {
	uint32_t pid, connection, psn_low, psn_high, screen_height;
	uint64_t timestamp;
} dl_activation;

/* The "you are the front process" event (big-endian form). */
size_t dl_event_encode_activation(uint8_t *out, size_t cap, const dl_activation *a);

typedef struct dl_input {
	float x, y;                  /* pointer */
	uint32_t buttons;            /* bit 0 left, bit 1 right */
	uint32_t grab_window;        /* window that got the button down */
	int32_t grab_pid;
	uint32_t focus_window;       /* receives key events */
	int32_t focus_pid;
	uint32_t flags;              /* modifier flags */
	uint32_t event_number;
	uint64_t last_click_time;
	float last_click_x, last_click_y;
	uint32_t click_count;
} dl_input;

void dl_input_init(dl_input *input, dl_server *server);

/*
 * One command, as the display front end (or a test) writes them, one per line:
 *   move X Y | down X Y [BUTTON] | up X Y [BUTTON] | scroll DY [DX] | key KEYCODE CHAR down|up | flags FLAGS
 * X and Y are screen points (top-left origin), BUTTON 0 (left) or 1 (right), KEYCODE a macOS virtual key code,
 * CHAR the character's code point (0 for none), FLAGS CGEventFlags in hex.
 */
void dl_input_command(dl_server *server, const char *line);

#endif
