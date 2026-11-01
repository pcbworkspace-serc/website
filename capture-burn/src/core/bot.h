/* Autopilot. Produces the same one-byte input a human does, so a bot run
 * is an ordinary replay. Used for: proving seeds are winnable, ablation
 * (is shooting / dodging / harvesting each necessary?), the attract-mode
 * demo on the deck, and the --bench frame-time measurement. */
#ifndef CB_BOT_H
#define CB_BOT_H
#include <stdint.h>
#include "sim.h"

typedef struct {
    int allow_shoot;       /* may fire the laser at all */
    int allow_dodge;       /* may change course to avoid rocks */
    int allow_harvest;     /* may detour for shards / mine M-type rocks */
    int shoot_everything;  /* fire at any rock in the beam, regardless of cost */
    double cruise;         /* wu/s */
    int side;              /* pass the planet on +1 / -1 side; 0 = auto */
} BotConfig;

typedef struct {
    BotConfig cfg;
    uint8_t held;
    int think_cd;
    int side;              /* which side of the planet to pass: +1 / -1 */
} Bot;

BotConfig bot_pilot(void);       /* full pilot */
BotConfig bot_no_shoot(void);    /* ablation: never fires */
BotConfig bot_no_dodge(void);    /* ablation: shoots everything, never swerves */
BotConfig bot_no_harvest(void);  /* ablation: ignores shards and mining */

void bot_init(Bot *b, BotConfig cfg);
uint8_t bot_input(Bot *b, const World *w);

#endif
