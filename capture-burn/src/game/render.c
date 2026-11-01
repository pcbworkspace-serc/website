/* Vector renderer. Everything is lines, points and rect batches through
 * SDL_Renderer, which batches into a handful of GL draw calls on the Pi.
 * Layout is designed at 480 px tall (the 7" panel) and scales with ui. */
#include "game.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define C(r_, g_, b_, a_) SDL_SetRenderDrawColor(g->ren, (Uint8)(r_), (Uint8)(g_), (Uint8)(b_), (Uint8)(a_))

float frand(Game *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return (float)(g->rng & 0xffffff) / 16777216.0f;
}

static float px_per_wu(const Game *g) { return (float)g->zoom * g->ui; }

void world_to_screen(const Game *g, double wx, double wy, float *sx, float *sy)
{
    float s = px_per_wu(g);
    *sx = g->w * 0.5f + (float)(wx - g->camx) * s;
    *sy = g->h * 0.5f - (float)(wy - g->camy) * s;
}

static int on_screen(const Game *g, float x, float y, float pad)
{
    return x > -pad && y > -pad && x < g->w + pad && y < g->h + pad;
}

static void circle(Game *g, float cx, float cy, float r, int seg)
{
    SDL_FPoint pts[129];
    if (seg > 128) seg = 128;
    for (int i = 0; i <= seg; i++) {
        float a = 6.2831853f * i / seg;
        pts[i].x = cx + r * cosf(a);
        pts[i].y = cy + r * sinf(a);
    }
    SDL_RenderDrawLinesF(g->ren, pts, seg + 1);
}

static void dashed_circle(Game *g, float cx, float cy, float r, int seg)
{
    if (seg > 256) seg = 256;
    for (int i = 0; i < seg; i += 2) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        SDL_RenderDrawLineF(g->ren, cx + r * cosf(a0), cy + r * sinf(a0), cx + r * cosf(a1), cy + r * sinf(a1));
    }
}

static void bar(Game *g, float x, float y, float w, float h, float frac, Uint8 r, Uint8 gg, Uint8 b)
{
    SDL_FRect o = {x, y, w, h};
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    SDL_FRect f = {x, y, w * frac, h};
    C(r, gg, b, 60);
    SDL_RenderFillRectF(g->ren, &o);
    C(r, gg, b, 255);
    SDL_RenderFillRectF(g->ren, &f);
    SDL_RenderDrawRectF(g->ren, &o);
}

/* ------------------------------------------------------------------ */

static void ship_pos(const Game *g, double alpha, double *x, double *y, double *a)
{
    const Ship *s = &g->world->ship;
    *x = s->px + (s->x - s->px) * alpha;
    *y = s->py + (s->y - s->py) * alpha;
    double d = s->ang - s->pang;
    while (d > M_PI) d -= 2 * M_PI;
    while (d < -M_PI) d += 2 * M_PI;
    *a = s->pang + d * alpha;
}

void camera_update(Game *g, float dt, double alpha)
{
    double sx, sy, sa;
    ship_pos(g, alpha, &sx, &sy, &sa);
    const Ship *s = &g->world->ship;
    double dx = sx - PLANET_X, dy = sy - PLANET_Y;
    double d = sqrt(dx * dx + dy * dy);
    double base = 0.85;
    double t = (SOI_R + 2500.0 - d) / 2500.0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    t = t * t * (3 - 2 * t);
    double reach = d > PLANET_R * 2.2 ? d : PLANET_R * 2.2;
    double fit = (DESIGN_H * 0.92) / (2.0 * reach + 260.0);
    double zt = base + (fit - base) * t;
    double k = 1.0 - exp(-dt * 2.2);
    if (g->zoom <= 0) g->zoom = zt;
    g->zoom += (zt - g->zoom) * k;
    /* look ahead along velocity when cruising; frame ship+planet on approach */
    double lead = 0.9 * (1.0 - t);
    double view_w = g->w / px_per_wu(g);
    double lx = s->vx * lead, ly = s->vy * lead;
    double lm = sqrt(lx * lx + ly * ly), lmax = view_w * 0.3;
    if (lm > lmax) { lx *= lmax / lm; ly *= lmax / lm; }
    double tx = sx + lx + (PLANET_X - sx) * 0.5 * t;
    double ty = sy + ly + (PLANET_Y - sy) * 0.5 * t;
    double kc = 1.0 - exp(-dt * 5.0);
    g->camx += (tx - g->camx) * kc;
    g->camy += (ty - g->camy) * kc;
}

