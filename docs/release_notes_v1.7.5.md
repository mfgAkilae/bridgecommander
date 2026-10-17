## Bridge Commander multiplayer fix v1.7.5

Ramming AI ships now damages them, ramming was tested between players, and the measurements of the
original game were repeated with every fix switched off.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.** v1.7.5 works together with v1.7.0 to
   v1.7.4.

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.7.5.zip`** into the game folder.

### What is new

- **Ramming AI ships (`FixRammingDamage`).** In multiplayer only the host applies collision damage,
  from the collisions each player's computer reports for its own ship. When a client rammed an AI
  ship, the report for the AI ship lost its sender on the way and the host dropped it: the client
  took damage, the AI ship none (0 of 37 contacts in the tests). Now both ships take damage (21 of
  21, also on a dedicated server). The host's own checks of the report are unchanged. The starbase
  of the starbase mission stays immune to collisions, by the mission's design.
- **Ramming between players, measured.** Six players ramming each other, no weapons: with the
  original game both ships took damage in 3 of 10 contacts (2 contacts did nothing at all), and the
  ships on the other screens were 8–13 units off after the impact. With bcmpfix both ships take
  damage in 21 of 24 contacts, and the other screens are 0.3–1.5 units off. A graze that only one
  of the two players saw still damages only that player's ship; that is how the game reports
  collisions.
- **Runaway velocity of a network copy.** In one test a client's ship touched the starbase on the
  host's screen; the host's copy of it, held against the starbase by the position correction, got a
  physics velocity of up to 149 000 units/s. Its position stayed right, but a later collision of
  that copy had a broken force and destroyed the host's ship. A copy whose velocity runs away like
  this now has it reset (seen once in about 40 starbase-mission runs).
- **Measurements of the original game repeated.** The test bed's "original game" mode had left
  three fixes on since v1.3.0. With every fix off, the original game with a dedicated server and
  ten clients showed 28 % of the torpedo hits on all clients (the earlier figure was 51 %) and
  positions 5.7 units off (6.2). The README and `docs/measurements.md` have the new figures.
- **Correction to v1.7.4.** The notes of v1.7.4 said clients crashed whenever the starbase mission
  restarted. They crashed only when the test pilot created a ship in the very frame in which the
  end of a game deleted the ships; restarts through the end screen run without crashes, also
  without bcmpfix. `FixRestartCrashes` stays as a guard. The test pilot now waits for the end
  screen and restarts like a player.
- **Test tools.** Ramming test (`mptest.py --ram`, `--ram-nofire`, `tools/rams.py`), collision
  telemetry, a test pilot that restarts after the end of a game.

### Release check

Exact v1.7.5 build (simulated network, 45–300 s each, mean position error / 95th percentile in
game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.13, 0.0 % | 0.04 / 0.09, 0.1 % | – | 12 of 12 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.21, 0.0 % | 0.06 / 0.13, 0.0 % | – | 6 of 6 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.05 / 0.10, 0.0 % | 0.04 / 0.09, 0.0 % | 0.07 / 0.14, 0.0 % | 57 of 57 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.11, 0.0 % | 0.04 / 0.09, 0.0 % | 0.07 / 0.14, 0.0 % | 68 of 68 |
| 5 players against 5 AI ships | 0.04 / 0.12, 0.0 % (with the AI ships) | 0.05 / 0.10, 0.1 % | 0.07 / 0.15, 0.1 % | 280 of 281 |
| dedicated server + 10 clients | – | 0.06 / 0.14, 0.0 % | 0.08 / 0.18, 0.0 % | 166 of 169 |
| starbase mission, 6 players | 0.02 / 0.08, 0.0 % (with the starbase) | 0.04 / 0.09, 0.1 % | 0.07 / 0.15, 0.3 % | 215 of 216 |
| starbase mission, forced damage, five restarts | 0.05 / 0.22, 0.0 % | 0.06 / 0.18, 0.0 % | 0.11 / 0.38, 0.0 % | 159 of 160 |

Phaser beams shown by the other players 99.5–100 %. Ramming without weapons: between players both
ships damaged in 7 of 9 contacts (the other two were grazes only one player saw), with AI ships in
26 of 29. Mixed game with a v1.7.5 host, v1.7.4, v1.7.3 and v1.7.5 clients: phaser beams and torpedo
hits 100 % among them. Starbase mission with five restarts: no crash. Kick test: peak error median
3.4, max 5.4; join test: every client saw every ship. Installer update from v1.7.4 and a fresh
install over another mod's `dinput.dll` with uninstall tested. No crashes, no script errors.

### Known open points

- Tested in one PC with simulated network conditions, up to eleven players; no real internet games
  yet (#26). The direct phaser reports ran on loopback addresses only.

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
