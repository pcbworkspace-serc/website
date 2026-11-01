#include "sim.h"
#include "detmath.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Material model                                                       */
/* ------------------------------------------------------------------ */

/* Bulk densities (g/cm^3) from spacecraft/radar measurements:
 * C-type ~1.2-1.4 (Bennu 1.19, Mathilde 1.3), S-type ~2.7 (Eros 2.67),
 * M-type ~4 (Psyche estimates 3.8-4.1). */
double sim_rho(RockType t)
{
    switch (t) {
    case RT_S: return 2.7;
    case RT_M: return 4.0;
    default: return 1.3;
    }
}

/* Energy recovered per unit of shard mass collected. Metal-rich rock is
 * the valuable ore; carbonaceous rubble is barely worth chasing. */
double sim_shard_value_per_mass(RockType t)
{
    switch (t) {
    case RT_S: return 2.5;
    case RT_M: return 9.0;
    default: return 1.0;
    }
}

double sim_rock_mass(double r, double rho) { return rho * r * r * r * MASS_K; }
double sim_radius_from_mass(double m, double rho) { return dm_cbrt(m / (rho * MASS_K)); }

/* Catastrophic-disruption threshold Q*_D (energy per unit mass), using the
 * two-regime form of Benz & Asphaug (1999): strength-dominated for small
 * bodies (falls with size), gravity-dominated for large (rises with size).
 * Minimum at r = 0.462 * QD_RT ~ 25 wu (~100 m). */
double sim_qstar(double r)
{
    double x = r / QD_RT;
    return QD_Q0 * (dm_pow(x, QD_A) + dm_pow(x, QD_B));
}

double sim_shots_to_break(const Body *b)
{
    double need = (b->hp - b->dmg) / LASER_ENERGY;
    if (need <= 0) return 0;
    double n = dm_floor(need);
    return (n < need) ? n + 1 : n;
}

