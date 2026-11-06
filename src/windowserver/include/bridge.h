/*
 * Window canvases: apps draw their windows into memory the server hands out.
 *
 * The app side (bridge/daylight-bridge.m, loaded into AppKit apps) asks for a canvas per window, lets AppKit draw
 * the window into it on the CPU and reports each finished frame. The server composites the canvases into the
 * screen. These messages are Daylight's own (not part of the WindowServer protocol) and arrive on the window
 * server's service port.
 */
#ifndef DAYLIGHT_BRIDGE_H
#define DAYLIGHT_BRIDGE_H

#include <stdint.h>

#include "daylight.h"

/* 'DYL\1': AttachCanvas(window, width px, height px) -> (memory entry [copy-send], bytes per row, size) */
#define DL_BRIDGE_ATTACH 0x44594c01
/* 'DYL\2': simpleroutine CanvasFlushed(window, x, y, width, height in screen pixels, top-left origin, level) */
#define DL_BRIDGE_FLUSH 0x44594c02
/* 'DYL\3': simpleroutine CanvasDetach(window) */
#define DL_BRIDGE_DETACH 0x44594c03
/* 'DYL\4': deprecated (was CanvasGlass); the glass surface is rendered by Apple's own pipeline. */
#define DL_BRIDGE_GLASS 0x44594c04

/* Canvas pixels: 8-bit RGBA, alpha premultiplied, rows top-down. */
#define DL_CANVAS_BYTES_PER_PIXEL 4

typedef struct dl_canvas {
	uint32_t window;
	int32_t pid;
	char app[48];            /* bundle id the owning process registered (for the shell's menu bar/Dock) */
	uint32_t port;           /* memory entry handed to the app */
	uint8_t *pixels;         /* the server's mapping */
	uint32_t width, height, row_bytes;
	int32_t x, y;            /* screen position of the top-left corner */
	int32_t level;           /* window level (SkyLight): higher draws in front (Dock/menu bar are high) */
	uint32_t visible;
	uint64_t stamp;          /* last flush, for stacking (most recent on top) */
} dl_canvas;

#define DL_MAX_CANVASES 256

typedef struct dl_canvases {
	dl_canvas canvas[DL_MAX_CANVASES];
	uint32_t count;
	uint64_t clock;
	uint32_t screen_width, screen_height;
	uint32_t dirty;
	const char *screen_path;  /* PNG written after flushes; NULL disables */
} dl_canvases;

void dl_canvases_init(dl_canvases *canvases, dl_server *server, uint32_t screen_width, uint32_t screen_height);

/* Handles a bridge message; returns 1 if the id was one of the bridge's. */
int dl_bridge_handle(dl_server *server, const dl_request *req, dl_reply *reply);

/* Composites every visible canvas over the desktop into rgba (screen_width x screen_height, rows top-down). */
void dl_canvases_composite(const dl_canvases *canvases, uint8_t *rgba);

/* Writes 8-bit RGBA (not premultiplied) as a PNG with stored (uncompressed) deflate blocks. 0 on success. */
int dl_write_png(const char *path, const uint8_t *rgba, uint32_t width, uint32_t height);

#endif
