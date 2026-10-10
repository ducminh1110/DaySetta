/*
 * dsws_probe: PROBE CLIENT, NOT APPLE UI. A Darling-built Mach client that talks to the window server with the
 * real MIG routines (29000, 29010, 32000, 30263 CreateWindow, 30311 WindowIsOrderedIn) and then draws a window
 * through the server's canvas messages (0x44594c01 attach, 0x44594c02 flush). The canvas memory is mapped from
 * a host file (see ESC-G1-1) because this Darling cannot pass memory entries between processes.
 *   usage: dsws_probe [x y]    (window position on the virtual screen)
 */
#include <fcntl.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "shell_font.h"

static int failures;
static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	fflush(stdout);
	if (!ok)
		failures++;
}

static kern_return_t rpc(mach_msg_header_t *msg, mach_msg_size_t send, mach_msg_size_t rcv, mach_port_t dest, int id,
	mach_msg_bits_t extra)
{
	msg->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE) | extra;
	msg->msgh_size = send;
	msg->msgh_remote_port = dest;
	msg->msgh_local_port = mig_get_reply_port();
	msg->msgh_voucher_port = MACH_PORT_NULL;
	msg->msgh_id = id;
	return mach_msg(msg, MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_TIMEOUT, send, rcv, msg->msgh_local_port, 3000,
		MACH_PORT_NULL);
}

static void text(uint8_t *px, uint32_t rb, int x, int y, const char *s, int scale, uint8_t r, uint8_t g, uint8_t b)
{
	for (; *s; s++, x += DL_FONT_W * scale) {
		if (*s < DL_FONT_FIRST || *s > DL_FONT_LAST)
			continue;
		const uint8_t *gl = dl_font_bits + (*s - DL_FONT_FIRST) * DL_FONT_H;
		for (int row = 0; row < DL_FONT_H * scale; row++)
			for (int col = 0; col < DL_FONT_W * scale; col++)
				if (gl[row / scale] & (0x80 >> (col / scale))) {
					uint8_t *p = px + (size_t)(y + row) * rb + (size_t)(x + col) * 4;
					p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
				}
	}
}

