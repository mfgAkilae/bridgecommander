## Bridge Commander multiplayer fix v1.7.3

Synchronises the phasers of AI ships, places newly appearing ships correctly, and was tested in a
30-minute game with ten players.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.** v1.7.3 works together with v1.7.0 to
   v1.7.2; the AI ships' phasers are synchronised when the host runs v1.7.3.

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.7.3.zip`** into the game folder.

### What is new

- **Phasers of AI ships (issue #28).** The host reported only its own ship's phaser banks, so every
  computer decided the firing of the AI ships the host flies by itself. Now the host reports their
  banks too. Five players against five AI ships: beams of the AI ships shown on the other screens
  91.7 % → 100 %, the same banks firing 70.1 % → 99.4 %. This also concerns AI ships of mods and
  the starbase of the starbase mission (tested only with AI ships the test pilot creates).
- **Ships sliding into place (issue #29).** A ship that appears on your screen (a player joins or
  respawns, an AI ship is created) started at the position in the game's "object created" message,
  which can be seconds old, and slid to its real position for up to a second (in the 30-minute test
  up to 110 units). Now it is placed at the first position received for it (`SnapNewShips`,
  default 1): 0.25 s after it appears at most 1.5 units off, before up to 10.7.
- **Phaser beam length.** v1.7.0 reported that a beam lasted about 50 ms longer on the other screens.
  That was the test pilot's polling: measured on the frame, beams last as long on every screen
  (+1 ms median). No change was needed.
- **Test tools.** AI enemies in the test bed (`--ai-ships`, `--target ai`), frame-exact phaser
  telemetry, `tools/spawn.py`.

### 30-minute test

Dedicated server, ten clients and two AI ships, 60 ms ±20 ms, 1 % loss, every 300 s all hulls set to
5 % (65 deaths). No stalls, crashes or script errors; memory and event handlers stayed flat; 68–71
frames per second (median) on the clients. Positions 0.05–0.10 units off (mean), torpedo hits seen
by all ten clients 1566 of 1576, phaser beams shown on the other screens 99.8 % for players and AI
ships.

Release check with the exact v1.7.3 build (simulated network, 180–240 s each, mean position error /
95th percentile in game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.13, 0.0 % | 0.04 / 0.09, 0.1 % | – | 4 of 4 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.11 / 0.22, 0.0 % | 0.07 / 0.15, 0.1 % | – | 4 of 4 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.14, 0.1 % | 0.05 / 0.11, 0.0 % | 0.08 / 0.18, 0.1 % | 73 of 73 |
| 6 players, all hulls at 5 % after 40 s | 0.05 / 0.10, 0.0 % | 0.04 / 0.09, 0.0 % | 0.07 / 0.15, 0.0 % | 58 of 58 |
| 5 players against 5 AI ships | 0.05 / 0.11, 0.0 % (with the AI ships) | 0.04 / 0.09, 0.0 % | 0.07 / 0.14, 0.0 % | 279 of 283 |
| dedicated server + 10 clients | – | 0.08 / 0.17, 0.0 % | 0.10 / 0.23, 0.0 % | 140 of 142 |

Phaser beams the shooter hit were shown by the other players in 99.6–100 % (AI ships: 96.0 %, see
below). Mixed game with a v1.7.3 host, v1.7.2, v1.7.0 and v1.7.3 clients and one player without the
mod: phasers and torpedo hits 100 % among the v1.7.x players. Kick test: peak error after a kick
median 3.4 units, max 5.4; join test: every client saw every ship. Update from v1.7.2 with the
installer tested. No crashes, no script errors.

### Known open points

- Tested in one PC with simulated network conditions, up to eleven players; no real internet games
  yet (#26). The direct phaser reports ran on loopback addresses only.
- At the edge of the phaser range (here 162–175 units) a beam can hit on the host and show no hits
  on the other screens, which still show the beam itself (#30). The damage is the host's.

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
