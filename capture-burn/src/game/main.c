/* Capture Burn: SDL2 platform layer for the cyberdeck (Raspberry Pi,
 * 7" touchscreen, built-in keyboard). Fixed 120 Hz simulation, render
 * interpolation, keyboard / touch / gamepad, replays, telemetry, bench. */
#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static void usage(void)
{
    puts("capture-burn: cross the belt, brake into orbit.\n"
         "\n"
         "  --windowed          run in a window (default: fullscreen)\n"
         "  --size WxH          window size (default 800x480)\n"
         "  --seed N            play seed N (default: today's date, YYYYMMDD)\n"
         "  --demo              watch the autopilot play\n"
         "  --replay FILE       watch a recorded run (.cbr)\n"
         "  --touch             always show the on-screen touch pads\n"
         "  --fps N             cap frame rate (e.g. 30 to save battery)\n"
         "  --no-vsync          do not wait for vertical sync\n"
         "  --software          use SDL's software renderer\n"
         "  --mute              start muted (M toggles)\n"
         "  --bench SECONDS     autopilot plays; print frame-time percentiles\n"
         "  --shot FILE.bmp     save a screenshot and exit (with --shot-at)\n"
         "  --shot-at SECONDS   simulated time of the screenshot\n"
         "  --speed X           run the simulation X times faster (demo/replay)\n"
         "\n"
         "Keys: arrows/WASD turn+thrust, Space fire, S/Down brake-orient,\n"
         "      Esc pause, M mute, F11 fullscreen, F12 screenshot.\n"
         "Data: replays and runs.csv in ~/.local/share/cyberdeck/capture-burn/");
}

static uint64_t daily_seed(void)
{
    time_t t = time(0);
    struct tm tm;
    localtime_r(&t, &tm);
    return (uint64_t)(tm.tm_year + 1900) * 10000 + (uint64_t)(tm.tm_mon + 1) * 100 + (uint64_t)tm.tm_mday;
}

static void stamp(char *buf, size_t n)
{
    time_t t = time(0);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y%m%d-%H%M%S", &tm);
}

static void resize(Game *g)
{
    SDL_GetRendererOutputSize(g->ren, &g->w, &g->h);
    g->ui = g->h / DESIGN_H;
    if (g->ui < 0.5f) g->ui = 0.5f;
    render_layout_buttons(g);
}

/* ------------------------------------------------------------------ */
/* runs                                                                 */
/* ------------------------------------------------------------------ */

static void reset_view(Game *g)
{
    g->camx = g->world->ship.x;
    g->camy = g->world->ship.y;
    g->zoom = 0.85;
    g->nparts = g->nbeams = 0;
    g->trauma = g->flash = 0;
    g->acc = 0;
    g->hitstop = 0;
}

static void start_run(Game *g, InputSource src)
{
    g->src = src;
    if (src == SRC_REPLAY) {
        sim_init(g->world, g->play.seed);
        g->play_pos = 0;
    } else {
        sim_init(g->world, g->seed);
    }
    replay_free(&g->rec);
    replay_init(&g->rec, g->world->seed);
    bot_init(&g->bot, bot_pilot());
    reset_view(g);
    g->state = GS_PLAY;
    if (src == SRC_HUMAN) {
        g->run_index++;
        g->used_kbd = g->used_touch = g->used_pad = 0;
    }
}

static void title_demo(Game *g)
{
    g->src = SRC_BOT;
    sim_init(g->world, g->seed);
    bot_init(&g->bot, bot_pilot());
    reset_view(g);
    g->state = GS_TITLE;
}

static void load_best(Game *g)
{
    g->best_known = 0;
    g->best_energy = 0;
    char path[1024];
    snprintf(path, sizeof path, "%sruns.csv", g->pref);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        unsigned long long seed;
        char outcome[64];
        double secs, energy;
        /* session,run,time,seed,input,outcome,seconds,energy,... */
        if (sscanf(line, "%*[^,],%*[^,],%*[^,],%llu,%*[^,],%63[^,],%lf,%lf", &seed, outcome, &secs, &energy) == 4 &&
            seed == g->world->seed && !strcmp(outcome, "captured")) {
            if (!g->best_known || energy > g->best_energy) g->best_energy = energy;
            g->best_known = 1;
        }
    }
    fclose(f);
}

