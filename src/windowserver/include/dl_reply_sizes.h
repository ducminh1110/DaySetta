/* Expected reply sizes of WindowServer routines, from the client code (tools/trace/reply_sizes.py). */
#ifndef DL_REPLY_SIZES_H
#define DL_REPLY_SIZES_H

#include <stddef.h>
#include <stdint.h>

typedef struct dl_reply_size {
	int32_t id;
	uint32_t size;   /* receive buffer the client uses: header + NDR + RetCode + out-parameters */
} dl_reply_size;

extern const dl_reply_size dl_reply_sizes[];
extern const size_t dl_reply_size_count;

static inline uint32_t dl_reply_size_lookup(int32_t id)
{
	for (size_t i = 0; i < dl_reply_size_count; i++)
		if (dl_reply_sizes[i].id == id)
			return dl_reply_sizes[i].size;
	return 0;
}

#endif
