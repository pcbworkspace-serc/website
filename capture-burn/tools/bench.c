/* cb-bench: run autopilot variants over many seeds, headless.
 *
 *   cb-bench [--seeds N] [--start S] [--jobs J] [--bot NAME|all] [-v]
 *
 * NAME is pilot, no-shoot, no-dodge or no-harvest. Seeds are split across
 * J worker processes (fork); results are deterministic regardless of J.
 *   cb-bench --winnable [--seeds N] ...   portfolio solver per seed
 *
 * Exit status is 0 if the pass criteria hold (see docs/DESIGN.md):
 *   all:       pilot >= 85%, no-shoot <= 40%, no-dodge <= 40%, gap >= 45 pts
 *   winnable:  >= 99% of seeds won by at least one portfolio configuration
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "sim.h"
#include "bot.h"

#define MAX_TICKS (SIM_HZ * 900)

typedef struct {
    uint64_t seed;
    uint8_t status;
    uint32_t ticks;
    float energy, hull, gained, laser, thrust;
    uint32_t shots, broken, hits;
} Result;

static BotConfig config_named(const char *n)
{
    if (!strcmp(n, "no-shoot")) return bot_no_shoot();
    if (!strcmp(n, "no-dodge")) return bot_no_dodge();
    if (!strcmp(n, "no-harvest")) return bot_no_harvest();
    return bot_pilot();
}

static Result run_one(World *w, uint64_t seed, BotConfig cfg)
{
    Bot b;
    bot_init(&b, cfg);
    sim_init(w, seed);
    while (w->status == ST_RUNNING && w->tick < MAX_TICKS) {
        sim_step(w, bot_input(&b, w));
        sim_clear_events(w);
    }
    Result r;
    memset(&r, 0, sizeof r);
    r.seed = seed;
    r.status = (uint8_t)(w->status == ST_RUNNING ? ST_LOST_ADRIFT : w->status);
    r.ticks = w->tick;
    r.energy = (float)w->ship.energy;
    r.hull = (float)w->ship.hull;
    r.gained = (float)w->stats.e_gained;
    r.laser = (float)w->stats.e_laser;
    r.thrust = (float)w->stats.e_thrust;
    r.shots = w->stats.shots;
    r.broken = w->stats.rocks_broken;
    r.hits = w->stats.collisions;
    return r;
}

static int run_bot(const char *name, uint64_t start, int nseeds, int jobs, int verbose, double *win_rate)
{
    BotConfig cfg = config_named(name);
    Result *res = calloc((size_t)nseeds, sizeof *res);
    int (*pipes)[2] = calloc((size_t)jobs, sizeof *pipes);
    pid_t *pids = calloc((size_t)jobs, sizeof *pids);
    for (int j = 0; j < jobs; j++) {
        if (pipe(pipes[j])) { perror("pipe"); return 2; }
        pids[j] = fork();
        if (pids[j] == 0) {
            close(pipes[j][0]);
            World *w = malloc(sizeof *w);
            for (int i = j; i < nseeds; i += jobs) {
                Result r = run_one(w, start + (uint64_t)i, cfg);
                if (write(pipes[j][1], &r, sizeof r) != (ssize_t)sizeof r) _exit(3);
            }
            _exit(0);
        }
        close(pipes[j][1]);
    }
    for (int j = 0; j < jobs; j++) {
        for (int i = j; i < nseeds; i += jobs) {
            size_t got = 0;
            while (got < sizeof(Result)) {
                ssize_t k = read(pipes[j][0], (char *)&res[i] + got, sizeof(Result) - got);
                if (k <= 0) { fprintf(stderr, "worker %d died\n", j); return 2; }
                got += (size_t)k;
            }
        }
        close(pipes[j][0]);
        waitpid(pids[j], 0, 0);
    }

    int count[8] = {0};
    double t_sum = 0, e_sum = 0, g_sum = 0, l_sum = 0, th_sum = 0, h_sum = 0;
    double shots = 0, broken = 0, hits = 0;
    int wins = 0;
    for (int i = 0; i < nseeds; i++) {
        Result *r = &res[i];
        count[r->status]++;
        if (r->status == ST_WON) {
            wins++;
            t_sum += r->ticks / (double)SIM_HZ;
            e_sum += r->energy;
            h_sum += r->hull;
        }
        g_sum += r->gained;
        l_sum += r->laser;
        th_sum += r->thrust;
        shots += r->shots;
        broken += r->broken;
        hits += r->hits;
        if (verbose)
            printf("  %-10s seed %-8llu %-24s t=%6.1fs E=%5.1f hull=%5.1f shots=%4u broke=%3u gained=%5.1f\n",
                   name, (unsigned long long)r->seed, sim_status_text((Status)r->status),
                   r->ticks / (double)SIM_HZ, r->energy, r->hull, r->shots, r->broken, r->gained);
    }
    double wr = 100.0 * wins / nseeds;
    *win_rate = wr;
    printf("%-11s win %5.1f%%  | hull %3d crash %3d passed %3d range %3d adrift %3d"
           " | win: t %5.1fs E %4.1f hull %4.1f | per run: shots %5.1f broke %4.1f"
           " hits %4.1f laserE %4.1f thrustE %4.1f gainedE %4.1f\n",
           name, wr, count[ST_LOST_HULL], count[ST_LOST_CRASH], count[ST_LOST_PASSED],
           count[ST_LOST_RANGE], count[ST_LOST_ADRIFT],
           wins ? t_sum / wins : 0, wins ? e_sum / wins : 0, wins ? h_sum / wins : 0,
           shots / nseeds, broken / nseeds, hits / nseeds, l_sum / nseeds, th_sum / nseeds, g_sum / nseeds);
    fflush(stdout);
    free(res);
    free(pipes);
    free(pids);
    return 0;
}

/* Winnability: a seed is proven winnable if any configuration in a small
 * portfolio (cruise speed x pass side x harvest on/off) captures. */