/* ------------------------------------------------------------------ */

static void draw_stars(Game *g)
{
    static float sx[180], sy[180];
    static int init = 0;
    if (!init) {
        Uint32 r = 0xC0FFEE11u;
        for (int i = 0; i < 180; i++) {
            r = r * 1664525u + 1013904223u; sx[i] = (r >> 8) / 16777216.0f;
            r = r * 1664525u + 1013904223u; sy[i] = (r >> 8) / 16777216.0f;
        }
        init = 1;
    }
    static const float par[3] = {0.03f, 0.08f, 0.18f};
    static const Uint8 lum[3] = {70, 120, 190};
    SDL_FPoint pts[60];
    for (int L = 0; L < 3; L++) {
        float s = px_per_wu(g) * par[L] / 0.85f * 0.85f;
        float ox = (float)fmod(-g->camx * s, (double)g->w);
        float oy = (float)fmod(g->camy * s, (double)g->h);
        for (int i = 0; i < 60; i++) {
            float x = sx[L * 60 + i] * g->w + ox, y = sy[L * 60 + i] * g->h + oy;
            if (x < 0) x += g->w;
            if (x >= g->w) x -= g->w;
            if (y < 0) y += g->h;
            if (y >= g->h) y -= g->h;
            pts[i].x = x;
            pts[i].y = y;
        }
        C(lum[L], lum[L], lum[L] + 30 > 255 ? 255 : lum[L] + 30, 255);
        SDL_RenderDrawPointsF(g->ren, pts, 60);
    }
}

static void draw_planet(Game *g)
{
    float cx, cy;
    world_to_screen(g, PLANET_X, PLANET_Y, &cx, &cy);
    float s = px_per_wu(g);
    float R = PLANET_R * s;
    if (cx + SOI_R * s < 0 || cx - SOI_R * s > g->w) return;
    /* SOI and atmosphere rings */
    C(70, 90, 130, 110);
    dashed_circle(g, cx, cy, SOI_R * s, 160);
    C(90, 200, 230, 90);
    circle(g, cx, cy, ATMO_R * s, 96);
    if (cx + R < 0 || cx - R > g->w || cy + R < 0 || cy - R > g->h) return;
    /* banded disc, scanline-filled (works on every SDL2 >= 2.0.10) */
    static SDL_FRect rows[2][2400], night[2400];
    int n[2] = {0, 0}, nn = 0;
    int y0 = (int)floorf(cy - R), y1 = (int)ceilf(cy + R);
    if (y0 < 0) y0 = 0;
    if (y1 > g->h) y1 = g->h;
    for (int y = y0; y < y1 && n[0] + n[1] < 2400; y++) {
        float dy = (y + 0.5f - cy) / R;
        if (dy <= -1 || dy >= 1) continue;
        float hw = R * sqrtf(1 - dy * dy);
        float x0 = cx - hw, x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 > g->w) x1 = (float)g->w;
        if (x1 <= x0) continue;
        int band = ((int)floorf((dy + 1.0f) * 7.0f + 0.35f * sinf(dy * 9.0f))) & 1;
        SDL_FRect *rr = &rows[band][n[band]++];
        rr->x = x0; rr->y = (float)y; rr->w = x1 - x0; rr->h = 1;
        /* curved terminator: night starts at a fixed fraction of each row */
        float tx = cx + hw * 0.35f;
        if (tx < x0) tx = x0;
        if (tx < x1) night[nn++] = (SDL_FRect){tx, (float)y, x1 - tx, 1};
    }
    C(38, 96, 122, 255);
    SDL_RenderFillRectsF(g->ren, rows[0], n[0]);
    C(50, 122, 146, 255);
    SDL_RenderFillRectsF(g->ren, rows[1], n[1]);
    /* night side */
    C(5, 10, 20, 130);
    SDL_RenderFillRectsF(g->ren, night, nn);
    C(140, 220, 240, 255);
    circle(g, cx, cy, R, 96);
}

typedef struct { int threat; double tca; } ThreatInfo;

