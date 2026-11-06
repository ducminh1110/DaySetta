/*
 * Desktop shell (see shell.h): menu bar and Dock, drawn by the window server over the composited windows.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bridge.h"
#include "shell.h"
#include "shell_font.h"

/* ---- a tiny drawing surface over the screen's RGBA buffer -------------------------------- */

typedef struct surf {
	uint8_t *px;
	int w, h;
} surf;

/* Source-over blend of a straight (non-premultiplied) colour onto the surface. */
static void spx(surf *s, int x, int y, int r, int g, int b, int a)
{
	uint8_t *d;
	int ia;

	if (a <= 0 || x < 0 || y < 0 || x >= s->w || y >= s->h)
		return;
	if (a >= 255) {
		a = 255;
	}
	d = s->px + ((size_t)y * s->w + x) * 4;
	ia = 255 - a;
	d[0] = (uint8_t)((r * a + d[0] * ia) / 255);
	d[1] = (uint8_t)((g * a + d[1] * ia) / 255);
	d[2] = (uint8_t)((b * a + d[2] * ia) / 255);
	d[3] = 255;
}

static void fill_rect(surf *s, int x, int y, int w, int h, int r, int g, int b, int a)
{
	for (int py = y; py < y + h; py++)
		for (int px = x; px < x + w; px++)
			spx(s, px, py, r, g, b, a);
}

/* Standard rounded-rectangle test: the corners are quarter circles of radius r. */
static int in_rrect(int px, int py, int x, int y, int w, int h, int r)
{
	int cx, cy, dx, dy;

	if (px < x || py < y || px >= x + w || py >= y + h)
		return 0;
	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;
	cx = px < x + r ? x + r : (px >= x + w - r ? x + w - 1 - r : px);
	cy = py < y + r ? y + r : (py >= y + h - r ? y + h - 1 - r : py);
	dx = px - cx;
	dy = py - cy;
	return dx * dx + dy * dy <= r * r;
}

static void fill_rrect(surf *s, int x, int y, int w, int h, int rad, int r, int g, int b, int a)
{
	for (int py = y; py < y + h; py++)
		for (int px = x; px < x + w; px++)
			if (in_rrect(px, py, x, y, w, h, rad))
				spx(s, px, py, r, g, b, a);
}

/* A vertical gradient rounded rectangle (one colour at the top, another at the bottom). */
static void fill_rrect_gradient(surf *s, int x, int y, int w, int h, int rad, int r0, int g0, int b0, int r1,
	int g1, int b1, int a)
{
	for (int py = y; py < y + h; py++) {
		int t = h > 1 ? (py - y) * 255 / (h - 1) : 0;
		int r = r0 + (r1 - r0) * t / 255, g = g0 + (g1 - g0) * t / 255, b = b0 + (b1 - b0) * t / 255;
		for (int px = x; px < x + w; px++)
			if (in_rrect(px, py, x, y, w, h, rad))
				spx(s, px, py, r, g, b, a);
	}
}

static void stroke_rrect(surf *s, int x, int y, int w, int h, int rad, int r, int g, int b, int a)
{
	for (int py = y; py < y + h; py++)
		for (int px = x; px < x + w; px++)
			if (in_rrect(px, py, x, y, w, h, rad) && !in_rrect(px, py, x + 1, y + 1, w - 2, h - 2, rad - 1))
				spx(s, px, py, r, g, b, a);
}

static void fill_circle(surf *s, int cx, int cy, int rad, int r, int g, int b, int a)
{
	for (int py = cy - rad; py <= cy + rad; py++)
		for (int px = cx - rad; px <= cx + rad; px++) {
			int dx = px - cx, dy = py - cy;
			if (dx * dx + dy * dy <= rad * rad)
				spx(s, px, py, r, g, b, a);
		}
}

/* ---- the 8x13 bitmap font ----------------------------------------------------------------- */

static int text_width(const char *str, int scale)
{
	return (int)strlen(str) * DL_FONT_W * scale;
}

