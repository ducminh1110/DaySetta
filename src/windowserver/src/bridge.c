/*
 * Window canvases (see bridge.h): allocation, frame reports, compositing and a minimal PNG writer.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge.h"
#include "session.h"
#include "shell.h"

void dl_canvases_init(dl_canvases *canvases, dl_server *server, uint32_t screen_width, uint32_t screen_height)
{
	memset(canvases, 0, sizeof(*canvases));
	canvases->screen_width = screen_width;
	canvases->screen_height = screen_height;
	canvases->dirty = 1; /* draw the desktop shell before the first window is flushed */
	server->canvases = canvases;
	dl_shell_init(screen_width, screen_height);
}

static dl_canvas *find(dl_canvases *canvases, uint32_t window)
{
	for (uint32_t i = 0; i < canvases->count; i++)
		if (canvases->canvas[i].window == window)
			return &canvases->canvas[i];
	return NULL;
}

/* AttachCanvas: a new canvas, or the existing one when the size did not change. */
static void attach(dl_server *server, const dl_request *req, dl_reply *reply)
{
	dl_canvases *canvases = server->canvases;
	size_t size;
	const uint8_t *in = dl_request_data(req, &size);
	uint32_t args[3];

	if (!in || size < sizeof(args) || !server->host.new_shared_memory) {
		reply->retcode = DL_MIG_TYPE_ERROR;
		return;
	}
	memcpy(args, in, sizeof(args));
	uint32_t window = args[0], width = args[1], height = args[2];
	if (width == 0 || height == 0 || width > 16384 || height > 16384) {
		reply->retcode = DL_MIG_TYPE_ERROR;
		return;
	}
	dl_canvas *c = find(canvases, window);
	if (!c || c->width != width || c->height != height) {
		if (!c) {
			if (canvases->count >= DL_MAX_CANVASES) {
				reply->retcode = DL_KERN_RESOURCE_SHORTAGE;
				return;
			}
			c = &canvases->canvas[canvases->count++];
			memset(c, 0, sizeof(*c));
			c->window = window;
		}
		/* FIXME: the previous mapping of a resized canvas is leaked */
		uint32_t row_bytes = width * DL_CANVAS_BYTES_PER_PIXEL;
		size_t bytes = (size_t)row_bytes * height;
		uint8_t *zero = calloc(1, bytes);
		void *mapped = NULL;
		c->port = zero ? server->host.new_shared_memory(server->host.ctx, zero, bytes, &mapped) : 0;
		free(zero);
		if (!c->port) {
			reply->retcode = DL_KERN_RESOURCE_SHORTAGE;
			return;
		}
		c->pixels = mapped;
		c->width = width;
		c->height = height;
		c->row_bytes = row_bytes;
	}
	c->pid = req->client_pid;
	dl_connection *conn = server->session ? dl_session_connection_for_pid(server->session, req->client_pid) : NULL;
	const char *app = conn && conn->name[0] ? conn->name : "app";
	size_t app_len = strlen(app);
	if (app_len >= sizeof(c->app))
		app_len = sizeof(c->app) - 1;
	memcpy(c->app, app, app_len);
	c->app[app_len] = '\0';
	if (server->log)
		fprintf(server->log, "daylight: canvas for window 0x%x: %ux%u\n", window, width, height);
	uint32_t out[2] = { c->row_bytes, c->row_bytes * c->height };
	dl_reply_add_port(reply, c->port, DL_PORT_COPY_SEND);
	dl_reply_put(reply, out, sizeof(out));
}

static void flush(dl_server *server, const dl_request *req)
{
	dl_canvases *canvases = server->canvases;
	size_t size;
	const uint8_t *in = dl_request_data(req, &size);
	int32_t args[6];

	if (!in || size < sizeof(args))
		return;
	memcpy(args, in, sizeof(args));
	dl_canvas *c = find(canvases, (uint32_t)args[0]);
	if (!c)
		return;
	c->x = args[1];
	c->y = args[2];
	c->level = args[5];
	c->visible = 1;
	c->stamp = ++canvases->clock;
	canvases->dirty = 1;
	if (server->log && c->window == 0xca000001)
		fprintf(server->log, "daylight: CA canvas flush %ux%u at %d,%d level %d\n", c->width, c->height,
			c->x, c->y, c->level);
}

int dl_bridge_handle(dl_server *server, const dl_request *req, dl_reply *reply)
{
	if (!server->canvases)
		return 0;
	switch (req->id) {
	case DL_BRIDGE_ATTACH:
		attach(server, req, reply);
		return 1;
	case DL_BRIDGE_FLUSH:
		flush(server, req);
		return 1;
	case DL_BRIDGE_DETACH: {
		size_t size;
		const uint8_t *in = dl_request_data(req, &size);
		uint32_t window;
		if (in && size >= 4) {
			memcpy(&window, in, 4);
			dl_canvas *c = find(server->canvases, window);
			if (c) {
				c->visible = 0;
				server->canvases->dirty = 1;
			}
		}
		return 1;
	}
	case DL_BRIDGE_GLASS: {
		/* Deprecated: the glass surface is rendered by Apple's own pipeline (Core Animation's
		 * CASDFLayer material inside the Dock's CA context), never by the compositor. */
		return 1;
	}
	}
	return 0;
}

