## Bridge Commander multiplayer fix v1.7.1

Two fixes for big and long games: dedicated servers no longer lose the first player's ships, and
long games with many respawns keep their sync.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.** v1.7.1 works together with v1.7.0.

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.7.1.zip`** into the game folder.

### What is new

- **Dedicated server: invisible ships (issue #24, a bug of the original game).** Every machine
  numbers the objects it creates from the range of its player slot. A dedicated server has no slot
  and used slot 0, the first player's range, for its own objects (the copies of everybody's
  torpedoes). When that player respawned with an ID the server was using, the server refused the new
  ship: nobody saw it, and that player got no more position updates of the other ships. A dedicated
  server now uses slot 15, which no player gets (the game allows at most 8 players). New INI option
  `FixDedicatedObjectIds` (default 1).
- **Long games (issue #25).** bcmpfix keeps a few small tables per remote ship (phaser reports,
  latency, velocity and orientation tracking). Every respawn creates a new ship; once a table was
  full, a new ship could take a live ship's entry, which then lost its state again and again. Now
  the entry unused longest is reused.
- **Tests with ten players and mixed versions (issues #13, #26).** A dedicated server with ten
  clients, and six-player games mixing v1.7.0, v1.6.0 and the original game, all in Windows Sandbox:
  no errors in any combination. Phasers are synchronised only among players with the same version as
  the host; positions and torpedoes also work with v1.6.0 players. Details: `docs/measurements.md`.
- **Test tools.** The test harness runs each instance with its own version (`--variant`), as a
  dedicated server (`--dedicated`) and with more than eight players (`--max-players`);
  `tools/mixed.py` reports the sync by the versions involved.

Release check with the exact v1.7.1 build (simulated network, 180 s each, mean position error / 95th
percentile in game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.14, 0.0 % | 0.04 / 0.09, 0.0 % | – | 4 of 4 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.20, 0.0 % | 0.07 / 0.15, 0.0 % | – | 4 of 4 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.06–0.07 / 0.12–0.14, 0.0 % | 0.04–0.05 / 0.09–0.11, 0.0 % | 0.06–0.07 / 0.13–0.15, 0.0 % | 56 of 56, 87 of 89 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.13, 0.0 % | 0.05 / 0.10, 0.0 % | 0.07 / 0.14, 0.0 % | 68 of 68 |
| dedicated server + 10 clients, 60 ms | – | 0.09 / 0.18, 0.2 % | 0.13 / 0.27, 0.3 % | 145 of 153 |

Phaser beams the shooter hit were shown by the other players in 99.6–100 % (six players). In a game
with a v1.7.1 host, v1.7.0 and v1.7.1 clients and one player without the mod, all v1.7.x players
were synchronised with each other (phasers 100 %, torpedoes 100 %, positions 0.06–0.08). Clients
showed torpedo hits 62–72 ms before the host (median). Kick test: peak error after a kick median 3.4
units, max 6.4; join test: every client saw every ship. Update from v1.7.0 with the installer tested
(`FixDedicatedObjectIds` is on with a kept `bcmpfix.ini`). No crashes, no script errors.

In one of two dedicated-server runs of the release check one client instance stopped drawing frames
for up to 47 s (the sandbox runs eleven games at once); a stall of several seconds also happened
with v1.7.0 in a comparison run. While it lasts, the game's main thread is busy in the game's own
code (event and object processing), not waiting for the graphics card (#27).

### Known open points

- Tested in one PC with simulated network conditions, up to eleven players; no real internet games
  yet (#26). The direct phaser reports ran on loopback addresses only.
- A phaser beam lasts about 50 ms longer on the other screens than on the shooter's.
- In big sandbox games a client instance can stall for seconds (also with v1.7.0, #27).

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