static void draw_text(surf *s, const char *str, int x, int y, int scale, int r, int g, int b, int a)
{
	for (; *str; str++) {
		unsigned char c = (unsigned char)*str;
		if (c < DL_FONT_FIRST || c > DL_FONT_LAST) {
			x += DL_FONT_W * scale;
			continue;
		}
		const uint8_t *glyph = dl_font_bits + (size_t)(c - DL_FONT_FIRST) * DL_FONT_H;
		for (int row = 0; row < DL_FONT_H; row++) {
			uint8_t bits = glyph[row];
			for (int col = 0; col < DL_FONT_W; col++)
				if (bits & (0x80 >> col))
					fill_rect(s, x + col * scale, y + row * scale, scale, scale, r, g, b, a);
		}
		x += DL_FONT_W * scale;
	}
}

/* ---- the Apple logo and status icons ------------------------------------------------------ */

/* A recognisable silhouette: a body with a bite out of the right and a small leaf on top. */
static void draw_apple(surf *s, int cx, int cy, int col_r, int col_g, int col_b)
{
	for (int py = cy - 9; py <= cy + 8; py++)
		for (int px = cx - 8; px <= cx + 8; px++) {
			int dx = px - cx, dy = py - cy;
			int body = dx * dx * 49 + (dy + 1) * (dy + 1) * 52 <= 49 * 52;
			int bite = (dx - 6) * (dx - 6) * 16 + (dy + 3) * (dy + 3) * 18 <= 16 * 18;
			int leaf = (dx - 1) * (dx - 1) * 2 + (dy + 8) * (dy + 8) * 8 <= 2 * 8;
			if ((body && !bite) || leaf)
				spx(s, px, py, col_r, col_g, col_b, 255);
		}
}

static void draw_wifi(surf *s, int x, int y, int col_r, int col_g, int col_b)
{
	int cx = x + 10, cy = y + 13;
	for (int py = y; py < y + 14; py++)
		for (int px = x; px < x + 20; px++) {
			int dx = px - cx, dy = cy - py; /* dy > 0 points up */
			if (dy <= 0 || dy * dy < dx * dx) /* only the upward, roughly 45..135 degree wedge */
				continue;
			int d2 = dx * dx + dy * dy;
			for (int k = 0; k < 3; k++) {
				int rr = 4 + k * 4;
				int lo = (rr - 1) * (rr - 1), hi = (rr + 1) * (rr + 1);
				if (d2 >= lo && d2 <= hi)
					spx(s, px, py, col_r, col_g, col_b, 255);
			}
		}
	fill_circle(s, cx, cy - 1, 1, col_r, col_g, col_b, 255);
}

static void draw_battery(surf *s, int x, int y, int col_r, int col_g, int col_b)
{
	fill_rrect(s, x, y, 24, 12, 3, col_r, col_g, col_b, 110);
	stroke_rrect(s, x, y, 24, 12, 3, col_r, col_g, col_b, 255);
	fill_rect(s, x + 24, y + 4, 2, 4, col_r, col_g, col_b, 255); /* the nub */
	fill_rrect(s, x + 2, y + 2, 17, 8, 2, col_r, col_g, col_b, 255);
}

/* ---- application names -------------------------------------------------------------------- */

static const struct {
	const char *bundle, *name;
} app_names[] = {
	{ "com.apple.terminal", "Terminal" },
	{ "com.apple.Terminal", "Terminal" },
	{ "com.apple.Safari", "Safari" },
	{ "com.apple.WebKit.WebContent", "Safari" },
	{ "com.apple.recoverybrowser", "Web Browser" },
	{ "com.apple.diskimagemounter", "DiskImageMounter" },
	{ "com.apple.diskutility", "Disk Utility" },
	{ "com.apple.installassistant.macostahoe", "Install macOS Tahoe" },
	{ "com.apple.macos-update-assistant", "macOS Update Assistant" },
	{ "com.apple.textinputswitcher", "TextInputSwitcher" },
	{ "com.apple.language-chooser", "Language Chooser" },
	{ "com.apple.startupdisk", "Startup Disk" },
	{ "com.apple.startupsecurityutility", "Startup Security Utility" },
	{ "com.apple.securebootrecovery", "Secure Boot Recovery" },
	{ "com.apple.boot-picker", "Startup Manager" },
	{ "com.apple.recovery-springboard", "Recovery" },
	{ "com.apple.recoverydiagnose.recoverydiagnose-app", "Recovery Diagnose" },
	{ "com.apple.keyrecoveryassistant", "Key Recovery Assistant" },
	{ "com.apple.device-recovery-assistant", "Device Recovery Assistant" },
	{ "com.apple.eraseallcontentandsettings", "Erase All Content and Settings" },
	{ "com.apple.time-machine-system-restore", "Time Machine System Restore" },
	{ "com.apple.mac-sharing-mode", "Mac Sharing Mode" },
	{ "com.apple.lock", "Lock" },
	{ "com.apple.DwellControl", "Dwell Control" },
	{ "com.apple.KeyboardAccessAgent", "Keyboard Access" },
	{ "com.apple.VoiceOver", "VoiceOver" },
	{ "com.apple.PowerChime", "PowerChime" },
	{ "com.apple.NetAuthAgent", "NetAuthAgent" },
	{ "com.apple.finder", "Finder" },
};