/* Desktop: a vertical blue gradient. */
static void desktop(uint8_t *rgba, uint32_t w, uint32_t h)
{
	for (uint32_t y = 0; y < h; y++) {
		uint8_t r = (uint8_t)(40 + 20 * y / h), g = (uint8_t)(80 + 30 * y / h), b = (uint8_t)(140 + 40 * y / h);
		uint8_t *p = rgba + (size_t)y * w * 4;
		for (uint32_t x = 0; x < w; x++, p += 4) {
			p[0] = r;
			p[1] = g;
			p[2] = b;
			p[3] = 255;
		}
	}
}

void dl_canvases_composite(const dl_canvases *canvases, uint8_t *rgba)
{
	const uint32_t sw = canvases->screen_width, sh = canvases->screen_height;
	const dl_canvas *order[DL_MAX_CANVASES];
	uint32_t n = 0;

	desktop(rgba, sw, sh);
	for (uint32_t i = 0; i < canvases->count; i++)
		if (canvases->canvas[i].visible && canvases->canvas[i].pixels)
			order[n++] = &canvases->canvas[i];
	/* back to front: lower window level first, then oldest flush first within a level.
	 * Normal windows are level 0; the Dock and the menu bar sit at high levels and
	 * must stay in front of them no matter which flushed last. */
	for (uint32_t i = 1; i < n; i++)
		for (uint32_t j = i; j > 0; j--) {
			const dl_canvas *a = order[j - 1], *b = order[j];
			int swap = a->level > b->level || (a->level == b->level && a->stamp > b->stamp);
			if (!swap)
				break;
			order[j] = a;
			order[j - 1] = b;
		}
	for (uint32_t k = 0; k < n; k++) {
		const dl_canvas *c = order[k];
		for (uint32_t y = 0; y < c->height; y++) {
			int32_t sy = c->y + (int32_t)y;
			if (sy < 0 || sy >= (int32_t)sh)
				continue;
			const uint8_t *src = c->pixels + (size_t)y * c->row_bytes;
			for (uint32_t x = 0; x < c->width; x++, src += 4) {
				int32_t sx = c->x + (int32_t)x;
				if (sx < 0 || sx >= (int32_t)sw || src[3] == 0)
					continue;
				uint8_t *d = rgba + ((size_t)sy * sw + (size_t)sx) * 4;
				uint32_t ia = 255 - src[3];
				/* premultiplied source over opaque destination */
				d[0] = (uint8_t)(src[0] + d[0] * ia / 255);
				d[1] = (uint8_t)(src[1] + d[1] * ia / 255);
				d[2] = (uint8_t)(src[2] + d[2] * ia / 255);
				d[3] = 255;
			}
		}
	}
	dl_shell_draw(canvases, rgba);
}

/* PNG: CRC-32 and Adler-32 by hand, image data in stored deflate blocks (no compression library needed). */
static uint32_t crc_table[256];

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
			crc_table[i] = c;
		}
	crc = ~crc;
	while (n--)
		crc = crc_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
	return ~crc;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static int chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
	uint8_t hdr[8];
	put32(hdr, (uint32_t)n);
	memcpy(hdr + 4, type, 4);
	uint32_t crc = crc32_update(crc32_update(0, (const uint8_t *)type, 4), data, n);
	uint8_t tail[4];
	put32(tail, crc);
	return fwrite(hdr, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) && fwrite(tail, 1, 4, f) == 4 ? 0 : -1;
}

int dl_write_png(const char *path, const uint8_t *rgba, uint32_t width, uint32_t height)
{
	size_t raw_row = (size_t)width * 4 + 1, raw = raw_row * height;
	size_t blocks = (raw + 65534) / 65535;
	size_t zsize = 2 + raw + blocks * 5 + 4;
	uint8_t *z = malloc(zsize), *row = malloc(raw_row);
	if (!z || !row) {
		free(z);
		free(row);
		return -1;
	}
	size_t o = 0, done = 0;
	uint32_t a = 1, b = 0;
	z[o++] = 0x78;
	z[o++] = 0x01;
	size_t left_in_block = 0;
	for (uint32_t y = 0; y < height; y++) {
		row[0] = 0; /* filter: none */
		memcpy(row + 1, rgba + (size_t)y * width * 4, (size_t)width * 4);
		for (size_t i = 0; i < raw_row; i++) {
			if (left_in_block == 0) {
				size_t n = raw - done < 65535 ? raw - done : 65535;
				z[o++] = raw - done <= 65535 ? 1 : 0;
				z[o++] = (uint8_t)n;
				z[o++] = (uint8_t)(n >> 8);
				z[o++] = (uint8_t)~n;
				z[o++] = (uint8_t)(~n >> 8);
				left_in_block = n;
			}
			z[o++] = row[i];
			a = (a + row[i]) % 65521;
			b = (b + a) % 65521;
			left_in_block--;
			done++;
		}
	}
	put32(z + o, (b << 16) | a);
	o += 4;

	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "wb");
	int rc = -1;
	if (f) {
		static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
		uint8_t ihdr[13];
		put32(ihdr, width);
		put32(ihdr + 4, height);
		ihdr[8] = 8;  /* bit depth */
		ihdr[9] = 6;  /* RGBA */
		ihdr[10] = ihdr[11] = ihdr[12] = 0;
		rc = fwrite(sig, 1, 8, f) == 8 && !chunk(f, "IHDR", ihdr, 13) && !chunk(f, "IDAT", z, o) &&
			!chunk(f, "IEND", NULL, 0) ? 0 : -1;
		fclose(f);
		if (rc == 0)
			rc = rename(tmp, path);
	}
	free(z);
	free(row);
	return rc;
}
