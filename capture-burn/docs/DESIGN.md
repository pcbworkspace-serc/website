# Capture Burn: design

## The decision

There is **one energy reserve (100)**, and everything draws on it:

| Use | Cost | Why you'd spend it |
|---|---|---|
| Thrust | 7 E/s, i.e. **10 wu/s of Δv per E** | Change course, dodge, brake into orbit |
| Laser | **0.8 E per shot** (1 energy unit into the target) | Shatter a rock instead of going around it; mine ore |
| Life support | **0.13 E/s, always** | Time costs energy, so crawling isn't free |

Everything else follows from physics, not special rules:

- **Shooting is a gamble.** A shattered rock's largest piece keeps half the
  mass; the rest flies apart faster (mass and momentum conserved). A small
  rock takes 1 shot, a 100 m one about 7, and a big one hundreds: shoot the
  pebbles, go around the boulders.
- **Speed is a trade-off.** Fast means less life support but costlier dodges,
  harder hits and a more expensive capture; slow means paying life support
  for longer. The sweep below measures it: the cost is lopsided, with fast
  punished hard and slow only mildly.
- **Ore is a detour.** Metal-rich (M-type) rocks shed iron-nickel shards
  worth 9 E per unit mass when you crater them. The ship's magnetic collector
  pulls those in; silicate shards you have to fly through.
- **The finish is a test of what you saved.** Touching the planet is a
  crash. You win when your orbit is **bound** with periapsis above the
  atmosphere and apoapsis inside the sphere of influence. Braking at
  periapsis (the Oberth effect) is the cheap way to get there.

## Physics and sources

| Model | Implementation | Source / basis |
|---|---|---|
| Motion | Newtonian, semi-implicit (symplectic) Euler at 120 Hz | Energy-bounded for orbits; drift measured below |
| Planet gravity | Patched conics: gravity only inside the sphere of influence | Standard mission-design approximation |
| Capture | Two-body elements from the state vector: bound, r_p > 1.15 R, r_a < 0.9 SOI | Vis-viva / eccentricity vector |
| Shattering threshold | Q\*_D(r) = Q0 [(r/r_t)^-0.36 + (r/r_t)^1.36] | Benz & Asphaug (1999), *Icarus* 142: strength + gravity regimes (basalt fit) |
| Fragment masses | Largest remnant = 50% (the definition of Q\*_D), 2–4 fragments, the rest dust | Catastrophic-disruption definition |
| Fragment speeds | Kinetic energy proportional to deposited energy; smaller pieces faster (m^-1/6) | Common fragmentation scaling |
| Cratering | Sub-catastrophic hit ejects mass proportional to E / Q\*_D(r), back toward the shooter | Crater ejecta leave the impact face |
| Size distribution | Cumulative N(>r) ∝ r^-1.8 | Shallow end of observed small-body slopes (about 1.8 to 2.5) |
| Composition | 75% C, 17% S, 8% M | Rough main-belt taxonomy mix (C about 75%, S about 17%, other about 8%) |
| Densities | C 1.3, S 2.7, M 4.0 g/cm³ | Bennu 1.19, Mathilde 1.3, Eros 2.67, Psyche about 3.8–4.1 |
| Meteoroid streams | Bands of pebbles sharing one velocity, crossing the route | Comet debris trails (e.g. Perseids from 109P/Swift–Tuttle) |
| Magnetic collector | Pulls only M-type shards; equal and opposite force on the ship | Iron-nickel is ferromagnetic; Newton's third law |
| Collisions | Impulse resolution; dissipated energy is deposited into rocks (can shatter them); hull damage ∝ impulse | Same Q\*_D rule as the laser |

**Determinism.** The simulation core uses only + − × ÷ and sqrt, which IEEE-754
rounds correctly. Sine, cosine, atan2, exp, log, pow and cbrt are polynomial
implementations in `detmath.c`, within about 1e-15 of libm. The core is
compiled with `-ffp-contract=off` (no FMA fusion), which matters because
GCC on aarch64 fuses by default. The random generator is PCG32, checked
against the reference outputs. CI checks that the core contains no libm calls.

