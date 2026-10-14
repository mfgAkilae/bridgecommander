## Bridge Commander multiplayer fix v1.1.0

Better sync in games with three or more players, and torpedo and disruptor hits that all players see
the same way.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If v1.0.0 is installed, choose
   *Update*; your `bcmpfix.ini` is kept, and the new option is on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.** Players without the fix can still join, but
   they keep the original behaviour.

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.1.0.zip`** into the game folder.

### What is new

- **Three or more players: the host now passes on where the other ships really are.** The host used
  to send each client's ship to the other clients from its own smoothed copy, which trails the real
  ship by about 0.2 s. Now it sends the position it last received from that client, moved forward
  to the current time. In a six-player test, the other clients' ships were off by 1.11 units on
  average with v1.0.0 and by 0.73 units with v1.1.0 (original game: 7.09). New INI option
  `HostRelayReceivedState` (default 1).
- **Torpedo and disruptor hits.** Clients replay every hit the host reports. Two problems are fixed:
  - Replayed hits sometimes struck the hull where the host saw a shield hit, because the game
    checked the path from where the local torpedo copy had been. In a six-player test the shield/hull
    result differed from the host on 52 hits with v1.0.0 and on 4 with v1.1.0.
  - Disruptors fire in pairs; both hit reports could use the same local projectile, so one hit was
    lost.

  Hits seen by all five clients in a six-player test: 36 of 72 with v1.0.0, 80 of 87 with v1.1.0.

### Measurements, six players

Host and five clients, mixed Federation, Klingon, Romulan and Cardassian ships, 180 s per run.
Mean position error in game units (ships fly at about 4–6 units/s):

| Network (per direction) | Client sees another client's ship: original / v1.0.0 / v1.1.0 | Torpedo hits seen by all clients: original / v1.1.0 |
|---|---|---|
| 60 ms ±20 ms, 1 % loss | 7.09 / 1.11 / 0.73 | 36 of 68 / 80 of 87 |
| 100 ms ±40 ms, 3 % loss | 6.57 / – / 0.81 | 20 of 69 / 55 of 62 |

In the original game some clients hung in a Python error dialog while six players joined (4 of 20 in
the tests); with bcmpfix none did. The cause is still being investigated (issue #16).

### Known open points

- Tested in one PC with simulated network conditions, up to six players; no dedicated server, no real
  internet games yet.
- A small lateral error remains during hard turns.
- Rarely a client does not show a hit the host reported (1 of 87 in the six-player test); damage is
  not affected.
- Mods that ship their own `dinput.dll` cannot be combined with this fix (the installer keeps a
  backup of it).

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