/* A display name for the bundle id the app registered its connection under (falls back to the last part). */
static const char *display_name(const char *bundle, char *buf, size_t n)
{
	size_t i;

	if (!bundle || !*bundle)
		return "Desktop";
	for (i = 0; i < sizeof(app_names) / sizeof(app_names[0]); i++)
		if (!strcmp(bundle, app_names[i].bundle))
			return app_names[i].name;
	const char *tail = strrchr(bundle, '.');
	tail = (tail && tail[1]) ? tail + 1 : bundle;
	snprintf(buf, n, "%s", tail);
	if (buf[0] >= 'a' && buf[0] <= 'z')
		buf[0] = (char)(buf[0] - 'a' + 'A');
	return buf;
}

/* ---- shell state -------------------------------------------------------------------------- */

#define DL_MAX_DOCK_ITEMS DL_MAX_CANVASES

typedef struct dock_item {
	int32_t pid;
	uint32_t window;
	char name[64];
	int x, y, w, h;
} dock_item;

static struct {
	uint32_t screen_width, screen_height;
	int32_t front_pid;
	int hover;
	int dirty;
	int disabled; /* DAYLIGHT_SHELL=0: do not draw the built-in shell (real Dock running) */
	int last_minute;
	int launchpad_x, launchpad_y; /* the always-present Launchpad tile, for hit testing */
	dock_item item[DL_MAX_DOCK_ITEMS];
	int nitems;
	/* panel rectangle of the last draw */
	int panel_x, panel_y, panel_w, panel_h;
} shell;

void dl_shell_init(uint32_t screen_width, uint32_t screen_height)
{
	memset(&shell, 0, sizeof(shell));
	shell.screen_width = screen_width;
	shell.screen_height = screen_height;
	shell.hover = -1;
	shell.last_minute = -1;
	shell.dirty = 1;
	const char *en = getenv("DAYLIGHT_SHELL");
	if (en && (!strcmp(en, "0") || !strcmp(en, "off"))) {
		shell.disabled = 1;
		shell.dirty = 0;
	}
}

void dl_shell_set_front(int32_t pid)
{
	if (shell.front_pid != pid) {
		shell.front_pid = pid;
		shell.dirty = 1;
	}
}

int dl_shell_needs_redraw(void)
{
	if (shell.disabled)
		return 0;
	time_t t = time(NULL);
	struct tm tm;
	memcpy(&tm, localtime(&t), sizeof(tm));
	if (tm.tm_min != shell.last_minute)
		shell.dirty = 1;
	return shell.dirty;
}