int main(int argc, char **argv)
{
	int32_t wx = argc > 2 ? atoi(argv[1]) : 120, wy = argc > 2 ? atoi(argv[2]) : 90;
	const uint32_t W = 640, Hh = 360;
	mach_port_t server;
	kern_return_t kr = bootstrap_look_up(bootstrap_port, "com.apple.windowserver.active", &server);
	check(kr == KERN_SUCCESS, "bootstrap look up com.apple.windowserver.active");
	if (kr)
		return 1;

	struct { mach_msg_header_t head; mach_msg_body_t body; mach_msg_port_descriptor_t port; NDR_record_t ndr;
		uint32_t major, minor; uint8_t li, pad[3]; mach_msg_max_trailer_t t; } v;
	memset(&v, 0, sizeof v);
	kr = rpc(&v.head, sizeof(mach_msg_header_t), sizeof v, server, 29000, 0);
	check(kr == KERN_SUCCESS && v.major == 600 && v.head.msgh_id == 29100, "29000 GetCoreGraphicsServerVersion 600.0");

	struct { mach_msg_header_t head; mach_msg_body_t body; mach_msg_port_descriptor_t port; mach_msg_max_trailer_t t; } s;
	memset(&s, 0, sizeof s);
	kr = rpc(&s.head, sizeof(mach_msg_header_t), sizeof s, server, 29010, 0);
	check(kr == KERN_SUCCESS && s.head.msgh_id == 29110, "29010 GetSessionPort");

	union {
		struct { mach_msg_header_t head; mach_msg_body_t body; mach_msg_port_descriptor_t notify, token; NDR_record_t ndr;
			uint32_t name_offset, name_count; char name[16]; uint32_t flags[2]; uint32_t a, b; } req;
		struct { mach_msg_header_t head; mach_msg_body_t body; mach_msg_port_descriptor_t connection, events; NDR_record_t ndr;
			uint32_t connection_id; mach_msg_max_trailer_t t; } rep;
	} n;
	mach_port_t notify;
	mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &notify);
	memset(&n, 0, sizeof n);
	n.req.body.msgh_descriptor_count = 2;
	n.req.notify = (mach_msg_port_descriptor_t){ .name = notify, .disposition = MACH_MSG_TYPE_MAKE_SEND, .type = MACH_MSG_PORT_DESCRIPTOR };
	n.req.token = (mach_msg_port_descriptor_t){ .name = mach_task_self(), .disposition = MACH_MSG_TYPE_COPY_SEND, .type = MACH_MSG_PORT_DESCRIPTOR };
	n.req.ndr = NDR_record;
	n.req.name_count = sizeof("dsws_probe");
	strcpy(n.req.name, "dsws_probe");
	n.req.flags[0] = 1;
	kr = rpc(&n.req.head, sizeof n.req, sizeof n.rep, s.port.name, 32000, MACH_MSGH_BITS_COMPLEX);
	check(kr == KERN_SUCCESS && n.rep.head.msgh_id == 32100 && n.rep.connection_id, "32000 NewConnectionPort");
	if (kr)
		return 1;
	mach_port_t conn = n.rep.connection.name;
	uint32_t conn_id = n.rep.connection_id;

	/* 30263 CreateWindow: one OOL shape region, NDR, connection id, flags (probe-chosen; the server reads only the region). */
	static uint8_t region[16];
	union {
		struct { mach_msg_header_t head; mach_msg_body_t body; mach_msg_ool_descriptor_t region; NDR_record_t ndr;
			uint32_t args[13]; } req;
		struct { mach_msg_header_t head; mach_msg_body_t body; mach_msg_port_descriptor_t shm; NDR_record_t ndr;
			uint32_t out[5]; mach_msg_max_trailer_t t; } rep;
	} c;
	memset(&c, 0, sizeof c);
	c.req.body.msgh_descriptor_count = 1;
	c.req.region = (mach_msg_ool_descriptor_t){ .address = region, .size = sizeof region, .deallocate = 0,
		.copy = MACH_MSG_VIRTUAL_COPY, .type = MACH_MSG_OOL_DESCRIPTOR };
	c.req.ndr = NDR_record;
	c.req.args[0] = conn_id;
	kr = rpc(&c.req.head, sizeof c.req, sizeof c.rep, conn, 30263, MACH_MSGH_BITS_COMPLEX);
	check(kr == KERN_SUCCESS && c.rep.head.msgh_id == 30363 && c.rep.out[0], "30263 CreateWindow -> window id");
	if (kr)
		return 1;
	uint32_t window = c.rep.out[0];
	printf("window id 0x%x tag 0x%x\n", window, c.rep.out[1]);

	/* AttachCanvas (Daylight-private 0x44594c01) */
	union {
		struct { mach_msg_header_t head; NDR_record_t ndr; uint32_t args[3]; } req;
		struct { mach_msg_header_t head; mach_msg_body_t body; mach_msg_port_descriptor_t entry; NDR_record_t ndr;
			uint32_t out[2]; mach_msg_max_trailer_t t; } rep;
	} a;
	memset(&a, 0, sizeof a);
	a.req.ndr = NDR_record;
	a.req.args[0] = window; a.req.args[1] = W; a.req.args[2] = Hh;
	kr = rpc(&a.req.head, sizeof a.req, sizeof a.rep, server, 0x44594c01, 0);
	check(kr == KERN_SUCCESS && (a.rep.head.msgh_bits & MACH_MSGH_BITS_COMPLEX) && a.rep.out[1] >= W * Hh * 4, "AttachCanvas reply");
	if (kr)
		return 1;
	int fd = open("/Volumes/SystemRoot/tmp/dsws/shm-last", O_RDWR);
	uint8_t *px = fd < 0 ? MAP_FAILED : mmap(NULL, a.rep.out[1], PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	check(px != MAP_FAILED, "map canvas memory (host file fallback, ESC-G1-1)");
	if (px == MAP_FAILED)
		return 1;
	uint32_t rb = a.rep.out[0];

	for (uint32_t y = 0; y < Hh; y++)
		for (uint32_t x = 0; x < W; x++) {
			uint8_t *p = px + (size_t)y * rb + x * 4;
			if (y < 28) { p[0] = p[1] = p[2] = 0xd8; }
			else { p[0] = (uint8_t)(0xf4 - y / 6); p[1] = (uint8_t)(0xf0 - y / 8); p[2] = (uint8_t)(0xff - x / 10); }
			p[3] = 255;
		}
	const uint8_t dots[3][3] = { { 0x56, 0x5f, 0xff }, { 0x2e, 0xbd, 0xff }, { 0x3d, 0xc9, 0x28 } };
	for (int d = 0; d < 3; d++)
		for (int yy = -6; yy <= 6; yy++)
			for (int xx = -6; xx <= 6; xx++)
				if (xx * xx + yy * yy <= 36) {
					uint8_t *p = px + (size_t)(14 + yy) * rb + (size_t)(18 + d * 22 + xx) * 4;
					p[0] = dots[d][0]; p[1] = dots[d][1]; p[2] = dots[d][2];
				}
	text(px, rb, 100, 7, "dsws_probe  (probe client)", 1, 0x30, 0x30, 0x30);
	text(px, rb, 40, 70, "PROBE CLIENT - NOT APPLE UI", 2, 0x20, 0x20, 0x90);
	text(px, rb, 40, 140, "Drawn by a Darling-built arm64 Mach client through the", 1, 0, 0, 0);
	text(px, rb, 40, 160, "DaySetta windowserver (Daylight MIG dispatch, macOS 27 tables).", 1, 0, 0, 0);
	text(px, rb, 40, 190, "MIG: 29000 29010 32000 30263 + canvas attach/flush", 1, 0, 0, 0);

	/* CanvasFlushed (simpleroutine 0x44594c02): window, x, y, w, h, level */
	struct { mach_msg_header_t head; NDR_record_t ndr; int32_t args[6]; } f;
	memset(&f, 0, sizeof f);
	f.head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
	f.head.msgh_size = sizeof f;
	f.head.msgh_remote_port = server;
	f.head.msgh_id = 0x44594c02;
	f.ndr = NDR_record;
	int32_t fa[6] = { (int32_t)window, wx, wy, (int32_t)W, (int32_t)Hh, 0 };
	memcpy(f.args, fa, sizeof fa);
	kr = mach_msg(&f.head, MACH_SEND_MSG, sizeof f, 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
	check(kr == KERN_SUCCESS, "CanvasFlushed sent");

	/* 30311 WindowIsOrderedIn on the real routine, as a liveness check of the window table. */
	struct { mach_msg_header_t head; NDR_record_t ndr; uint32_t args[2]; mach_msg_max_trailer_t t; } o;
	memset(&o, 0, sizeof o);
	o.ndr = NDR_record; o.args[0] = conn_id; o.args[1] = window;
	kr = rpc(&o.head, offsetof(typeof(o), t), sizeof o, conn, 30311, 0);
	check(kr == KERN_SUCCESS && o.head.msgh_id == 30411, "30311 WindowIsOrderedIn replied");

	sleep(2);
	printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures != 0;
}
