## Bridge Commander multiplayer fix v1.0.0

Fixes rubber-banding, teleporting ships and torpedo hits that only one player sees in
*Star Trek: Bridge Commander* multiplayer games.

### How to install (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. You can also drag `stbc.exe` onto it.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.** Players without the fix can still join, but
   they keep the original behaviour.

To update or undo: run `BC_MP_fix.exe` again and choose *Update* or *Uninstall*. The installer is not
signed, so Windows SmartScreen may warn ("More info" → "Run anyway").

Manual alternative: unpack **`bcmpfix-1.0.0.zip`** into the game folder (where `stbc.exe` is). To undo,
delete `dinput.dll`, `bcmpfix.ini` and `scripts\bcmpfix_mp.py`.

Requires `stbc.exe` version 1.1. The fix checks the original bytes before each change; on another
version it logs `NOT applied` and leaves that part of the game unchanged. No game files are included
in this release.

### What it fixes

- **Teleports and rubber-banding.** The game sends an absolute position once per second and deltas in
  between, and the receiver added the deltas up by mistake. After a lost packet a ship jumped ahead
  by seconds of travel and back. Now every update carries the absolute position.
- **Ships flung away when they appear**, slow correction (about 1 s) and a dead band of 0.5 units:
  remote ships now follow their real position within about 0.2 s, with updates every 50 ms at close
  range.
- **Lag.** Players measure their round trip time; remote ships are extrapolated by half of it, so you
  see them where they are now, not where they were.
- **Torpedo hits.** Each computer used to decide hits on its own copy of the torpedo, while only the
  host's decision counted for damage. Now the host reports every hit and all players see the same hits
  and misses (about one latency later than the host).
- **Stalls after packet loss.** A lost guaranteed packet blocked all following messages for 1 s; now
  0.3 s.
- **Frame timing after long uptime.** The game clock now starts at program start.

### Measurements

| Network (per direction) | Position error, original | Position error, bcmpfix | Torpedo hits seen by one player only, original / bcmpfix |
|---|---|---|---|
| 0 ms | 3.03 / 1.48 | 0.59 / 0.26 | 3 of 5 / 0 of 7 |
| 60 ms ±20 ms, 1 % loss | 3.40 / 2.44 | 0.53 / 0.18 | 7 of 15 / 0 of 5 |
| 100 ms ±40 ms, 3 % loss | 4.38 / 2.10 | 0.45 / 0.23 | 8 of 12 / 0 of 5 |
| 150 ms ±50 ms, 5 % loss | 4.55 / 2.49 | 0.60 / 0.32 | 4 of 7 / 0 of 4 |

Position error: mean distance in game units between where a ship really is and where the other player sees it (host ship / client ship); the ships fly at about 4–6 units/s. The worst single error in the original runs was 41 units (a teleport), with bcmpfix 1.6 units.

### Known open points

- Tested with two players (one host, one client).
- A small lateral error remains during hard turns.
- Rarely a client does not show a hit the host reported (2 of 30 in tests); damage is not affected.
- Mods that ship their own `dinput.dll` cannot be combined with this fix.

Details, evidence and tools: see the README in the repository.
