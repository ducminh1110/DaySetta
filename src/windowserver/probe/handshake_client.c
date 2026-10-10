/*
 * Runs inside Darling against a live daylight server and performs the SkyLight client handshake
 * with the same message layouts and the same reply checks SkyLight 26.6 applies.
 */
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		failures++;
}

typedef struct {
	mach_msg_header_t head;
	mach_msg_body_t body;
	mach_msg_port_descriptor_t port;
	NDR_record_t ndr;
	uint32_t major, minor;
	uint8_t logged_in;
	uint8_t pad[3];
	mach_msg_max_trailer_t trailer;
} version_reply;

typedef struct {
	mach_msg_header_t head;
	mach_msg_body_t body;
	mach_msg_port_descriptor_t port;
	mach_msg_max_trailer_t trailer;
} port_reply;

typedef struct {
	mach_msg_header_t head;
	mach_msg_body_t body;
	mach_msg_port_descriptor_t notify, token;
	NDR_record_t ndr;
	uint32_t name_offset, name_count;
	char name[16];
	uint32_t flags[2];
	uint32_t a, b;
} new_connection_request;

typedef struct {
	mach_msg_header_t head;
	mach_msg_body_t body;
	mach_msg_port_descriptor_t connection, events;
	NDR_record_t ndr;
	uint32_t connection_id;
	mach_msg_max_trailer_t trailer;
} new_connection_reply;

static kern_return_t rpc(mach_msg_header_t *msg, mach_msg_size_t send, mach_msg_size_t rcv, mach_port_t dest, int id,
	mach_msg_bits_t extra_bits)
{
	msg->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE) | extra_bits;
	msg->msgh_remote_port = dest;
	msg->msgh_local_port = mig_get_reply_port();
	msg->msgh_voucher_port = MACH_PORT_NULL;
	msg->msgh_id = id;
	return mach_msg(msg, MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_TIMEOUT, send, rcv, msg->msgh_local_port, 2000,
		MACH_PORT_NULL);
}

int main(void)
{
	mach_port_t server;
	kern_return_t kr = bootstrap_look_up(bootstrap_port, "com.apple.windowserver.active", &server);
	check(kr == KERN_SUCCESS, "look up com.apple.windowserver.active");
	if (kr != KERN_SUCCESS)
		return 1;

	/* 29000 GetCoreGraphicsServerVersion */
	version_reply v;
	memset(&v, 0, sizeof(v));
	kr = rpc(&v.head, sizeof(mach_msg_header_t), sizeof(v), server, 29000, 0);
	check(kr == KERN_SUCCESS && v.head.msgh_id == 29100, "29000 reply id");
	check((v.head.msgh_bits & MACH_MSGH_BITS_COMPLEX) && v.head.msgh_size == 0x3c && v.body.msgh_descriptor_count == 1 &&
		v.port.disposition == MACH_MSG_TYPE_MOVE_SEND && v.port.type == MACH_MSG_PORT_DESCRIPTOR, "29000 reply shape");
	check(v.major == 600 && v.minor == 0 && v.logged_in == 1, "29000 version 600.0, logged in");
	check(v.port.name == server, "29000 session port is the server port");

	/* 29010 GetSessionPort */
	port_reply s;
	memset(&s, 0, sizeof(s));
	kr = rpc(&s.head, sizeof(mach_msg_header_t), sizeof(s), server, 29010, 0);
	check(kr == KERN_SUCCESS && s.head.msgh_id == 29110 && s.head.msgh_size == 0x28 &&
		s.port.disposition == MACH_MSG_TYPE_MOVE_SEND, "29010 session port reply");

	/* 32000 NewConnectionPort */
	union {
		new_connection_request req;
		new_connection_reply rep;
	} n;
	mach_port_t notify;
	mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &notify);
	memset(&n, 0, sizeof(n));
	n.req.body.msgh_descriptor_count = 2;
	n.req.notify = (mach_msg_port_descriptor_t){ .name = notify, .disposition = MACH_MSG_TYPE_MAKE_SEND,
		.type = MACH_MSG_PORT_DESCRIPTOR };
	n.req.token = (mach_msg_port_descriptor_t){ .name = mach_task_self(), .disposition = MACH_MSG_TYPE_COPY_SEND,
		.type = MACH_MSG_PORT_DESCRIPTOR };
	n.req.ndr = NDR_record;
	n.req.name_count = sizeof("handshake");
	strcpy(n.req.name, "handshake");
	n.req.flags[0] = 1;
	kr = rpc(&n.req.head, sizeof(n.req), sizeof(n.rep), s.port.name, 32000, MACH_MSGH_BITS_COMPLEX);
	check(kr == KERN_SUCCESS && n.rep.head.msgh_id == 32100, "32000 reply id");
	check(n.rep.head.msgh_size == 0x40 && n.rep.body.msgh_descriptor_count == 2 &&
		n.rep.connection.disposition == MACH_MSG_TYPE_MOVE_SEND && n.rep.events.disposition == MACH_MSG_TYPE_MOVE_SEND,
		"32000 reply shape");
	check(n.rep.connection_id != 0 && MACH_PORT_VALID(n.rep.connection.name) && MACH_PORT_VALID(n.rep.events.name),
		"32000 connection id and ports");
	printf("connection id 0x%x\n", n.rep.connection_id);

	/* An unimplemented routine on the connection port answers MIG_BAD_ID instead of hanging. */
	struct {
		mach_msg_header_t head;
		NDR_record_t ndr;
		kern_return_t ret;
		mach_msg_max_trailer_t trailer;
	} e;
	memset(&e, 0, sizeof(e));
	kr = rpc(&e.head, sizeof(mach_msg_header_t), sizeof(e), n.rep.connection.name, 32001, 0);
	check(kr == KERN_SUCCESS && e.ret == MIG_BAD_ID, "unimplemented routine answers MIG_BAD_ID");

	printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures != 0;
}
