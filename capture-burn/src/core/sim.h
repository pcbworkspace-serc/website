/* Capture Burn: deterministic simulation core.
 *
 * Pure C11, no SDL, no libm. One call to sim_step() advances the world by
 * exactly 1/120 s given one byte of input. Same seed + same input bytes =>
 * same bits on every platform (see detmath.h).
 */
#ifndef CB_SIM_H
#define CB_SIM_H
#include <stdint.h>
#include "rng.h"

/* Bump whenever a change alters simulation results; replays record it. */
#define SIM_VERSION 1u

#define SIM_HZ 120
#define SIM_DT (1.0 / 120.0)

/* ---- input bits (one byte per tick) ---- */
enum {
    IN_LEFT = 1, IN_RIGHT = 2, IN_THRUST = 4, IN_FIRE = 8, IN_RETRO = 16
};

/* ---- world geometry (world units, "wu"; 1 wu = 4 m, see docs/DESIGN.md) ---- */
#define FIELD_X0      1500.0
#define PLANET_X      26000.0
#define PLANET_Y      0.0
#define PLANET_R      380.0
#define PLANET_GM     1.444e7      /* v_esc at SOI edge ~ 95 wu/s            */
#define SOI_R         3200.0       /* patched-conic sphere of influence       */
#define ATMO_R        (PLANET_R * 1.15) /* periapsis must clear this          */
#define CAPTURE_RA    (SOI_R * 0.9)     /* apoapsis must stay inside this     */
#define FIELD_X1      (PLANET_X - SOI_R - 600.0)
#define FIELD_HALF_Y  1750.0
#define NAV_HALF_Y    1800.0       /* beyond this (outside SOI) = lost         */
#define WRAP_Y        2400.0       /* rocks past this re-enter from the far side */
#define NSTREAMS      3            /* meteoroid streams crossing the route      */
#define STREAM_GAP    56.0         /* mean spacing of stream meteoroids         */

/* ---- ship ---- */
#define SHIP_R        9.0
#define SHIP_MASS     20.0
#define SHIP_TURN     3.4          /* rad/s                                    */
#define SHIP_ACCEL    70.0         /* wu/s^2                                   */
#define ENERGY_MAX    100.0
#define THRUST_COST   7.0          /* energy/s at full thrust => 10 wu/s per E */
#define LIFE_COST     0.13         /* energy/s, always                          */
#define LASER_COST    0.8          /* energy per shot                           */
#define LASER_ENERGY  1.0          /* energy deposited in target per shot       */
#define LASER_RANGE   520.0
#define LASER_COOLDOWN 0.20
#define HULL_MAX      100.0
#define DAMAGE_PER_IMPULSE 0.022
#define ADRIFT_LIMIT  15.0         /* seconds at zero energy before life fails */
#define PICKUP_R      (SHIP_R + 14.0)
#define MAGNET_R      90.0         /* collector pulls in metal (M-type) shards  */
#define MAGNET_ACC    260.0        /* wu/s^2 at contact, falling to 0 at MAGNET_R */

/* ---- rocks ---- */
#define ROCK_MIN_R    6.0
#define ROCK_MAX_R    80.0
#define SHARD_R       4.5          /* fragments smaller than this become shards */
#define MASS_K        0.004        /* m = rho * r^3 * MASS_K                    */
#define QD_RT         54.0         /* Q*_D transition radius (min at 0.462*RT)  */
#define QD_Q0         0.164        /* Q*_D scale: 1 shot breaks an r=8 C-type   */
#define QD_A          (-0.36)      /* strength-regime exponent (Benz&Asphaug 99) */
#define QD_B          1.36         /* gravity-regime exponent                    */
#define CRATER_EFF    0.15         /* sub-catastrophic shot ejects this fraction */
#define FRAG_KE       2857.0       /* KE given to fragments per unit deposited   */
#define SHARD_LIFE    22.0
#define ROCK_G        3.4          /* game G for big-rock gravity on the ship    */
#define ROCK_G_MIN_R  40.0

