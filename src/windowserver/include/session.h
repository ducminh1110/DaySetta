#ifndef DAYLIGHT_SESSION_H
#define DAYLIGHT_SESSION_H

#include <stdint.h>

#include "daylight.h"

#define DL_MAX_CONNECTIONS 256

typedef struct dl_connection {
	uint32_t id;
	int32_t pid;
	uint32_t port;         /* server receive right; the client's connection port */
	uint32_t shm_port;     /* memory entry for the per-connection shared region (0x40 bytes on macOS 27) */
	void *shm;             /* the server's mapping of it */
	uint32_t notify_port;  /* client's port for server-to-client notifications */
	uint32_t task_token;
	char name[256];
	/* Pending notification datagrams, fetched by the client with GetPortStreamOutofline. */
	uint8_t datagrams[8192];
	size_t datagrams_size;
} dl_connection;

typedef struct dl_session {
	dl_connection connections[DL_MAX_CONNECTIONS];
	size_t nconnections;
	uint32_t next_connection_id;
	uint32_t next_datagram_seq;
} dl_session;

/* Registers the handshake routines and attaches the session to the server. */
void dl_session_init(dl_session *session, dl_server *server);

dl_connection *dl_session_find_connection(dl_session *session, uint32_t id);
dl_connection *dl_session_connection_for_port(dl_session *session, uint32_t port);
dl_connection *dl_session_connection_for_pid(dl_session *session, int32_t pid);

/*
 * Notification types (CGSNotificationType) as AppKit registers for them (_NSDoOneTimeWindowNotificationRegistration).
 * Window notifications carry { 0, window id }.
 */
#define DL_NOTE_WINDOW_BECAME_VISIBLE 0x390
#define DL_NOTE_WINDOW_BECAME_OCCLUDED 0x391
#define DL_NOTE_WINDOW_SPACE_MEMBERSHIP_CHANGED 0x33b

/* Queues a notification for the connection and wakes its notification port. */
void dl_session_post(dl_server *server, dl_connection *conn, uint32_t type, const void *data, size_t size);

/* Queues an encoded event (see input.h) for the connection and wakes it up. */
void dl_session_post_event(dl_server *server, dl_connection *conn, const void *event, size_t size);

#endif
