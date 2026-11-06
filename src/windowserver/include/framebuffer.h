/*
 * The composited screen as a shared file, for display front ends outside Darling (daylight-x11):
 * /private/tmp/daylight/screen.fb inside the prefix. A 64-byte header, then height rows of stride bytes,
 * pixels 32-bit little-endian 0x00RRGGBB (B, G, R, X in memory), what X11 TrueColor visuals take as is.
 * `frame` works as a sequence lock: odd while the server writes the pixels, even when a frame is complete.
 */
#ifndef DAYLIGHT_FRAMEBUFFER_H
#define DAYLIGHT_FRAMEBUFFER_H

#include <stdint.h>

#define DL_FB_MAGIC 0x42464c44u /* "DLFB" */
#define DL_FB_VERSION 1
#define DL_FB_HEADER_SIZE 64

typedef struct dl_fb_header {
	uint32_t magic;
	uint32_t version;
	uint32_t width, height, stride;
	volatile uint32_t frame;
	uint32_t reserved[10];
} dl_fb_header;

#endif
