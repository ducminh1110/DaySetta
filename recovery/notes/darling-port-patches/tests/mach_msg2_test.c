/*
 * Exercises Darling's mach_msg2_trap through libsystem_kernel's exported mach_msg2_internal(),
 * packing arguments the way <mach/message.h>'s mach_msg2() inline does.
 */
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MACH64_MSG_VECTOR 0x0000000100000000ull
#define PACK(lo, hi) ((uint64_t)(hi) << 32 | (uint32_t)(lo))

typedef struct {
	mach_vm_address_t msgv_data;
	mach_vm_address_t msgv_rcv_addr;
	mach_msg_size_t msgv_send_size;
	mach_msg_size_t msgv_rcv_size;
} vec_t;

typedef struct {
	mach_msg_size_t msgdh_size;
	uint32_t msgdh_reserved;
} aux_t;

extern mach_msg_return_t mach_msg2_internal(void *data, uint64_t option64, uint64_t bits_and_send_size,
	uint64_t remote_and_local, uint64_t voucher_and_id, uint64_t desc_and_rcv_name,
	uint64_t rcv_size_and_priority, uint64_t timeout);

typedef struct {
	mach_msg_header_t head;
	char text[32];
} msg_t;

typedef struct {
	mach_msg_header_t head;
	char text[32];
	mach_msg_trailer_t trailer;
	char pad[64];
} rcv_t;

static int failures;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		failures++;
}

static mach_msg_return_t send2(mach_port_t port, msg_t *m, mach_msg_id_t reg_id)
{
	mach_msg_bits_t bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
	return mach_msg2_internal(m, MACH_SEND_MSG, PACK(bits, sizeof(*m)), PACK(port, MACH_PORT_NULL),
		PACK(MACH_PORT_NULL, reg_id), PACK(0, MACH_PORT_NULL), PACK(0, 0), 0);
}

static mach_msg_return_t rcv2(mach_port_t port, rcv_t *r)
{
	return mach_msg2_internal(r, MACH_RCV_MSG | MACH_RCV_TIMEOUT, PACK(0, 0), PACK(MACH_PORT_NULL, MACH_PORT_NULL),
		PACK(0, 0), PACK(0, port), PACK(sizeof(*r), 0), 1000);
}

int main(void)
{
	mach_port_t port;
	msg_t m;
	rcv_t r;

	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) != KERN_SUCCESS) {
		printf("FAIL mach_port_allocate\n");
		return 1;
	}

	/* 1. Scalar send, then scalar receive into a separate buffer. */
	memset(&m, 0, sizeof(m));
	strcpy(m.text, "scalar");
	check(send2(port, &m, 1234) == MACH_MSG_SUCCESS, "scalar send");
	memset(&r, 0, sizeof(r));
	check(rcv2(port, &r) == MACH_MSG_SUCCESS, "scalar receive");
	check(r.head.msgh_id == 1234 && strcmp(r.text, "scalar") == 0, "scalar payload and id");

	/* 2. The send header comes from the registers, not memory. */
	memset(&m, 0, sizeof(m));
	m.head.msgh_id = 999;
	strcpy(m.text, "regs");
	check(send2(port, &m, 4321) == MACH_MSG_SUCCESS, "send with stale header in memory");
	memset(&r, 0, sizeof(r));
	check(rcv2(port, &r) == MACH_MSG_SUCCESS && r.head.msgh_id == 4321, "register msgh_id wins");

	/* 3. Combined send + receive in one scalar call (to our own port). */
	{
		rcv_t buf;
		mach_msg_bits_t bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);

		memset(&buf, 0, sizeof(buf));
		strcpy(buf.text, "roundtrip");
		mach_msg_return_t mr = mach_msg2_internal(&buf, MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_TIMEOUT,
			PACK(bits, sizeof(msg_t)), PACK(port, MACH_PORT_NULL), PACK(MACH_PORT_NULL, 77),
			PACK(0, port), PACK(sizeof(buf), 0), 1000);
		check(mr == MACH_MSG_SUCCESS && buf.head.msgh_id == 77 && strcmp(buf.text, "roundtrip") == 0,
			"scalar send+receive");
	}

	/* 4. Vector form: separate receive buffer and an aux slot. */
	{
		vec_t vecs[2];
		aux_t aux = { 0xdead, 0xbeef };
		mach_msg_bits_t bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);

		memset(&m, 0, sizeof(m));
		strcpy(m.text, "vector");
		memset(&r, 0, sizeof(r));
		vecs[0] = (vec_t){ (mach_vm_address_t)&m, (mach_vm_address_t)&r, sizeof(m), sizeof(r) };
		vecs[1] = (vec_t){ (mach_vm_address_t)&aux, 0, 0, sizeof(aux) };
		mach_msg_return_t mr = mach_msg2_internal(vecs,
			MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_TIMEOUT | MACH64_MSG_VECTOR,
			PACK(bits, 2), PACK(port, MACH_PORT_NULL), PACK(MACH_PORT_NULL, 55),
			PACK(0, port), PACK(2, 0), 1000);
		check(mr == MACH_MSG_SUCCESS, "vector send+receive");
		check(r.head.msgh_id == 55 && strcmp(r.text, "vector") == 0, "vector receive buffer");
		check(strcmp(m.text, "vector") == 0, "vector send buffer untouched");
		check(aux.msgdh_size == 0, "vector aux reported empty");
	}

	/* 5. Ordinary mach_msg() now runs on mach_msg2 too. */
	memset(&m, 0, sizeof(m));
	m.head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
	m.head.msgh_size = sizeof(m);
	m.head.msgh_remote_port = port;
	m.head.msgh_id = 42;
	strcpy(m.text, "mach_msg");
	check(mach_msg(&m.head, MACH_SEND_MSG, sizeof(m), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL) == MACH_MSG_SUCCESS,
		"mach_msg send");
	memset(&r, 0, sizeof(r));
	check(mach_msg(&r.head, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(r), port, 1000, MACH_PORT_NULL) == MACH_MSG_SUCCESS
		&& r.head.msgh_id == 42 && strcmp(r.text, "mach_msg") == 0, "mach_msg receive");

	/* 6. Receive timeout is reported, not hung. */
	memset(&r, 0, sizeof(r));
	check(rcv2(port, &r) == MACH_RCV_TIMED_OUT, "receive timeout");

	printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures != 0;
}
