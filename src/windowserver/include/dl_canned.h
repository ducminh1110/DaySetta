/*
 * Recorded WindowServer replies (generated from real-Mac traces by tools/trace/canned.py), used to answer
 * routines Daylight does not implement yet so that clients keep going.
 */
#ifndef DL_CANNED_H
#define DL_CANNED_H

#include <stddef.h>
#include <stdint.h>

enum { DL_CANNED_PORT = 1, DL_CANNED_OOL = 2 };

typedef struct dl_canned_desc {
	uint8_t type;
	uint32_t size;        /* OOL: original size (data may be shorter: zero-filled) */
	const uint8_t *data;  /* OOL: recorded bytes */
} dl_canned_desc;

typedef struct dl_canned_reply {
	int32_t request_id;
	uint8_t complex;
	uint8_t ndesc;
	const dl_canned_desc *desc;
	uint32_t body_size;   /* bytes after the descriptors (or after the header when simple) */
	const uint8_t *body;
} dl_canned_reply;

extern const dl_canned_reply dl_canned_replies[];
extern const size_t dl_canned_count;

static inline const dl_canned_reply *dl_canned_lookup(int32_t id)
{
	for (size_t i = 0; i < dl_canned_count; i++)
		if (dl_canned_replies[i].request_id == id)
			return &dl_canned_replies[i];
	return NULL;
}

#endif
