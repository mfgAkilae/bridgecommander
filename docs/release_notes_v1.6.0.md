## Bridge Commander multiplayer fix v1.6.0

Phaser hits now follow the shooter: a beam hits, and does damage on the host, when it does on the
shooter's screen.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.** Players with v1.5.0 or older keep their
   own phaser decisions.

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.6.0.zip`** into the game folder.

### What is new

- **Phaser hit authority (issue #22).** The game sends only the start and stop of firing; each
  computer decided on its own which phaser banks could fire and when a beam reached its target. In
  six-player tests a bank the shooter fired did not fire on another screen in about 20 % of the
  cases, and a client's phaser beam did no damage on the host in 7–12 % of the beams. Now the
  shooter's computer reports which banks fire, which beams touch a ship and which of them hit; the
  other computers fire exactly these banks (without their own firing-arc check), keep them firing
  until the shooter stops, and show a hit when the shooter's beam hits. New INI option
  `PhaserHitAuthority` (default 1); the experimental `PhaserSync` of v1.5.0 only applies with
  `PhaserHitAuthority=0`.

  Release check, six players, 60 ms, three runs each, v1.5.0 → v1.6.0:

  | | v1.5.0 | v1.6.0 |
  |---|---|---|
  | bank firings of the shooter missing on another screen | 18.7–22.7 % | 5.2–6.3 % |
  | bank firings only on another screen | 154–258 | 0–5 |
  | beams the shooter hit and another screen did not show | 13.0–15.7 % | 3.3–4.7 % |
  | client's beams without damage on the host | 7.1–12.0 % | 0.0 % |
  | beams only other screens showed (on the host: with damage) | 44–70 (8–12) | 1–11 (0–1) |
  | host's phaser hits (0.5 s bins) also on a client | 91–97 % | 99–100 % |
  | beam start on another screen after the shooter's (median) | 271–279 ms | 223–224 ms |
- **Test tools.** `tools/phasers.py` also counts the beams that only other peers show, by peer and
  length.

Release check with the exact v1.6.0 build (simulated network, 180 s each, mean position error / 95th
percentile in game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.14, 0.0 % | 0.04 / 0.09, 0.0 % | – | 4 of 4 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.11 / 0.22, 0.0 % | 0.07 / 0.13, 0.0 % | – | 3 of 3 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.06–0.07 / 0.11–0.13, 0.0 % | 0.05 / 0.09–0.11, 0.0 % | 0.07 / 0.14–0.15, 0.0–0.1 % | 94 of 94, 89 of 90 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.11, 0.0 % | 0.05 / 0.09, 0.0 % | 0.07 / 0.14, 0.0 % | 61 of 61 |

Clients showed torpedo hits 65–71 ms before the host (median). Kick test: peak error after a kick
median 3.6 units, max 5.7; join test: every client saw every ship. Update from v1.5.0 with the
installer tested (a kept v1.5.0 `bcmpfix.ini` turns the phaser hit authority on). No hangs, no
crashes, no script errors.

### Known open points

- Tested in one PC with simulated network conditions, up to six players; no dedicated server, no real
  internet games yet.
- Phaser beams still start later on the other screens than on the shooter's (about 0.22 s with 60 ms
  latency); a client's beam reaches the other clients through the host.

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
