## Bridge Commander multiplayer fix v1.2.1

Remote ships no longer drift backwards after sharp turns.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new option is on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.**

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.2.1.zip`** into the game folder.

### What is new

- **Sharp turns (issue #17).** The game turns another player's ship on your screen only gradually
  towards the direction that player reports, and pushes it along its own nose. After a sharp turn at
  low speed the nose could point more than 90° away for seconds: the ship flew the wrong way and was
  pulled back, which looked like rubber-banding. bcmpfix now turns a ship whose nose lags more than
  30° behind the reported direction towards it (fully from 60°); smaller deviations are left to the
  game's own turning, which handles ordinary turns better. New INI option `OrientationCorrection`
  (default 1).

  Six players, 60 ms: the host's ship moved back against its direction of flight in 5.2–7.8 % of
  all 100 ms steps on the clients' screens with v1.2.0 and in 0.0–0.5 % with v1.2.1; its mean
  position error dropped from about 0.6 to 0.37 units.
- **Diagnostics.** `TraceStateSec` in `bcmpfix.ini` writes every received ship update and every
  position correction to `bcmpfix_trace.log` for the given number of seconds.

Release check with the exact v1.2.1 build (simulated network, 180 s each, mean position error in
game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients |
|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.34, 0.0 % | 0.19, 0.0 % | – |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.30, 0.1 % | 0.31, 0.0 % | – |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.39, 0.6 % | 0.23, 0.0 % | 0.39, 0.0 % |

No hangs, no script errors; torpedo hits seen by all five clients: 72 of 75.

Three other approaches were measured and are not enabled: always aligning ships to the reported
direction, predicting their turn, and extrapolating along the velocity estimated from received
positions. BC ships drift in turns (their velocity lags the nose by 6–23° on average), which the
game's own physics reproduces best. Details: `docs/measurements.md`.

### Known open points

- Tested in one PC with simulated network conditions, up to six players; no dedicated server, no real
  internet games yet.
- A lateral error of about 0.2–0.5 units remains during turns (issue #11).
- Rarely a client does not show a hit the host reported; damage is not affected.

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
