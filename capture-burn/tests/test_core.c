/* Core test suite: physics invariants, determinism, replay round-trip.
 * No framework: each CHECK prints on failure, main returns nonzero. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "detmath.h"
#include "rng.h"
#include "sim.h"
#include "replay.h"
#include "bot.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

static World *new_world(void)
{
    World *w = malloc(sizeof *w);
    if (!w) { perror("malloc"); exit(2); }
    return w;
}

/* ---------- deterministic math vs libm ---------- */
static void test_detmath(void)
{
    double worst_sin = 0, worst_exp = 0, worst_log = 0, worst_atan = 0, worst_pow = 0;
    Rng r;
    rng_seed(&r, 1, 1);
    for (int i = 0; i < 200000; i++) {
        double x = rng_range(&r, -50, 50);
        double e1 = fabs(dm_sin(x) - sin(x)), e2 = fabs(dm_cos(x) - cos(x));
        if (e1 > worst_sin) worst_sin = e1;
        if (e2 > worst_sin) worst_sin = e2;
        double y = rng_range(&r, -30, 30);
        double ee = fabs(dm_exp(y) - exp(y)) / exp(y);
        if (ee > worst_exp) worst_exp = ee;
        double z = dm_exp(rng_range(&r, -40, 40));
        double el = fabs(dm_log(z) - log(z)) / (fabs(log(z)) + 1e-300);
        if (fabs(log(z)) > 1e-6 && el > worst_log) worst_log = el;
        double a = rng_range(&r, -100, 100), b = rng_range(&r, -100, 100);
        double ea = fabs(dm_atan2(a, b) - atan2(a, b));
        if (ea > worst_atan) worst_atan = ea;
        double pb = rng_range(&r, 0.01, 20), pe = rng_range(&r, -3, 3);
        double ep = fabs(dm_pow(pb, pe) - pow(pb, pe)) / pow(pb, pe);
        if (ep > worst_pow) worst_pow = ep;
    }
    CHECK(worst_sin < 1e-14, "sin/cos max abs err %.3g", worst_sin);
    CHECK(worst_exp < 1e-14, "exp max rel err %.3g", worst_exp);
    CHECK(worst_log < 1e-14, "log max rel err %.3g", worst_log);
    CHECK(worst_atan < 1e-14, "atan2 max abs err %.3g", worst_atan);
    CHECK(worst_pow < 1e-13, "pow max rel err %.3g", worst_pow);
    CHECK(fabs(dm_cbrt(27.0) - 3.0) < 1e-15, "cbrt(27) = %.17g", dm_cbrt(27.0));
    CHECK(dm_floor(-1.5) == -2.0 && dm_floor(2.0) == 2.0 && dm_floor(-0.0) == 0.0, "floor");
    printf("  detmath: sin/cos %.1e  exp %.1e  log %.1e  atan2 %.1e  pow %.1e\n",
           worst_sin, worst_exp, worst_log, worst_atan, worst_pow);
}

/* ---------- PCG32 known answers (pcg32-demo, seed 42 stream 54) ---------- */
static void test_rng(void)
{
    static const uint32_t expect[6] = {0xa15c02b7, 0x7b47f409, 0xba1d3330,
                                       0x83d2f293, 0xbfa4784b, 0xcbed606e};
    Rng r;
    rng_seed(&r, 42u, 54u);
    for (int i = 0; i < 6; i++) {
        uint32_t v = rng_u32(&r);
        CHECK(v == expect[i], "pcg32 output %d: %08x != %08x", i, v, expect[i]);
    }
}

