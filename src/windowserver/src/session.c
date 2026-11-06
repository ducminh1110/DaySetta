/*
 * Session and connection handshake: the first three messages every SkyLight client sends.
 *
 * Layouts recovered from SkyLight 26.6 (x86_64): connect_and_check, get_session_port and
 * SLSNewConnection build these requests and validate the replies field by field.
 */
#include <string.h>

#include "daylight.h"
#include "dl_shm_seed.h"
#include "session.h"

/* connect_and_check rejects any other server version. */
#define DL_SERVER_VERSION_MAJOR 600
#define DL_SERVER_VERSION_MINOR 0

/* Connection IDs are opaque to clients but must be non-zero. */
#define DL_FIRST_CONNECTION_ID 0x1103

/*
 * GetCoreGraphicsServerVersion(server) -> (session port, major, minor, logged_in).
 * Returning the port the request came in on tells the client it talks to the session's server; any
 * other port marks the process as "launched before log in". logged_in = 1 skips the launch policy check.
 */
static void get_server_version(const dl_request *req, dl_reply *reply, void *ctx)
{
	(void)ctx;
	struct {
		uint32_t major;
		uint32_t minor;
		uint32_t logged_in; /* only the low byte is read */
	} out = { DL_SERVER_VERSION_MAJOR, DL_SERVER_VERSION_MINOR, 1 };

	dl_reply_add_port(reply, req->local_port, DL_PORT_MAKE_SEND);
	dl_reply_put(reply, &out, sizeof(out));
}

/* GetSessionPort(root) -> (session port). There is one session; its port is the service port. */
static void get_session_port(const dl_request *req, dl_reply *reply, void *ctx)
{
	(void)ctx;
	dl_reply_add_port(reply, req->local_port, DL_PORT_MAKE_SEND);
}

dl_connection *dl_session_find_connection(dl_session *session, uint32_t id)
{
	for (size_t i = 0; i < session->nconnections; i++) {
		if (session->connections[i].id == id)
			return &session->connections[i];
	}
	return NULL;
}

dl_connection *dl_session_connection_for_port(dl_session *session, uint32_t port)
{
	for (size_t i = 0; i < session->nconnections; i++) {
		if (session->connections[i].port == port)
			return &session->connections[i];
	}
	return NULL;
}

dl_connection *dl_session_connection_for_pid(dl_session *session, int32_t pid)
{
	for (size_t i = 0; i < session->nconnections; i++) {
		if (session->connections[i].pid == pid)
			return &session->connections[i];
	}
	return NULL;
}

/*
 * Notifications travel as datagrams: the server queues records and sends an empty message to the client's
 * notification port; the client's run loop then fetches the queue with GetPortStreamOutofline
 * (CGSSnarfAndDispatchDatagrams). A record is { u32 1, u32 length, u32 type, u32 sequence, data } where length
 * counts type, sequence and data (recorded on macOS 26).
 */
void dl_session_post(dl_server *server, dl_connection *conn, uint32_t type, const void *data, size_t size)
{
	uint32_t hdr[4] = { 1, (uint32_t)(8 + size), type, server->session->next_datagram_seq++ };

	if (conn->datagrams_size + sizeof(hdr) + size > sizeof(conn->datagrams))
		return;
	memcpy(conn->datagrams + conn->datagrams_size, hdr, sizeof(hdr));
	memcpy(conn->datagrams + conn->datagrams_size + sizeof(hdr), data, size);
	conn->datagrams_size += sizeof(hdr) + size;
	if (server->log)
		fprintf(server->log, "daylight: notification 0x%x to connection 0x%x\n", type, conn->id);
	if (server->host.notify && conn->notify_port)
		server->host.notify(server->host.ctx, conn->notify_port);
}

/* Events share the stream: { u32 0, u32 length, encoded event } (event_datagram_handler in SkyLight). */
void dl_session_post_event(dl_server *server, dl_connection *conn, const void *event, size_t size)
{
	uint32_t hdr[2] = { 0, (uint32_t)size };

	if (conn->datagrams_size + sizeof(hdr) + size > sizeof(conn->datagrams))
		return;
	memcpy(conn->datagrams + conn->datagrams_size, hdr, sizeof(hdr));
	memcpy(conn->datagrams + conn->datagrams_size + sizeof(hdr), event, size);
	conn->datagrams_size += sizeof(hdr) + size;
	if (server->host.notify && conn->notify_port)
		server->host.notify(server->host.ctx, conn->notify_port);
}