/* Same linear prediction the autopilot uses, for the HUD's red outlines. */
static int is_threat(const World *w, const Body *b, double *tca_out)
{
    const Ship *s = &w->ship;
    double px = b->x - s->x, py = b->y - s->y;
    double rvx = b->vx - s->vx, rvy = b->vy - s->vy;
    double v2 = rvx * rvx + rvy * rvy;
    if (v2 < 1e-6) return 0;
    double tca = -(px * rvx + py * rvy) / v2;
    if (tca < 0 || tca > 3.0) return 0;
    double mx = px + rvx * tca, my = py + rvy * tca;
    double c = b->r + SHIP_R + 6.0;
    if (mx * mx + my * my > c * c) return 0;
    *tca_out = tca;
    return 1;
}

static void rock_color(Game *g, int type, int alpha)
{
    switch (type) {
    case RT_S: C(196, 158, 112, alpha); break;
    case RT_M: C(150, 210, 240, alpha); break;
    default: C(122, 130, 146, alpha);
    }
}

static void draw_bodies(Game *g, double alpha, int *nthreat_off, float *offx, float *offy)
{
    World *w = g->world;
    float s = px_per_wu(g);
    SDL_FPoint pts[MAX_VERTS + 1];
    *nthreat_off = 0;
    int live = w->status == ST_RUNNING;
    for (int i = 0; i < w->nbody; i++) {
        Body *b = &w->body[i];
        double x = b->px + (b->x - b->px) * alpha, y = b->py + (b->y - b->py) * alpha;
        float cx, cy;
        world_to_screen(g, x, y, &cx, &cy);
        float R = (float)b->r * s;
        double tca = 0;
        int threat = live && b->kind == BK_ROCK && g->src != SRC_BOT && is_threat(w, b, &tca);
        if (!on_screen(g, cx, cy, R + 4)) {
            if (threat && *nthreat_off < 8) {
                offx[*nthreat_off] = cx;
                offy[*nthreat_off] = cy;
                (*nthreat_off)++;
            }
            continue;
        }
        if (b->kind == BK_SHARD) {
            if (b->life < 3.0 && fmodf(g->t * 8.0f, 1.0f) < 0.5f) continue;
            float d = 1.6f * g->ui + (float)b->r * s * 0.5f;
            if (d < 1.5f) d = 1.5f;
            int a = b->rtype == RT_M ? 255 : 200;
            if (b->rtype == RT_M) C(120, 235, 255, a);
            else if (b->rtype == RT_S) C(240, 196, 130, a);
            else C(170, 176, 190, a);
            float ph = (float)b->ang;
            SDL_FPoint q[5];
            for (int k = 0; k < 4; k++) {
                q[k].x = cx + d * cosf(ph + k * 1.5708f);
                q[k].y = cy - d * sinf(ph + k * 1.5708f);
            }
            q[4] = q[0];
            SDL_RenderDrawLinesF(g->ren, q, 5);
            continue;
        }
        /* motion streak for stream meteoroids */
        double sp2 = b->vx * b->vx + b->vy * b->vy;
        if (sp2 > 50.0 * 50.0) {
            float tx, ty;
            world_to_screen(g, x - b->vx * 0.25, y - b->vy * 0.25, &tx, &ty);
            rock_color(g, b->rtype, 70);
            SDL_RenderDrawLineF(g->ren, cx, cy, tx, ty);
        }
        int n = b->nverts;
        double a0 = b->pang + (b->ang - b->pang) * alpha;
        for (int k = 0; k < n; k++) {
            double a = a0 + 6.283185307 * k / n;
            float rr = R * b->shape[k] / 255.0f;
            if (rr < 1.2f) rr = 1.2f;
            pts[k].x = cx + rr * (float)cos(a);
            pts[k].y = cy - rr * (float)sin(a);
        }
        pts[n] = pts[0];
        if (threat) C(255, 84, 70, 255);
        else rock_color(g, b->rtype, 255);
        SDL_RenderDrawLinesF(g->ren, pts, n + 1);
        if (b->rtype == RT_M && R > 4) { /* metallic glint */
            C(220, 250, 255, 160 + (int)(90 * sinf(g->t * 3 + i)));
            SDL_RenderDrawPointF(g->ren, cx - R * 0.3f, cy - R * 0.3f);
        }
        if (threat) {
            /* pips: laser shots needed to shatter (shown up to 5) */
            int shots = (int)sim_shots_to_break(b);
            if (shots <= 5) {
                float p = 2.0f * g->ui;
                for (int k = 0; k < shots; k++) {
                    SDL_FRect r = {cx - (shots * 2.0f * p) * 0.5f + k * 2.0f * p, cy - R - 4 * g->ui - p, p, p};
                    SDL_RenderFillRectF(g->ren, &r);
                }
            }
        }
    }
}

