/* cb-replay: golden replays and replay inspection.
 *
 *   cb-replay verify DIR|FILE...   re-simulate, compare to recorded hash
 *   cb-replay make-golden DIR N    record pilot runs for seeds 1..N (.cbrt)
 *   cb-replay info FILE            print header
 *   cb-replay to-text IN OUT       convert a replay to the hex text form
 *
 * Golden replays are stored as hex text (.cbrt) so they survive any
 * text-only channel; the game writes compact binary (.cbr). Both load.
 *
 * Golden replays are the cross-platform determinism check: recorded on one
 * machine, they must reproduce bit-identically on x86_64, aarch64 and armv7.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"
#include "bot.h"
#include "replay.h"

static int verify_file(const char *path, World *w)
{
    Replay r;
    if (replay_load(&r, path)) {
        printf("FAIL %s: cannot read\n", path);
        return 1;
    }
    if (r.version != SIM_VERSION) {
        printf("FAIL %s: recorded with sim version %u, this build is %u\n", path, r.version, SIM_VERSION);
        replay_free(&r);
        return 1;
    }
    uint64_t h = replay_run(&r, w);
    int ok = h == r.final_hash && (uint8_t)w->status == r.status;
    printf("%s %s: seed %llu, %u ticks, %s, hash %016llx%s\n", ok ? "ok  " : "FAIL", path,
           (unsigned long long)r.seed, r.n, sim_status_text(w->status), (unsigned long long)h,
           ok ? "" : " (expected different)");
    if (!ok)
        printf("     expected %016llx / %s\n", (unsigned long long)r.final_hash,
               sim_status_text((Status)r.status));
    replay_free(&r);
    return ok ? 0 : 1;
}

static int has_suffix(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int verify_path(const char *p, World *w, int *count)
{
    if (has_suffix(p, ".cbr") || has_suffix(p, ".cbrt")) { (*count)++; return verify_file(p, w); }
    DIR *d = opendir(p);
    if (!d) { printf("FAIL %s: not a .cbr file or directory\n", p); return 1; }
    char *names[512];
    int n = 0, bad = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 512)
        if (has_suffix(e->d_name, ".cbr") || has_suffix(e->d_name, ".cbrt")) names[n++] = strdup(e->d_name);
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], cmp_str);
    for (int i = 0; i < n; i++) {
        char full[4096];
        snprintf(full, sizeof full, "%s/%s", p, names[i]);
        bad += verify_file(full, w);
        (*count)++;
        free(names[i]);
    }
    return bad;
}

int main(int argc, char **argv)
{
    World *w = malloc(sizeof *w);
    if (argc >= 3 && !strcmp(argv[1], "verify")) {
        int bad = 0, count = 0;
        for (int i = 2; i < argc; i++) bad += verify_path(argv[i], w, &count);
        free(w);
        if (count == 0) { printf("FAIL: no replays found\n"); return 1; }
        printf("%d replays, %d failed\n", count, bad);
        return bad ? 1 : 0;
    }
    if (argc == 4 && !strcmp(argv[1], "make-golden")) {
        int n = atoi(argv[3]);
        for (int s = 1; s <= n; s++) {
            Replay r;
            Bot b;
            replay_init(&r, (uint64_t)s);
            bot_init(&b, bot_pilot());
            sim_init(w, (uint64_t)s);
            while (w->status == ST_RUNNING && w->tick < SIM_HZ * 900) {
                uint8_t in = bot_input(&b, w);
                replay_push(&r, in);
                sim_step(w, in);
                sim_clear_events(w);
            }
            r.final_hash = sim_hash(w);
            r.status = (uint8_t)w->status;
            char path[4096];
            snprintf(path, sizeof path, "%s/seed%03d.cbrt", argv[2], s);
            if (replay_save_text(&r, path)) { printf("cannot write %s\n", path); return 1; }
            printf("%s: %s, %u ticks, hash %016llx\n", path, sim_status_text(w->status), r.n,
                   (unsigned long long)r.final_hash);
            replay_free(&r);
        }
        free(w);
        return 0;
    }
    free(w);
    if (argc == 4 && !strcmp(argv[1], "to-text")) {
        Replay r;
        if (replay_load(&r, argv[2])) { printf("cannot read %s\n", argv[2]); return 1; }
        int rc = replay_save_text(&r, argv[3]);
        replay_free(&r);
        if (rc) { printf("cannot write %s\n", argv[3]); return 1; }
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "info")) {
        Replay r;
        if (replay_load(&r, argv[2])) { printf("cannot read %s\n", argv[2]); return 1; }
        printf("version %u seed %llu ticks %u (%.1f s) outcome %s hash %016llx\n", r.version,
               (unsigned long long)r.seed, r.n, r.n / (double)SIM_HZ,
               sim_status_text((Status)r.status), (unsigned long long)r.final_hash);
        replay_free(&r);
        return 0;
    }
    fprintf(stderr, "usage:\n  %s verify DIR|FILE...\n  %s make-golden DIR N\n  %s info FILE\n  %s to-text IN OUT\n",
            argv[0], argv[0], argv[0], argv[0]);
    return 2;
}
