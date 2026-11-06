/*
 * daylight-x11: shows Daylight's screen in an X11 window and sends the window's mouse and keyboard input back.
 * A plain Linux program: it reads the shared framebuffer (server/include/framebuffer.h) and writes input commands
 * (server/include/input.h) to the input FIFO, both in Daylight's runtime directory on the host (/tmp/daylight).
 *
 *   cc -O2 -o daylight-x11 daylight-x11.c -lX11
 *   daylight-x11 [runtime directory (default /tmp/daylight)]
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "../../include/framebuffer.h"

static char fb_path[4096], input_path[4096];
static int input_fd = -1;

static void send_cmd(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void send_cmd(const char *fmt, ...)
{
	char line[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (input_fd < 0)
		input_fd = open(input_path, O_WRONLY | O_NONBLOCK);
	if (input_fd < 0 || n <= 0)
		return;
	if (write(input_fd, line, (size_t)n) < 0 && errno != EAGAIN) {
		close(input_fd);
		input_fd = -1;
	}
}

/* X keysym -> macOS virtual key code (Carbon kVK_*) and the character AppKit expects (0 = none). */
static const struct {
	KeySym sym;
	unsigned keycode, ch;
} keys[] = {
	{ XK_Return, 36, '\r' }, { XK_KP_Enter, 76, 3 }, { XK_Tab, 48, '\t' }, { XK_ISO_Left_Tab, 48, 25 },
	{ XK_space, 49, ' ' }, { XK_BackSpace, 51, 127 }, { XK_Escape, 53, 27 }, { XK_Delete, 117, 0xf728 },
	{ XK_Home, 115, 0xf729 }, { XK_End, 119, 0xf72b }, { XK_Page_Up, 116, 0xf72c }, { XK_Page_Down, 121, 0xf72d },
	{ XK_Left, 123, 0xf702 }, { XK_Right, 124, 0xf703 }, { XK_Down, 125, 0xf701 }, { XK_Up, 126, 0xf700 },
	{ XK_F1, 122, 0xf704 }, { XK_F2, 120, 0xf705 }, { XK_F3, 99, 0xf706 }, { XK_F4, 118, 0xf707 },
	{ XK_F5, 96, 0xf708 }, { XK_F6, 97, 0xf709 }, { XK_F7, 98, 0xf70a }, { XK_F8, 100, 0xf70b },
	{ XK_F9, 101, 0xf70c }, { XK_F10, 109, 0xf70d }, { XK_F11, 103, 0xf70e }, { XK_F12, 111, 0xf70f },
	{ XK_Shift_L, 56, 0 }, { XK_Shift_R, 60, 0 }, { XK_Control_L, 59, 0 }, { XK_Control_R, 62, 0 },
	{ XK_Alt_L, 58, 0 }, { XK_Alt_R, 61, 0 }, { XK_Super_L, 55, 0 }, { XK_Super_R, 54, 0 },
	{ XK_Caps_Lock, 57, 0 },
};

/* US layout: unshifted character -> key code */
static const char *const us_chars = "asdfhgzxcv\0bqweryt123465=97-80]ou[ip\0lj'k;\\,/nm.\0\0`";

static unsigned keycode_for_char(unsigned c)
{
	static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
	static const char base[] = "`1234567890-=[]\\;',./";
	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	const char *s = strchr(shifted, (int)c);
	if (c && s)
		c = (unsigned char)base[s - shifted];
	for (unsigned i = 0; i < 51; i++)
		if (us_chars[i] && (unsigned char)us_chars[i] == c)
			return i;
	return 0xffff;
}

static uint32_t mac_flags(unsigned state)
{
	uint32_t f = 0;
	if (state & ShiftMask)
		f |= 0x20000;
	if (state & ControlMask)
		f |= 0x40000;
	if (state & Mod1Mask)
		f |= 0x80000; /* Alt: Option */
	if (state & Mod4Mask)
		f |= 0x100000; /* Super: Command */
	if (state & LockMask)
		f |= 0x10000;
	return f;
}