static const char *outcome_tag(Status s)
{
    switch (s) {
    case ST_WON: return "captured";
    case ST_LOST_HULL: return "hull";
    case ST_LOST_CRASH: return "crash";
    case ST_LOST_PASSED: return "flew_past";
    case ST_LOST_RANGE: return "out_of_range";
    case ST_LOST_ADRIFT: return "adrift";
    default: return "quit";
    }
}

/* Save the replay and append one telemetry row (docs/PLAYTEST.md). */
static void finish_run(Game *g)
{
    World *w = g->world;
    if (g->src != SRC_HUMAN) return;
    load_best(g); /* best before this run, for the end screen */
    char ts[32], path[1024];
    stamp(ts, sizeof ts);
    g->rec.final_hash = sim_hash(w);
    g->rec.status = (uint8_t)w->status;
    snprintf(path, sizeof path, "%sreplays", g->pref);
    mkdir(path, 0755); /* fine if it already exists */
    snprintf(g->last_replay, sizeof g->last_replay, "%sreplays/%s-seed%llu.cbr", g->pref, ts,
             (unsigned long long)w->seed);
    if (replay_save(&g->rec, g->last_replay) != 0) g->last_replay[0] = 0;

    snprintf(path, sizeof path, "%sruns.csv", g->pref);
    FILE *f = fopen(path, "r");
    int fresh = f == 0;
    if (f) fclose(f);
    f = fopen(path, "a");
    if (!f) return;
    if (fresh)
        fprintf(f, "session,run,time,seed,input,outcome,seconds,energy_left,hull_left,shots,"
                   "rocks_broken,shards,energy_gained,e_thrust,e_laser,e_life,damage,replay\n");
    const char *input = g->used_touch ? (g->used_kbd ? "mixed" : "touch")
                      : (g->used_pad ? "gamepad" : "keyboard");
    const Stats *st = &w->stats;
    fprintf(f, "%u,%d,%s,%llu,%s,%s,%.2f,%.2f,%.1f,%u,%u,%u,%.2f,%.2f,%.2f,%.2f,%.1f,%s\n",
            g->session_start, g->run_index, ts, (unsigned long long)w->seed, input,
            outcome_tag(w->status), w->tick / (double)SIM_HZ, w->ship.energy, w->ship.hull,
            st->shots, st->rocks_broken, st->shards_collected, st->e_gained, st->e_thrust,
            st->e_laser, st->e_life, st->damage, g->last_replay);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* effects                                                              */
/* ------------------------------------------------------------------ */

static void burst(Game *g, double x, double y, int n, float speed, float life, Uint8 r, Uint8 gg, Uint8 b, float size)
{
    for (int i = 0; i < n && g->nparts < MAX_PARTS; i++) {
        Particle *p = &g->parts[g->nparts++];
        float a = frand(g) * 6.2831853f, s = speed * (0.3f + frand(g));
        p->x = (float)x;
        p->y = (float)y;
        p->vx = cosf(a) * s;
        p->vy = sinf(a) * s;
        p->max = p->life = life * (0.5f + frand(g));
        p->r = r; p->g = gg; p->b = b;
        p->size = size;
    }
}

static void type_rgb(int t, Uint8 *r, Uint8 *g, Uint8 *b)
{
    if (t == RT_M) { *r = 140; *g = 230; *b = 255; }
    else if (t == RT_S) { *r = 220; *g = 175; *b = 120; }
    else { *r = 150; *g = 158; *b = 172; }
}

static float near_gain(const Game *g, double x, double y)
{
    double dx = x - g->world->ship.x, dy = y - g->world->ship.y;
    double d = sqrt(dx * dx + dy * dy);
    float k = (float)(1.0 - d / 900.0);
    return k < 0 ? 0 : k;
}

static void handle_events(Game *g)
{
    World *w = g->world;
    int sound = g->state == GS_PLAY;
    for (int i = 0; i < w->nevents; i++) {
        Event *e = &w->events[i];
        Uint8 r, gg, b;
        type_rgb(e->rtype, &r, &gg, &b);
        float gain = near_gain(g, e->x, e->y);
        switch (e->kind) {
        case EV_SHOT:
            if (g->nbeams < MAX_BEAMS) g->beams[g->nbeams++] = (Beam){e->x, e->y, e->x2, e->y2, 0.09f};
            if (sound) audio_play(SND_SHOT, 1);
            break;
        case EV_HIT:
            burst(g, e->x, e->y, 5, 90, 0.25f, 255, 200, 220, 2);
            break;
        case EV_CRATER:
            burst(g, e->x, e->y, 6, 50, 0.4f, r, gg, b, 2);
            if (sound && gain > 0.05f) audio_play(SND_HIT, gain);
            break;
        case EV_BREAK:
            burst(g, e->x, e->y, 8 + (int)e->mag, 40 + (float)e->mag, 0.7f, r, gg, b, 2.5f);
            if (gain > 0.05f) {
                if (sound) audio_play(SND_BREAK, gain * (0.4f + (float)e->mag / 30.0f));
                g->trauma += gain * (0.06f + (float)e->mag / 250.0f);
                if (e->mag > 14 && gain > 0.5f) g->hitstop = 0.035;
            }
            break;
        case EV_PICKUP:
            burst(g, e->x, e->y, 4, 40, 0.3f, r, gg, b, 2);
            if (sound) audio_play(e->rtype == RT_M ? SND_PICKUP_M : SND_PICKUP, 1);
            break;
        case EV_SHIP_HIT:
            burst(g, e->x, e->y, 14, 110, 0.5f, 255, 170, 90, 2.5f);
            if (sound) audio_play(SND_SHIP_HIT, 0.4f + (float)e->mag / 30.0f);
            g->trauma += 0.2f + (float)e->mag / 45.0f;
            g->flash = fminf(1.0f, (float)e->mag / 25.0f + 0.2f);
            if (e->mag > 4) g->hitstop = 0.06;
            break;
        case EV_ROCK_BUMP:
            if (gain > 0.3f) burst(g, e->x, e->y, 2, 30, 0.2f, r, gg, b, 1.5f);
            break;
        case EV_SOI_ENTER:
            if (sound) audio_play(SND_SOI, 1);
            break;
        default:
            break;
        }
    }
    if (g->trauma > 1) g->trauma = 1;
    sim_clear_events(w);
}

static void update_effects(Game *g, float dt)
{
    for (int i = 0; i < g->nparts;) {
        Particle *p = &g->parts[i];
        p->life -= dt;
        if (p->life <= 0) { g->parts[i] = g->parts[--g->nparts]; continue; }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        i++;
    }
    for (int i = 0; i < g->nbeams;) {
        g->beams[i].t -= dt;
        if (g->beams[i].t <= 0) { g->beams[i] = g->beams[--g->nbeams]; continue; }
        i++;
    }
    g->trauma -= dt * 1.6f;
    if (g->trauma < 0) g->trauma = 0;
    g->flash -= dt * 3.0f;
    if (g->flash < 0) g->flash = 0;
    const Ship *s = &g->world->ship;
    int running = g->world->status == ST_RUNNING && g->state == GS_PLAY;
    if (running && s->thrusting && g->nparts < MAX_PARTS - 2) {
        double c = cos(s->ang), sn = sin(s->ang);
        Particle *p = &g->parts[g->nparts++];
        p->x = (float)(s->x - c * SHIP_R * 1.2);
        p->y = (float)(s->y - sn * SHIP_R * 1.2);
        float jit = (frand(g) - 0.5f) * 0.6f;
        p->vx = (float)(s->vx - (c + jit * -sn) * 120.0);
        p->vy = (float)(s->vy - (sn + jit * c) * 120.0);
        p->max = p->life = 0.25f + 0.2f * frand(g);
        p->r = 255; p->g = (Uint8)(140 + 80 * frand(g)); p->b = 60;
        p->size = 2;
    }
    audio_thrust(running && s->thrusting && g->src != SRC_BOT);
    if (running && g->src == SRC_HUMAN && s->energy < 15) {
        g->low_beep -= dt;
        if (g->low_beep <= 0) { audio_play(SND_BEEP, 1); g->low_beep = 1.0f; }
    }
}

/* ------------------------------------------------------------------ */
/* input                                                                */
/* ------------------------------------------------------------------ */

static void poll_held(Game *g)
{
    const Uint8 *k = SDL_GetKeyboardState(0);
    Uint8 b = 0;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) b |= IN_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) b |= IN_RIGHT;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) b |= IN_THRUST;
    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_J] || k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) b |= IN_FIRE;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) b |= IN_RETRO;
    g->key_bits = b;
    if (b) g->used_kbd = 1;

    b = 0;
    if (g->pad) {
        SDL_GameController *p = g->pad;
        int lx = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
        if (lx < -12000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) b |= IN_LEFT;
        if (lx > 12000 || SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= IN_RIGHT;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A) ||
            SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) b |= IN_THRUST;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_X) ||
            SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= IN_FIRE;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B) ||
            SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) b |= IN_RETRO;
    }
    g->pad_bits = b;
    if (b) g->used_pad = 1;

    b = 0;
    for (int f = 0; f <= MAX_FINGERS; f++) {
        float x, y;
        if (f < MAX_FINGERS) {
            if (!g->fingers[f].active) continue;
            x = g->fingers[f].x;
            y = g->fingers[f].y;
        } else {
            if (!g->mouse_down) continue;
            x = g->mouse_x;
            y = g->mouse_y;
        }
        for (int i = 0; i < g->nbuttons; i++) {
            SDL_FRect *r = &g->buttons[i].rect;
            if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) b |= g->buttons[i].bit;
        }
    }
    g->touch_bits = b;
    if (b) g->used_touch = 1;
}

