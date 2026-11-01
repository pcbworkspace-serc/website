/* Replays: seed + one input byte per tick, run-length encoded.
 * A 5-minute run is typically a few KB. Because the simulation is
 * deterministic, a replay plus its final hash is a verifiable claim:
 * anyone can re-run it and check the hash. */
#ifndef CB_REPLAY_H
#define CB_REPLAY_H
#include <stdint.h>
#include "sim.h"

typedef struct {
    uint32_t version;
    uint64_t seed;
    uint8_t *in;
    uint32_t n, cap;
    uint8_t status;      /* outcome recorded at save time */
    uint64_t final_hash; /* sim_hash() after the last tick */
} Replay;

void replay_init(Replay *r, uint64_t seed);
void replay_free(Replay *r);
int replay_push(Replay *r, uint8_t input);
int replay_save(const Replay *r, const char *path);      /* binary .cbr */
int replay_save_text(const Replay *r, const char *path); /* hex text .cbrt */
int replay_load(Replay *r, const char *path); /* either form; 0 on success */

/* Re-simulate from scratch. Returns sim_hash() at the end. */
uint64_t replay_run(const Replay *r, World *w);

#endif
