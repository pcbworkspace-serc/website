#include "bot.h"
#include "detmath.h"
#include <string.h>

BotConfig bot_pilot(void)
{
    BotConfig c = {1, 1, 1, 0, 135.0, 0};
    return c;
}
BotConfig bot_no_shoot(void)
{
    BotConfig c = bot_pilot();
    c.allow_shoot = 0;
    return c;
}
BotConfig bot_no_dodge(void)
{
    BotConfig c = bot_pilot();
    c.allow_dodge = 0;
    c.shoot_everything = 1;
    return c;
}
BotConfig bot_no_harvest(void)
{
    BotConfig c = bot_pilot();
    c.allow_harvest = 0;
    return c;
}

void bot_init(Bot *b, BotConfig cfg)
{
    memset(b, 0, sizeof *b);
    b->cfg = cfg;
    b->side = cfg.side;
}

#define THINK_EVERY 3
#define HORIZON 3.5
#define NEAR_R 900.0
#define MAX_NEAR 256

typedef struct { int idx; double tca, miss, clear; } Threat;

/* Earliest predicted collision if the ship moved at (vx,vy) from now on.
 * Rocks are extrapolated linearly; ignore_idx is a rock we intend to shoot. */
static int earliest_threat(const World *w, const int *near, int nn, double vx, double vy,
                           int ignore_idx, double margin, Threat *out)
{
    const Ship *s = &w->ship;
    int found = 0;
    double best = HORIZON;
    for (int k = 0; k < nn; k++) {
        int i = near[k];
        if (i == ignore_idx) continue;
        const Body *b = &w->body[i];
        double px = b->x - s->x, py = b->y - s->y;
        double rvx = b->vx - vx, rvy = b->vy - vy;
        double clear = b->r * 1.25 + SHIP_R + margin; /* big rocks hurt more */
        double v2 = rvx * rvx + rvy * rvy;
        double tca = v2 > 1e-9 ? -(px * rvx + py * rvy) / v2 : 0;
        if (tca < 0) tca = 0;
        if (tca > best) continue;
        double mx = px + rvx * tca, my = py + rvy * tca;
        double miss2 = mx * mx + my * my;
        if (miss2 >= clear * clear) continue;
        /* time when the separation first drops below clearance */
        double miss = dm_sqrt(miss2);
        double half = dm_sqrt(clear * clear - miss2) / (v2 > 1e-9 ? dm_sqrt(v2) : 1e-9);
        double t_in = tca - half;
        if (t_in < 0) t_in = 0;
        if (t_in < best) {
            best = t_in;
            out->idx = i;
            out->tca = t_in;
            out->miss = miss;
            out->clear = clear;
            found = 1;
        }
    }
    return found;
}

static uint8_t turn_toward(double ang, double target, double *err)
{
    double d = dm_wrap_angle(target - ang);
    if (err) *err = d;
    double tol = SHIP_TURN * SIM_DT * THINK_EVERY * 0.6;
    if (d > tol) return IN_LEFT;
    if (d < -tol) return IN_RIGHT;
    return 0;
}

/* Is rock i inside the laser beam right now (within range)? */
static int in_beam(const World *w, int i)
{
    const Ship *s = &w->ship;
    const Body *b = &w->body[i];
    double dx, dy;
    sim_ship_dir(w, &dx, &dy);
    double ox = s->x + dx * SHIP_R, oy = s->y + dy * SHIP_R;
    double cx = b->x - ox, cy = b->y - oy;
    double t = cx * dx + cy * dy;
    if (t < 0 || t > LASER_RANGE) return 0;
    double px = cx - dx * t, py = cy - dy * t;
    return px * px + py * py < (b->r * 0.85) * (b->r * 0.85);
}

static double len(double x, double y) { return dm_sqrt(x * x + y * y); }

/* Sampled velocity obstacles: try a fan of velocities around the route
 * velocity (plus current velocity and shard intercepts) and return the
 * cheapest safe one. Cost is in energy units. */
