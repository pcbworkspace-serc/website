/* Tiny synthesizer: every sound is generated, nothing to load.
 * 22.05 kHz mono S16 keeps the audio thread cheap on a Pi. If there is no
 * audio device (HDMI-less deck, no speaker), everything is a no-op. */
#include "game.h"
#include <math.h>

#define RATE 22050
#define MAX_VOICES 24

typedef enum { W_SQUARE, W_NOISE, W_SINE, W_SAW } Wave;

typedef struct {
    int on;
    Wave wave;
    float f0, f1;        /* start/end frequency (exponential sweep) */
    float dur, t, delay;
    float vol, lp, lpstate, phase;
} Voice;

static SDL_AudioDeviceID dev;
static Voice voices[MAX_VOICES];
static Uint32 noise = 0x12345678u;
static volatile int thrust_on, muted;
static float thrust_level, thrust_lp;

static float nrand(void)
{
    noise ^= noise << 13;
    noise ^= noise >> 17;
    noise ^= noise << 5;
    return (float)(noise & 0xffff) / 32768.0f - 1.0f;
}

static void callback(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    Sint16 *out = (Sint16 *)stream;
    int n = len / 2;
    const float dt = 1.0f / RATE;
    for (int i = 0; i < n; i++) {
        float mix = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice *s = &voices[v];
            if (!s->on) continue;
            if (s->delay > 0) { s->delay -= dt; continue; }
            float u = s->t / s->dur;
            if (u >= 1) { s->on = 0; continue; }
            float f = s->f0 * powf(s->f1 / s->f0, u);
            s->phase += f * dt;
            if (s->phase >= 1) s->phase -= floorf(s->phase);
            float x;
            switch (s->wave) {
            case W_SQUARE: x = s->phase < 0.5f ? 0.6f : -0.6f; break;
            case W_SAW: x = 2.0f * s->phase - 1.0f; break;
            case W_SINE: x = sinf(6.2831853f * s->phase); break;
            default:
                s->lpstate += s->lp * (nrand() - s->lpstate);
                x = s->lpstate * 2.0f;
            }
            float env = (u < 0.02f ? u / 0.02f : 1.0f) * (1.0f - u) * (1.0f - u);
            mix += x * env * s->vol;
            s->t += dt;
        }
        /* engine rumble follows the thrust key with a short glide */
        thrust_level += ((thrust_on ? 1.0f : 0.0f) - thrust_level) * 0.0015f;
        if (thrust_level > 0.001f) {
            thrust_lp += 0.06f * (nrand() - thrust_lp);
            mix += thrust_lp * 1.6f * thrust_level * 0.22f;
        }
        if (muted) mix = 0;
        /* soft clip */
        mix = mix / (1.0f + fabsf(mix));
        out[i] = (Sint16)(mix * 30000.0f);
    }
}

int audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return -1;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = callback;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) return -1;
    SDL_PauseAudioDevice(dev, 0);
    return 0;
}

static void voice(Wave w, float f0, float f1, float dur, float vol, float lp, float delay)
{
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++)
        if (!voices[i].on) { slot = i; break; }
    if (slot < 0) { /* steal the oldest */
        float best = -1;
        for (int i = 0; i < MAX_VOICES; i++)
            if (voices[i].t / voices[i].dur > best) { best = voices[i].t / voices[i].dur; slot = i; }
    }
    Voice *v = &voices[slot];
    v->on = 1;
    v->wave = w;
    v->f0 = f0;
    v->f1 = f1;
    v->dur = dur;
    v->t = 0;
    v->delay = delay;
    v->vol = vol;
    v->lp = lp;
    v->lpstate = 0;
    v->phase = 0;
    SDL_UnlockAudioDevice(dev);
}

void audio_play(Sound s, float g)
{
    switch (s) {
    case SND_SHOT: voice(W_SQUARE, 1700, 380, 0.07f, 0.16f * g, 0, 0); break;
    case SND_HIT: voice(W_NOISE, 1, 1, 0.06f, 0.22f * g, 0.35f, 0); break;
    case SND_BREAK:
        voice(W_NOISE, 1, 1, 0.38f, 0.45f * g, 0.12f, 0);
        voice(W_SQUARE, 140, 45, 0.25f, 0.12f * g, 0, 0);
        break;
    case SND_PICKUP: voice(W_SINE, 700, 1300, 0.06f, 0.14f * g, 0, 0); break;
    case SND_PICKUP_M: voice(W_SINE, 1000, 2000, 0.08f, 0.18f * g, 0, 0); break;
    case SND_SHIP_HIT:
        voice(W_NOISE, 1, 1, 0.45f, 0.6f * g, 0.07f, 0);
        voice(W_SQUARE, 95, 45, 0.3f, 0.25f * g, 0, 0);
        break;
    case SND_SOI: voice(W_SINE, 300, 620, 0.5f, 0.16f, 0, 0); break;
    case SND_WIN:
        voice(W_SINE, 523, 523, 0.25f, 0.2f, 0, 0);
        voice(W_SINE, 659, 659, 0.25f, 0.2f, 0, 0.12f);
        voice(W_SINE, 784, 784, 0.5f, 0.2f, 0, 0.24f);
        voice(W_SINE, 1046, 1046, 0.7f, 0.15f, 0, 0.36f);
        break;
    case SND_LOSE: voice(W_SAW, 420, 55, 1.0f, 0.22f, 0, 0); break;
    case SND_BEEP: voice(W_SQUARE, 880, 880, 0.08f, 0.10f, 0, 0); break;
    case SND_UI: voice(W_SQUARE, 600, 900, 0.05f, 0.10f, 0, 0); break;
    }
}

void audio_thrust(int on) { thrust_on = on; }
void audio_mute(int m) { muted = m; }

void audio_quit(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
}