/* ---------- Q*_D shape ---------- */
static void test_qstar(void)
{
    double qmin = 1e9, rmin = 0;
    for (double r = 2; r < 120; r += 0.5) {
        double q = sim_qstar(r);
        if (q < qmin) { qmin = q; rmin = r; }
    }
    CHECK(fabs(rmin - 0.462 * QD_RT) < 1.0, "Q*_D minimum at r=%.1f, expected %.1f", rmin, 0.462 * QD_RT);
    Body b;
    memset(&b, 0, sizeof b);
    b.r = 8; b.m = sim_rock_mass(8, sim_rho(RT_C)); b.hp = sim_qstar(8) * b.m;
    CHECK(sim_shots_to_break(&b) == 1, "r=8 C-type should take 1 shot, takes %.0f", sim_shots_to_break(&b));
    b.r = 12; b.m = sim_rock_mass(12, sim_rho(RT_C)); b.hp = sim_qstar(12) * b.m;
    CHECK(sim_shots_to_break(&b) == 3, "r=12 C-type should take 3 shots, takes %.0f", sim_shots_to_break(&b));
}

/* ---------- conservation: fragmentation, cratering, collisions ---------- */
static void totals(const World *w, double *m, double *px, double *py)
{
    *m = *px = *py = 0;
    for (int i = 0; i < w->nbody; i++) {
        const Body *b = &w->body[i];
        if (!b->alive) continue;
        *m += b->m;
        *px += b->m * b->vx;
        *py += b->m * b->vy;
    }
}

static void test_conservation(void)
{
    World *w = new_world();
    Rng r;
    rng_seed(&r, 7, 3);
    double worst_m = 0, worst_p = 0, worst_cm = 0, worst_cp = 0, worst_kp = 0;
    for (int trial = 0; trial < 3000; trial++) {
        memset(w, 0, sizeof *w);
        w->seed = (uint64_t)trial;
        w->next_id = 1;
        rng_seed(&w->rng, (uint64_t)trial, 9);
        RockType t = (RockType)rng_below(&r, 3);
        double rad = rng_range(&r, 5.0, 70.0);
        sim_add_rock(w, 0, 0, rng_range(&r, -80, 80), rng_range(&r, -80, 80), rad, t);
        double m0, px0, py0, m1, px1, py1;
        totals(w, &m0, &px0, &py0);
        double scale = m0 * (80.0 + 200.0);
        if (trial % 2 == 0) {
            double a = rng_range(&r, -3.14, 3.14);
            int ok = sim_fragment(w, 0, w->body[0].hp * rng_range(&r, 1.0, 3.0), cos(a), sin(a));
            CHECK(ok == 1, "fragment refused (trial %d)", trial);
            totals(w, &m1, &px1, &py1);
            double em = fabs(m1 - m0) / m0, ep = hypot(px1 - px0, py1 - py0) / scale;
            if (em > worst_m) worst_m = em;
            if (ep > worst_p) worst_p = ep;
        } else {
            double a = rng_range(&r, -3.14, 3.14);
            sim_crater(w, 0, LASER_ENERGY, cos(a), sin(a));
            totals(w, &m1, &px1, &py1);
            double em = fabs(m1 - m0) / m0, ep = hypot(px1 - px0, py1 - py0) / scale;
            if (em > worst_cm) worst_cm = em;
            if (ep > worst_cp) worst_cp = ep;
        }
    }
    /* rock-rock collisions */
    for (int trial = 0; trial < 3000; trial++) {
        memset(w, 0, sizeof *w);
        w->next_id = 1;
        double r1 = rng_range(&r, 5, 60), r2 = rng_range(&r, 5, 60);
        double a = rng_range(&r, -3.14, 3.14), d = (r1 + r2) * rng_range(&r, 0.6, 0.99);
        sim_add_rock(w, 0, 0, rng_range(&r, -50, 50), rng_range(&r, -50, 50), r1, RT_C);
        sim_add_rock(w, d * cos(a), d * sin(a), rng_range(&r, -50, 50), rng_range(&r, -50, 50), r2, RT_S);
        double m0, px0, py0, m1, px1, py1;
        totals(w, &m0, &px0, &py0);
        sim_collide_bodies(w, 0, 1);
        totals(w, &m1, &px1, &py1);
        double ep = hypot(px1 - px0, py1 - py0) / (m0 * 100.0);
        if (ep > worst_kp) worst_kp = ep;
    }
    CHECK(worst_m < 1e-12, "fragment mass drift %.3g", worst_m);
    CHECK(worst_p < 1e-9, "fragment momentum drift %.3g", worst_p);
    CHECK(worst_cm < 1e-12, "crater mass drift %.3g", worst_cm);
    CHECK(worst_cp < 1e-9, "crater momentum drift %.3g", worst_cp);
    CHECK(worst_kp < 1e-12, "collision momentum drift %.3g", worst_kp);
    printf("  conservation (relative): fragment m %.1e p %.1e | crater m %.1e p %.1e | collision p %.1e\n",
           worst_m, worst_p, worst_cm, worst_cp, worst_kp);
    free(w);
}