static double plan_velocity(const World *w, const BotConfig *c, const int *near, int nn,
                            const int *shards, int ns, double vdx, double vdy, int ignore,
                            double *obx, double *oby)
{
    const Ship *s = &w->ship;
    double V = len(s->vx, s->vy);
    double best_cost = 1e300, bx = vdx, by = vdy;
    double base = dm_atan2(vdy, vdx), vmag = len(vdx, vdy);
    static const double offs[] = {0, 0.15, -0.15, 0.3, -0.3, 0.5, -0.5, 0.8, -0.8, 1.2, -1.2};
    static const double facs[] = {1.0, 0.75, 1.2, 0.5};
    int ncand = c->allow_dodge ? 11 * 4 + 1 : 1;
    for (int k = 0; k < ncand + ns && k < ncand + 24; k++) {
        double cx, cy, bonus = 0;
        if (k < ncand) {
            if (k == ncand - 1 && c->allow_dodge) { cx = s->vx; cy = s->vy; }
            else {
                double a = base + offs[k % 11];
                double m = vmag * facs[k / 11 % 4];
                cx = m * dm_cos(a);
                cy = m * dm_sin(a);
            }
        } else {
            if (!c->allow_harvest || !c->allow_dodge) break;
            const Body *sh = &w->body[shards[k - ncand]];
            if (s->energy > ENERGY_MAX - sh->value || sh->value < 0.8) continue;
            double dx = sh->x - s->x, dy = sh->y - s->y;
            double d = len(dx, dy);
            if (d > 380.0) continue;
            double m = V > 70.0 ? V : 70.0;
            cx = sh->vx + dx / d * m;
            cy = sh->vy + dy / d * m;
            for (int q = 0; q < ns; q++) {
                const Body *o2 = &w->body[shards[q]];
                double ex = o2->x - sh->x, ey = o2->y - sh->y;
                if (ex * ex + ey * ey < 45.0 * 45.0) bonus += o2->value;
            }
            bonus -= (d / m) * LIFE_COST; /* time is energy too */
        }
        double dv = len(cx - s->vx, cy - s->vy);
        double dr = len(cx - vdx, cy - vdy);
        double cost = (dv + 1.5 * dr) / 10.0 - bonus;
        Threat t2;
        if (earliest_threat(w, near, nn, cx, cy, ignore, 12.0, &t2)) {
            /* time to rotate onto the burn direction, then to burn */
            double turn = dv > 1e-6 ? dm_fabs(dm_wrap_angle(dm_atan2(cy - s->vy, cx - s->vx) - s->ang)) / SHIP_TURN : 0;
            double reach = turn + dv / SHIP_ACCEL + 0.15;
            /* A course that collides is never acceptable; if it can still be
             * changed in time it is merely bad, otherwise hopeless. */
            cost += t2.tca < reach ? 1e6 : 30.0 + 30.0 / (t2.tca + 0.3);
        }
        if (cost < best_cost) { best_cost = cost; bx = cx; by = cy; }
    }
    *obx = bx;
    *oby = by;
    return best_cost;
}