## Scaling table (1 wu = 4 m)

| Quantity | Game | Real equivalent | Note |
|---|---|---|---|
| Rock radius | 5–80 wu | 20–320 m | Field rocks 6–80, stream pebbles 5–7.5 |
| Q\*_D minimum | r ≈ 25 wu | ≈ 100 m | Where the strength and gravity regimes cross |
| Ship cruise | ≈ 135 wu/s | ≈ 540 m/s | Real encounter speeds are km/s: compressed so you can react |
| Stream speed | 80–120 wu/s | 320–480 m/s | |
| Route | 26,000 wu | 104 km | |
| Planet | R = 380 wu, v_esc at SOI edge ≈ 95 wu/s | 1.5 km body | A small moon, not a planet: compressed so capture fits on one screen |
| Masses | m = ρ r³ × 0.004 | | Ship mass 20 (about a radius-8 S-type rock) |

## Tuning values

| Constant | Value | Constant | Value |
|---|---|---|---|
| Energy max | 100 | Hull | 100 |
| Thrust | 70 wu/s², 7 E/s | Turn rate | 3.4 rad/s |
| Laser | 0.8 E/shot, range 520, 5 shots/s | Laser energy deposited | 1.0 / shot |
| Life support | 0.13 E/s | Zero-energy limit | 15 s, then life support fails |
| Field rocks | 760, authored intensity curve | Streams | 3, width 220–300, spacing 56, speed 80–120 |
| Shard value per unit mass | C 1, S 2.5, M 9 | Magnet | radius 90, 260 wu/s² |
| Hull damage | 0.022 × impulse | Crater ejecta | 0.15 × E / Q\*_D |

## Verification

All numbers below were measured in this repository's build, with commands
anyone can re-run (see the README).

### Physics and determinism: `cb-test`, 1,614 checks, all pass on x86_64, aarch64 and armv7

| Check | Target | Measured |
|---|---|---|
| detmath vs libm (200k random inputs) | < 1e-13 | ≤ 2.0e-15 |
| Mass conserved: shattering / cratering (1,500 trials each) | < 1e-12 relative | 9.3e-16 / 2.2e-16 |
| Momentum conserved: shattering / cratering / collision | < 1e-9 relative | 1.8e-16 / 9.2e-17 / 1.9e-16 |
| Orbital energy drift over 10,000 steps (83 s) | < 1e-3 | 3.0e-7 |
| Orbital period vs Kepler's third law | < 0.5% | 2.1e-5 % |
| PCG32 vs reference outputs | exact | exact |
| Level generation (20 seeds): no overlaps, clear start, streams present | pass | pass |
| Same inputs give the same hash; one flipped input bit changes it; replay file round-trip | pass | pass |

### Bit-identical across machines: golden replays

Ten full runs (about 190 s each) were recorded on x86_64. Their final state
hashes reproduce exactly on:

| Platform | Compiler | SDL2 | Golden | Tests | Game binary |
|---|---|---|---|---|---|
| x86_64, Ubuntu 24.04 | GCC 13.3 | 2.30.0 | 10/10 | pass | runs |
| aarch64, Ubuntu 24.04 userland (qemu) | GCC 13.2 | 2.30.0 | 10/10 | pass | runs (software-renderer fallback) |
| armv7l hard-float, Ubuntu 22.04 userland (qemu) | GCC 11.2 | 2.0.20 | 10/10 | pass | runs |
| armv7, cross-compiled (GCC 13), qemu-user | GCC 13 | n/a | 2/2 checked | | |

Both ARM game builds rendered the same seed with the same world state at
t = 46 s. Raspberry Pi OS itself couldn't be fetched in the build sandbox.
Its Bookworm release ships SDL2 2.26.5, which lies between the two versions
tested. `install-pi.sh` re-runs the golden replays on the deck.