/* ---------- orbit: energy drift and Kepler's third law ---------- */
static void test_orbit(void)
{
    World *w = new_world();
    memset(w, 0, sizeof *w);
    w->next_id = 1;
    w->ship.energy = ENERGY_MAX;
    w->ship.hull = HULL_MAX;
    double r0 = 1500.0;
    double vc = sqrt(PLANET_GM / r0);
    w->ship.x = PLANET_X + r0;
    w->ship.y = PLANET_Y;
    w->ship.vx = 0;
    w->ship.vy = vc;
    Orbit o0 = sim_orbit(w->ship.x, w->ship.y, w->ship.vx, w->ship.vy);
    CHECK(o0.bound && o0.e < 1e-6 && fabs(o0.rp - r0) < 1e-3, "circular orbit elements e=%.3g rp=%.3f", o0.e, o0.rp);
    double e0 = o0.energy, worst = 0;
    double period_expect = 2.0 * 3.14159265358979323846 * sqrt(r0 * r0 * r0 / PLANET_GM);
    double t_prev = -1, period = 0;
    double prev_y = w->ship.y - PLANET_Y;
    int crossings = 0;
    int steps = (int)(2.2 * period_expect * SIM_HZ);
    for (int i = 0; i < steps; i++) {
        sim_step(w, 0);
        w->status = ST_RUNNING;      /* a circular orbit is "captured"; keep going */
        w->ship.energy = ENERGY_MAX; /* life support is not under test here */
        Orbit o = sim_orbit(w->ship.x, w->ship.y, w->ship.vx, w->ship.vy);
        double drift = fabs((o.energy - e0) / e0);
        if (i < 10000 && drift > worst) worst = drift;
        double y = w->ship.y - PLANET_Y;
        if (prev_y < 0 && y >= 0 && w->ship.x > PLANET_X) {
            double frac = -prev_y / (y - prev_y);
            double t = (i + frac) * SIM_DT;
            if (t_prev >= 0) { period = t - t_prev; crossings++; }
            t_prev = t;
        }
        prev_y = y;
    }
    CHECK(worst < 1e-3, "orbital energy drift over 10000 steps %.3g", worst);
    CHECK(crossings >= 1, "no full orbit observed");
    double perr = fabs(period - period_expect) / period_expect;
    CHECK(perr < 5e-3, "period %.3f s vs Kepler %.3f s (err %.3g)", period, period_expect, perr);
    printf("  orbit: energy drift %.1e over 10000 steps | period %.3f s vs Kepler %.3f s (err %.1e)\n",
           worst, period, period_expect, perr);
    free(w);
}

