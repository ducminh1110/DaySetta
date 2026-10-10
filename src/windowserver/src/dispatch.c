#include <stdlib.h>
#include <string.h>

#include "daylight.h"

static ptrdiff_t routine_index(int32_t id)
{
	const dl_routine_info *info = dl_routine_lookup(id);
	return info ? info - dl_routines : -1;
}

void dl_server_init(dl_server *server, FILE *log)
{
	memset(server, 0, sizeof(*server));
	for (size_t i = 0; i < DL_ROUTINE_COUNT; i++)
		server->trace[i].id = dl_routines[i].id;
	server->log = log;
}

int dl_register(dl_server *server, int32_t id, dl_handler handler, void *ctx)
{
	ptrdiff_t i = routine_index(id);
	if (i < 0)
		return -1;
	server->handlers[i] = handler;
	server->contexts[i] = ctx;
	return 0;
}

void dl_dispatch(dl_server *server, const dl_request *req, dl_reply *reply)
{
	ptrdiff_t i = routine_index(req->id);

	reply->retcode = DL_MIG_BAD_ID;
	reply->body_size = 0;
	reply->nports = 0;
	reply->ool = NULL;
	reply->ool_size = 0;
	reply->has_ool = 0;

	if (i < 0) {
		server->unknown_ids++;
		if (server->log)
			fprintf(server->log, "daylight: unknown msgh_id %d (pid %d)\n", req->id, req->client_pid);
		return;
	}

	server->trace[i].calls++;
	server->trace[i].handled = server->handlers[i] != NULL;

	if (server->log) {
		fprintf(server->log, "daylight: %s.%s (%d) pid %d%s\n", dl_routines[i].subsystem, dl_routines[i].name,
			req->id, req->client_pid, server->handlers[i] ? "" : " [unimplemented]");
		if (dl_routines[i].status)
			fprintf(server->log, "daylight:   (macOS 27: %s)\n", dl_routines[i].status == 1 ? "shape CHANGED vs 26.6" : "ADDED, unnamed");
	}

	if (server->handlers[i]) {
		reply->retcode = 0;
		server->handlers[i](req, reply, server->contexts[i]);
	}
}

static int by_calls_desc(const void *a, const void *b)
{
	const dl_trace_entry *x = a, *y = b;
	if (x->calls != y->calls)
		return x->calls < y->calls ? 1 : -1;
	return x->id - y->id;
}

void dl_trace_write_json(const dl_server *server, FILE *out)
{
	dl_trace_entry sorted[DL_ROUTINE_COUNT];
	memcpy(sorted, server->trace, sizeof(sorted));
	qsort(sorted, DL_ROUTINE_COUNT, sizeof(sorted[0]), by_calls_desc);

	fprintf(out, "{\n  \"unknown_ids\": %llu,\n  \"routines\": [\n", (unsigned long long)server->unknown_ids);
	int first = 1;
	for (size_t i = 0; i < DL_ROUTINE_COUNT && sorted[i].calls; i++) {
		const dl_routine_info *info = dl_routine_lookup(sorted[i].id);
		fprintf(out, "%s    {\"id\": %d, \"subsystem\": \"%s\", \"name\": \"%s\", \"calls\": %llu, \"implemented\": %s}",
			first ? "" : ",\n", sorted[i].id, info->subsystem, info->name,
			(unsigned long long)sorted[i].calls, sorted[i].handled ? "true" : "false");
		first = 0;
	}
	fprintf(out, "\n  ]\n}\n");
}

#define DL_NDR_SIZE 8
#define DL_PORT_DESCRIPTOR_SIZE 12
#define DL_DESCRIPTOR_TYPE_PORT 0

int dl_request_parse_ports(const dl_request *req, dl_request_ports *out)
{
	const uint8_t *p = req->body;
	uint32_t count;

	memset(out, 0, sizeof(*out));
	if (!req->complex || req->body_size < 4)
		return -1;
	memcpy(&count, p, 4);
	if (count > DL_MAX_PORTS || req->body_size < 4 + (size_t)count * DL_PORT_DESCRIPTOR_SIZE + DL_NDR_SIZE)
		return -1;
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *d = p + 4 + i * DL_PORT_DESCRIPTOR_SIZE;
		if (d[11] != DL_DESCRIPTOR_TYPE_PORT)
			return -1;
		memcpy(&out->names[i], d, 4);
	}
	out->count = count;
	out->data = p + 4 + count * DL_PORT_DESCRIPTOR_SIZE + DL_NDR_SIZE;
	out->data_size = req->body_size - (4 + count * DL_PORT_DESCRIPTOR_SIZE + DL_NDR_SIZE);
	return 0;
}

const uint8_t *dl_request_inline_data(const dl_request *req, size_t *size)
{
	const uint8_t *p = req->body;
	size_t off = 4;
	uint32_t count;

	if (!req->complex)
		return dl_request_data(req, size);
	if (req->body_size < 4)
		return NULL;
	memcpy(&count, p, 4);
	for (uint32_t i = 0; i < count; i++) {
		/* the type is byte 11 of every descriptor: port descriptors take 12 bytes, out-of-line ones 16 */
		if (off + 12 > req->body_size)
			return NULL;
		off += p[off + 11] == DL_DESCRIPTOR_TYPE_PORT ? 12 : 16;
	}
	if (off + DL_NDR_SIZE > req->body_size)
		return NULL;
	*size = req->body_size - off - DL_NDR_SIZE;
	return p + off + DL_NDR_SIZE;
}

const uint8_t *dl_request_data(const dl_request *req, size_t *size)
{
	if (req->complex || req->body_size < DL_NDR_SIZE)
		return NULL;
	*size = req->body_size - DL_NDR_SIZE;
	return (const uint8_t *)req->body + DL_NDR_SIZE;
}

int dl_reply_add_port(dl_reply *reply, uint32_t name, uint8_t disposition)
{
	if (reply->nports >= DL_MAX_PORTS)
		return -1;
	reply->ports[reply->nports] = name;
	reply->port_dispositions[reply->nports] = disposition;
	reply->nports++;
	return 0;
}

int dl_reply_put(dl_reply *reply, const void *data, size_t size)
{
	if (reply->body_size + size > reply->body_capacity)
		return -1;
	memcpy((uint8_t *)reply->body + reply->body_size, data, size);
	reply->body_size += size;
	return 0;
}

void dl_reply_set_ool(dl_reply *reply, const void *data, size_t size)
{
	reply->ool = data;
	reply->ool_size = size;
	reply->has_ool = 1;
}

#ifdef __APPLE__
#include <mach/mach_time.h>
uint64_t dl_now_ticks(void)
{
	return mach_absolute_time();
}
#else
#include <time.h>
uint64_t dl_now_ticks(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#endif