static void draw_ship(Game *g, double alpha)
{
    double x, y, a;
    ship_pos(g, alpha, &x, &y, &a);
    float cx, cy;
    world_to_screen(g, x, y, &cx, &cy);
    float R = SHIP_R * px_per_wu(g);
    float minR = 6.5f * g->ui;
    if (R < minR) R = minR;
    float ca = (float)cos(a), sa = (float)sin(a);
#define P(fx, fy) (SDL_FPoint){cx + R * ((fx) * ca - (fy) * sa), cy - R * ((fx) * sa + (fy) * ca)}
    SDL_FPoint hull[5] = {P(1.6f, 0), P(-1.1f, 0.95f), P(-0.55f, 0), P(-1.1f, -0.95f), P(1.6f, 0)};
    const Ship *s = &g->world->ship;
    if (s->thrusting && g->world->status == ST_RUNNING) {
        float L = 1.7f + 0.8f * frand(g);
        SDL_FPoint fl[3] = {P(-0.7f, 0.45f), P(-0.7f - L, 0), P(-0.7f, -0.45f)};
        C(255, 170, 60, 255);
        SDL_RenderDrawLinesF(g->ren, fl, 3);
    }
    if (s->hull < 35 && fmodf(g->t * 4, 1.0f) < 0.5f) C(255, 150, 90, 255);
    else C(235, 245, 255, 255);
    SDL_RenderDrawLinesF(g->ren, hull, 5);
    /* nose marker so heading reads at a glance */
    SDL_FPoint nose = P(1.6f, 0);
    C(120, 230, 255, 255);
    SDL_FRect nr = {nose.x - 1, nose.y - 1, 2, 2};
    SDL_RenderFillRectF(g->ren, &nr);
#undef P
}

static void draw_beams_particles(Game *g)
{
    for (int i = 0; i < g->nbeams; i++) {
        Beam *b = &g->beams[i];
        float x1, y1, x2, y2;
        world_to_screen(g, b->x1, b->y1, &x1, &y1);
        world_to_screen(g, b->x2, b->y2, &x2, &y2);
        int a = (int)(255 * (b->t / 0.09f));
        if (a < 0) a = 0;
        C(255, 90, 140, a);
        SDL_RenderDrawLineF(g->ren, x1, y1, x2, y2);
        SDL_RenderDrawLineF(g->ren, x1 + 1, y1, x2 + 1, y2);
        C(255, 220, 235, a);
        SDL_RenderDrawLineF(g->ren, x1, y1 + 0.5f, x2, y2 + 0.5f);
    }
    for (int i = 0; i < g->nparts; i++) {
        Particle *p = &g->parts[i];
        float sx, sy;
        world_to_screen(g, p->x, p->y, &sx, &sy);
        if (!on_screen(g, sx, sy, 4)) continue;
        float f = p->life / p->max;
        C(p->r, p->g, p->b, (int)(255 * f));
        float sz = p->size * g->ui;
        SDL_FRect r = {sx - sz * 0.5f, sy - sz * 0.5f, sz, sz};
        SDL_RenderFillRectF(g->ren, &r);
    }
}

