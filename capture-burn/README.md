# Capture Burn

A small, deep 2D space game for the team's cyberdeck (Raspberry Pi Model B,
7" touchscreen, built-in keyboard, PiSugar battery). One ship, one laser,
one journey across an asteroid belt, one planet at the end.

**The decision.** One energy reserve powers the thrusters, the laser and
life support. Shooting a rock costs energy and splits it into faster pieces;
dodging costs thrust; going slowly costs life support; metal shards refuel
you, but chasing them costs time. You don't win by touching the planet: you
win by braking into a **bound orbit** (periapsis above the atmosphere,
apoapsis inside the planet's sphere of influence). That brake has to come
out of whatever energy you saved on the way.

See [`docs/DESIGN.md`](docs/DESIGN.md) for the physics, its sources, the scaling
table and the verification results.

## Install on the deck

```bash
git clone <this repo> && cd website/capture-burn
sh scripts/install-pi.sh
```

This installs `cmake`, `pkg-config` and `libsdl2-dev` if they're missing,
then builds, **runs the physics tests and golden replays on the deck itself**,
and installs to `~/.local` with a Games-menu launcher. Works on Raspberry Pi OS
Bookworm or Trixie, 32- or 64-bit. Add `--autostart` to launch at login.

Then measure this deck (the result is saved to the data folder):

```bash
capture-burn --bench 60
```

Without a desktop session, run it from a console: `SDL_VIDEODRIVER=kmsdrm capture-burn`.

## Controls

| Action | Keyboard | Touch | Gamepad |
|---|---|---|---|
| Turn | ← → or A D | ◀ ▶ pads | stick / d-pad |
| Thrust | ↑ or W | THRUST | A / RT |
| Fire laser | Space, J, Ctrl | FIRE | X / RB |
| Brake-orient (turn to retrograde) | ↓ or S | BRAKE | B / LB |
| Pause | Esc, P | tap the route bar | Start |

The title screen has: **N** new seed, **D** watch the autopilot, **M** mute,
**F11** fullscreen, **F12** screenshot. Red outlines mark rocks on a
collision course; the pips above them show how many laser shots shatter them.
Near the planet the predicted path is green once you're captured.

**Seeds.** The default seed is today's date, so every deck flies the same belt
each day. The end screen prints a one-line result
(`SHARE: SEED 20261101 CAPTURED E61.3 T191`) that fits in a Meshtastic text
message over the T114. Replays are verifiable: anyone can re-run one and
check its final hash.

**Battery.** `--fps 30` halves render work. The game also idles while paused
or minimised.

## Files

```
src/core/   deterministic simulation (C11, no SDL, no libm): sim, detmath, rng, replay, bot
src/game/   SDL2 front end: main loop, renderer, 5x7 font, synthesized audio
tests/      physics & determinism tests (cb-test)
tools/      cb-bench (autopilot ablations, winnability), cb-replay (golden replays)
golden/     10 replays (hex text) recorded on x86_64 that must reproduce bit-for-bit everywhere
scripts/    install-pi.sh, playtest-summary.py
docs/       DESIGN.md, PLAYTEST.md
```

Player data is in `~/.local/share/cyberdeck/capture-burn/`: `runs.csv`
(telemetry for each run), `replays/*.cbr`, `bench-*.txt`, `shot-*.bmp`.

## Verify (any Linux machine)

```bash
cmake -S . -B build && cmake --build build -j
./build/cb-test                      # physics invariants + determinism
./build/cb-replay verify golden      # bit-identical golden replays
./build/cb-bench --seeds 300         # no dominant strategy (autopilot ablations)
./build/cb-bench --winnable --seeds 300
python3 scripts/playtest-summary.py  # after a playtest (docs/PLAYTEST.md)
```

CI (`.github/workflows/capture-burn.yml`) runs all of this on x86_64. It also
cross-compiles for aarch64 and armv7 and replays the golden runs under qemu.
