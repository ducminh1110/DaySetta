/* Initial shared-memory contents recorded from a real window server (tools/trace/shmseed.py). */
#ifndef DL_SHM_SEED_H
#define DL_SHM_SEED_H

#include <stddef.h>
#include <stdint.h>

#define DL_SHM_SEED(name) extern const uint8_t dl_shm_##name[]; extern const size_t dl_shm_##name##_size;
DL_SHM_SEED(event_scoreboard)
DL_SHM_SEED(event_source)
DL_SHM_SEED(display_a)
DL_SHM_SEED(display_b)
DL_SHM_SEED(display_c)
DL_SHM_SEED(connection)
#undef DL_SHM_SEED

#endif
