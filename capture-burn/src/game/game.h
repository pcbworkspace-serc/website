/* SDL2 front end shared state. The simulation never sees any of this. */
#ifndef CB_GAME_H
#define CB_GAME_H
#include <SDL.h>
#include "sim.h"
#include "bot.h"
#include "replay.h"

#define DESIGN_H 480.0f      /* layout is designed for the 7" 800x480 panel */
#define MAX_PARTS 700
#define MAX_BEAMS 16
#define MAX_FINGERS 10

typedef enum { GS_TITLE, GS_PLAY, GS_PAUSE, GS_END } GameState;
typedef enum { SRC_HUMAN, SRC_BOT, SRC_REPLAY } InputSource;

typedef struct {
    float x, y, vx, vy, life, max, size;
    Uint8 r, g, b;
} Particle;

typedef struct { double x1, y1, x2, y2; float t; } Beam;

typedef struct { SDL_FingerID id; float x, y; int active; } Finger;

typedef struct {
    SDL_FRect rect;
    const char *label;
    Uint8 bit;
} TouchButton;

typedef struct {
    /* platform */
    SDL_Window *win;
    SDL_Renderer *ren;
    int w, h;          /* output pixels */
    float ui;          /* h / 480 */
    int fullscreen, vsync, fps_cap, muted;
    SDL_GameController *pad;
    char *pref;        /* ~/.local/share/cyberdeck/capture-burn/ */

    /* simulation */
    World *world;
    Replay rec;        /* inputs of the current run */
    Replay play;       /* replay being watched */
    uint32_t play_pos;
    Bot bot;
    InputSource src;
    uint64_t seed;
    double acc;
    double hitstop;

    /* flow */
    GameState state;
    int run_index;     /* runs started this session */
    Uint32 session_start;
    int used_kbd, used_touch, used_pad;
    char last_replay[512];
    double best_energy; /* best reserve on this seed, from runs.csv */
    int best_known;

    /* input */
    Uint8 key_bits, pad_bits, touch_bits;
    Finger fingers[MAX_FINGERS];
    int mouse_down;
    float mouse_x, mouse_y;
    int touch_ui;      /* show touch overlay */
    TouchButton buttons[5];
    int nbuttons;

    /* camera & effects (render only) */
    double camx, camy, zoom;
    float trauma, t;
    Particle parts[MAX_PARTS];
    int nparts;
    Beam beams[MAX_BEAMS];
    int nbeams;
    float flash;       /* red flash on hull hit */
    float low_beep;
    Uint32 rng;        /* visual-only randomness */

    /* bench */
    double bench_seconds;
    double *frame_ms, *work_ms;
    int nframes, frame_cap;
} Game;

/* font.c */
void font_draw(SDL_Renderer *r, float x, float y, float scale, const char *s);
float font_width(const char *s, float scale);
void font_draw_centered(SDL_Renderer *r, float cx, float y, float scale, const char *s);

/* audio.c */
typedef enum {
    SND_SHOT, SND_HIT, SND_BREAK, SND_PICKUP, SND_PICKUP_M, SND_SHIP_HIT,
    SND_SOI, SND_WIN, SND_LOSE, SND_BEEP, SND_UI
} Sound;
int audio_init(void);
void audio_play(Sound s, float gain);
void audio_thrust(int on);
void audio_mute(int m);
void audio_quit(void);

/* render.c */
void render_frame(Game *g, double alpha);
void camera_update(Game *g, float dt, double alpha);
void render_layout_buttons(Game *g);
void world_to_screen(const Game *g, double wx, double wy, float *sx, float *sy);
float frand(Game *g);

#endif