const char *sim_status_text(Status s)
{
    switch (s) {
    case ST_RUNNING: return "EN ROUTE";
    case ST_WON: return "CAPTURED IN ORBIT";
    case ST_LOST_HULL: return "HULL BREACHED";
    case ST_LOST_CRASH: return "IMPACTED THE PLANET";
    case ST_LOST_PASSED: return "FLEW PAST THE PLANET";
    case ST_LOST_RANGE: return "LOST: OUT OF NAV RANGE";
    case ST_LOST_ADRIFT: return "LIFE SUPPORT FAILED";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Orbit (two-body, planet frame)                                       */
/* ------------------------------------------------------------------ */

Orbit sim_orbit(double x, double y, double vx, double vy)
{
    Orbit o;
    double rx = x - PLANET_X, ry = y - PLANET_Y;
    double r = dm_sqrt(rx * rx + ry * ry);
    double v2 = vx * vx + vy * vy;
    double mu = PLANET_GM;
    o.energy = 0.5 * v2 - mu / r;
    double h = rx * vy - ry * vx;
    double e2 = 1.0 + 2.0 * o.energy * h * h / (mu * mu);
    o.e = dm_sqrt(e2 > 0 ? e2 : 0);
    o.rp = h * h / (mu * (1.0 + o.e));
    if (o.energy < 0) {
        o.bound = 1;
        o.a = -mu / (2.0 * o.energy);
        o.ra = o.a * (1.0 + o.e);
    } else {
        o.bound = 0;
        o.a = mu / (2.0 * o.energy);
        o.ra = 1e30;
    }
    return o;
}

int sim_orbit_captured(const Orbit *o)
{
    return o->bound && o->rp > ATMO_R && o->ra < CAPTURE_RA;
}

void sim_ship_dir(const World *w, double *dx, double *dy)
{
    *dx = dm_cos(w->ship.ang);
    *dy = dm_sin(w->ship.ang);
}

/* ------------------------------------------------------------------ */
/* Bodies & events                                                      */
/* ------------------------------------------------------------------ */

static void push_event(World *w, EventKind k, double x, double y, double mag, int rtype)
{
    if (w->nevents >= MAX_EVENTS) return;
    Event *e = &w->events[w->nevents++];
    memset(e, 0, sizeof *e);
    e->kind = (uint8_t)k;
    e->x = x;
    e->y = y;
    e->mag = mag;
    e->rtype = (uint8_t)rtype;
}

void sim_clear_events(World *w) { w->nevents = 0; }

/* Visual-only shape, from its own RNG stream so cosmetic changes never
 * perturb the physics sequence. */
static void make_shape(Body *b, uint64_t seed)
{
    Rng vr;
    rng_seed(&vr, seed ^ ((uint64_t)b->id * 0x9E3779B97F4A7C15ULL), 77);
    int n = b->r > 20 ? 12 + (int)rng_below(&vr, 3) : 8 + (int)rng_below(&vr, 3);
    if (b->kind == BK_SHARD) n = 4 + (int)rng_below(&vr, 2);
    if (n > MAX_VERTS) n = MAX_VERTS;
    b->nverts = (uint8_t)n;
    for (int i = 0; i < n; i++)
        b->shape[i] = (uint8_t)(200 + rng_below(&vr, 56)); /* 0.78 .. 1.0 */
    b->ang = rng_range(&vr, 0, DM_TAU);
    b->pang = b->ang;
    double period = rng_range(&vr, 4.0, 30.0);
    b->spin = (rng_unit(&vr) < 0.5 ? -1 : 1) * DM_TAU / period;
}

static Body *new_body(World *w)
{
    if (w->nbody >= MAX_BODIES) return 0;
    Body *b = &w->body[w->nbody++];
    memset(b, 0, sizeof *b);
    b->id = w->next_id++;
    b->alive = 1;
    return b;
}

int sim_add_rock(World *w, double x, double y, double vx, double vy, double r, RockType t)
{
    Body *b = new_body(w);
    if (!b) return -1;
    b->kind = BK_ROCK;
    b->rtype = (uint8_t)t;
    b->x = b->px = x;
    b->y = b->py = y;
    b->vx = vx;
    b->vy = vy;
    b->r = r;
    b->rho = sim_rho(t);
    b->m = sim_rock_mass(r, b->rho);
    b->hp = sim_qstar(r) * b->m;
    make_shape(b, w->seed);
    return (int)(b - w->body);
}

static Body *add_shard(World *w, double x, double y, double vx, double vy, double m, RockType t)
{
    Body *b = new_body(w);
    if (!b) return 0;
    b->kind = BK_SHARD;
    b->rtype = (uint8_t)t;
    b->x = b->px = x;
    b->y = b->py = y;
    b->vx = vx;
    b->vy = vy;
    b->m = m;
    b->rho = sim_rho(t);
    b->r = 2.0 + 1.5 * dm_clamp(m, 0, 1.5);
    b->life = SHARD_LIFE;
    b->value = m * sim_shard_value_per_mass(t);
    make_shape(b, w->seed);
    return b;
}

/* ------------------------------------------------------------------ */
/* Fragmentation (catastrophic disruption)                             */
/* ------------------------------------------------------------------ */

#define MAX_PIECES 16

/* Break body idx. Mass and momentum are conserved exactly up to rounding:
 * piece masses sum to the parent's (last piece takes the remainder) and
 * the kick velocities have their mass-weighted mean removed. The kinetic
 * energy of the kicks is FRAG_KE * edep, so over-killing a rock throws its
 * pieces faster. Returns 1 if broken, 0 if there was no room. */
int sim_fragment(World *w, int idx, double edep, double dirx, double diry)
{
    Body P = w->body[idx];
    if (!P.alive || P.kind != BK_ROCK) return 0;
    double M = P.m;
    double pm[MAX_PIECES];
    uint8_t shard[MAX_PIECES];
    int n = 0;

    /* Largest remnant: half the mass at exactly Q*_D (the definition). */
    pm[n] = 0.5 * M; shard[n] = 0; n++;
    int nf = 2 + (int)rng_below(&w->rng, 3);
    double wts[4], wsum = 0;
    for (int i = 0; i < nf; i++) {
        double u = rng_range(&w->rng, 0.25, 1.0);
        wts[i] = u * u * u;
        wsum += wts[i];
    }
    double mf = 0.35 * M;
    for (int i = 0; i < nf; i++) { pm[n] = mf * wts[i] / wsum; shard[n] = 0; n++; }

    /* Pieces too small to be rocks become shard mass. */
    double shard_mass = 0;
    int rocks = 0;
    for (int i = 0; i < n; i++) {
        if (sim_radius_from_mass(pm[i], P.rho) < SHARD_R) { shard_mass += pm[i]; pm[i] = 0; }
        else rocks++;
    }
    /* Compact rock pieces. */
    int k = 0;
    for (int i = 0; i < n; i++) if (pm[i] > 0) pm[k++] = pm[i];
    n = k;
    for (int i = 0; i < n; i++) shard[i] = 0;
    double rock_sum = 0;
    for (int i = 0; i < n; i++) rock_sum += pm[i];
    shard_mass = M - rock_sum; /* remainder: dust + small pieces (~15%+) */

    int ns = (int)(shard_mass / 0.5);
    if (ns < 1) ns = 1;
    if (ns > 10) ns = 10;
    int room = MAX_BODIES - w->nbody;
    if (n + ns > room) ns = room - n;
    if (ns < 1) return 0; /* no room at all: rock survives this time */
    double acc = 0;
    for (int i = 0; i < ns; i++) {
        double piece = (i == ns - 1) ? (shard_mass - acc) : shard_mass / ns;
        acc += piece;
        pm[n] = piece; shard[n] = 1; n++;
    }
    (void)rocks;

    /* Kick velocities. */
    double ux[MAX_PIECES], uy[MAX_PIECES], th[MAX_PIECES];
    double base = dm_atan2(diry, dirx);
    double px = 0, py = 0;
    for (int i = 0; i < n; i++) {
        th[i] = shard[i] ? rng_range(&w->rng, -DM_PI, DM_PI)
                         : base + rng_range(&w->rng, -1.9, 1.9);
        double s = dm_pow(pm[i] / M, -1.0 / 6.0);
        ux[i] = s * dm_cos(th[i]);
        uy[i] = s * dm_sin(th[i]);
        px += pm[i] * ux[i];
        py += pm[i] * uy[i];
    }
    double cx = px / M, cy = py / M;
    double K = 0;
    for (int i = 0; i < n; i++) {
        ux[i] -= cx;
        uy[i] -= cy;
        K += 0.5 * pm[i] * (ux[i] * ux[i] + uy[i] * uy[i]);
    }
    /* A single piece has no partner to push against: no kick. (Also guards
     * against amplifying a rounding residue into a huge velocity.) */
    double lam = (n > 1 && K > 1e-9 * M) ? dm_sqrt(FRAG_KE * edep / K) : 0;

    w->body[idx].alive = 0;
    uint32_t group = w->next_id++;
    for (int i = 0; i < n; i++) {
        double vx = P.vx + lam * ux[i], vy = P.vy + lam * uy[i];
        double ox = dm_cos(th[i]), oy = dm_sin(th[i]);
        if (shard[i]) {
            double d = rng_range(&w->rng, 0, 0.6) * P.r;
            Body *b = add_shard(w, P.x + ox * d, P.y + oy * d, vx, vy, pm[i], (RockType)P.rtype);
            if (b) b->group = group;
        } else {
            double r = sim_radius_from_mass(pm[i], P.rho);
            double d = (P.r - r) * 0.5;
            int j = sim_add_rock(w, P.x + ox * d, P.y + oy * d, vx, vy, r, (RockType)P.rtype);
            if (j >= 0) {
                w->body[j].group = group;
                w->body[j].life = 0.35; /* siblings pass through each other briefly */
                w->body[j].m = pm[i];   /* exact mass, not re-derived from r */
                w->body[j].hp = sim_qstar(r) * pm[i];
            }
        }
    }
    w->stats.rocks_broken++;
    push_event(w, EV_BREAK, P.x, P.y, P.r, P.rtype);
    return 1;
}

/* Sub-catastrophic hit: cratering ejecta. Ejected mass per shot scales as
 * E / Q*_D(r), so rocks near the Q*_D minimum (~25 wu) shed the most per
 * shot. Ejecta leave the impact face (back toward the shooter); the parent
 * recoils so momentum is conserved. */
void sim_crater(World *w, int idx, double edep, double dirx, double diry)
{
    Body *P = &w->body[idx];
    double mej = CRATER_EFF * edep / sim_qstar(P->r);
    if (mej > 0.25 * P->m) mej = 0.25 * P->m;
    int k = (int)(mej / 0.35 + 0.5);
    if (k < 1) k = 1;
    if (k > 4) k = 4;
    if (MAX_BODIES - w->nbody < k) return;
    double back = dm_atan2(-diry, -dirx);
    double hx = P->x - dirx * P->r, hy = P->y - diry * P->r;
    double prx = 0, pry = 0, acc = 0;
    double pvx = P->vx, pvy = P->vy, prho = P->rho;
    uint8_t rt = P->rtype;
    double Mrem = P->m - mej;
    double shards_m[4], sux[4], suy[4];
    for (int i = 0; i < k; i++) {
        shards_m[i] = (i == k - 1) ? (mej - acc) : mej / k;
        acc += shards_m[i];
        double a = back + rng_range(&w->rng, -0.6, 0.6);
        double s = rng_range(&w->rng, 25.0, 60.0);
        sux[i] = s * dm_cos(a);
        suy[i] = s * dm_sin(a);
        prx += shards_m[i] * sux[i];
        pry += shards_m[i] * suy[i];
    }
    /* Parent recoil (relative to the shared initial velocity). */
    P->vx = pvx - prx / Mrem;
    P->vy = pvy - pry / Mrem;
    P->m = Mrem;
    P->r = sim_radius_from_mass(Mrem, prho);
    P->hp = sim_qstar(P->r) * Mrem;
    for (int i = 0; i < k; i++)
        add_shard(w, hx, hy, pvx + sux[i], pvy + suy[i], shards_m[i], (RockType)rt);
    push_event(w, EV_CRATER, hx, hy, mej, rt);
}

/* ------------------------------------------------------------------ */
/* Collisions                                                           */
/* ------------------------------------------------------------------ */

/* Energy deposited into a rock per unit of kinetic energy dissipated in a
 * collision: one laser unit corresponds to FRAG_KE/0.3 KE units. */
#define KE_PER_UNIT (FRAG_KE / 0.3)

void sim_collide_bodies(World *w, int i, int j)
{
    Body *a = &w->body[i], *b = &w->body[j];
    double dx = b->x - a->x, dy = b->y - a->y;
    double rr = a->r + b->r;
    double d2 = dx * dx + dy * dy;
    if (d2 >= rr * rr || d2 <= 0) return;
    if (a->group && a->group == b->group && (a->life > 0 || b->life > 0)) return;
    double d = dm_sqrt(d2);
    double nx = dx / d, ny = dy / d;
    double ima = 1.0 / a->m, imb = 1.0 / b->m;
    /* positional correction, split by inverse mass */
    double pen = rr - d;
    double ca = pen * ima / (ima + imb), cb = pen * imb / (ima + imb);
    a->x -= nx * ca; a->y -= ny * ca;
    b->x += nx * cb; b->y += ny * cb;
    double vn = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny;
    if (vn >= 0) return;
    const double e = 0.5;
    double J = -(1.0 + e) * vn / (ima + imb);
    a->vx -= J * ima * nx; a->vy -= J * ima * ny;
    b->vx += J * imb * nx; b->vy += J * imb * ny;
    double mu = 1.0 / (ima + imb);
    double lost = 0.5 * mu * vn * vn * (1.0 - e * e);
    double dep = lost / KE_PER_UNIT;
    a->dmg += 0.5 * dep;
    b->dmg += 0.5 * dep;
    if (dep > 0.05)
        push_event(w, EV_ROCK_BUMP, a->x + nx * a->r, a->y + ny * a->r, dep, a->rtype);
}

static void collide_ship(World *w, int i)
{
    Ship *s = &w->ship;
    Body *b = &w->body[i];
    double dx = b->x - s->x, dy = b->y - s->y;
    double rr = SHIP_R + b->r;
    double d2 = dx * dx + dy * dy;
    if (d2 >= rr * rr || d2 <= 0) return;
    double d = dm_sqrt(d2);
    double nx = dx / d, ny = dy / d;
    double ims = 1.0 / SHIP_MASS, imb = 1.0 / b->m;
    double pen = rr - d;
    double cs = pen * ims / (ims + imb), cb = pen * imb / (ims + imb);
    s->x -= nx * cs; s->y -= ny * cs;
    b->x += nx * cb; b->y += ny * cb;
    double vn = (b->vx - s->vx) * nx + (b->vy - s->vy) * ny;
    if (vn >= 0) return;
    const double e = 0.4;
    double J = -(1.0 + e) * vn / (ims + imb);
    s->vx -= J * ims * nx; s->vy -= J * ims * ny;
    b->vx += J * imb * nx; b->vy += J * imb * ny;
    double dmg = DAMAGE_PER_IMPULSE * J;
    s->hull -= dmg;
    w->stats.damage += dmg;
    w->stats.collisions++;
    double mu = 1.0 / (ims + imb);
    b->dmg += 0.5 * mu * vn * vn * (1.0 - e * e) / KE_PER_UNIT;
    push_event(w, EV_SHIP_HIT, s->x + nx * SHIP_R, s->y + ny * SHIP_R, dmg, b->rtype);
}

/* Broad phase: hashed uniform grid. Each rock is inserted into every cell
 * its bounding box touches; a pair is resolved only in the one cell holding
 * the min corner of the two boxes' overlap, so it is visited exactly once.
 * Counting sort keeps bodies in index order inside each bucket: fully
 * deterministic, no qsort, no allocation. */
#define GCELL 64.0
#define NBUCKET 4096
#define MAX_CELLS_PER_BODY 16
static int g_start[NBUCKET];
static int g_cnt[NBUCKET];
static int g_fill[NBUCKET];
static int g_items[MAX_BODIES * MAX_CELLS_PER_BODY];
static int g_ix0[MAX_BODIES], g_iy0[MAX_BODIES], g_ix1[MAX_BODIES], g_iy1[MAX_BODIES];

static unsigned bucket_of(int ix, int iy)
{
    uint32_t h = (uint32_t)ix * 73856093u ^ (uint32_t)iy * 19349663u;
    return h & (NBUCKET - 1);
}

/* Distinct buckets touched by body i (cells can share a bucket). */
static int body_buckets(int i, unsigned *out)
{
    int n = 0;
    for (int iy = g_iy0[i]; iy <= g_iy1[i]; iy++)
        for (int ix = g_ix0[i]; ix <= g_ix1[i]; ix++) {
            unsigned b = bucket_of(ix, iy);
            int dup = 0;
            for (int k = 0; k < n; k++) if (out[k] == b) { dup = 1; break; }
            if (!dup && n < MAX_CELLS_PER_BODY) out[n++] = b;
        }
    return n;
}

static int g_bkoff[MAX_BODIES + 1];
static unsigned g_bk[MAX_BODIES * MAX_CELLS_PER_BODY];
static unsigned g_used[NBUCKET];

static void broad_phase(World *w)
{
    int nused = 0, nb = 0;
    for (int i = 0; i < w->nbody; i++) {
        Body *b = &w->body[i];
        g_bkoff[i] = nb;
        if (!b->alive || b->kind != BK_ROCK) { g_ix0[i] = 1; g_ix1[i] = 0; continue; }
        g_ix0[i] = (int)dm_floor((b->x - b->r) / GCELL);
        g_ix1[i] = (int)dm_floor((b->x + b->r) / GCELL);
        g_iy0[i] = (int)dm_floor((b->y - b->r) / GCELL);
        g_iy1[i] = (int)dm_floor((b->y + b->r) / GCELL);
        int n = body_buckets(i, &g_bk[nb]);
        for (int k = 0; k < n; k++) {
            unsigned c = g_bk[nb + k];
            if (g_cnt[c]++ == 0) g_used[nused++] = c; /* first use */
        }
        nb += n;
    }
    g_bkoff[w->nbody] = nb;
    /* Prefix sums over used buckets only, in first-use order (deterministic). */
    int acc = 0;
    for (int u = 0; u < nused; u++) {
        unsigned c = g_used[u];
        g_start[c] = acc;
        g_fill[c] = acc;
        acc += g_cnt[c];
    }
    for (int i = 0; i < w->nbody; i++)
        for (int k = g_bkoff[i]; k < g_bkoff[i + 1]; k++) g_items[g_fill[g_bk[k]]++] = i;
    for (int u = 0; u < nused; u++) {
        unsigned c = g_used[u];
        int beg = g_start[c], fin = g_fill[c];
        for (int p = beg; p < fin; p++) {
            int a = g_items[p];
            for (int q = p + 1; q < fin; q++) {
                int b = g_items[q];
                int ix = g_ix0[a] > g_ix0[b] ? g_ix0[a] : g_ix0[b];
                int iy = g_iy0[a] > g_iy0[b] ? g_iy0[a] : g_iy0[b];
                if (ix > g_ix1[a] || ix > g_ix1[b] || iy > g_iy1[a] || iy > g_iy1[b]) continue;
                if (bucket_of(ix, iy) != c) continue;
                if (!w->body[a].alive || !w->body[b].alive) continue;
                sim_collide_bodies(w, a, b);
            }
        }
    }
    /* leave the count array zeroed for next step */
    for (int u = 0; u < nused; u++) g_cnt[g_used[u]] = 0;
}

/* ------------------------------------------------------------------ */
/* Laser                                                                */
/* ------------------------------------------------------------------ */

static void fire_laser(World *w)
{
    Ship *s = &w->ship;
    double dx, dy;
    sim_ship_dir(w, &dx, &dy);
    double ox = s->x + dx * SHIP_R, oy = s->y + dy * SHIP_R;
    int best = -1;
    double bt = LASER_RANGE;
    for (int i = 0; i < w->nbody; i++) {
        Body *b = &w->body[i];
        if (!b->alive || b->kind != BK_ROCK) continue;
        double cx = b->x - ox, cy = b->y - oy;
        double tca = cx * dx + cy * dy;
        double c2 = cx * cx + cy * cy;
        double r2 = b->r * b->r;
        if (c2 <= r2) { best = i; bt = 0; break; }
        if (tca < 0) continue;
        double d2 = c2 - tca * tca;
        if (d2 > r2) continue;
        double t = tca - dm_sqrt(r2 - d2);
        if (t < bt) { bt = t; best = i; }
    }
    s->energy -= LASER_COST;
    w->stats.e_laser += LASER_COST;
    w->stats.shots++;
    if (w->nevents < MAX_EVENTS) {
        push_event(w, EV_SHOT, ox, oy, 0, 0);
        w->events[w->nevents - 1].x2 = ox + dx * bt;
        w->events[w->nevents - 1].y2 = oy + dy * bt;
    }
    if (best < 0) return;
    Body *b = &w->body[best];
    b->dmg += LASER_ENERGY;
    push_event(w, EV_HIT, ox + dx * bt, oy + dy * bt, b->r, b->rtype);
    if (b->dmg >= b->hp) sim_fragment(w, best, b->dmg, dx, dy);
    else sim_crater(w, best, LASER_ENERGY, dx, dy);
}

/* ------------------------------------------------------------------ */
/* Level generation                                                     */
/* ------------------------------------------------------------------ */

/* Authored intensity curve over route progress u in [0,1]:
 * calm start, first wave, breather, main peak, release, quiet approach. */
static double intensity(double u)
{
    static const double U[] = {0.00, 0.10, 0.30, 0.42, 0.55, 0.75, 0.85, 0.93, 1.00};
    static const double I[] = {0.15, 0.35, 0.70, 0.30, 0.55, 1.00, 0.60, 0.25, 0.10};
    if (u <= 0) return I[0];
    for (int i = 1; i < 9; i++)
        if (u <= U[i]) return I[i - 1] + (I[i] - I[i - 1]) * (u - U[i - 1]) / (U[i] - U[i - 1]);
    return I[8];
}

/* Cumulative size distribution N(>r) ~ r^-b. b = 1.8 is at the shallow end
 * of observed small-body values (1.8 - 2.5) so large rocks stay visible. */
#define SIZE_B 1.8

static double sample_radius(Rng *r)
{
    double u = rng_unit(r);
    double k = 1.0 - dm_pow(ROCK_MIN_R / ROCK_MAX_R, SIZE_B);
    return ROCK_MIN_R * dm_pow(1.0 - u * k, -1.0 / SIZE_B);
}

static void generate(World *w)
{
    const int target = 760;
    int placed = 0;
    for (int attempt = 0; attempt < 60000 && placed < target; attempt++) {
        double x = rng_range(&w->rng, FIELD_X0, FIELD_X1);
        double y = rng_range(&w->rng, -FIELD_HALF_Y, FIELD_HALF_Y);
        double u = (x - FIELD_X0) / (FIELD_X1 - FIELD_X0);
        double p = rng_unit(&w->rng);
        double r = sample_radius(&w->rng);
        double tsel = rng_unit(&w->rng);
        double a = rng_range(&w->rng, -1, 1), c = rng_range(&w->rng, -1, 1);
        if (p >= intensity(u)) continue;
        int ok = 1;
        for (int i = 0; i < w->nbody && ok; i++) {
            Body *b = &w->body[i];
            double dx = b->x - x, dy = b->y - y, g = b->r + r + 25.0 + 0.5 * r;
            if (dx * dx + dy * dy < g * g) ok = 0;
        }
        if (!ok) continue;
        /* Rough real mix: ~75% C, ~17% S, ~8% other (here: M). */
        RockType t = tsel < 0.75 ? RT_C : (tsel < 0.92 ? RT_S : RT_M);
        double sp = dm_pow(12.0 / r, 0.3);
        sim_add_rock(w, x, y, 8.0 * c * sp, 18.0 * a * sp, r, t);
        placed++;
    }

    /* Meteoroid streams: comet-debris trails of small bodies sharing one
     * velocity, crossing the route. They wrap in y (sim_step), so they are
     * effectively endless: you cannot fly around one. Crossing one means
     * taking hits, waiting for a gap, or shooting one. */
    static const double SU[NSTREAMS] = {0.24, 0.52, 0.80};
    for (int k = 0; k < NSTREAMS; k++) {
        double cx = FIELD_X0 + (FIELD_X1 - FIELD_X0) * (SU[k] + rng_range(&w->rng, -0.04, 0.04));
        double width = rng_range(&w->rng, 220.0, 300.0);
        double dir = rng_unit(&w->rng) < 0.5 ? -1.0 : 1.0;
        double speed = rng_range(&w->rng, 80.0, 120.0);
        int want = (int)(width * 2.0 * WRAP_Y / (STREAM_GAP * STREAM_GAP));
        int got = 0;
        for (int attempt = 0; attempt < want * 30 && got < want; attempt++) {
            double x = cx + rng_range(&w->rng, -0.5, 0.5) * width;
            double y = rng_range(&w->rng, -WRAP_Y, WRAP_Y);
            double u = rng_unit(&w->rng);
            double r = 5.0 * dm_pow(1.0 - u * (1.0 - dm_pow(5.0 / 7.5, 2.5)), -1.0 / 2.5);
            RockType t = rng_unit(&w->rng) < 0.85 ? RT_C : RT_S;
            double jx = rng_range(&w->rng, -5, 5), jy = rng_range(&w->rng, -6, 6);
            int ok = 1;
            for (int i = 0; i < w->nbody && ok; i++) {
                Body *b = &w->body[i];
                double dx = b->x - x, dy = b->y - y, g = b->r + r + 18.0;
                if (dx * dx + dy * dy < g * g) ok = 0;
            }
            if (!ok) continue;
            sim_add_rock(w, x, y, jx, dir * speed + jy, r, t);
            got++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Init / step                                                          */
/* ------------------------------------------------------------------ */

void sim_init(World *w, uint64_t seed)
{
    memset(w, 0, sizeof *w);
    w->seed = seed;
    w->next_id = 1;
    rng_seed(&w->rng, seed, 0xC0FFEE);
    w->ship.energy = ENERGY_MAX;
    w->ship.hull = HULL_MAX;
    generate(w);
}

static void end(World *w, Status s)
{
    w->status = s;
    push_event(w, EV_END, w->ship.x, w->ship.y, (double)s, 0);
}

static void compact(World *w)
{
    int k = 0;
    for (int i = 0; i < w->nbody; i++)
        if (w->body[i].alive) {
            if (k != i) w->body[k] = w->body[i];
            k++;
        }
    w->nbody = k;
}

static void planet_gravity(double x, double y, double *ax, double *ay)
{
    double rx = x - PLANET_X, ry = y - PLANET_Y;
    double r2 = rx * rx + ry * ry;
    if (r2 >= SOI_R * SOI_R) return; /* patched conics: outside SOI, none */
    double r = dm_sqrt(r2);
    double a = PLANET_GM / r2;
    *ax -= a * rx / r;
    *ay -= a * ry / r;
}

void sim_step(World *w, uint8_t in)
{
    if (w->status != ST_RUNNING) return;
    Ship *s = &w->ship;
    const double dt = SIM_DT;

    s->px = s->x; s->py = s->y; s->pang = s->ang;
    for (int i = 0; i < w->nbody; i++) {
        Body *b = &w->body[i];
        b->px = b->x; b->py = b->y; b->pang = b->ang;
    }

    /* --- ship attitude --- */
    double spd2 = s->vx * s->vx + s->vy * s->vy;
    if ((in & IN_RETRO) && spd2 > 1.0) {
        double target = dm_atan2(-s->vy, -s->vx);
        double diff = dm_wrap_angle(target - s->ang);
        double step = SHIP_TURN * dt;
        s->ang += dm_clamp(diff, -step, step);
    } else {
        if (in & IN_LEFT) s->ang += SHIP_TURN * dt;
        if (in & IN_RIGHT) s->ang -= SHIP_TURN * dt;
    }
    s->ang = dm_wrap_angle(s->ang);

    /* --- forces on ship --- */
    double dx, dy;
    sim_ship_dir(w, &dx, &dy);
    double ax = 0, ay = 0;
    s->thrusting = 0;
    if ((in & IN_THRUST) && s->energy > 0) {
        ax += SHIP_ACCEL * dx;
        ay += SHIP_ACCEL * dy;
        s->energy -= THRUST_COST * dt;
        w->stats.e_thrust += THRUST_COST * dt;
        s->thrusting = 1;
    }
    planet_gravity(s->x, s->y, &ax, &ay);
    /* Big rocks pull the ship (and the ship pulls them back equally). */
    for (int i = 0; i < w->nbody; i++) {
        Body *b = &w->body[i];
        if (!b->alive || b->kind != BK_ROCK || b->r < ROCK_G_MIN_R) continue;
        double rx = b->x - s->x, ry = b->y - s->y;
        double lim = 8.0 * b->r;
        double r2 = rx * rx + ry * ry;
        if (r2 > lim * lim) continue;
        double soft = (b->r + SHIP_R) * (b->r + SHIP_R);
        if (r2 < soft) r2 = soft;
        double r = dm_sqrt(r2);
        double f = ROCK_G * b->m * SHIP_MASS / r2; /* force magnitude */
        ax += f / SHIP_MASS * rx / r;
        ay += f / SHIP_MASS * ry / r;
        b->vx -= f / b->m * rx / r * dt;
        b->vy -= f / b->m * ry / r * dt;
    }
    /* semi-implicit (symplectic) Euler */
    s->vx += ax * dt; s->vy += ay * dt;
    s->x += s->vx * dt; s->y += s->vy * dt;

    /* --- laser --- */
    if (s->fire_cd > 0) s->fire_cd -= dt;
    if ((in & IN_FIRE) && s->fire_cd <= 0 && s->energy >= LASER_COST) {
        fire_laser(w);
        s->fire_cd = LASER_COOLDOWN;
    }

    /* --- life support --- */
    s->energy -= LIFE_COST * dt;
    w->stats.e_life += LIFE_COST * dt;
    if (s->energy < 0) s->energy = 0;

    /* --- bodies --- */
    for (int i = 0; i < w->nbody; i++) {
        Body *b = &w->body[i];
        if (!b->alive) continue;
        double bx = 0, by = 0;
        planet_gravity(b->x, b->y, &bx, &by);
        if (b->kind == BK_SHARD && b->rtype == RT_M) {
            /* Iron-nickel is ferromagnetic: the collector pulls metal shards
             * in, and the shard pulls the ship back equally (Newton III). */
            double mx = s->x - b->x, my = s->y - b->y;
            double d2 = mx * mx + my * my;
            if (d2 < MAGNET_R * MAGNET_R && d2 > 1e-6) {
                double d = dm_sqrt(d2);
                double a = MAGNET_ACC * (1.0 - d / MAGNET_R);
                bx += a * mx / d;
                by += a * my / d;
                s->vx -= a * b->m / SHIP_MASS * mx / d * dt;
                s->vy -= a * b->m / SHIP_MASS * my / d * dt;
            }
        }
        b->vx += bx * dt; b->vy += by * dt;
        b->x += b->vx * dt; b->y += b->vy * dt;
        if (b->kind == BK_ROCK && (b->y > WRAP_Y || b->y < -WRAP_Y)) {
            b->y += b->y > 0 ? -2.0 * WRAP_Y : 2.0 * WRAP_Y;
            b->py = b->y; /* no interpolation streak across the wrap */
            b->px = b->x;
        }
        b->ang += b->spin * dt; /* visual only; wrap rarely */
        if (b->ang > DM_PI || b->ang < -DM_PI) b->ang = dm_wrap_angle(b->ang);
        if (b->life > 0) {
            b->life -= dt;
            if (b->kind == BK_SHARD && b->life <= 0) b->alive = 0;
        }
        double px = b->x - PLANET_X, py = b->y - PLANET_Y;
        if (px * px + py * py < PLANET_R * PLANET_R) b->alive = 0; /* burns up */
    }

    /* --- collisions --- */
    broad_phase(w);
    for (int i = 0; i < w->nbody; i++) {
        Body *b = &w->body[i];
        if (!b->alive) continue;
        if (b->kind == BK_ROCK) {
            collide_ship(w, i);
        } else {
            double ddx = b->x - s->x, ddy = b->y - s->y;
            if (ddx * ddx + ddy * ddy < PICKUP_R * PICKUP_R) {
                s->energy += b->value;
                if (s->energy > ENERGY_MAX) s->energy = ENERGY_MAX;
                w->stats.e_gained += b->value;
                w->stats.shards_collected++;
                push_event(w, EV_PICKUP, b->x, b->y, b->value, b->rtype);
                b->alive = 0;
            }
        }
    }
    /* Collisions can push deposited energy past Q*_D: break those rocks. */
    int n0 = w->nbody;
    for (int i = 0; i < n0; i++) {
        Body *b = &w->body[i];
        if (b->alive && b->kind == BK_ROCK && b->dmg >= b->hp) {
            double a = rng_range(&w->rng, -DM_PI, DM_PI);
            sim_fragment(w, i, b->dmg, dm_cos(a), dm_sin(a));
        }
    }
    compact(w);

    /* --- mission status --- */
    w->tick++;
    double rx = s->x - PLANET_X, ry = s->y - PLANET_Y;
    double d2 = rx * rx + ry * ry;
    int in_soi = d2 < SOI_R * SOI_R;
    if (in_soi && !s->in_soi) {
        push_event(w, EV_SOI_ENTER, s->x, s->y, 0, 0);
        if (!w->stats.soi_tick) w->stats.soi_tick = w->tick;
    }
    if (!in_soi && s->in_soi) push_event(w, EV_SOI_EXIT, s->x, s->y, 0, 0);
    s->in_soi = (uint8_t)in_soi;

    if (s->energy <= 0) s->zero_energy_t += dt;
    else s->zero_energy_t = 0;

    if (d2 < PLANET_R * PLANET_R) { end(w, ST_LOST_CRASH); return; }
    if (s->hull <= 0) { s->hull = 0; end(w, ST_LOST_HULL); return; }
    if (in_soi) {
        Orbit o = sim_orbit(s->x, s->y, s->vx, s->vy);
        if (sim_orbit_captured(&o)) { end(w, ST_WON); return; }
    } else {
        if (s->x > PLANET_X) { end(w, ST_LOST_PASSED); return; }
        if (s->y > NAV_HALF_Y || s->y < -NAV_HALF_Y || s->x < -1500) { end(w, ST_LOST_RANGE); return; }
    }
    if (s->zero_energy_t >= ADRIFT_LIMIT) { end(w, ST_LOST_ADRIFT); return; }
}

/* ------------------------------------------------------------------ */
/* State hash (FNV-1a over explicit fields; no struct padding)          */
/* ------------------------------------------------------------------ */

typedef struct { uint64_t h; } Fnv;
static void fnv_bytes(Fnv *f, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) { f->h ^= b[i]; f->h *= 0x100000001b3ULL; }
}
static void fnv_u64(Fnv *f, uint64_t v)
{
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i)); /* endian-free */
    fnv_bytes(f, b, 8);
}
static void fnv_d(Fnv *f, double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    fnv_u64(f, u);
}

uint64_t sim_hash(const World *w)
{
    Fnv f = {0xcbf29ce484222325ULL};
    const Ship *s = &w->ship;
    fnv_u64(&f, w->seed);
    fnv_u64(&f, w->tick);
    fnv_u64(&f, (uint64_t)w->status);
    fnv_u64(&f, w->rng.state);
    fnv_u64(&f, w->rng.inc);
    fnv_u64(&f, w->next_id);
    fnv_d(&f, s->x); fnv_d(&f, s->y); fnv_d(&f, s->vx); fnv_d(&f, s->vy);
    fnv_d(&f, s->ang); fnv_d(&f, s->energy); fnv_d(&f, s->hull);
    fnv_d(&f, s->fire_cd); fnv_d(&f, s->zero_energy_t);
    fnv_u64(&f, (uint64_t)w->nbody);
    for (int i = 0; i < w->nbody; i++) {
        const Body *b = &w->body[i];
        fnv_u64(&f, b->id);
        fnv_u64(&f, b->group);
        fnv_u64(&f, (uint64_t)b->kind | ((uint64_t)b->rtype << 8) | ((uint64_t)b->alive << 16));
        fnv_d(&f, b->x); fnv_d(&f, b->y); fnv_d(&f, b->vx); fnv_d(&f, b->vy);
        fnv_d(&f, b->r); fnv_d(&f, b->m); fnv_d(&f, b->hp); fnv_d(&f, b->dmg);
        fnv_d(&f, b->life); fnv_d(&f, b->value);
    }
    return f.h;
}