/* GetPortStreamOutofline(connection) -> (OOL datagram stream, its size); empty when nothing is pending. */
static void get_port_stream(const dl_request *req, dl_reply *reply, void *ctx)
{
	dl_server *server = ctx;
	static uint8_t out[sizeof(((dl_connection *)0)->datagrams)];
	uint32_t size = 0;
	dl_connection *conn = dl_session_connection_for_port(server->session, req->local_port);

	if (conn) {
		size = (uint32_t)conn->datagrams_size;
		memcpy(out, conn->datagrams, size);
		conn->datagrams_size = 0;
	}
	dl_reply_set_ool(reply, size ? out : NULL, size);
	dl_reply_put(reply, &size, sizeof(size));
}

/*
 * NewConnectionPort(session, notify port [make-send], task id token [copy-send], process name,
 *                   flag_a, flag_b, int_a, int_b) -> (connection port, connection shared memory, connection id)
 * SLSNewConnection maps the second port as a 0x40-byte region.
 */
static void new_connection_port(const dl_request *req, dl_reply *reply, void *ctx)
{
	dl_server *server = ctx;
	dl_session *session = server->session;
	dl_request_ports in;
	uint32_t name_offset, name_count;

	if (dl_request_parse_ports(req, &in) != 0 || in.count != 2 || in.data_size < 8) {
		reply->retcode = DL_MIG_TYPE_ERROR;
		return;
	}
	memcpy(&name_offset, in.data, 4);
	memcpy(&name_count, in.data + 4, 4);
	if (name_offset != 0 || name_count > in.data_size - 8) {
		reply->retcode = DL_MIG_TYPE_ERROR;
		return;
	}
	if (session->nconnections >= DL_MAX_CONNECTIONS || !server->host.new_port) {
		reply->retcode = DL_KERN_RESOURCE_SHORTAGE;
		return;
	}

	dl_connection *conn = &session->connections[session->nconnections];
	memset(conn, 0, sizeof(*conn));
	conn->port = server->host.new_port(server->host.ctx);
	conn->shm_port = server->host.new_shared_memory ?
		server->host.new_shared_memory(server->host.ctx, dl_shm_connection, dl_shm_connection_size, &conn->shm) :
		server->host.new_port(server->host.ctx);
	if (!conn->port || !conn->shm_port) {
		reply->retcode = DL_KERN_RESOURCE_SHORTAGE;
		return;
	}
	conn->id = session->next_connection_id++;
	conn->pid = req->client_pid;
	conn->notify_port = in.names[0];
	conn->task_token = in.names[1];
	size_t n = name_count < sizeof(conn->name) ? name_count : sizeof(conn->name) - 1;
	memcpy(conn->name, in.data + 8, n);
	conn->name[n] = '\0';
	session->nconnections++;

	if (server->log)
		fprintf(server->log, "daylight: new connection 0x%x for pid %d (%s)\n", conn->id, conn->pid, conn->name);

	dl_reply_add_port(reply, conn->port, DL_PORT_MAKE_SEND);
	dl_reply_add_port(reply, conn->shm_port, server->host.new_shared_memory ? DL_PORT_COPY_SEND : DL_PORT_MAKE_SEND);
	dl_reply_put(reply, &conn->id, sizeof(conn->id));
}

void dl_session_init(dl_session *session, dl_server *server)
{
	memset(session, 0, sizeof(*session));
	session->next_connection_id = DL_FIRST_CONNECTION_ID;
	session->next_datagram_seq = 0x1000;
	server->session = session;

	dl_register(server, DL_MSG_GetCoreGraphicsServerVersion, get_server_version, server);
	dl_register(server, DL_MSG_GetSessionPort, get_session_port, server);
	dl_register(server, DL_MSG_NewConnectionPort, new_connection_port, server);
	dl_register(server, DL_MSG_GetPortStreamOutofline, get_port_stream, server);
}