/* Builds the Dock model from the visible canvases: one tile per distinct process. */
static void build_dock(const struct dl_canvases *canvases)
{
	int stride = DL_DOCK_ITEM + DL_DOCK_GAP;

	shell.nitems = 0;
	for (uint32_t i = 0; canvases && i < canvases->count; i++) {
		const dl_canvas *c = &canvases->canvas[i];
		if (!c->visible || !c->pixels || c->pid <= 0)
			continue;
		int found = 0;
		for (int k = 0; k < shell.nitems; k++)
			if (shell.item[k].pid == c->pid) {
				found = 1;
				break;
			}
		if (found || shell.nitems >= DL_MAX_DOCK_ITEMS)
			continue;
		dock_item *it = &shell.item[shell.nitems];
		it->pid = c->pid;
		it->window = c->window;
		char buf[64];
		snprintf(it->name, sizeof(it->name), "%s", display_name(c->app, buf, sizeof(buf)));
		shell.nitems++;
	}

	int n = shell.nitems + 1; /* the Launchpad tile is always there */
	int content = n * DL_DOCK_ITEM + (n - 1) * DL_DOCK_GAP;
	int panel_w = content + 2 * DL_DOCK_GAP;
	int panel_x = (int)(shell.screen_width - panel_w) / 2;
	int panel_y = (int)shell.screen_height - DL_DOCK_BOTTOM_MARGIN - DL_DOCK_PANEL_HEIGHT;
	shell.panel_x = panel_x;
	shell.panel_y = panel_y;
	shell.panel_w = panel_w;
	shell.panel_h = DL_DOCK_PANEL_HEIGHT;
	int ty = panel_y + (DL_DOCK_PANEL_HEIGHT - DL_DOCK_ITEM) / 2;
	int x = panel_x + DL_DOCK_GAP;
	shell.launchpad_x = x;
	shell.launchpad_y = ty;
	for (int k = 0; k < shell.nitems; k++, x += stride) {
		shell.item[k].x = x;
		shell.item[k].y = ty;
		shell.item[k].w = DL_DOCK_ITEM;
		shell.item[k].h = DL_DOCK_ITEM;
	}
}

static void hue_rgb(int h, int *r, int *g, int *b)
{
	int s = 150, v = 232;
	int c = v * s / 255;
	int hp = (h % 360) / 60;
	int f = (h % 60) * 255 / 60;
	int p = v - c, q = v - c * f / 255, t = v - c * (255 - f) / 255;
	int rr, gg, bb;
	switch (hp) {
	case 0: rr = v; gg = t; bb = p; break;
	case 1: rr = q; gg = v; bb = p; break;
	case 2: rr = p; gg = v; bb = t; break;
	case 3: rr = p; gg = q; bb = v; break;
	case 4: rr = t; gg = p; bb = v; break;
	default: rr = v; gg = p; bb = q; break;
	}
	*r = rr;
	*g = gg;
	*b = bb;
}

static void draw_launchpad(surf *s, int x, int y, int w, int h)
{
	(void)w;
	(void)h;
	fill_rrect_gradient(s, x, y, DL_DOCK_ITEM, DL_DOCK_ITEM, 12, 96, 100, 112, 66, 70, 82, 255);
	fill_rrect(s, x, y, DL_DOCK_ITEM, DL_DOCK_ITEM, 12, 40, 44, 54, 60);
	int cx = x + DL_DOCK_ITEM / 2, cy = y + DL_DOCK_ITEM / 2;
	for (int gy = -1; gy <= 1; gy++)
		for (int gx = -1; gx <= 1; gx++)
			fill_circle(s, cx + gx * 11, cy + gy * 11, 3, 240, 242, 248, 255);
}

static void draw_dock_tile(surf *s, int x, int y, const char *name, int selected)
{
	int h = 0;
	while (name[h])
		h = (h * 131 + (unsigned char)name[h]) & 0x7fffffff;
	int r, g, b;
	hue_rgb(h % 360, &r, &g, &b);
	if (selected)
		fill_rrect(s, x - 3, y - 3, DL_DOCK_ITEM + 6, DL_DOCK_ITEM + 6, 14, 255, 255, 255, 70);
	fill_rrect_gradient(s, x, y, DL_DOCK_ITEM, DL_DOCK_ITEM, 12, r, g, b, r * 3 / 4, g * 3 / 4, b * 3 / 4, 255);
	stroke_rrect(s, x, y, DL_DOCK_ITEM, DL_DOCK_ITEM, 12, 255, 255, 255, 90);
	char initial[2] = { name[0] ? name[0] : '?', 0 };
	if (initial[0] >= 'a' && initial[0] <= 'z')
		initial[0] = (char)(initial[0] - 'a' + 'A');
	draw_text(s, initial, x + (DL_DOCK_ITEM - DL_FONT_W * 2) / 2, y + (DL_DOCK_ITEM - DL_FONT_H * 2) / 2, 2, 255,
		255, 255, 255);
	/* the running indicator dot macOS shows under an open app */
	fill_circle(s, x + DL_DOCK_ITEM / 2, y + DL_DOCK_ITEM + 6, 2, 255, 255, 255, 190);
}

