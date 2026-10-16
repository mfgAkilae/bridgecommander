## Bridge Commander multiplayer fix v1.7.4

Torpedoes near the starbase, phaser banks that kept an old target, respawned ships, a crash guard,
and the findings of two code reviews.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.** v1.7.4 works together with v1.7.0 to
   v1.7.3.

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.7.4.zip`** into the game folder.

### What is new

- **Torpedoes near the starbase (issue #31).** Every computer flies its own copy of a torpedo. Near
  the starbase a client's copy could touch the starbase while the host's copy flew on and hit an
  attacker: the client showed a starbase hit the host never had, and could not replay the host's
  real hit. Now a client shows a hit at once only on the ship the torpedo was fired at, and replays
  a host's hit only on the ship the host reported. Six players in the starbase mission: torpedo
  hits seen by all clients 87.4 % → 96.8 %, starbase hits only a client showed 503 → 1.
- **Phaser banks on an old target (issue #30).** When a bank's target leaves its arc or range, the
  shooter's weapon system moves the bank to its next target without stopping it. The other
  computers kept the old target and could fire at a ship the shooter did not fire at, which looked
  like hits missing at the edge of the range. Now every bank the owner fires aims at the owner's
  target and point. Five players against five AI ships: AI beams missing on another screen
  0.46 % → 0.02 %.
- **Respawned ships driven off.** A respawned ship could take over the measured movement and turn
  of a ship that had died before (the game reused its memory) and was driven along it for a moment,
  in tests up to 95 units off. Now every new ship starts without history.
- **Crash guard (`FixRestartCrashes`).** In the tests clients crashed when the test pilot created
  its new ship in the frame in which the end of a starbase game deleted the ships, also with the
  original game: the weapons display and a mission event handler read through pointers that were
  already empty. These reads are now skipped. A player who picks a ship in the menu is not known to
  reach that frame; restarts through the end screen ran without crashes, also without bcmpfix.
  (Corrected after the release: the first version of these notes called it a crash at every restart.)
- **Code reviews.** Two independent reviews of the less tested parts found, among others: the
  direct phaser channel took a "hello" from any address and kept its peers into the next game, a
  host with `TorpedoHitAuthority=0` still made its clients wait for hit reports, the script's
  clock wrapped after 24.8 days of Windows uptime, and the installer could leave a mix of versions
  when an update failed. All are fixed; the list is in `docs/measurements.md`.

### Release check

Exact v1.7.4 build (simulated network, 180–300 s each, mean position error / 95th percentile in
game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.13, 0.0 % | 0.05 / 0.10, 0.0 % | – | 5 of 5 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.21, 0.0 % | 0.07 / 0.16, 0.0 % | – | 6 of 6 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.10, 0.0 % | 0.05 / 0.09, 0.0 % | 0.07 / 0.15, 0.0 % | 82 of 82 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.10, 0.0 % | 0.04 / 0.09, 0.0 % | 0.07 / 0.15, 0.0 % | 80 of 80 |
| 5 players against 5 AI ships | 0.04 / 0.11, 0.1 % (with the AI ships) | 0.05 / 0.10, 0.0 % | 0.07 / 0.14, 0.0 % | 274 of 278 |
| dedicated server + 10 clients | – | 0.06 / 0.14, 0.0 % | 0.08 / 0.19, 0.1 % | 151 of 152 |
| starbase mission, 6 players | 0.02 / 0.09, 0.0 % (with the starbase) | 0.04 / 0.09, 0.0 % | 0.07 / 0.14, 0.1 % | 193 of 193 |

Phaser beams shown by the other players 99.6–100 %; in the AI run all 2724 of the AI ships' beams
(v1.7.3: 96.0 %). Mixed game with a v1.7.4 host, v1.7.3, v1.7.2 and v1.7.4 clients: phaser beams and
torpedo hits 97.7–100 % among them. Starbase mission with forced damage: no crash; three restarts
per run through the end screen (corrected test pilot): no crash. Kick test: peak error median 3.5,
max 5.6; join test: every client saw every ship. Installer update from v1.7.3, fresh install over
another mod's `dinput.dll` with uninstall, and an update with a locked file ("Nothing was changed",
v1.7.3 kept) tested. No crashes, no script errors.

### Known open points

- Tested in one PC with simulated network conditions, up to eleven players; no real internet games
  yet (#26). The direct phaser reports ran on loopback addresses only.

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