uint8_t bot_input(Bot *bt, const World *w)
{
    if (bt->think_cd > 0) {
        bt->think_cd--;
        /* Never hold fire longer than one think: avoid wasted shots. */
        return bt->held;
    }
    bt->think_cd = THINK_EVERY - 1;
    const BotConfig *c = &bt->cfg;
    const Ship *s = &w->ship;
    double rx = s->x - PLANET_X, ry = s->y - PLANET_Y;
    double rpl = len(rx, ry);
    uint8_t out = 0;

    if (!bt->side) bt->side = s->y >= 0 ? 1 : -1;

    /* ---------------- inside the sphere of influence ---------------- */
    if (s->in_soi) {
        Orbit o = sim_orbit(s->x, s->y, s->vx, s->vy);
        double hx = rx * s->vy - ry * s->vx;
        if (o.rp < ATMO_R + 120.0) {
            /* raise periapsis: thrust along the direction of motion's tangent */
            double tx = -ry / rpl, ty = rx / rpl;
            if (hx < 0) { tx = -tx; ty = -ty; }
            double err;
            out |= turn_toward(s->ang, dm_atan2(ty, tx), &err);
            if (dm_fabs(err) < 0.3) out |= IN_THRUST;
        } else {
            double vr = (rx * s->vx + ry * s->vy) / rpl;
            int near_peri = rpl < o.rp * 1.25 || vr > 0;
            out |= IN_RETRO;
            if (near_peri) {
                double err = dm_wrap_angle(dm_atan2(-s->vy, -s->vx) - s->ang);
                if (dm_fabs(err) < 0.25) out |= IN_THRUST;
            }
        }
        bt->held = out;
        return out;
    }

    /* ---------------- gather nearby rocks & shards ------------------ */
    int near[MAX_NEAR], nn = 0;
    int shards[MAX_NEAR], ns = 0;
    for (int i = 0; i < w->nbody; i++) {
        const Body *b = &w->body[i];
        double dx = b->x - s->x, dy = b->y - s->y;
        double lim = NEAR_R + b->r;
        if (dx * dx + dy * dy > lim * lim) continue;
        if (b->kind == BK_ROCK) { if (nn < MAX_NEAR) near[nn++] = i; }
        else if (ns < MAX_NEAR) shards[ns++] = i;
    }

    /* ---------------- energy budget: will we make it? ----------------- */
    /* Remaining life support at cruise, plus the measured per-distance cost
     * of dodging/shooting (~1.8 E per 1000 wu), plus the capture burn and a
     * reserve. Harvest only when the projection says we will run short. */
    double remain = PLANET_X - SOI_R - s->x;
    if (remain < 0) remain = 0;
    double e_need = LIFE_COST * (remain / c->cruise + 25.0) + 0.0018 * remain + 8.0 + 6.0;
    int harvest = c->allow_harvest && s->energy < e_need;

    /* ---------------- route: desired velocity ----------------------- */
    double vdx, vdy;
    double approach = rpl < SOI_R + 2500.0;
    if (approach && (s->vx * -rx + s->vy * -ry) > 0) {
        /* Shape the hyperbola so periapsis lands in the capture window. */
        Orbit o = sim_orbit(s->x, s->y, s->vx, s->vy);
        double lx = -s->vy, ly = s->vx; /* perpendicular to velocity */
        double ll = len(lx, ly);
        if (ll < 1e-9) { lx = 0; ly = 1; ll = 1; }
        lx /= ll; ly /= ll;
        if (lx * rx + ly * ry < 0) { lx = -lx; ly = -ly; } /* points away from planet */
        double push = 0;
        if (o.rp < 700.0) push = 18.0;
        else if (o.rp > 1350.0) push = -18.0;
        vdx = s->vx + lx * push;
        vdy = s->vy + ly * push;
        double sp = len(vdx, vdy);
        double want = c->cruise;
        if (sp > 1e-9) { vdx *= want / sp; vdy *= want / sp; }
    } else {
        double cr = c->cruise;
        double rp_t = 950.0;
        double b_aim = rp_t * dm_sqrt(1.0 + 2.0 * PLANET_GM / (rp_t * cr * cr));
        if (b_aim > NAV_HALF_Y - 500.0) b_aim = NAV_HALF_Y - 500.0;
        double ax = PLANET_X - s->x, ay = bt->side * b_aim - s->y;
        double al = len(ax, ay);
        vdx = ax / al * cr;
        vdy = ay / al * cr;
    }

    /* ---------------- velocity plan & shoot-or-dodge decision ---------- */
    /* Every option is priced in energy: thrust at 1 E per 10 wu/s, a shot
     * at LASER_COST each. Shoot the most urgent threat only if that is
     * cheaper than the cheapest safe way around it. */
    double bx, by, sbx, sby;
    BotConfig pc = *c;
    pc.allow_harvest = harvest;
    c = &pc;
    double cost_dodge = plan_velocity(w, c, near, nn, shards, ns, vdx, vdy, -1, &bx, &by);
    int shoot_idx = -1;
    Threat th;
    int has_threat = earliest_threat(w, near, nn, s->vx, s->vy, -1, 14.0, &th);
    if (has_threat && c->allow_shoot) {
        const Body *b = &w->body[th.idx];
        double shots = sim_shots_to_break(b);
        double dist = len(b->x - s->x, b->y - s->y);
        double aim = dm_atan2(b->y - s->y, b->x - s->x);
        double turn_t = dm_fabs(dm_wrap_angle(aim - s->ang)) / SHIP_TURN;
        double shoot_t = turn_t + (shots - 1) * LASER_COOLDOWN + 0.1;
        int feasible = dist < LASER_RANGE + 60 && shoot_t < th.tca &&
                       s->energy > shots * LASER_COST + 8.0;
        if (feasible) {
            double cost_shoot = plan_velocity(w, c, near, nn, shards, ns, vdx, vdy, th.idx, &sbx, &sby) +
                                shots * LASER_COST;
            /* If no dodge is safe, shooting is the only way through. */
            if (c->shoot_everything || cost_shoot < cost_dodge || cost_dodge >= 1e6) {
                shoot_idx = th.idx;
                bx = sbx;
                by = sby;
            }
        }
    }

    /* ---------------- mining (M-type ore): strafing run ----------------- */
    /* Fire at a metal-rich rock while closing on it: crater ejecta fly back
     * toward the shooter and are swept up on the way past. No stop needed. */
    int mine_idx = -1;
    if (harvest && c->allow_shoot && shoot_idx < 0 &&
        s->energy > 6.0 && !(has_threat && th.idx >= 0 && w->body[th.idx].rtype != RT_M && th.tca < 1.2)) {
        double bestd = LASER_RANGE - 20.0;
        double V = len(s->vx, s->vy);
        for (int k = 0; k < nn; k++) {
            const Body *b = &w->body[near[k]];
            if (b->rtype != RT_M || b->r < 10.0) continue;
            double ex = b->x - s->x, ey = b->y - s->y;
            double d = len(ex, ey);
            if (d - b->r < 140.0 || d > bestd) continue;
            if (V > 1 && (ex * s->vx + ey * s->vy) / (d * V) < 0.97) continue; /* within ~14 deg ahead */
            bestd = d;
            mine_idx = near[k];
        }
    }

    /* ---------------- execute ---------------------------------------- */
    double dvx = bx - s->vx, dvy = by - s->vy;
    double need = len(dvx, dvy);
    int target = shoot_idx >= 0 ? shoot_idx : mine_idx;
    if (target >= 0 && (shoot_idx >= 0 || need < 12.0)) {
        const Body *b = &w->body[target];
        out |= turn_toward(s->ang, dm_atan2(b->y - s->y, b->x - s->x), 0);
        if (in_beam(w, target)) out |= IN_FIRE;
    } else if (need > 5.0) {
        double err;
        out |= turn_toward(s->ang, dm_atan2(dvy, dvx), &err);
        if (dm_fabs(err) < 0.3) out |= IN_THRUST;
    } else {
        out |= turn_toward(s->ang, dm_atan2(vdy, vdx), 0);
    }

    /* opportunistic fire at anything threatening that sits in the beam */
    if (c->allow_shoot && !(out & IN_FIRE) && s->energy > 14.0) {
        for (int k = 0; k < nn; k++) {
            int i = near[k];
            if (!in_beam(w, i)) continue;
            const Body *b = &w->body[i];
            if (c->shoot_everything) { out |= IN_FIRE; break; }
            Threat t3;
            int one = i;
            if (sim_shots_to_break(b) <= 1 &&
                earliest_threat(w, &one, 1, s->vx, s->vy, -1, 14.0, &t3) && t3.tca < 2.5) {
                out |= IN_FIRE;
                break;
            }
        }
    }
    bt->held = out;
    return out;
}
