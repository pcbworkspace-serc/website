# Playtest protocol (5 players, on the deck)

The bots can show that every seed is winnable and that no single tactic
dominates. They can't show that a person *notices* the decisions. Only
watching people can.

## Setup
- Deck charged, game installed (`scripts/install-pi.sh`), sound on.
- Note today's seed on the title screen. All five players use the same one.
- **Relaunch the game for each player.** Each launch is a new session in
  `runs.csv`, which is how the summary script separates players.
- Record the screen and voice (a phone on a stand is fine) or take notes.

## Script (read it, don't improvise)
> "This is a game about flying a ship to a planet. I can't help while you
> play. Please think out loud. Play as long as you like, and tell me when
> you're done."

Then press nothing and say nothing. Don't explain the controls: the title
screen does that, and seeing whether it's enough is part of the test. If
the player asks a question, answer "What do you think?"

## Watch for (write down the time)
- First time they shoot, first time they brake, first time they see a stream.
- Do they ever choose *not* to shoot something on purpose?
- Do they find the capture rule (green path), or fly past the planet?
- Touch or keyboard? Do they find the touch pads without help?

## After (3 questions, write the answers down word for word)
1. "Tell me about a moment where you had to choose between two things."
2. "What made you lose / what made you win?"
3. "What would you change?"

## Data
```bash
python3 scripts/playtest-summary.py
```
This prints runs per player, captures, time to first loss, input method,
whether they started a second run unprompted, and where energy went.
Replays of every run are in `~/.local/share/cyberdeck/capture-burn/replays/`.
Watch one with `capture-burn --replay FILE`.

## Pass criteria
| Criterion | Target | Result |
|---|---|---|
| Each player names a real decision (shoot vs dodge, mine vs move on, when to brake) | 5 of 5 | |
| Player starts a second run without being asked | at least 3 of 5 | |
| Player discovers braking into orbit without help | note only | |

| Player | Runs | Captured | Decision they named (verbatim) | Second run? | Notes |
|---|---|---|---|---|---|
| 1 | | | | | |
| 2 | | | | | |
| 3 | | | | | |
| 4 | | | | | |
| 5 | | | | | |