/* Predicted path under planet gravity, same integrator as the sim. */
static void draw_trajectory(Game *g, double alpha)
{
    const Ship *s = &g->world->ship;
    double x, y, a;
    ship_pos(g, alpha, &x, &y, &a);
    double dx = x - PLANET_X, dy = y - PLANET_Y;
    double d = sqrt(dx * dx + dy * dy);
    if (d > SOI_R * 1.6) {
        float x1, y1, x2, y2;
        world_to_screen(g, x, y, &x1, &y1);
        world_to_screen(g, x + s->vx * 1.5, y + s->vy * 1.5, &x2, &y2);
        C(120, 160, 220, 90);
        SDL_RenderDrawLineF(g->ren, x1, y1, x2, y2);
        return;
    }
    Orbit o = sim_orbit(s->x, s->y, s->vx, s->vy);
    int captured = sim_orbit_captured(&o);
    double px = s->x, py = s->y, vx = s->vx, vy = s->vy, h = 0.1;
    int impact = 0;
    double best_r = 1e30, bpx = 0, bpy = 0;
    SDL_FPoint pts[480];
    int n = 0;
    for (int i = 0; i < 1400; i++) {
        double rx = px - PLANET_X, ry = py - PLANET_Y;
        double r2 = rx * rx + ry * ry, r = sqrt(r2);
        if (r < PLANET_R) { impact = 1; break; }
        if (r < SOI_R) {
            vx -= PLANET_GM / r2 * rx / r * h;
            vy -= PLANET_GM / r2 * ry / r * h;
        } else if (i > 10 && rx * vx + ry * vy > 0) break;
        px += vx * h;
        py += vy * h;
        if (r < best_r) { best_r = r; bpx = px; bpy = py; }
        if (i % 3 == 0 && n < 480) {
            world_to_screen(g, px, py, &pts[n].x, &pts[n].y);
            n++;
        }
    }
    if (captured) C(110, 255, 140, 220);
    else if (impact) C(255, 90, 80, 220);
    else if (o.bound) C(255, 200, 90, 220);
    else C(170, 200, 255, 170);
    SDL_RenderDrawPointsF(g->ren, pts, n);
    if (!impact && best_r < SOI_R) {
        float qx, qy;
        world_to_screen(g, bpx, bpy, &qx, &qy);
        SDL_FRect r = {qx - 2 * g->ui, qy - 2 * g->ui, 4 * g->ui, 4 * g->ui};
        SDL_RenderFillRectF(g->ren, &r);
        font_draw(g->ren, qx + 4 * g->ui, qy - 3 * g->ui, g->ui, "PE");
    }
}

static void edge_arrow(Game *g, float tx, float ty)
{
    float cx = g->w * 0.5f, cy = g->h * 0.5f;
    float dx = tx - cx, dy = ty - cy;
    float m = fmaxf(fabsf(dx) / (cx - 18 * g->ui), fabsf(dy) / (cy - 18 * g->ui));
    if (m <= 0) return;
    float ex = cx + dx / m, ey = cy + dy / m;
    float l = sqrtf(dx * dx + dy * dy);
    float ux = dx / l, uy = dy / l, s = 9 * g->ui;
    SDL_FPoint t[4] = {{ex + ux * s, ey + uy * s},
                       {ex - uy * s * 0.6f, ey + ux * s * 0.6f},
                       {ex + uy * s * 0.6f, ey - ux * s * 0.6f},
                       {ex + ux * s, ey + uy * s}};
    SDL_RenderDrawLinesF(g->ren, t, 4);
}

