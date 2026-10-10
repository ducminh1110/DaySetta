/*
 * Daylight — an open WindowServer for Darling.
 *
 * Core dispatch API. This layer knows message IDs and raw message bytes only, so it builds and is
 * tested on plain Linux; the Mach receive loop (main_darling.c) feeds it inside Darling.
 */
#ifndef DAYLIGHT_H
#define DAYLIGHT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "dl_routines.h"

/* MIG conventions. */
#define DL_MIG_BAD_ID (-303)
#define DL_MIG_TYPE_ERROR (-300)
#define DL_KERN_RESOURCE_SHORTAGE 6
#define DL_MIG_REPLY_ID_OFFSET 100

/* Mach port dispositions used in descriptors (values from <mach/message.h>). */
#define DL_PORT_MOVE_SEND 17
#define DL_PORT_COPY_SEND 19
#define DL_PORT_MAKE_SEND 20

#define DL_MAX_PORTS 4

typedef struct dl_request {
	int32_t id;
	const void *body;     /* bytes after the mach_msg_header_t (descriptor count first if complex) */
	size_t body_size;
	int32_t client_pid;   /* -1 when unknown */
	uint32_t local_port;  /* port the request arrived on */
	uint8_t complex;      /* MACH_MSGH_BITS_COMPLEX was set */
} dl_request;

/*
 * A reply is simple (NDR + RetCode + body) unless the handler returns ports, in which case it is
 * complex (descriptors, then NDR + body, no RetCode; no NDR either when the body is empty), as MIG
 * generates for successful replies.
 */
typedef struct dl_reply {
	int32_t retcode;      /* kern_return_t placed in the MIG reply */
	void *body;           /* caller-owned buffer for the reply body */
	size_t body_capacity;
	size_t body_size;
	uint32_t ports[DL_MAX_PORTS];
	uint8_t port_dispositions[DL_MAX_PORTS];
	size_t nports;
	/* One out-of-line buffer, sent after the ports (copied by the kernel; the data stays owned by the caller). */
	const void *ool;
	size_t ool_size;
	uint8_t has_ool;
} dl_reply;

/* Ports carried by a complex request, in descriptor order. */
typedef struct dl_request_ports {
	uint32_t names[DL_MAX_PORTS];
	size_t count;
	const uint8_t *data;  /* inline data after the descriptors and NDR record */
	size_t data_size;
} dl_request_ports;

/* Parses port descriptors of a complex request; returns -1 on malformed or non-port descriptors. */
int dl_request_parse_ports(const dl_request *req, dl_request_ports *out);

/* Inline data of a simple request, after the NDR record; NULL if the body is too short. */
const uint8_t *dl_request_data(const dl_request *req, size_t *size);

/* Inline data after the NDR record of a simple or complex request (any descriptors); NULL if malformed. */
const uint8_t *dl_request_inline_data(const dl_request *req, size_t *size);

int dl_reply_add_port(dl_reply *reply, uint32_t name, uint8_t disposition);
int dl_reply_put(dl_reply *reply, const void *data, size_t size);

/* What the Mach front end provides to handlers. */
typedef struct dl_host {
	/* Creates a port the server receives on; returns its name, 0 on failure. */
	uint32_t (*new_port)(void *ctx);
	/*
	 * Creates shareable memory of `size` bytes initialised from `init`, maps it into the server and returns a
	 * memory-entry port (send right) for clients to map; *mapped receives the server's mapping. 0 on failure.
	 */
	uint32_t (*new_shared_memory)(void *ctx, const void *init, size_t size, void **mapped);
	/* Wakes a client: an empty message (id 0) to its notification port. NULL where there is no Mach. */
	void (*notify)(void *ctx, uint32_t port);
	/* Sends a simple message (id, body) to a send right the server owns, consuming that right. */
	void (*send_once)(void *ctx, uint32_t port, int32_t id, const void *body, size_t size);
	void *ctx;
} dl_host;

/* A monotonic timestamp in mach_absolute_time units where available (0 on plain Linux builds). */
uint64_t dl_now_ticks(void);

/* Attaches a single out-of-line buffer to a reply. */
void dl_reply_set_ool(dl_reply *reply, const void *data, size_t size);

typedef void (*dl_handler)(const dl_request *req, dl_reply *reply, void *ctx);

typedef struct dl_trace_entry {
	int32_t id;
	uint64_t calls;
	uint8_t handled;
} dl_trace_entry;

typedef struct dl_server {
	dl_handler handlers[DL_ROUTINE_COUNT];
	void *contexts[DL_ROUTINE_COUNT];
	dl_trace_entry trace[DL_ROUTINE_COUNT];
	uint64_t unknown_ids;   /* messages whose id is not in the protocol table */
	FILE *log;              /* NULL disables per-message logging */
	dl_host host;
	struct dl_session *session;
	struct dl_display *display;
	struct dl_windows *windows;
	struct dl_canvases *canvases;
	struct dl_input *input;
} dl_server;

void dl_server_init(dl_server *server, FILE *log);

/* Returns 0 on success, -1 if id is not a known routine. */
int dl_register(dl_server *server, int32_t id, dl_handler handler, void *ctx);

/* Dispatches one request. Unimplemented or unknown routines reply with MIG_BAD_ID. */
void dl_dispatch(dl_server *server, const dl_request *req, dl_reply *reply);

/* Writes per-routine call counts as JSON, most-called first: the work list for implementers. */
void dl_trace_write_json(const dl_server *server, FILE *out);

#endif