static void draw_menu_bar(surf *s)
{
	int w = (int)shell.screen_width;
	int fg_r = 28, fg_g = 28, fg_b = 32;

	fill_rect(s, 0, 0, w, DL_MENU_BAR_HEIGHT, 244, 245, 249, 205);

	draw_apple(s, 18, 12, fg_r, fg_g, fg_b);

	const char *title = "Desktop";
	/* the front application's name, taken from the Dock item (a canvas) it owns */
	for (int i = 0; i < shell.nitems; i++)
		if (shell.item[i].pid == shell.front_pid)
			title = shell.item[i].name;
	draw_text(s, title, 36, (DL_MENU_BAR_HEIGHT - DL_FONT_H) / 2, 1, fg_r, fg_g, fg_b, 255);

	time_t t = time(NULL);
	struct tm tm;
	memcpy(&tm, localtime(&t), sizeof(tm));
	char clock[16];
	snprintf(clock, sizeof(clock), "%02d:%02d", tm.tm_hour, tm.tm_min);
	int cw = text_width(clock, 1);
	int cx = w - 12 - cw;
	draw_text(s, clock, cx, (DL_MENU_BAR_HEIGHT - DL_FONT_H) / 2, 1, fg_r, fg_g, fg_b, 255);
	int bx = cx - 12 - 26;
	draw_battery(s, bx, (DL_MENU_BAR_HEIGHT - 12) / 2, fg_r, fg_g, fg_b);
	draw_wifi(s, bx - 8 - 20, (DL_MENU_BAR_HEIGHT - 14) / 2, fg_r, fg_g, fg_b);
}

static void draw_dock(surf *s)
{
	int rad = DL_DOCK_PANEL_HEIGHT / 3;
	fill_rrect(s, shell.panel_x, shell.panel_y, shell.panel_w, shell.panel_h, rad, 226, 230, 238, 130);
	stroke_rrect(s, shell.panel_x, shell.panel_y, shell.panel_w, shell.panel_h, rad, 255, 255, 255, 70);
	draw_launchpad(s, shell.launchpad_x, shell.launchpad_y, DL_DOCK_ITEM, DL_DOCK_ITEM);
	for (int i = 0; i < shell.nitems; i++)
		draw_dock_tile(s, shell.item[i].x, shell.item[i].y, shell.item[i].name, shell.hover == i);
}

void dl_shell_draw(const struct dl_canvases *canvases, uint8_t *rgba)
{
	if (shell.disabled)
		return;
	surf s = { rgba, (int)shell.screen_width, (int)shell.screen_height };

	if (shell.screen_width != canvases->screen_width || shell.screen_height != canvases->screen_height) {
		shell.screen_width = canvases->screen_width;
		shell.screen_height = canvases->screen_height;
	}
	build_dock(canvases);
	draw_dock(&s);
	draw_menu_bar(&s);

	time_t t = time(NULL);
	struct tm tm;
	memcpy(&tm, localtime(&t), sizeof(tm));
	shell.last_minute = tm.tm_min;
	shell.dirty = 0;
}

void dl_shell_motion(float x, float y)
{
	if (shell.disabled)
		return;
	int hover = -1;
	for (int i = 0; i < shell.nitems; i++)
		if (x >= shell.item[i].x && y >= shell.item[i].y && x < shell.item[i].x + shell.item[i].w &&
			y < shell.item[i].y + shell.item[i].h)
			hover = i;
	if (hover != shell.hover) {
		shell.hover = hover;
		shell.dirty = 1;
	}
}

int dl_shell_press(float x, float y, uint32_t button, int32_t *focus_pid)
{
	*focus_pid = 0;
	if (shell.disabled)
		return 0;
	if (button != 0)
		return 0;
	if (y < DL_MENU_BAR_HEIGHT)
		return 1;
	if (y < shell.panel_y || y >= shell.panel_y + shell.panel_h || x < shell.panel_x ||
		x >= shell.panel_x + shell.panel_w)
		return 0;
	for (int i = 0; i < shell.nitems; i++)
		if (x >= shell.item[i].x && y >= shell.item[i].y && x < shell.item[i].x + shell.item[i].w &&
			y < shell.item[i].y + shell.item[i].h) {
			*focus_pid = shell.item[i].pid;
			break;
		}
	return 1; /* the Dock always swallows the click, even between tiles */
}
