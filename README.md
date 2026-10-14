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

See [`release/README.md`](release/README.md). In short: copy `dinput.dll`, `bcmpfix.ini` and
`scripts\bcmpfix_mp.py` into the game folder of **every** player. The DLL is a proxy for
`dinput.dll`; it patches the game in memory at start-up and does not change any game files.

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

What bcmpfix does about it:

| Problem | Fix |
|---|---|
| 1 | absolute position in every update; correct delta base on the receiver (for unmodded senders) |
| 2 | initialise the proxy's last update time |
| 3 | correction gain ×5, no dead band, 50 ms update interval at close range |
| 4 | peers measure the round trip time with pings; the state update reader extrapolates remote ships by RTT/2 + 30 ms |
| 5 | the host reports each torpedo hit; clients suppress their own torpedo/ship hits and replay the host's hit with their torpedo copy, including the shield/hull decision |
| 6 | resend interval 300 ms |
| 7 | clock relative to process start |

## Verification

Test bed: two game instances (host and client) inside Windows Sandbox, each driven by a scripted
pilot (`tools/game_scripts/bcmptest.py`) that chases, jinks, brakes and fires phasers and torpedoes.
Both instances log their own and the remote ship's position every 100 ms; `tools/analyze.py`
compares them. The DLL's test mode simulates latency, jitter and packet loss per direction.

Position error = distance between where a ship is on its owner's machine and where the other player
sees it at the same time, in game units (ships fly at about 5 units/s). Development runs, 180 s each:

| Network | Original (host ship / client ship) | Movement fixes | + latency compensation |
|---|---|---|---|
| 0 ms | 3.62 / 1.74 | 0.62 / 0.26 | – |
| 60 ms ±20, 1 % loss | 3.39 / 1.67 | 0.93 / 0.50 | 0.53 / 0.19 |
| 100 ms ±40, 3 % loss | 4.26 / 2.18 | 1.02 / 0.52 | 0.46–0.58 / 0.24–0.25 |

Torpedo hits: in the original, about 40 % of the hits were seen by only one player. With host
authority: 7 of 7 hits consistent at 60 ms, 8 of 9 at 100 ms with 3 % loss (the 9th hit was still
being replayed when the test ended).

## Open points

- Only tested with two players (one host, one client).
- A small lateral error remains during hard turns (extrapolation follows the forward vector only).
- Clients show a torpedo hit about one latency after the host.
- Peers without the mod keep the original behaviour. Torpedo authority turns on only when the host
  runs the mod (the client waits for an answer to its ping).
- Developed with a DRM-free `stbc.exe` 1.1. Other executables are untested; if the expected bytes
  differ, the DLL logs `NOT applied` and leaves that part of the game unchanged.

## Repository layout

- `src/`, `build.cmd` – the DLL (C, MSVC x86). `build.cmd` writes `out\dinput.dll`.
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
   Results land in `sbx/runs/<name>/`.
5. Compare: `python -I tools/analyze.py sbx/runs/run1`.

`mptest.py` refuses to start the game outside Windows Sandbox: two windowed instances grab mouse and
keyboard and make the desktop unusable.

## License

MIT for the code in this repository (see `LICENSE`). *Star Trek: Bridge Commander* is © its rights
holders; this project contains none of its files.