static void draw_hud(Game *g)
{
    World *w = g->world;
    const Ship *s = &w->ship;
    float u = g->ui, ts = 2 * u;
    char buf[160];
    /* energy & hull */
    C(150, 230, 255, 255);
    font_draw(g->ren, 10 * u, 8 * u, ts, "ENERGY");
    int low = s->energy < 15;
    bar(g, 10 * u, 25 * u, 170 * u, 10 * u, (float)(s->energy / ENERGY_MAX),
        low ? 255 : 90, low ? 120 : 220, low ? 90 : 255);
    snprintf(buf, sizeof buf, "%.0f", s->energy);
    C(230, 245, 255, 255);
    font_draw(g->ren, 186 * u, 24 * u, ts, buf);
    C(255, 190, 130, 255);
    font_draw(g->ren, 10 * u, 42 * u, ts, "HULL");
    bar(g, 10 * u, 59 * u, 170 * u, 7 * u, (float)(s->hull / HULL_MAX), 255, 160, 90);

    /* distance / speed */
    double dx = s->x - PLANET_X, dy = s->y - PLANET_Y, d = sqrt(dx * dx + dy * dy) - PLANET_R;
    double v = sqrt(s->vx * s->vx + s->vy * s->vy);
    if (d > 1000) snprintf(buf, sizeof buf, "DIST %.1fK", d / 1000.0);
    else snprintf(buf, sizeof buf, "DIST %.0f", d);
    C(200, 215, 235, 255);
    font_draw(g->ren, g->w - 10 * u - font_width(buf, ts), 8 * u, ts, buf);
    snprintf(buf, sizeof buf, "SPD %.0f", v);
    font_draw(g->ren, g->w - 10 * u - font_width(buf, ts), 26 * u, ts, buf);

    /* route progress */
    float x0 = g->w * 0.32f, x1 = g->w * 0.68f, yy = 14 * u;
    C(90, 110, 140, 255);
    SDL_RenderDrawLineF(g->ren, x0, yy, x1, yy);
    C(80, 170, 200, 255);
    circle(g, x1 + 4 * u, yy, 4 * u, 16);
    float f = (float)(s->x / PLANET_X);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    C(235, 245, 255, 255);
    SDL_FRect m = {x0 + (x1 - x0) * f - 2 * u, yy - 3 * u, 4 * u, 6 * u};
    SDL_RenderFillRectF(g->ren, &m);

    if (w->status != ST_RUNNING) return;
    float cy = g->h * 0.5f;
    int blink = fmodf(g->t * 2.5f, 1.0f) < 0.6f;
    /* stream warning: many small fast meteoroids ahead */
    int stream = 0;
    for (int i = 0; i < w->nbody && stream < 6; i++) {
        const Body *b = &w->body[i];
        if (b->kind != BK_ROCK || b->r > 8) continue;
        double ex = b->x - s->x, ey = b->y - s->y;
        if (ex < 0 || ex > 1300 || fabs(ey) > 900) continue;
        if (b->vx * b->vx + b->vy * b->vy > 60.0 * 60.0) stream++;
    }
    if (stream >= 6 && blink && !s->in_soi) {
        C(255, 200, 90, 255);
        font_draw_centered(g->ren, g->w * 0.5f, 30 * u, ts, "METEOROID STREAM AHEAD");
    }
    if (s->zero_energy_t > 0) {
        snprintf(buf, sizeof buf, "LIFE SUPPORT FAILING %.0f", ADRIFT_LIMIT - s->zero_energy_t);
        C(255, 80, 70, 255);
        font_draw_centered(g->ren, g->w * 0.5f, cy - 60 * u, ts, buf);
    } else if (low && blink) {
        C(255, 110, 90, 255);
        font_draw_centered(g->ren, g->w * 0.5f, cy - 60 * u, ts, "LOW ENERGY");
    }
    /* orbital readout near the planet */
    if (d < SOI_R * 1.3) {
        Orbit o = sim_orbit(s->x, s->y, s->vx, s->vy);
        int cap = sim_orbit_captured(&o);
        if (o.bound) snprintf(buf, sizeof buf, "PERI %.0f  APO %.0f", o.rp - PLANET_R, o.ra - PLANET_R);
        else snprintf(buf, sizeof buf, "PERI %.0f  ESCAPE", o.rp - PLANET_R);
        float by = g->h - (g->touch_ui ? 150 : 66) * u;
        if (cap) C(110, 255, 140, 255);
        else if (o.rp < ATMO_R) C(255, 90, 80, 255);
        else C(255, 210, 120, 255);
        font_draw_centered(g->ren, g->w * 0.5f, by, ts, buf);
        const char *hint = o.rp < ATMO_R ? "PERIAPSIS TOO LOW - BURN SIDEWAYS"
                         : (!o.bound ? "BRAKE (S) NEAR PERIAPSIS TO BE CAPTURED"
                         : (o.ra >= CAPTURE_RA ? "KEEP BRAKING - APOAPSIS OUTSIDE SOI" : ""));
        if (hint[0]) {
            C(200, 210, 230, 255);
            font_draw_centered(g->ren, g->w * 0.5f, by + 16 * u, u * 1.5f, hint);
        }
    }
}

void render_layout_buttons(Game *g)
{
    float u = g->ui, sz = 78 * u, m = 10 * u;
    g->nbuttons = 5;
    g->buttons[0] = (TouchButton){{m, g->h - m - sz, sz, sz}, "<", IN_LEFT};
    g->buttons[1] = (TouchButton){{2 * m + sz, g->h - m - sz, sz, sz}, ">", IN_RIGHT};
    g->buttons[2] = (TouchButton){{m, g->h - 2 * m - sz - sz * 0.55f, 2 * sz + m, sz * 0.55f}, "BRAKE", IN_RETRO};
    g->buttons[3] = (TouchButton){{g->w - m - sz * 1.15f, g->h - m - sz * 1.15f, sz * 1.15f, sz * 1.15f}, "FIRE", IN_FIRE};
    g->buttons[4] = (TouchButton){{g->w - 2 * m - sz * 2.15f, g->h - m - sz, sz, sz}, "THRUST", IN_THRUST};
}

