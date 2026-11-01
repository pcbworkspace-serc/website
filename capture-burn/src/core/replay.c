#include "replay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void replay_init(Replay *r, uint64_t seed)
{
    memset(r, 0, sizeof *r);
    r->version = SIM_VERSION;
    r->seed = seed;
}

void replay_free(Replay *r)
{
    free(r->in);
    r->in = 0;
    r->n = r->cap = 0;
}

int replay_push(Replay *r, uint8_t input)
{
    if (r->n == r->cap) {
        uint32_t nc = r->cap ? r->cap * 2 : 4096;
        uint8_t *p = realloc(r->in, nc);
        if (!p) return -1;
        r->in = p;
        r->cap = nc;
    }
    r->in[r->n++] = input;
    return 0;
}

/* ---- serialisation to a byte buffer (little-endian, RLE inputs) ---- */

typedef struct { uint8_t *p; size_t n, cap; } Buf;

static int put(Buf *b, uint8_t v)
{
    if (b->n == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 1024;
        uint8_t *q = realloc(b->p, nc);
        if (!q) return -1;
        b->p = q;
        b->cap = nc;
    }
    b->p[b->n++] = v;
    return 0;
}
static void put_u32(Buf *b, uint32_t v) { for (int i = 0; i < 4; i++) put(b, (uint8_t)(v >> (8 * i))); }
static void put_u64(Buf *b, uint64_t v) { for (int i = 0; i < 8; i++) put(b, (uint8_t)(v >> (8 * i))); }

static void serialise(const Replay *r, Buf *b)
{
    put(b, 'C'); put(b, 'B'); put(b, 'R'); put(b, '1');
    put_u32(b, r->version);
    put_u64(b, r->seed);
    put_u32(b, r->n);
    put(b, r->status);
    put_u64(b, r->final_hash);
    uint32_t i = 0;
    while (i < r->n) {
        uint8_t v = r->in[i];
        uint32_t run = 1;
        while (i + run < r->n && r->in[i + run] == v && run < 65535) run++;
        put(b, v);
        put(b, (uint8_t)(run & 0xff));
        put(b, (uint8_t)(run >> 8));
        i += run;
    }
}

static int save(const Replay *r, const char *path, int text)
{
    Buf b = {0, 0, 0};
    serialise(r, &b);
    FILE *f = fopen(path, text ? "w" : "wb");
    if (!f) { free(b.p); return -1; }
    if (text) {
        /* Hex text form ("CBRT"): same bytes, but safe for text-only
         * channels such as web editors, chat, or a LoRa text message. */
        fputs("CBRT\n", f);
        for (size_t i = 0; i < b.n; i++) {
            fprintf(f, "%02x", b.p[i]);
            if (i % 32 == 31 || i == b.n - 1) fputc('\n', f);
        }
    } else {
        fwrite(b.p, 1, b.n, f);
    }
    int ok = ferror(f) == 0;
    fclose(f);
    free(b.p);
    return ok ? 0 : -1;
}

int replay_save(const Replay *r, const char *path) { return save(r, path, 0); }
int replay_save_text(const Replay *r, const char *path) { return save(r, path, 1); }

/* ---- parsing from a byte buffer ---- */

typedef struct { const uint8_t *p; size_t n, pos; } Rd;

static int get(Rd *d, uint8_t *v)
{
    if (d->pos >= d->n) return -1;
    *v = d->p[d->pos++];
    return 0;
}
static int get_u32(Rd *d, uint32_t *v)
{
    *v = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t c;
        if (get(d, &c)) return -1;
        *v |= (uint32_t)c << (8 * i);
    }
    return 0;
}
static int get_u64(Rd *d, uint64_t *v)
{
    *v = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t c;
        if (get(d, &c)) return -1;
        *v |= (uint64_t)c << (8 * i);
    }
    return 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse(Replay *r, const uint8_t *data, size_t len)
{
    Rd d = {data, len, 0};
    uint8_t m[4];
    uint32_t n;
    for (int i = 0; i < 4; i++)
        if (get(&d, &m[i])) return -1;
    if (memcmp(m, "CBR1", 4) != 0) return -1;
    if (get_u32(&d, &r->version) || get_u64(&d, &r->seed) || get_u32(&d, &n)) return -1;
    if (get(&d, &r->status) || get_u64(&d, &r->final_hash)) return -1;
    while (r->n < n) {
        uint8_t v, lo, hi;
        if (get(&d, &v) || get(&d, &lo) || get(&d, &hi)) return -1;
        uint32_t run = (uint32_t)lo | ((uint32_t)hi << 8);
        if (run == 0) return -1;
        for (uint32_t k = 0; k < run && r->n < n; k++)
            if (replay_push(r, v)) return -1;
    }
    return 0;
}

int replay_load(Replay *r, const char *path)
{
    replay_init(r, 0);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    Buf raw = {0, 0, 0};
    int c;
    while ((c = fgetc(f)) != EOF)
        if (put(&raw, (uint8_t)c)) { fclose(f); free(raw.p); return -1; }
    fclose(f);

    int rc;
    if (raw.n >= 4 && memcmp(raw.p, "CBRT", 4) == 0) {
        /* hex text form: decode the digits after the magic line */
        Buf bin = {0, 0, 0};
        int hi = -1;
        rc = 0;
        for (size_t i = 4; i < raw.n && rc == 0; i++) {
            int v = hexval(raw.p[i]);
            if (v < 0) {
                if (raw.p[i] != '\n' && raw.p[i] != '\r' && raw.p[i] != ' ') rc = -1;
                continue;
            }
            if (hi < 0) hi = v;
            else { rc = put(&bin, (uint8_t)(hi << 4 | v)); hi = -1; }
        }
        if (hi >= 0) rc = -1;
        if (rc == 0) rc = parse(r, bin.p, bin.n);
        free(bin.p);
    } else {
        rc = parse(r, raw.p, raw.n);
    }
    free(raw.p);
    if (rc) replay_free(r);
    return rc;
}

uint64_t replay_run(const Replay *r, World *w)
{
    sim_init(w, r->seed);
    for (uint32_t i = 0; i < r->n && w->status == ST_RUNNING; i++) {
        sim_step(w, r->in[i]);
        sim_clear_events(w);
    }
    return sim_hash(w);
}