/* ---------- capture rule ---------- */
static void test_capture(void)
{
    /* bound, periapsis above atmosphere, apoapsis inside SOI */
    Orbit o = sim_orbit(PLANET_X + 1200, 0, 0, sqrt(PLANET_GM / 1200));
    CHECK(sim_orbit_captured(&o), "circular orbit at 1200 should count as captured");
    /* hyperbolic flyby */
    o = sim_orbit(PLANET_X + 1200, 0, 0, 1.6 * sqrt(PLANET_GM / 1200));
    CHECK(!sim_orbit_captured(&o) && !o.bound, "fast flyby must not count");
    /* grazing the atmosphere */
    o = sim_orbit(PLANET_X + 2000, 0, 0, 0.3 * sqrt(PLANET_GM / 2000));
    CHECK(o.bound && o.rp < ATMO_R && !sim_orbit_captured(&o), "orbit dipping into atmosphere must not count (rp=%.0f)", o.rp);
}

/* ---------- level generation ---------- */
static void test_level(void)
{
    World *w = new_world();
    for (uint64_t seed = 1; seed <= 20; seed++) {
        sim_init(w, seed);
        int overlaps = 0, near_start = 0, streams = 0;
        for (int i = 0; i < w->nbody; i++) {
            Body *a = &w->body[i];
            if (hypot(a->x, a->y) < 1200) near_start++;
            if (hypot(a->vx, a->vy) > 60) streams++;
            for (int j = i + 1; j < w->nbody; j++) {
                Body *b = &w->body[j];
                double d = hypot(a->x - b->x, a->y - b->y);
                if (d < a->r + b->r) overlaps++;
            }
        }
        CHECK(w->nbody > 1000, "seed %llu: only %d bodies", (unsigned long long)seed, w->nbody);
        CHECK(overlaps == 0, "seed %llu: %d overlapping rocks at start", (unsigned long long)seed, overlaps);
        CHECK(near_start == 0, "seed %llu: %d rocks inside the start zone", (unsigned long long)seed, near_start);
        CHECK(streams > 400, "seed %llu: only %d stream meteoroids", (unsigned long long)seed, streams);
    }
    free(w);
}

/* ---------- determinism & replay round trip ---------- */
static void test_determinism(void)
{
    World *a = new_world(), *b = new_world();
    Bot bot;
    Replay rp;
    replay_init(&rp, 1234);
    bot_init(&bot, bot_pilot());
    sim_init(a, 1234);
    for (int i = 0; i < SIM_HZ * 60 && a->status == ST_RUNNING; i++) {
        uint8_t in = bot_input(&bot, a);
        replay_push(&rp, in);
        sim_step(a, in);
        sim_clear_events(a);
    }
    rp.final_hash = sim_hash(a);
    rp.status = (uint8_t)a->status;
    uint64_t h2 = replay_run(&rp, b);
    CHECK(h2 == rp.final_hash, "same inputs gave different hash: %016llx vs %016llx",
          (unsigned long long)h2, (unsigned long long)rp.final_hash);
    char path[] = "/tmp/cb-test-XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) {
        FILE *f = fdopen(fd, "wb");
        if (f) fclose(f);
        CHECK(replay_save(&rp, path) == 0, "replay save failed");
        Replay back;
        CHECK(replay_load(&back, path) == 0, "replay load failed");
        CHECK(back.n == rp.n && back.seed == rp.seed && back.final_hash == rp.final_hash &&
              memcmp(back.in, rp.in, rp.n) == 0, "replay file round trip mismatch");
        uint64_t h3 = replay_run(&back, b);
        CHECK(h3 == rp.final_hash, "loaded replay diverged");
        replay_free(&back);
        remove(path);
    }
    /* A one-bit input change must change the outcome hash. */
    if (rp.n > 100) rp.in[100] ^= IN_FIRE;
    uint64_t h4 = replay_run(&rp, b);
    CHECK(h4 != rp.final_hash, "flipping one input bit did not change the hash");
    replay_free(&rp);
    printf("  determinism: 60 s bot run reproduces hash %016llx\n", (unsigned long long)h2);
    free(a);
    free(b);
}

int main(void)
{
    printf("capture-burn core tests\n");
    test_detmath();
    test_rng();
    test_qstar();
    test_conservation();
    test_orbit();
    test_capture();
    test_level();
    test_determinism();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