static int run_winnable(uint64_t start, int nseeds, int jobs, double *rate)
{
    static const double cruise[] = {135, 122, 150, 110};
    int (*pipes)[2] = calloc((size_t)jobs, sizeof *pipes);
    pid_t *pids = calloc((size_t)jobs, sizeof *pids);
    for (int j = 0; j < jobs; j++) {
        if (pipe(pipes[j])) return 2;
        pids[j] = fork();
        if (pids[j] == 0) {
            close(pipes[j][0]);
            World *w = malloc(sizeof *w);
            for (int i = j; i < nseeds; i += jobs) {
                int which = -1, k = 0;
                for (int ci = 0; ci < 4 && which < 0; ci++)
                    for (int side = 1; side >= -1 && which < 0; side -= 2)
                        for (int hv = 1; hv >= 0 && which < 0; hv--, k++) {
                            BotConfig cfg = bot_pilot();
                            cfg.cruise = cruise[ci];
                            cfg.side = side;
                            cfg.allow_harvest = hv;
                            Result r = run_one(w, start + (uint64_t)i, cfg);
                            if (r.status == ST_WON) which = k;
                        }
                if (write(pipes[j][1], &which, sizeof which) != (ssize_t)sizeof which) _exit(3);
            }
            _exit(0);
        }
        close(pipes[j][1]);
    }
    int ok = 0, hist[17] = {0};
    for (int j = 0; j < jobs; j++) {
        for (int i = j; i < nseeds; i += jobs) {
            int which;
            if (read(pipes[j][0], &which, sizeof which) != (ssize_t)sizeof which) return 2;
            if (which >= 0) { ok++; hist[which]++; }
            else printf("  seed %llu: no configuration won\n", (unsigned long long)(start + (uint64_t)i));
        }
        close(pipes[j][0]);
        waitpid(pids[j], 0, 0);
    }
    *rate = 100.0 * ok / nseeds;
    printf("winnable    %5.1f%% of seeds (won by config #: ", *rate);
    for (int k = 0; k < 16; k++) if (hist[k]) printf("%d:%d ", k, hist[k]);
    printf(")\n");
    free(pipes);
    free(pids);
    return 0;
}

int main(int argc, char **argv)
{
    int nseeds = 200, jobs = 4, verbose = 0;
    uint64_t start = 1;
    const char *bot = "all";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seeds") && i + 1 < argc) nseeds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = strtoull(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--jobs") && i + 1 < argc) jobs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bot") && i + 1 < argc) bot = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--winnable")) bot = "winnable";
        else {
            fprintf(stderr, "usage: %s [--seeds N] [--start S] [--jobs J] [--bot pilot|no-shoot|no-dodge|no-harvest|all] [--winnable] [-v]\n", argv[0]);
            return 2;
        }
    }
    if (jobs < 1) jobs = 1;
    if (jobs > nseeds) jobs = nseeds;
    printf("seeds %llu..%llu, %d jobs\n", (unsigned long long)start,
           (unsigned long long)(start + (uint64_t)nseeds - 1), jobs);
    if (!strcmp(bot, "winnable")) {
        double r;
        if (run_winnable(start, nseeds, jobs, &r)) return 2;
        return r >= 99.0 ? 0 : 1;
    }
    const char *names[] = {"pilot", "no-shoot", "no-dodge", "no-harvest"};
    double wr[4] = {-1, -1, -1, -1};
    for (int k = 0; k < 4; k++) {
        if (strcmp(bot, "all") && strcmp(bot, names[k])) continue;
        if (run_bot(names[k], start, nseeds, jobs, verbose, &wr[k])) return 2;
    }
    if (!strcmp(bot, "all")) {
        /* No dominant pure strategy: the mixed pilot must beat both
         * single-tactic ablations by a wide margin. */
        double worst = wr[1] > wr[2] ? wr[1] : wr[2];
        int ok = wr[0] >= 85.0 && wr[1] <= 40.0 && wr[2] <= 40.0 && wr[0] - worst >= 45.0;
        printf("criteria (pilot >= 85%%, no-shoot <= 40%%, no-dodge <= 40%%, gap >= 45 pts): %s\n",
               ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    return 0;
}
