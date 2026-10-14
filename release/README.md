# bcmpfix – Multiplayer fixes for Star Trek: Bridge Commander 1.1

bcmpfix fixes the synchronisation errors in Bridge Commander multiplayer:

- Ships jump or teleport to other places.
- Ships are put back to an earlier position (rubber-banding).
- A torpedo hits on one screen and misses on the other, and the ship explodes anyway.

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
- **Mixed sessions:** Players without the mod can join. They keep the original behaviour, including the known errors.
- **Host without the mod:** Torpedo synchronisation only turns on when the host runs the mod too. The mod detects this automatically.
- **Other mods:** Mods that ship their own `dinput.dll` are not compatible with bcmpfix.
- **Windows 10/11:** A graphics wrapper such as dgVoodoo2 (`DDraw.dll` + `D3DImm.dll`) is still recommended. It is independent of bcmpfix.

## What the mod fixes

### 1. Positions of other ships

- **Teleports after packet loss:** Position updates carry an absolute position only once per second and deltas in between. The receiver added these deltas up by mistake. When an absolute position got lost, the ship jumped forward by several seconds of travel and then back.
  - Fix: every update carries the absolute position, and the receiver applies deltas correctly.
- **Newly spawned ships flung away:** A newly created network ship used the whole game time as the time step of its first frame and was flung far away. This is fixed.
- **Slow correction:** Remote ships were pulled to their network position with a time constant of about 1 s. Errors below 0.5 units were not corrected at all.
  - Now: about 0.2 s, no dead band, and updates at close range twice as often (every 50 ms).
- **Latency compensation:** Players measure the network round trip to each other with pings. Remote ships are extrapolated by that time. Without this, you see every ship where it was one network latency ago.

### 2. Torpedoes

In the original game, each computer flies its own copy of each torpedo and decides on its own whether it hits. Only the host decides damage and the death of a ship. This is why some hits are seen by only one player.

With bcmpfix, the host also decides what everybody sees:

- The host reports each torpedo hit with its target and hit point.
- The other computers ignore their own hit detection for torpedoes against ships.
- Instead, they replay the host's hit with their copy of the torpedo at that position. They also take over whether the hit struck the shield or the hull.

All players then see the same hits and misses as the host, delayed by about one network latency.

### 3. Network and timing

- **Resending lost packets:** Guaranteed messages (firing, cloaking, deaths, game events) arrive in a fixed order. A lost packet used to hold up all following messages for at least 1 s. Now the resend happens after 0.3 s.
- **Game clock:** The game clock was computed from the time since Windows started and stored as a 32-bit float. After days of uptime, frame timing became inaccurate by up to ±15 ms. Windows Fast Startup does not reset this time on shutdown. Now the clock counts from program start.

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
