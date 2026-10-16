## Bridge Commander multiplayer fix v1.7.2

Fixes the freezes in big games: the player who joined first could freeze for seconds, up to a
minute.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.** v1.7.2 works together with v1.7.0 and
   v1.7.1; the freeze fix works on each player's own computer.

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.7.2.zip`** into the game folder.

### What is new

- **Freezes in big games (issue #27, a bug of the original game).** The combat music watches the
  enemy ships with a range condition (`Conditions.ConditionInRange`). Whenever the group of enemy
  ships changed, the condition registered its event handlers again without removing the old ones,
  and the handlers it had already added registered more: their number doubled with every join, death
  and respawn. Every object the game deletes (torpedoes, debris) has to be looked up in all handlers,
  so with ten players the player who joined first (about 100000 handlers after three minutes) froze
  for up to a minute. The script layer now registers the handlers once (`FixConditionInRange`,
  default 1).

  Dedicated server with ten clients, three runs each: v1.7.1 froze for 37 s in one run; v1.7.2 never
  dropped below 50 frames per second on the first client, and its handlers stayed at about 400.
- **Test tools.** The test build samples the game when a frame takes longer than 2 s and counts the
  event handlers (test mode only); `tools/stackdecode.py` decodes the samples.

Release check with the exact v1.7.2 build (simulated network, 180 s each, mean position error / 95th
percentile in game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.13, 0.0 % | 0.04 / 0.09, 0.1 % | – | 9 of 9 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.20, 0.0 % | 0.07 / 0.15, 0.0 % | – | 5 of 5 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.10, 0.0 % | 0.04 / 0.08–0.09, 0.0–0.2 % | 0.06–0.07 / 0.14, 0.0–0.2 % | 82 of 83, 45 of 45 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.11, 0.0 % | 0.04 / 0.09, 0.0 % | 0.06 / 0.13, 0.0 % | 85 of 85 |
| dedicated server + 10 clients, 60 ms | – | 0.13 / 0.37, 0.0 % | 0.16 / 0.44, 0.2 % | 171 of 173 |

No instance stalled in the two dedicated runs (longest gap between frames of the first client
0.2 s); the second one and the mixed game ran while other programs loaded the PC (about 20 instead
of 65 frames per second, positions 0.27 / 0.55, 112 of 115 torpedo hits seen by all). Phaser beams
the shooter hit were shown by the other players in 99.4–100 %. Mixed game with a v1.7.2 host, v1.7.1,
v1.7.0 and v1.7.2 clients and one player without the mod: phasers 98.7–100 %, torpedo hits 97.8–100 %
among the v1.7.x players. Clients showed torpedo hits 63–66 ms before the host (median). Kick test:
peak error after a kick median 3.3–3.4 units, max 5.2 in two runs (a first run, in which the ships
drifted up to 2300 units apart and got fewer updates, had a median of 3.6 and a maximum of 59); join
test: every client saw every ship. Update from v1.7.1 with the installer tested
(`FixConditionInRange` is on with a kept `bcmpfix.ini`). No crashes, no script errors.

### Known open points

- Tested in one PC with simulated network conditions, up to eleven players; no real internet games
  yet (#26). The direct phaser reports ran on loopback addresses only.
- A phaser beam lasts about 50 ms longer on the other screens than on the shooter's.

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
