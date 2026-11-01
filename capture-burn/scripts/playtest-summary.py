#!/usr/bin/env python3
"""Summarise playtest telemetry written by the game (runs.csv).

Each launch of the game is one session (one player, per docs/PLAYTEST.md).
The facilitator starts nothing after the first run, so any second run in a
session was started unprompted.

    python3 scripts/playtest-summary.py [path/to/runs.csv]
"""
import csv
import os
import sys
from collections import OrderedDict

path = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    "~/.local/share/cyberdeck/capture-burn/runs.csv")
if not os.path.exists(path):
    sys.exit(f"no telemetry at {path} - play a run first")

sessions = OrderedDict()
with open(path, newline="") as f:
    for row in csv.DictReader(f):
        sessions.setdefault(row["session"], []).append(row)

print(f"{path}: {len(sessions)} session(s)\n")
print(f"{'session':>11} {'runs':>4} {'captured':>8} {'first loss (s)':>14} "
      f"{'1st outcome':>12} {'input':>8}  2nd run unprompted")
captured_any = second = 0
for sid, runs in sessions.items():
    runs.sort(key=lambda r: int(r["run"]))
    caps = sum(r["outcome"] == "captured" for r in runs)
    losses = [r for r in runs if r["outcome"] not in ("captured", "quit")]
    first_loss = f"{float(losses[0]['seconds']):.0f}" if losses else "-"
    more = len(runs) >= 2
    captured_any += caps > 0
    second += more
    inputs = sorted({r["input"] for r in runs})
    print(f"{sid:>11} {len(runs):>4} {caps:>8} {first_loss:>14} "
          f"{runs[0]['outcome']:>12} {'/'.join(inputs):>8}  {'yes' if more else 'no'}")

n = len(sessions)
print(f"\nplayers who captured at least once: {captured_any}/{n}")
print(f"players who started a second run unprompted: {second}/{n} "
      f"(target: at least 3 of 5) -> {'PASS' if n >= 5 and second >= 3 else 'not yet'}")

# Where does energy go, by outcome? (Do players discover the economy?)
rows = [r for rs in sessions.values() for r in rs]
for outcome in sorted({r["outcome"] for r in rows}):
    rs = [r for r in rows if r["outcome"] == outcome]
    avg = lambda k: sum(float(r[k]) for r in rs) / len(rs)
    print(f"  {outcome:>12} x{len(rs):<3} thrust {avg('e_thrust'):5.1f}  laser {avg('e_laser'):5.1f}"
          f"  life {avg('e_life'):5.1f}  harvested {avg('energy_gained'):5.1f}  shots {avg('shots'):5.1f}")
