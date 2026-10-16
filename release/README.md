# bcmpfix – Multiplayer fixes for Star Trek: Bridge Commander 1.1

bcmpfix fixes the synchronisation errors in Bridge Commander multiplayer:

- Ships jump or teleport to other places.
- Ships are put back to an earlier position (rubber-banding).
- A torpedo hits on one screen and misses on the other, and the ship explodes anyway.
- A phaser beam hits on the shooter's screen and does no damage, or shows only on some screens.

The mod is a DLL that loads as `dinput.dll` when the game starts, plus a small Python script. It does not change any game files: `stbc.exe`, the data and the original scripts stay untouched.

## Installation

**All players** install the mod, the host above all.

**With the installer:** run `BC_MP_fix.exe`. It finds the game folder (or asks for it), checks `stbc.exe` and installs the files. Run it again to update or uninstall.

**By hand:** unpack the release archive `bcmpfix-<version>.zip` into the game folder, which is the folder that contains `stbc.exe`. This puts the files here:

| File | Destination |
|---|---|
| `dinput.dll` | game folder |
| `bcmpfix.ini` | game folder |
| `scripts\bcmpfix_mp.py` | `scripts` subfolder of the game folder |

Start the game as usual. The mod writes `bcmpfix.log` into the game folder. Each fix that is active appears there with `applied`.

**Uninstall:** run `BC_MP_fix.exe` and choose *Uninstall*, or delete the three files. The archive also contains this README (`bcmpfix_README.md`) and the license (`bcmpfix_LICENSE.txt`); they are not needed by the game.

### Requirements and compatibility

