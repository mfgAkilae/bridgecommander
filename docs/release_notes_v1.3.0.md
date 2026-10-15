## Bridge Commander multiplayer fix v1.3.0

Ships pushed by hits no longer fall behind, a joining player no longer misses a ship, and torpedoes
fly the same path on every computer.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.**

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.3.0.zip`** into the game folder.

### What is new

- **Knockback (issue #19).** A torpedo hit or an explosion can push a ship sideways or backwards
  while its nose keeps pointing ahead. The game reports the speed of a ship but extrapolates it along
  its nose, so on the other screens the ship fell behind by tens of units for many seconds. bcmpfix
  now detects this and follows the velocity measured from the received positions until the ship is
  back on course. New INI option `KnockbackCorrection` (default 1).
- **Distant ships (issue #20).** The game sends fewer position updates for distant ships: a fifth
  beyond 228 units, one per second beyond 343 units. bcmpfix triples these distances. The host's
  upload grows to about 25 kB/s with six players. New INI option `UpdateDistanceScalePercent`
  (default 300, 100 = original).

  Test with a scripted kick (every player pushes its own ship sideways and back at 20 units/s every
  30 s, six players): peak position error after a kick, median 54 units with v1.2.1 and 4.7 with
  v1.3.0; the error stayed above 5 units for 11 s on average with v1.2.1 and for 0.6 s with v1.3.0.
- **Invisible ship after joining (issue #18, original game bug).** If a player spawned a ship while
  another player was still joining, the joining player could miss that ship for the whole game.
  The ship is now shown once the host sends it again during the join. New INI option
  `FixGhostShip` (default 1).
- **Torpedo paths (issues #8 and #9).** Torpedoes fired by another player are created on your
  computer from a fire message. The original swapped two of its flags (other players saw torpedoes
  leave the tube sideways and heard no launch sound), and it gave these torpedoes more turning power
  and a longer guidance time and re-aimed disruptor shots. They now keep the shooter's values. This
  matters on the host too, whose copy decides the hit. New INI options `FixTorpedoFireFlags` and
  `SymmetricTorpedoSpawn` (default 1).
- **Torpedo hits shown on all screens (issue #15).** The host could report a disruptor hit twice;
  clients then showed a second, wrong hull hit or found no torpedo for it. Each hit is now reported
  once. Six players: 108 of 109 and 64 of 67 host hits were seen by all five clients, and no replay
  was left without a torpedo (before: 32–89 per game).
- **Test tools.** `--kick` for the test pilot, `tools/knock.py` (errors after kicks),
  `tools/diverge.py` (divergence events with the trace) and `tools/torpstats.py` (replay statistics).

Release check with the exact v1.3.0 build (simulated network, mean position error / 95th percentile
in game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients |
|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.33 / 0.45, 0.0 % | 0.21 / 0.36, 0.2 % | – |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.31 / 0.53, 0.1 % | 0.31 / 0.59, 0.0 % | – |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.34 / 0.44, 0.0 % | 0.25 / 0.39, 0.0 % | 0.41 / 0.76, 0.1 % |

No hangs, no script errors. The kick test with the exact build: peak error median 5.3 units, at most
7.9; in three six-player join tests every client saw every ship (the ghost-ship fix fired in two).

### Known open points

- Tested in one PC with simulated network conditions, up to six players; no dedicated server, no real
  internet games yet.
- A lateral error of about 0.2–0.5 units remains during turns (issue #11).
- Very short phaser hits that the shooter sees are often missing on the other screens (1–5 % of the
  phaser hits); damage is not affected.

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