#define MAX_BODIES    4096
#define MAX_EVENTS    64
#define MAX_VERTS     14

typedef enum { RT_C = 0, RT_S = 1, RT_M = 2 } RockType;
typedef enum { BK_ROCK = 0, BK_SHARD = 1 } BodyKind;

typedef enum {
    ST_RUNNING = 0,
    ST_WON,          /* captured into orbit */
    ST_LOST_HULL,    /* hull destroyed */
    ST_LOST_CRASH,   /* hit the planet */
    ST_LOST_PASSED,  /* flew past the planet */
    ST_LOST_RANGE,   /* left navigation range */
    ST_LOST_ADRIFT   /* energy at zero too long: life support failed */
} Status;

typedef enum {
    EV_SHOT, EV_HIT, EV_CRATER, EV_BREAK, EV_PICKUP, EV_SHIP_HIT,
    EV_ROCK_BUMP, EV_SOI_ENTER, EV_SOI_EXIT, EV_END
} EventKind;

typedef struct {
    uint8_t kind;
    uint8_t rtype;
    double x, y;     /* where */
    double x2, y2;   /* beam end for EV_SHOT */
    double mag;      /* size / impulse / energy, event-specific */
} Event;

typedef struct {
    uint32_t id;
    uint32_t group;      /* fragments of the same break ignore each other briefly */
    uint8_t kind, rtype, alive, nverts;
    double x, y, vx, vy;
    double px, py;       /* previous tick position (render interpolation only) */
    double r, m, rho;
    double hp, dmg;      /* catastrophic threshold & deposited energy */
    double life;         /* shards: seconds left; rocks: sibling-ignore timer */
    double value;        /* shards: energy on pickup */
    double ang, spin, pang; /* visual rotation */
    uint8_t shape[MAX_VERTS]; /* visual radial multipliers, /255 */
} Body;

typedef struct {
    double x, y, vx, vy, ang;
    double px, py, pang;
    double energy, hull, fire_cd;
    double zero_energy_t;
    uint8_t thrusting, in_soi;
} Ship;

typedef struct {
    uint32_t shots, rocks_broken, shards_collected, collisions;
    double e_thrust, e_laser, e_life, e_gained, damage;
    uint32_t soi_tick;   /* first tick inside SOI, 0 if never */
} Stats;

typedef struct {
    uint64_t seed;
    uint32_t tick;
    uint32_t next_id;
    Status status;
    Rng rng;
    Ship ship;
    Body body[MAX_BODIES];
    int nbody;
    Stats stats;
    /* Events since the last sim_clear_events(); not part of the hash. */
    Event events[MAX_EVENTS];
    int nevents;
    /* scratch for broad phase (rebuilt every step) */
    int sap[MAX_BODIES];
    int nsap;
} World;

typedef struct {
    double e, a, rp, ra, energy; /* specific orbital energy */
    int bound;
} Orbit;

void sim_init(World *w, uint64_t seed);
void sim_step(World *w, uint8_t input);
void sim_clear_events(World *w);
uint64_t sim_hash(const World *w);

/* Physics helpers, exposed for tests, bots and the HUD. */
double sim_qstar(double r);                     /* specific energy to disrupt */
double sim_rock_mass(double r, double rho);
double sim_radius_from_mass(double m, double rho);
double sim_shots_to_break(const Body *b);
double sim_rho(RockType t);
double sim_shard_value_per_mass(RockType t);
Orbit sim_orbit(double x, double y, double vx, double vy);
int sim_orbit_captured(const Orbit *o);
void sim_ship_dir(const World *w, double *dx, double *dy);

/* Fragmentation & collision, exposed so tests can check conservation. */
int sim_fragment(World *w, int idx, double edep, double dirx, double diry);
void sim_crater(World *w, int idx, double edep, double dirx, double diry);
void sim_collide_bodies(World *w, int i, int j);
int sim_add_rock(World *w, double x, double y, double vx, double vy, double r, RockType t);

const char *sim_status_text(Status s);

#endif