static void draw_touch(Game *g)
{
    if (!g->touch_ui) return;
    for (int i = 0; i < g->nbuttons; i++) {
        TouchButton *b = &g->buttons[i];
        int on = (g->touch_bits & b->bit) != 0;
        C(120, 200, 255, on ? 90 : 28);
        SDL_RenderFillRectF(g->ren, &b->rect);
        C(150, 210, 255, on ? 230 : 110);
        SDL_RenderDrawRectF(g->ren, &b->rect);
        float sc = (strlen(b->label) > 2 ? 1.6f : 3.0f) * g->ui;
        font_draw_centered(g->ren, b->rect.x + b->rect.w * 0.5f, b->rect.y + b->rect.h * 0.5f - 3.5f * sc, sc, b->label);
    }
}

static void dim(Game *g, int a)
{
    SDL_FRect r = {0, 0, (float)g->w, (float)g->h};
    C(2, 4, 10, a);
    SDL_RenderFillRectF(g->ren, &r);
}

static void draw_title(Game *g)
{
    float u = g->ui, cx = g->w * 0.5f;
    char buf[160];
    dim(g, 150);
    C(150, 230, 255, 255);
    font_draw_centered(g->ren, cx, 52 * u, 6 * u, "CAPTURE BURN");
    C(255, 200, 120, 255);
    font_draw_centered(g->ren, cx, 108 * u, 2 * u, "CROSS THE BELT. BRAKE INTO ORBIT.");
    C(205, 215, 230, 255);
    font_draw_centered(g->ren, cx, 146 * u, 1.5f * u, "ONE RESERVE POWERS THRUST, LASER AND LIFE SUPPORT.");
    font_draw_centered(g->ren, cx, 162 * u, 1.5f * u, "SHATTER A ROCK OR GO AROUND IT. METAL SHARDS REFUEL YOU.");
    font_draw_centered(g->ren, cx, 178 * u, 1.5f * u, "AT THE PLANET, BRAKE NEAR PERIAPSIS UNTIL THE PATH TURNS GREEN.");
    C(150, 190, 230, 255);
    font_draw_centered(g->ren, cx, 214 * u, 1.5f * u, "ARROWS/WASD TURN + THRUST   SPACE FIRE   S/DOWN BRAKE-ORIENT");
    font_draw_centered(g->ren, cx, 230 * u, 1.5f * u, "ESC PAUSE   M MUTE   F11 FULLSCREEN   TOUCH: ON-SCREEN PADS");
    snprintf(buf, sizeof buf, "SEED %llu%s", (unsigned long long)g->seed,
             g->seed >= 20000101ULL && g->seed <= 21001231ULL ? " (DAILY)" : "");
    C(230, 240, 255, 255);
    font_draw_centered(g->ren, cx, 272 * u, 2 * u, buf);
    C(150, 170, 200, 255);
    font_draw_centered(g->ren, cx, 296 * u, 1.5f * u, "N NEW SEED   D WATCH AUTOPILOT   Q QUIT");
    if (fmodf(g->t * 1.5f, 1.0f) < 0.7f) {
        C(120, 255, 160, 255);
        font_draw_centered(g->ren, cx, 340 * u, 2.5f * u, "PRESS SPACE OR TAP TO LAUNCH");
    }
}