static void key_event(XKeyEvent *e, int down)
{
	char text[8] = "";
	KeySym sym = 0;
	int n = XLookupString(e, text, sizeof(text) - 1, &sym, NULL);
	unsigned keycode = 0xffff, ch = 0;

	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		if (keys[i].sym == sym) {
			keycode = keys[i].keycode;
			ch = keys[i].ch;
		}
	if (keycode == 0xffff && n == 1) {
		ch = (unsigned char)text[0];
		/* with Control, X gives control characters; AppKit wants the letter */
		KeySym base = XLookupKeysym(e, 0);
		if (base >= 'a' && base <= 'z')
			keycode = keycode_for_char((unsigned)base);
		else
			keycode = keycode_for_char(ch);
		if ((e->state & (Mod4Mask | ControlMask)) && base >= XK_space && base < 0x7f)
			ch = (unsigned)base;
	}
	if (keycode == 0xffff)
		return;
	send_cmd("flags %x\n", mac_flags(e->state));
	send_cmd("key %u %u %s\n", keycode, ch, down ? "down" : "up");
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/tmp/daylight";
	snprintf(fb_path, sizeof(fb_path), "%.4000s/screen.fb", dir);
	snprintf(input_path, sizeof(input_path), "%.4000s/input", dir);
	signal(SIGPIPE, SIG_IGN);

	int fd;
	struct stat st;
	while ((fd = open(fb_path, O_RDONLY)) < 0 || fstat(fd, &st) != 0 || st.st_size < DL_FB_HEADER_SIZE) {
		if (fd >= 0)
			close(fd);
		fprintf(stderr, "daylight-x11: waiting for %s\n", fb_path);
		sleep(1);
	}
	const uint8_t *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	const dl_fb_header *hdr = (const dl_fb_header *)map;
	if (hdr->magic != DL_FB_MAGIC || (size_t)st.st_size < DL_FB_HEADER_SIZE + (size_t)hdr->stride * hdr->height) {
		fprintf(stderr, "daylight-x11: %s is not a Daylight framebuffer\n", fb_path);
		return 1;
	}
	unsigned w = hdr->width, h = hdr->height;

	Display *dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "daylight-x11: cannot open display\n");
		return 1;
	}
	int scr = DefaultScreen(dpy);
	Visual *vis = DefaultVisual(dpy, scr);
	if (DefaultDepth(dpy, scr) < 24) {
		fprintf(stderr, "daylight-x11: needs a 24-bit TrueColor display\n");
		return 1;
	}
	Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, w, h, 0, 0, 0);
	XStoreName(dpy, win, "Daylight");
	XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask |
		KeyReleaseMask | StructureNotifyMask);
	XMapWindow(dpy, win);
	GC gc = XCreateGC(dpy, win, 0, NULL);
	char *pixels = malloc((size_t)hdr->stride * h);
	XImage *img = XCreateImage(dpy, vis, 24, ZPixmap, 0, pixels, w, h, 32, (int)hdr->stride);
	img->byte_order = LSBFirst;

	uint32_t shown = ~0u;
	int pending_motion = 0, mx = 0, my = 0;
	for (;;) {
		int redraw = 0;
		while (XPending(dpy)) {
			XEvent e;
			XNextEvent(dpy, &e);
			switch (e.type) {
			case Expose:
				redraw = 1;
				break;
			case MotionNotify:
				pending_motion = 1;
				mx = e.xmotion.x;
				my = e.xmotion.y;
				break;
			case ButtonPress:
			case ButtonRelease:
				if (pending_motion) {
					send_cmd("move %d %d\n", mx, my);
					pending_motion = 0;
				}
				send_cmd("flags %x\n", mac_flags(e.xbutton.state));
				if (e.xbutton.button == 1 || e.xbutton.button == 3)
					send_cmd("%s %d %d %d\n", e.type == ButtonPress ? "down" : "up", e.xbutton.x, e.xbutton.y,
						e.xbutton.button == 3 ? 1 : 0);
				else if (e.type == ButtonPress && e.xbutton.button >= 4 && e.xbutton.button <= 7)
					send_cmd("scroll %d %d\n", e.xbutton.button == 4 ? 3 : e.xbutton.button == 5 ? -3 : 0,
						e.xbutton.button == 6 ? 3 : e.xbutton.button == 7 ? -3 : 0);
				break;
			case KeyPress:
			case KeyRelease:
				key_event(&e.xkey, e.type == KeyPress);
				break;
			}
		}
		if (pending_motion) {
			send_cmd("move %d %d\n", mx, my);
			pending_motion = 0;
		}
		uint32_t frame = hdr->frame;
		if ((frame & 1) == 0 && (frame != shown || redraw)) {
			memcpy(pixels, map + DL_FB_HEADER_SIZE, (size_t)hdr->stride * h);
			__sync_synchronize();
			if (hdr->frame == frame) {
				XPutImage(dpy, win, gc, img, 0, 0, 0, 0, w, h);
				XFlush(dpy);
				shown = frame;
			}
		}
		struct timespec ts = { 0, 8000000 };
		nanosleep(&ts, NULL);
	}
}
