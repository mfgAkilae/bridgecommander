# Star Trek: Bridge Commander – multiplayer fix

Multiplayer games of *Star Trek: Bridge Commander* (Totally Games / Activision, 2002) are known for
their bad network code:

- Ships are put back to an earlier position (rubber-banding) or teleport to other places.
- Ships drift out of sync between the players.
- A torpedo hits on one screen and misses on the other, and the ship explodes anyway.

This repository contains **bcmpfix**, a runtime fix for `stbc.exe` version 1.1, the tools used to
reverse engineer the game, and an automated test bed that runs two game instances against each other
inside Windows Sandbox under simulated network conditions.

No game files are included. You need your own copy of the game.

## Installation

**Every** player installs the fix, the host above all.

1. Download `BC_MP_fix.exe` from the [latest release](https://github.com/mfgAkilae/bridgecommander/releases/latest).
2. Run it. It finds the game (next to the installer, in the current folder, via the registry
   entries of the Activision and GOG installers, or by asking), checks that `stbc.exe` is
   version 1.1 and installs the fix. You can also drag `stbc.exe` onto `BC_MP_fix.exe`.
3. Run it again to update or uninstall. Uninstalling restores another mod's `dinput.dll` if the
   installer had to move it aside.

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway"). It asks
for administrator rights only when the game folder is write-protected (for example under
`Program Files`). Command line: `BC_MP_fix.exe [<game folder>] [--install | --uninstall] [--quiet]
[--force] [--log <file>]`; exit codes 0 = done, 1 = error, 2 = cancelled, 3 = unsupported
`stbc.exe` (with `--quiet`, unless `--force`).

Manual installation: unpack `bcmpfix-<version>.zip` into the game folder, see
[`release/README.md`](release/README.md). The DLL is a proxy for `dinput.dll`; it patches the game in
memory at start-up and does not change any game files.

## How BC multiplayer works

- One player hosts. Each computer simulates the whole scene; every peer sends unreliable state updates
  (opcode 0x1C) for the ships it owns. Remote ships are "proxies" that a network AI steers towards the
  received position.
- Weapon fire is sent as guaranteed messages. Every peer spawns its own copy of each torpedo and runs
  its own hit detection. Only the host applies damage and decides deaths.
- Guaranteed messages are delivered in order over UDP 22101 with a 1 s resend interval.

## Bugs found and fixed

Details with addresses: [`docs/findings.md`](docs/findings.md).

1. **Delta accumulation (teleports).** State updates carry an absolute position once per second and
   deltas in between. The receiver adds the deltas up instead of applying them to the last absolute
   position. After one lost update the ship surges ahead by seconds of travel and is yanked back.
   The host re-broadcasts the glitch.
2. **First-frame fling.** A new network proxy uses the whole game time as its first time step.
3. **Slow correction with dead band.** Proxies are pulled to the received position with a time
   constant of about 1 s, and errors below 0.5 units are never corrected.
4. **No latency compensation.** Remote ships are shown where they were one latency ago.
5. **Divergent torpedo hits.** Each peer decides hits on its own copy of the torpedo; MP receivers
   even spawn torpedoes with different guidance parameters. Only the host's decision counts.
6. **Head-of-line blocking.** One lost guaranteed packet holds back all following ones for 1 s.
7. **Float clock.** Frame time comes from the time since Windows started, stored as a 32-bit float;
   the frame timing gets coarse after days of uptime.
8. **Host relays its smoothed copy.** With three or more players the host sends each client's ship
   to the other clients from its own lagging proxy, not from what that client reported.
9. **Join race.** A player ship created while a client joins makes the mission script fail
   (`RebuildInfoPane` before its panel exists).
10. **Blocking script error console.** In windowed mode any script error opens the "TG Python
    Exception Debug Console" and the game waits for keyboard input there: it freezes.
11. **Lagging proxy orientation.** Remote ships turn physically towards the reported direction and
    are driven along their own nose. After a sharp turn at low speed the nose lags by more than 90°
    for seconds, the ship flies the wrong way and is dragged back.
12. **Swapped torpedo flags.** The fire message's "launch sound" and "launch sideways" bits are
    swapped between sender and receiver: other players see torpedoes leave the tube sideways and hear
    no launch sound.
13. **Ghost ship.** A ship spawned while another client joins can reach that client before its
    mission set exists. The ship is created outside every set and never shown; the host's later copy
    is dropped because the object already exists. That player never sees the ship.
14. **Knockback.** The state update reports the magnitude of the velocity, and the game extrapolates
    along the nose. After a hit's impulse or an explosion pushes a ship sideways or backwards, its
    remote copies fall behind by tens of units for many seconds.
15. **Distance LOD.** Beyond 228 units a player gets a fifth of the position updates of a ship,
    beyond 343 units one per second; any change of course at that range is seen up to a second late.
16. **Remote ships fly their own path in turns.** The game turns a remote ship slowly towards the
    reported direction; in every turn its nose lags 30–40° behind the owner's. It flies along that
    nose and is pulled back sideways: about 0.3 units of lateral error in turns.
17. **Late torpedo copies.** A torpedo fired by another player is created from the fire message, one
    network latency after the shooter's (disruptor pulses through a message of their own), and hits
    that much later.
18. **Phaser banks and hits decided per computer.** Only the start and stop of firing are sent;
    every computer decides per bank whether it can fire, and when its beam reaches the target. Banks
    fire on one screen and not on another, and a hit the shooter sees can do no damage on the host.

What bcmpfix does about it:

| Problem | Fix |
|---|---|
| 1 | absolute position in every update; correct delta base on the receiver (for unmodded senders) |
| 2 | initialise the proxy's last update time |
| 3 | correction gain ×5, no dead band, 50 ms update interval at close range; a ship ahead of its network position slows down instead of being pulled back (v1.2.0) |
| 4 | peers measure the round trip time with pings; the state update reader extrapolates remote ships by RTT/2 + 30 ms |
| 5 | the host reports each torpedo hit; clients suppress their own torpedo/ship hits and replay the host's hit with their torpedo copy, including the shield/hull decision |
| 6 | resend interval 300 ms |
| 7 | clock relative to process start |
| 8 | the host relays the received, extrapolated state (position, forward/up, speed) (v1.1.0, orientation v1.2.0) |
| 9 | the panel update is skipped while the panel does not exist (v1.2.0) |
| 10 | script errors go to `bcmpfix.log`, the game continues as in fullscreen (v1.2.0) |
| 11 | a remote ship whose nose lags more than 30° behind the reported direction is turned towards it (v1.2.1) |
| 12 | the sender writes the bits the way receivers read them (v1.3.0) |
| 13 | the host's later copy is read into the ship without a set, which puts it into the mission set (v1.3.0) |
| 14 | while the reported speed differs clearly from the remote copy's own, it follows the velocity measured from received positions; large backward errors are corrected at full speed (v1.3.0) |
| 15 | LOD distances ×3 (v1.3.0) |
| 16 | remote ships follow the velocity measured from their received positions, along its estimated turn, and are moved exactly by it; their nose follows the reported one (v1.4.0) |
| 17 | clients advance such copies by the message latency and show their own copy's hit at once; the host's report confirms it (v1.5.0) |
| 18 | the owner reports its firing banks and which beams are on the target; the other computers fire exactly those banks and show a hit when the owner's beam hits (`PhaserHitAuthority`, v1.6.0); they start a reported bank at once, and the reports also go straight from player to player instead of only through the host (`DirectPhaserReports`, v1.7.0) |

## Verification

Test bed: two game instances (host and client) inside Windows Sandbox, each driven by a scripted
pilot (`tools/game_scripts/bcmptest.py`) that chases, jinks, brakes and fires phasers and torpedoes.
Both instances log their own and the remote ship's position every 100 ms; `tools/analyze.py`
compares them. The DLL's test mode simulates latency, jitter and packet loss per direction.

Position error = distance between where a ship is on its owner's machine and where the other player
sees it at the same time, in game units (ships fly at about 4–6 units/s). Mean error, one 180 s duel
per row; latency, jitter and loss apply per direction:

| Network | Original: host ship / client ship | bcmpfix: host ship / client ship |
|---|---|---|
| 0 ms | 3.03 / 1.48 | 0.59 / 0.26 |
| 60 ms ±20 ms, 1 % loss | 3.40 / 2.44 | 0.53 / 0.18 |
| 100 ms ±40 ms, 3 % loss | 4.38 / 2.10 | 0.45 / 0.23 |
| 150 ms ±50 ms, 5 % loss | 4.55 / 2.49 | 0.60 / 0.32 |

- The mean error drops by 80–93 %. The largest error in any bcmpfix run was 1.61 units; the original
  reached 17 units at 60 ms and 41 units (a teleport) at 150 ms.
- Remote ships lagged 0.8–2.9 units behind their real position in the original and 0.02–0.21 units
  with bcmpfix.
- Torpedo hits: in the original runs, 22 of 39 hits were seen by only one player. With bcmpfix all 21
  hits were seen by both, with the same shield/hull result. Clients showed them 126–305 ms (mean per
  run) after the host.

Six players (host + five clients, mixed Federation, Klingon, Romulan and Cardassian ships), 60 ms
±20 ms, 1 % loss: other clients' ships were 7.09 units off in the original, 1.11 with v1.0.0 and
0.73 with v1.1.0 (host relay), 0.44 with v1.2.0, 0.41 with v1.3.0 and 0.08 with v1.4.0. Torpedo hits seen by all five
clients: 36 of 68 (original), 36 of 72 (v1.0.0), 80 of 87 (v1.1.0), 58 of 61 (v1.2.0), 64 of 67 and
108 of 109 (v1.3.0).

Knockback (every player pushes its own ship sideways and back at 20 units/s every 30 s, six players):
peak error after a kick, median 54 units with v1.2.1 and 5 with v1.3.0; the error stayed above 5 units
for 11 s on average with v1.2.1 and for under 1 s with v1.3.0.

Rubber-banding (100 ms steps in which a remote ship moves back against its direction of flight),
six players, other clients' ships: 11.3 % in the original, 9.3 % with v1.1.0, 0.9 % with v1.2.0.

Dedicated server with ten clients (eleven instances, 60 ms ±20 ms, 1 % loss): other clients' ships
were 6.24 units off in the original and 0.10 with v1.7.0; torpedo hits seen by all ten clients: 72 of
141 (original), 125 of 127 and 149 of 149 (v1.7.0); client phaser beams without damage on the host:
43 % (original), 0.3–3 % (v1.7.0).

Full tables and method: [`docs/measurements.md`](docs/measurements.md).

## Open points

Tracked as [issues](https://github.com/mfgAkilae/bridgecommander/issues):

- Tested in one sandbox with up to eleven players: a dedicated server with ten clients and a listen
  server with ten clients (#13), not over the internet (#26). The game's menu allows at most eight
  players; the tests raised the limit by script. With ten clients and 60 ms bcmpfix kept positions
  within 0.1–0.2 units (original: 6.2), and 94–100 % of the torpedo hits were seen by all clients
  (original: 51 %); the host's upload was about 87 kB/s. On a dedicated server the first client's
  ships could collide with the server's own object IDs and then stay invisible for everyone (a bug of
  the original game, fixed in v1.7.1, #24).
- Mixed games (#26, six players with v1.7.0, v1.6.0 and the original game): no errors in any
  combination. Positions and torpedo reports work across bcmpfix versions if the host runs bcmpfix;
  players without the mod keep the original errors. Phasers are synchronised only among players with
  the same version as the host. Since v1.1.0 the host relays the received state of client ships
  instead of the host's smoothed copy (#10, see six-player results in `docs/measurements.md`).
- Since v1.4.0 remote ships follow the velocity measured from received positions (#11): in turns
  their lateral error dropped from 0.2–0.3 to about 0.05 units and their nose from 30–40° to a few
  degrees off. What remains is mostly the latency itself at the start and end of maneuvers.
- Since v1.5.0 clients show a torpedo hit when their own copy hits, together with the shooter, and
  the host confirms it about one latency later (#12). In 2–3 % of the hits a client shows a hit the
  host does not have, and shield or hull differ in about 1–2 %.
- Since v1.6.0 the shooter decides phaser hits (#22): a client's phaser beam does damage on the host
  in 99–100 % of the beams (before 88–93 %), a beam the shooter hits is missing on another screen in
  3–5 % instead of 13–16 %, and beams only other screens show dropped from 44–70 to 1–11 per run.
- Since v1.7.0 phaser beams start on the other screens about one network latency after the shooter's
  (#23; 60 ms latency: 57 ms median, v1.6.0 223 ms). The players' bcmpfix copies exchange the phaser
  reports directly by UDP on the game port; the host tells each player the others' IP addresses.
  Tested only on loopback in the sandbox: behind NAT routers the direct packets may not arrive, then
  the reports come through the host as before. A beam lasts about 50 ms longer on the other screens
  than on the shooter's.
- Torpedo flight paths still differ a little between players: each computer spawns another player's
  torpedo from that player's ship as it sees it. Since v1.3.0 the flag bits (#8) and the receivers'
  guidance values (#9) match the shooter's. The hit outcome is the host's.
- Peers without the mod keep the original behaviour. Torpedo authority turns on only when the host
  runs the mod (the client waits for an answer to its ping).
- Developed with a DRM-free `stbc.exe` 1.1. Other executables are untested (#14); if the expected
  bytes differ, the DLL logs `NOT applied` and leaves that part of the game unchanged.

## Repository layout

- `src/`, `build.cmd` – the DLL (C, MSVC x86). `build.cmd` writes `out\dinput.dll`.
- `installer/` – `BC_MP_fix.exe` (C, Win32): finds the game, checks `stbc.exe`, installs, updates
  or removes the fix; the mod files are embedded as resources. `installer\build.cmd` writes
  `out\BC_MP_fix.exe` (run `build.cmd` first), `tools/make_release.py` collects the release files.
- `scripts/bcmpfix_mp.py` – the script layer (Python 1.5): pings, latency compensation, torpedo hit
  reports and replay.
- `release/` – player README and default `bcmpfix.ini`.
- `docs/findings.md` – reverse engineering notes with addresses.
- `tools/` – test bed and analysis tools:
  - `mptest.py` – starts host and client with a test configuration (inside Windows Sandbox only).
  - `sbx_agent.py`, `sbx_runner.py`, `sbx_job.py` – job queue between host and sandbox.
  - `analyze.py` – position error, torpedo hit matching.
  - `game_scripts/` – `Local.py` and `bcmptest.py`, the scripted pilot (copy into the game's `scripts` folder for tests).
  - `sandbox/` – Windows Sandbox configuration and dgVoodoo2 settings.
  - `ghidra/`, `asm.cmd`, `fn.py`, `gen_names.py` – Ghidra scripts and helpers.
  - `dis15.py`, `pseudo15.py` – disassembler and pseudo-source generator for the game's Python 1.5 bytecode.
  - `stackdump.py`, `wincap.py`, `loadtest.c`, `is5extract.py` – debugging and extraction helpers.

## Reproduce the results yourself

Requirements: Windows 10/11 Pro with Windows Sandbox, Python 3.10 on the host, MSVC Build Tools
(x86), dgVoodoo2 (`DDraw.dll`, `D3DImm.dll`; the sandbox has no Direct3D 7 renderer without it).

1. Build the DLL: `build.cmd`. Copy `out\dinput.dll`, `release\bcmpfix.ini`,
   `scripts\bcmpfix_mp.py` and `tools\game_scripts\*.py` into a copy of the game folder.
2. Create a folder for the job queue (`sbx`) with an `overlay` subfolder that holds the dgVoodoo2
   DLLs and `tools/sandbox/dgVoodoo.conf`.
3. Edit the host paths in `tools/sandbox/bc-test.wsb` (game copy, `tools`, Python 3.10, `sbx`) and
   start it. The sandbox agent copies the game to local disk and waits for jobs.
4. Set `BCMP_SBX` to the `sbx` folder and submit jobs from the host, for example
   `python -I tools/sbx_job.py run1 --fixes on --latency 60 --jitter 20 --loss 10 --seconds 180`.
   More players: `--clients 10 --species 1,2,6,7,8,10`; a dedicated server: `--dedicated`; more
   than eight players: `--max-players 12`; another version for one instance: `--variant client2=v1.6.0`
   (dinput.dll and bcmpfix_mp.py from `<game>/bcmpfix_variants/v1.6.0/`) or `--variant client2=off`
   (original game). Results land in `sbx/runs/<name>/`.
5. Compare: `python -I tools/analyze.py sbx/runs/run1`.

`mptest.py` refuses to start the game outside Windows Sandbox: two windowed instances grab mouse and
keyboard and make the desktop unusable.

## License

MIT for the code in this repository (see `LICENSE`). *Star Trek: Bridge Commander* is © its rights
holders; this project contains none of its files.