static void finger_set(Game *g, SDL_FingerID id, float x, float y, int down)
{
    int slot = -1;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (g->fingers[i].active && g->fingers[i].id == id) { slot = i; break; }
    if (!down) { if (slot >= 0) g->fingers[slot].active = 0; return; }
    if (slot < 0)
        for (int i = 0; i < MAX_FINGERS; i++)
            if (!g->fingers[i].active) { slot = i; break; }
    if (slot < 0) return;
    g->fingers[slot] = (Finger){id, x, y, 1};
}

static void new_seed(Game *g)
{
    g->seed = ((uint64_t)SDL_GetPerformanceCounter() ^ (uint64_t)time(0) * 2654435761u) % 900000000ULL + 1000;
}

static void save_shot(Game *g, const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, g->w, g->h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    if (SDL_RenderReadPixels(g->ren, 0, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0)
        SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

/* "confirm" from keyboard Enter/Space, a tap, or gamepad A/Start */
static void confirm(Game *g, int *running)
{
    (void)running; /* same signature as back() */
    switch (g->state) {
    case GS_TITLE: audio_play(SND_UI, 1); start_run(g, SRC_HUMAN); break;
    case GS_PAUSE: g->state = GS_PLAY; break;
    case GS_END: audio_play(SND_UI, 1); start_run(g, SRC_HUMAN); break;
    default: break;
    }
}

static void back(Game *g, int *running)
{
    switch (g->state) {
    case GS_TITLE: *running = 0; break;
    case GS_PLAY:
        if (g->src == SRC_HUMAN) g->state = GS_PAUSE;
        else title_demo(g);
        break;
    case GS_PAUSE: g->state = GS_PLAY; break;
    case GS_END: title_demo(g); break;
    }
}

static void on_key(Game *g, SDL_Keycode k, int *running)
{
    if (k == SDLK_F11) {
        g->fullscreen = !g->fullscreen;
        SDL_SetWindowFullscreen(g->win, g->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        return;
    }
    if (k == SDLK_m) { g->muted = !g->muted; audio_mute(g->muted); return; }
    if (k == SDLK_F12) {
        char ts[32], path[1024];
        stamp(ts, sizeof ts);
        snprintf(path, sizeof path, "%sshot-%s.bmp", g->pref, ts);
        save_shot(g, path);
        return;
    }
    if (g->state == GS_PLAY && g->src != SRC_HUMAN) { /* any key stops a demo/replay */
        title_demo(g);
        return;
    }
    switch (g->state) {
    case GS_TITLE:
        if (k == SDLK_SPACE || k == SDLK_RETURN || k == SDLK_KP_ENTER) confirm(g, running);
        else if (k == SDLK_n) { new_seed(g); title_demo(g); audio_play(SND_UI, 1); }
        else if (k == SDLK_d) start_run(g, SRC_BOT);
        else if (k == SDLK_ESCAPE || k == SDLK_q) *running = 0;
        break;
    case GS_PLAY:
        if (k == SDLK_ESCAPE || k == SDLK_p) back(g, running);
        break;
    case GS_PAUSE:
        if (k == SDLK_ESCAPE || k == SDLK_p || k == SDLK_SPACE || k == SDLK_RETURN) g->state = GS_PLAY;
        else if (k == SDLK_q) {
            finish_run(g); /* logged as "quit" for the playtest data */
            title_demo(g);
        }
        break;
    case GS_END:
        if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_KP_ENTER) confirm(g, running);
        else if (k == SDLK_n) { new_seed(g); start_run(g, SRC_HUMAN); }
        else if (k == SDLK_r && g->last_replay[0]) {
            replay_free(&g->play);
            if (replay_load(&g->play, g->last_replay) == 0) start_run(g, SRC_REPLAY);
        } else if (k == SDLK_ESCAPE || k == SDLK_q) back(g, running);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* bench                                                                */
/* ------------------------------------------------------------------ */

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double pct(double *v, int n, double p)
{
    if (!n) return 0;
    int i = (int)(p * (n - 1) + 0.5);
    return v[i];
}

static void bench_report(Game *g, double wall, uint32_t ticks)
{
    int n = g->nframes;
    qsort(g->frame_ms, (size_t)n, sizeof(double), cmp_d);
    qsort(g->work_ms, (size_t)n, sizeof(double), cmp_d);
    SDL_RendererInfo ri;
    SDL_GetRendererInfo(g->ren, &ri);
    SDL_version v;
    SDL_GetVersion(&v);
    char out[2048];
    snprintf(out, sizeof out,
             "capture-burn bench\n"
             "  SDL %d.%d.%d, video driver %s, renderer %s%s\n"
             "  output %dx%d, %d frames in %.1f s = %.1f fps, sim %.0f ticks/s (target %d)\n"
             "  frame interval ms: p50 %.2f  p95 %.2f  p99 %.2f  max %.2f\n"
             "  cpu work ms (sim+render, before present): p50 %.2f  p95 %.2f  p99 %.2f  max %.2f\n"
             "  bodies at end: %d\n",
             v.major, v.minor, v.patch, SDL_GetCurrentVideoDriver(), ri.name,
             (ri.flags & SDL_RENDERER_ACCELERATED) ? " (accelerated)" : " (software)",
             g->w, g->h, n, wall, n / wall, ticks / wall, SIM_HZ,
             pct(g->frame_ms, n, 0.5), pct(g->frame_ms, n, 0.95), pct(g->frame_ms, n, 0.99), g->frame_ms[n - 1],
             pct(g->work_ms, n, 0.5), pct(g->work_ms, n, 0.95), pct(g->work_ms, n, 0.99), g->work_ms[n - 1],
             g->world->nbody);
    fputs(out, stdout);
    char ts[32], path[1024];
    stamp(ts, sizeof ts);
    snprintf(path, sizeof path, "%sbench-%s.txt", g->pref, ts);
    FILE *f = fopen(path, "w");
    if (f) { fputs(out, f); fclose(f); printf("  saved %s\n", path); }
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    static Game G;
    Game *g = &G;
    int windowed = 0, win_w = 800, win_h = 480, demo = 0, software = 0;
    double shot_at = 0, speed = 1.0;
    const char *shot = 0, *replay_path = 0;
    g->vsync = 1;
    g->seed = daily_seed();
    g->rng = 0x9E3779B9u ^ (Uint32)time(0);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--windowed")) windowed = 1;
        else if (!strcmp(a, "--size") && i + 1 < argc) { sscanf(argv[++i], "%dx%d", &win_w, &win_h); windowed = 1; }
        else if (!strcmp(a, "--seed") && i + 1 < argc) g->seed = strtoull(argv[++i], 0, 10);
        else if (!strcmp(a, "--demo")) demo = 1;
        else if (!strcmp(a, "--replay") && i + 1 < argc) replay_path = argv[++i];
        else if (!strcmp(a, "--touch")) g->touch_ui = 1;
        else if (!strcmp(a, "--fps") && i + 1 < argc) g->fps_cap = atoi(argv[++i]);
        else if (!strcmp(a, "--no-vsync")) g->vsync = 0;
        else if (!strcmp(a, "--software")) software = 1;
        else if (!strcmp(a, "--mute")) g->muted = 1;
        else if (!strcmp(a, "--bench") && i + 1 < argc) g->bench_seconds = atof(argv[++i]);
        else if (!strcmp(a, "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(a, "--shot-at") && i + 1 < argc) shot_at = atof(argv[++i]);
        else if (!strcmp(a, "--speed") && i + 1 < argc) speed = atof(argv[++i]);
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else { fprintf(stderr, "unknown option %s\n", a); usage(); return 2; }
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"); /* we handle fingers ourselves */
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    char *pref = SDL_GetPrefPath("cyberdeck", "capture-burn");
    g->pref = pref ? pref : SDL_strdup("./");
    g->fullscreen = !windowed;
    Uint32 wf = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
    if (g->fullscreen) wf |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    g->win = SDL_CreateWindow("Capture Burn", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, wf);
    if (!g->win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    Uint32 rf = software ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
    if (g->vsync) rf |= SDL_RENDERER_PRESENTVSYNC;
    g->ren = SDL_CreateRenderer(g->win, -1, rf);
    if (!g->ren) { /* e.g. no GL driver: fall back to software */
        fprintf(stderr, "accelerated renderer unavailable (%s), using software\n", SDL_GetError());
        g->ren = SDL_CreateRenderer(g->win, -1, SDL_RENDERER_SOFTWARE);
        g->vsync = 0;
    }
    if (!g->ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_RendererInfo ri;
    SDL_GetRendererInfo(g->ren, &ri);
    if (!(ri.flags & SDL_RENDERER_PRESENTVSYNC)) g->vsync = 0;
    SDL_SetRenderDrawBlendMode(g->ren, SDL_BLENDMODE_BLEND);
    if (g->fullscreen) SDL_ShowCursor(SDL_DISABLE);
    resize(g);
    if (audio_init() != 0) fprintf(stderr, "no audio device: running silent\n");
    audio_mute(g->muted);

    g->world = malloc(sizeof *g->world);
    if (!g->world) return 1;
    g->session_start = (Uint32)time(0);
    replay_init(&g->rec, g->seed);
    replay_init(&g->play, 0);

    if (replay_path) {
        if (replay_load(&g->play, replay_path) != 0) { fprintf(stderr, "cannot read %s\n", replay_path); return 1; }
        if (g->play.version != SIM_VERSION)
            fprintf(stderr, "warning: replay from sim version %u, this is %u: it will diverge\n",
                    g->play.version, SIM_VERSION);
        start_run(g, SRC_REPLAY);
    } else if (demo || g->bench_seconds > 0) {
        start_run(g, SRC_BOT);
    } else {
        title_demo(g);
    }
    if (g->bench_seconds > 0) {
        g->frame_cap = (int)(g->bench_seconds * 400) + 64;
        g->frame_ms = calloc((size_t)g->frame_cap, sizeof(double));
        g->work_ms = calloc((size_t)g->frame_cap, sizeof(double));
    }

    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    Uint64 bench_start = last;
    uint32_t bench_ticks = 0;
    double end_timer = 0;
    int running = 1, visible = 1;
    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)freq;
        last = now;
        if (dt > 0.25) dt = 0.25;
        g->t += (float)dt;

        SDL_Event ev;
        int wait = (!visible || g->state == GS_PAUSE) && !shot && g->bench_seconds <= 0;
        while (wait ? SDL_WaitEventTimeout(&ev, 100) : SDL_PollEvent(&ev)) {
            wait = 0;
            switch (ev.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) resize(g);
                else if (ev.window.event == SDL_WINDOWEVENT_HIDDEN || ev.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    visible = 0;
                    if (g->state == GS_PLAY && g->src == SRC_HUMAN) g->state = GS_PAUSE;
                } else if (ev.window.event == SDL_WINDOWEVENT_SHOWN || ev.window.event == SDL_WINDOWEVENT_RESTORED ||
                           ev.window.event == SDL_WINDOWEVENT_EXPOSED) visible = 1;
                else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST && g->state == GS_PLAY && g->src == SRC_HUMAN)
                    g->state = GS_PAUSE;
                break;
            case SDL_KEYDOWN:
                if (!ev.key.repeat) on_key(g, ev.key.keysym.sym, &running);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!g->pad) g->pad = SDL_GameControllerOpen(ev.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (g->pad && SDL_GameControllerGetAttached(g->pad) == SDL_FALSE) {
                    SDL_GameControllerClose(g->pad);
                    g->pad = 0;
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    if (g->state == GS_PLAY) back(g, &running);
                    else confirm(g, &running);
                } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A && g->state != GS_PLAY) {
                    confirm(g, &running);
                } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    back(g, &running);
                }
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
            case SDL_FINGERUP: {
                float x = ev.tfinger.x * g->w, y = ev.tfinger.y * g->h;
                finger_set(g, ev.tfinger.fingerId, x, y, ev.type != SDL_FINGERUP);
                if (ev.type == SDL_FINGERDOWN) {
                    g->touch_ui = 1;
                    if (g->state == GS_PLAY && g->src == SRC_HUMAN && y < 50 * g->ui &&
                        x > g->w * 0.3f && x < g->w * 0.7f)
                        g->state = GS_PAUSE; /* tap the route bar to pause */
                    else if (g->state == GS_PLAY && g->src != SRC_HUMAN) on_key(g, SDLK_ESCAPE, &running);
                    else if (g->state != GS_PLAY) confirm(g, &running);
                }
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                /* Some 7" panels report as an absolute pointer, not touch. */
                if (ev.button.which == SDL_TOUCH_MOUSEID || ev.button.button != SDL_BUTTON_LEFT) break;
                g->mouse_down = ev.type == SDL_MOUSEBUTTONDOWN;
                {
                    int ww, wh;
                    SDL_GetWindowSize(g->win, &ww, &wh);
                    g->mouse_x = (float)ev.button.x * g->w / (float)ww;
                    g->mouse_y = (float)ev.button.y * g->h / (float)wh;
                }
                if (g->mouse_down) {
                    if (g->state == GS_PLAY && g->src == SRC_HUMAN) g->touch_ui = 1;
                    else if (g->state == GS_PLAY) on_key(g, SDLK_ESCAPE, &running);
                    else confirm(g, &running);
                }
                break;
            case SDL_MOUSEMOTION:
                if (g->mouse_down && ev.motion.which != SDL_TOUCH_MOUSEID) {
                    int ww, wh;
                    SDL_GetWindowSize(g->win, &ww, &wh);
                    g->mouse_x = (float)ev.motion.x * g->w / (float)ww;
                    g->mouse_y = (float)ev.motion.y * g->h / (float)wh;
                }
                break;
            }
        }
        if (!running) break;
        poll_held(g);

        /* ---- simulation: fixed 120 Hz steps ---- */
        Uint64 f0 = SDL_GetPerformanceCounter();
        World *w = g->world;
        int simulate = (g->state == GS_PLAY || g->state == GS_TITLE) && w->status == ST_RUNNING;
        if (simulate) {
            if (g->hitstop > 0) g->hitstop -= dt;
            else g->acc += dt * (g->src == SRC_HUMAN ? 1.0 : speed);
            int steps = 0, max_steps = speed > 1 ? (int)(8 * speed) : 8;
            while (g->acc >= SIM_DT && steps < max_steps && w->status == ST_RUNNING) {
                uint8_t in = 0;
                if (g->src == SRC_HUMAN) {
                    in = g->key_bits | g->pad_bits | g->touch_bits;
                    replay_push(&g->rec, in);
                } else if (g->src == SRC_BOT) {
                    in = bot_input(&g->bot, w);
                } else {
                    in = g->play_pos < g->play.n ? g->play.in[g->play_pos++] : 0;
                }
                sim_step(w, in);
                handle_events(g);
                g->acc -= SIM_DT;
                steps++;
                bench_ticks++;
            }
            if (steps == max_steps) g->acc = 0; /* device too slow: slow down, don't spiral */
            if (w->status != ST_RUNNING) {
                if (g->state == GS_TITLE) {
                    end_timer = 0;
                } else {
                    g->state = GS_END;
                    if (g->src != SRC_BOT || g->bench_seconds <= 0)
                        audio_play(w->status == ST_WON ? SND_WIN : SND_LOSE, 1);
                    finish_run(g);
                }
                audio_thrust(0);
            }
        }
        /* title attract loop / bench: restart the demo when it ends */
        if (w->status != ST_RUNNING && (g->state == GS_TITLE || (g->bench_seconds > 0 && g->state == GS_END))) {
            end_timer += dt;
            if (end_timer > 2.0) {
                end_timer = 0;
                if (g->state == GS_TITLE) title_demo(g);
                else { new_seed(g); start_run(g, SRC_BOT); }
            }
        }

        update_effects(g, (float)dt);
        double alpha = g->acc / SIM_DT;
        if (alpha > 1) alpha = 1;
        camera_update(g, (float)dt, alpha);
        if (!visible && !shot && g->bench_seconds <= 0) continue;

        render_frame(g, alpha);
        Uint64 w1 = SDL_GetPerformanceCounter();
        if (shot && (w->tick >= (uint32_t)(shot_at * SIM_HZ) || g->state == GS_END)) {
            save_shot(g, shot);
            printf("saved %s at t=%.1f s (%s)\n", shot, w->tick / (double)SIM_HZ, sim_status_text(w->status));
            break;
        }
        SDL_RenderPresent(g->ren);

        if (g->bench_seconds > 0) {
            if (g->nframes < g->frame_cap) {
                g->frame_ms[g->nframes] = dt * 1000.0;
                g->work_ms[g->nframes] = (double)(w1 - f0) / (double)freq * 1000.0; /* sim + render */
                g->nframes++;
            }
            double wall = (double)(SDL_GetPerformanceCounter() - bench_start) / (double)freq;
            if (wall >= g->bench_seconds) { bench_report(g, wall, bench_ticks); break; }
        }
        /* frame cap when there is no vsync, or when asked to save battery */
        int cap = g->fps_cap > 0 ? g->fps_cap : (g->vsync ? 0 : 60);
        if (cap > 0) {
            double target = 1.0 / cap;
            double spent = (double)(SDL_GetPerformanceCounter() - now) / (double)freq;
            if (spent < target) SDL_Delay((Uint32)((target - spent) * 1000.0));
        }
    }

    audio_quit();
    replay_free(&g->rec);
    replay_free(&g->play);
    free(g->world);
    if (g->pad) SDL_GameControllerClose(g->pad);
    SDL_DestroyRenderer(g->ren);
    SDL_DestroyWindow(g->win);
    SDL_free(g->pref);
    SDL_Quit();
    return 0;
}
