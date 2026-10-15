## Bridge Commander multiplayer fix v1.4.0

Other players' ships now follow their real path in turns, and their nose points where their owner's
points.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.**

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.4.0.zip`** into the game folder.

### What is new

- **Turns (issue #11).** The game turns another player's ship only slowly towards the direction that
  player reports. In every turn its nose pointed 30–40° away from the owner's, the ship flew along
  that wrong nose and was pulled back sideways. bcmpfix now moves remote ships along the velocity
  measured from their received positions, including its turn, and exactly by it, so they no longer
  fly their own path. Their nose follows the reported direction. New INI options `VelocityTracking`,
  `TrackingArc` (default 1) and `TrackingLeadOffsetMs` (default −45, the latency compensation no longer
  needs the extra time for the old correction lag).

  | Mean position error / 95th percentile (game units) | v1.3.0 | v1.4.0 |
  |---|---|---|
  | 2 players, 60 ms: host ship / client ship | 0.36 / 0.45–0.53, 0.18–0.23 / 0.32–0.43 | 0.07 / 0.14–0.15, 0.05 / 0.09–0.10 |
  | 2 players, 150 ms: host ship / client ship | 0.30 / 0.51, 0.33 / 0.61 | 0.10 / 0.21, 0.06 / 0.13 |
  | 6 players, 60 ms: other clients' ships | 0.40 / 0.70 | 0.07–0.08 / 0.16 |
  | Lateral error in turns (0.3–0.6 rad/s), 6 players | 0.15–0.33 | 0.04–0.05 |
  | Angle between a remote ship's nose and its owner's (median) | 29–33° | 3–5° |

- **Crash fix.** Rarely every client crashed at the same moment (access violation) when a torpedo
  hit reported by the host was replayed on a ship that was just exploding: bcmpfix read that ship's
  shields, whose memory the game had already released. This bug was in every earlier version
  (seen in 2 of about 150 six-player test games). The shields are now only used for living ships.
- **Every confirmed torpedo hit shows (issue #15).** When the replay of a host-confirmed hit found
  no collision after three approaches, the torpedo was removed without an effect. Now the collision
  is run directly at the impact point. In six-player tests 0 replays were left without a hit (before:
  0–9 per game).
- **Diagnostics.** The test pilot logs each ship's nose; `tools/lateral.py` measures lateral and
  along-track errors by turn rate, the nose angle and the smoothness of remote tracks. A torpedo
  replay that ends without a hit is now logged with its target.

Release check with the exact v1.4.0 build (simulated network, 180 s each, mean position error / 95th
percentile in game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.15, 0.0 % | 0.05 / 0.09, 0.0 % | – | 8 of 8 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.05 / 0.09–0.10, 0.0 % | 0.05 / 0.10, 0.0 % | 0.08 / 0.16, 0.0 % | 93 of 93 and 48 of 48 seen by all five clients |
| 6 players, all hulls at 5 % after 40 s | 0.05 / 0.10, 0.0 % | 0.05 / 0.11, 0.0 % | 0.09 / 0.18, 0.0 % | 61 of 61 |

The same sources before the last robustness change (2 players at 150 ms, kick and join tests
included): 0.10 / 0.21 and 0.06 / 0.13 at 150 ms; peak error after a kick median 3.2 units; every
client saw every ship in two join tests. No hangs, no crashes, no script errors.

### Known open points

- Tested in one PC with simulated network conditions, up to six players; no dedicated server, no real
  internet games yet.
- What remains of the position error is mostly the latency itself at the start and end of maneuvers.
- Very short phaser hits that the shooter sees are often missing on the other screens (1–5 % of the
  phaser hits); damage is not affected.

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