static void draw_end(Game *g)
{
    World *w = g->world;
    const Stats *st = &w->stats;
    float u = g->ui, cx = g->w * 0.5f, y = 70 * u;
    char buf[200];
    dim(g, 170);
    if (w->status == ST_WON) C(110, 255, 140, 255);
    else C(255, 100, 85, 255);
    font_draw_centered(g->ren, cx, y, 4 * u, sim_status_text(w->status));
    y += 50 * u;
    C(220, 230, 245, 255);
    snprintf(buf, sizeof buf, "TIME %.1fS   ENERGY LEFT %.1f   HULL %.0f", w->tick / (double)SIM_HZ,
             w->ship.energy, w->ship.hull);
    font_draw_centered(g->ren, cx, y, 2 * u, buf);
    y += 24 * u;
    snprintf(buf, sizeof buf, "SPENT: THRUST %.1f  LASER %.1f  LIFE %.1f   HARVESTED %.1f",
             st->e_thrust, st->e_laser, st->e_life, st->e_gained);
    C(170, 200, 230, 255);
    font_draw_centered(g->ren, cx, y, 1.5f * u, buf);
    y += 18 * u;
    snprintf(buf, sizeof buf, "SHOTS %u   ROCKS SHATTERED %u   SHARDS %u   HULL DAMAGE %.0f",
             st->shots, st->rocks_broken, st->shards_collected, st->damage);
    font_draw_centered(g->ren, cx, y, 1.5f * u, buf);
    y += 30 * u;
    if (g->src == SRC_HUMAN && w->status == ST_WON) {
        if (g->best_known) snprintf(buf, sizeof buf, "BEST RESERVE ON THIS SEED: %.1f", g->best_energy);
        else snprintf(buf, sizeof buf, "FIRST CAPTURE ON THIS SEED");
        C(255, 220, 120, 255);
        font_draw_centered(g->ren, cx, y, 2 * u, buf);
        y += 24 * u;
    }
    snprintf(buf, sizeof buf, "SHARE: SEED %llu %s E%.1f T%.0f", (unsigned long long)w->seed,
             w->status == ST_WON ? "CAPTURED" : "LOST", w->ship.energy, w->tick / (double)SIM_HZ);
    C(150, 170, 200, 255);
    font_draw_centered(g->ren, cx, y, 1.5f * u, buf);
    y += 34 * u;
    C(230, 240, 255, 255);
    font_draw_centered(g->ren, cx, y, 2 * u, "ENTER/TAP RETRY   N NEW SEED   R REPLAY   ESC TITLE");
}

static void draw_pause(Game *g)
{
    dim(g, 140);
    C(230, 240, 255, 255);
    font_draw_centered(g->ren, g->w * 0.5f, g->h * 0.4f, 4 * g->ui, "PAUSED");
    font_draw_centered(g->ren, g->w * 0.5f, g->h * 0.4f + 46 * g->ui, 2 * g->ui, "ESC/TAP RESUME   Q GIVE UP");
}

void render_frame(Game *g, double alpha)
{
    float shake = g->trauma * g->trauma * 9.0f * g->ui;
    double cx = g->camx, cy = g->camy;
    if (shake > 0.01f) {
        g->camx += (frand(g) - 0.5f) * 2 * shake / px_per_wu(g);
        g->camy += (frand(g) - 0.5f) * 2 * shake / px_per_wu(g);
    }
    C(4, 6, 13, 255);
    SDL_RenderClear(g->ren);
    draw_stars(g);
    draw_planet(g);
    if (g->state != GS_TITLE) draw_trajectory(g, alpha);
    int noff = 0;
    float offx[8], offy[8];
    draw_bodies(g, alpha, &noff, offx, offy);
    draw_beams_particles(g);
    if (g->world->status == ST_RUNNING || g->world->status == ST_WON) draw_ship(g, alpha);
    C(255, 84, 70, 230);
    for (int i = 0; i < noff; i++) edge_arrow(g, offx[i], offy[i]);
    if (g->flash > 0) {
        SDL_FRect r = {0, 0, (float)g->w, (float)g->h};
        C(255, 40, 30, (int)(g->flash * 90));
        SDL_RenderFillRectF(g->ren, &r);
    }
    g->camx = cx;
    g->camy = cy;
    if (g->state != GS_TITLE) draw_hud(g);
    if (g->src == SRC_BOT && g->state == GS_PLAY) {
        C(255, 220, 120, 255);
        font_draw_centered(g->ren, g->w * 0.5f, g->h - 22 * g->ui, 2 * g->ui, "AUTOPILOT DEMO - ANY KEY TO STOP");
    } else if (g->src == SRC_REPLAY && g->state == GS_PLAY) {
        C(255, 220, 120, 255);
        font_draw_centered(g->ren, g->w * 0.5f, g->h - 22 * g->ui, 2 * g->ui, "REPLAY - ANY KEY TO STOP");
    }
    if (g->state == GS_PLAY && g->src == SRC_HUMAN) draw_touch(g);
    if (g->state == GS_TITLE) draw_title(g);
    else if (g->state == GS_END) draw_end(g);
    else if (g->state == GS_PAUSE) draw_pause(g);
}