- **Version:** The mod is written for `stbc.exe` version 1.1. It checks the original bytes before each change. If a location does not match, for example in another program version, the mod skips that change and writes `NOT applied` to the log. The game then runs unchanged.
- **Mixed sessions:** Players without the mod can join. They keep the original behaviour, including the known errors, also for the ships of bcmpfix players. bcmpfix players see their ships better (tests with six players: 0.5 units off instead of about 5).
- **Host without the mod:** Torpedo and phaser synchronisation only turn on when the host runs the mod too. The mod detects this automatically.
- **Older bcmpfix versions:** Positions and torpedoes work with v1.6.0 players too. Phasers are synchronised only among players with the same phaser report format (v1.7.x, or v1.6.0), and only if the host has it as well; all players should update.
- **Direct phaser reports:** Players who run bcmpfix v1.7.0 send their phaser reports to each other directly (UDP on the game's port, 22101), not only through the host. For this the host tells each of them the other players' IP addresses and ports. If a router or firewall drops these packets, the reports still arrive through the host, only later. `DirectPhaserReports=0` in `bcmpfix.ini` turns this off.
- **Other mods:** Mods that ship their own `dinput.dll` are not compatible with bcmpfix.
- **Windows 10/11:** A graphics wrapper such as dgVoodoo2 (`DDraw.dll` + `D3DImm.dll`) is still recommended. It is independent of bcmpfix.

## What the mod fixes

### 1. Positions of other ships

- **Teleports after packet loss:** Position updates carry an absolute position only once per second and deltas in between. The receiver added these deltas up by mistake. When an absolute position got lost, the ship jumped forward by several seconds of travel and then back.
  - Fix: every update carries the absolute position, and the receiver applies deltas correctly.
- **Newly spawned ships flung away:** A newly created network ship used the whole game time as the time step of its first frame and was flung far away. This is fixed.
- **Ships sliding into place:** A ship that appears on your screen (a player joins or respawns, an AI ship is created) started at the position in the game's "object created" message. That position can be seconds old: when you join, you get every ship at the place where it was created. The ship then slid to its real position for up to a second, in a ten-player game sometimes over 100 units. Since v1.7.3 it starts at the first position received for it (`SnapNewShips`).
- **Slow correction:** Remote ships were pulled to their network position with a time constant of about 1 s. Errors below 0.5 units were not corrected at all.
  - Now: about 0.2 s, no dead band, and updates at close range twice as often (every 50 ms).
  - A ship that is ahead of its network position slows down instead of being pulled back, so it does not jump back (rubber-banding).
  - A ship whose nose lags more than 30° behind the direction its owner reported (after a sharp turn) is turned towards it, so it does not fly the wrong way and drift back.
- **Turns:** The game turned another player's ship only slowly towards the direction that player reported; in every turn its nose pointed 30–40° away and the ship slid sideways. bcmpfix now moves remote ships along the velocity measured from their received positions, including the turn, and turns their nose to the reported direction.
- **Knockback:** A hit or an explosion can push a ship sideways or backwards while its nose keeps pointing ahead. The game extrapolated it along its nose, and on the other screens it fell behind by tens of units for many seconds. While a ship is knocked off its course, bcmpfix now follows the velocity measured from its received positions.
- **Distant ships:** The game sends fewer position updates for distant ships, one per second beyond 343 units. bcmpfix triples these distances. This doubles the host's upload (about 25 kB/s with six players).
- **Three or more players:** The host passes on each player's ship as that player reported it (position, direction, speed), not its own smoothed copy.
- **Latency compensation:** Players measure the network round trip to each other with pings. Remote ships are extrapolated by that time. Without this, you see every ship where it was one network latency ago.

### 2. Torpedoes

In the original game, each computer flies its own copy of each torpedo and decides on its own whether it hits. Only the host decides damage and the death of a ship. This is why some hits are seen by only one player.

With bcmpfix, the host also decides what everybody sees:

- The host reports each torpedo hit with its target and hit point.
- The other computers ignore their own hit detection for torpedoes against ships.
- Instead, they replay the host's hit with their copy of the torpedo at that position. They also take over whether the hit struck the shield or the hull.

All players then see the same hits and misses as the host. Since v1.5.0 your computer shows a hit as soon as its own copy of the torpedo hits, at about the same moment as the shooter; the host's report confirms it a moment later.

- **Torpedo paths:** Torpedoes fired by another player are created on your computer from the fire message. The original gave them more turning power and a longer guidance time there, re-aimed disruptor shots, and swapped two flags: they left the tube sideways and made no launch sound. bcmpfix keeps the shooter's values. This also matters on the host, whose copy decides the hit.

### 3. Phasers

In the original game, only the start and stop of firing are sent. Each computer decides on its own, from its own view of both ships, which phaser banks can fire and when a beam reaches its target. Near the edge of a firing arc, or when the bank charges differ, the computers decide differently: in tests with six players, a bank that fired on the shooter's screen did not fire on another screen in about 22 % of the cases. The host applies the damage, so a hit the shooter saw sometimes did no damage at all.

Since v1.6.0 the shooter's computer decides (`PhaserHitAuthority=1`):

- It reports which of its phaser banks fire and which of their beams touch a ship and hit it.
- The other computers fire exactly these banks, without their own firing-arc check, and keep them firing until the shooter stops.
- A beam hits, and does damage on the host, when it does on the shooter's screen.

In tests with six players, a client's phaser beam did damage on the host in 99–100 % of the beams (before 88–93 %). A beam the shooter hit was missing on another screen in 3–5 % instead of 13–16 %, and beams that only the other screens showed dropped from 44–70 to 1–11 per run.

Since v1.7.0 the beams also start almost together with the shooter's: about 0.06–0.08 s later on the other screens with 60 ms network latency per direction (v1.6.0: 0.22 s, the original: 0.27 s). Two changes did this:

- The other computers start a reported bank at once. Before, the original's target check made them wait for the weapon system's next attempt, which comes every 0.33 s.
- Each player sends the reports directly to every other bcmpfix player (`DirectPhaserReports=1`). Before, a client's report reached the other clients only through the host, one latency later.

Since v1.7.3 the host also reports the phaser banks of the ships its AI flies, for example AI ships added by mods or the starbase in the starbase mission (tested only with AI ships created by the test tool). Before, every computer decided their firing itself: in a test with five AI ships, 8.3 % of their beams were missing on another screen; now none were.

### 4. Network and timing

- **Resending lost packets:** Guaranteed messages (firing, cloaking, deaths, game events) arrive in a fixed order. A lost packet used to hold up all following messages for at least 1 s. Now the resend happens after 0.3 s.
- **Game clock:** The game clock was computed from the time since Windows started and stored as a 32-bit float. After days of uptime, frame timing became inaccurate by up to ±15 ms. Windows Fast Startup does not reset this time on shutdown. Now the clock counts from program start.

### 5. Joining and freezes

- **Joining a running game:** If a player's ship arrived while you were still joining, the game's own mission script failed. In windowed mode the game then froze. bcmpfix skips the failing step until the game is ready for it.
- **Invisible ship after joining:** If another player spawned a ship while you were joining, that ship could stay invisible on your screen for the whole game. bcmpfix shows it when the host sends it again during your join.
- **Dedicated server:** On a dedicated server the first player's new ship could be refused by the server and stay invisible for everybody, and that player got no more updates of the other ships. The server numbered its own objects (copies of torpedoes) in the first player's range. Since v1.7.1 it uses a range no player gets.
- **Freezes in big games:** The game's combat music watches the enemy ships with a range condition that added its event handlers again whenever the group of enemy ships changed, so their number doubled with every join, death and respawn. With many players the game then froze for seconds, up to a minute, whenever objects were deleted, above all for the player who joined first. Since v1.7.2 the handlers are registered once (`FixConditionInRange`).
- **Script errors in windowed mode:** Any script error opened the "TG Python Exception Debug Console" in a separate window, and the game waited for keyboard input there. bcmpfix writes the error to `bcmpfix.log` and the game continues, as it does in fullscreen.

You can turn off each fix separately in `bcmpfix.ini`. The file has comments.

## Measurements

Two scripted players fight a duel, 180 s per run. Network conditions are simulated (latency and loss apply per direction). Details: `docs/measurements.md` in the repository https://github.com/mfgAkilae/bridgecommander.

| Network (per direction) | Position error, original | Position error, bcmpfix | Torpedo hits seen by one player only, original / bcmpfix |
|---|---|---|---|
| 0 ms | 3.03 / 1.48 | 0.59 / 0.26 | 3 of 5 / 0 of 7 |
| 60 ms ±20 ms, 1 % loss | 3.40 / 2.44 | 0.53 / 0.18 | 7 of 15 / 0 of 5 |
| 100 ms ±40 ms, 3 % loss | 4.38 / 2.10 | 0.45 / 0.23 | 8 of 12 / 0 of 5 |
| 150 ms ±50 ms, 5 % loss | 4.55 / 2.49 | 0.60 / 0.32 | 4 of 7 / 0 of 4 |

Position error: mean distance in game units between where a ship really is and where the other player sees it (host ship / client ship); the ships fly at about 4–6 units/s. The worst single error in the original runs was 41 units (a teleport), with bcmpfix 1.6 units.

## Troubleshooting

- **Log:** `bcmpfix.log` in the game folder. Each fix appears there with `applied` or `NOT applied`. With `Verbose=1` and `NetStatsSec=10`, the log also contains network statistics and torpedo counters.
- **Game does not start:** Delete `dinput.dll` from the game folder. The original game then runs again.