### No dominant strategy: `cb-bench`, seeds 1–300

| Autopilot | Wins | Main way it loses | Energy left on a win |
|---|---|---|---|
| Pilot (shoots, dodges, harvests; prices each choice in energy) | **90.7%** | life support (25) | 26.9 |
| Never shoots (dodge only) | **31.0%** | life support (194) | 10.4 |
| Shoots everything, never dodges | **26.7%** | hull (156) | 21.9 |
| No harvesting | 90.7% | life support (24) | 27.3 |

Criteria: pilot ≥ 85%, each pure strategy ≤ 40%, gap ≥ 45 points. Result:
**PASS** (59.7-point gap). Harvesting doesn't change the bot's win rate (see
the known limits).

**Winnability.** Every one of the 300 seeds is won by at least one
configuration of a small solver portfolio (cruise speed × pass side ×
harvesting on/off): 272 by the default pilot, 28 by an alternative.

### Speed trade-off (pilot, 60 seeds per speed)

| Cruise (wu/s) | 80 | 95 | 110 | 122 | 135 | 150 | 165 |
|---|---|---|---|---|---|---|---|
| Wins | 90.0% | **96.7%** | 93.3% | 90.0% | 91.7% | 83.3% | 70.0% |
| Run length on a win | 328 s | 274 s | 237 s | 214 s | 194 s | 175 s | 160 s |

The best speed is about 95–110 wu/s, which gives 4–4.5-minute runs. Above
that, losses climb fast; below it they rise slowly. (With 60 seeds each
figure carries about ±4 points of noise.) The reference pilot flies at 135,
so its 90.7% is not the ceiling.

### Performance

On x86_64 (this build machine, Xvfb with Mesa llvmpipe, 800×480), simulation
plus render takes p50 0.42 ms, p99 0.80 ms per frame with about 2,000 bodies,
against a 16.7 ms budget at 60 fps. The simulation alone takes 60–110 µs per
120 Hz tick. **Not measured on the deck yet**: run `capture-burn --bench 60`.

### Memory safety

- ASan + UBSan: tests and golden replays are clean.
- LeakSanitizer found one leak in `cb-replay`; it's fixed.
- valgrind: 0 errors, 0 leaks.

## What the autopilot taught the design

The bots weren't added afterwards to validate the design; they shaped it.
Each row below is a measured problem and the change it caused.

| Measured | Meaning | Change |
|---|---|---|
| Never-shoot bot won 100% with 37 E left | The laser was pointless, so dodging dominated | Added meteoroid streams: endless bands you can't fly around |
| Pilot spent more on shots than dodging saved | Shots were priced above the Δv they replaced | Laser cost 2.0 → 0.8 E; pilot compares shot cost against dodge cost |
| Slowing down before streams cost about 12 E each | Δv to shed speed and regain it is expensive | Removed (the bot was wrong, the physics was right) |
| Pilot coasted into big rocks | A collision course was penalised less than a dodge | Any collision course is now heavily penalised; turn time counted |
| Mining lost energy | Ejecta flew past a ship moving at 135 wu/s | Strafing runs on rocks dead ahead, plus the ferromagnetic collector |
| Harvesting is still about neutral for the bot | See results | Left as an option for humans; the playtest will show whether people use it |
| Lowering life support (0.22 → 0.13 E/s) to give the pilot headroom also made slow flight cheap | The best speed moved from about 135 to about 100 wu/s, and the slow side barely hurts | Kept for now; raise life support if playtesters crawl through the belt |

## Known limits

- **Frame times on the actual deck are not measured yet.** On x86 the CPU
  work is under 1 ms per frame (p99). Run `capture-burn --bench 60` on the
  deck. The ARM builds run correctly under emulation, but emulation says
  nothing about speed.
- **The human playtest has not been run.** See `docs/PLAYTEST.md`.
- For the autopilot, harvesting is neutral rather than a clear win.
