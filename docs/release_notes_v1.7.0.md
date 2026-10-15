## Bridge Commander multiplayer fix v1.7.0

Phaser beams now start on the other screens about one network latency after the shooter's, not
0.22 s later.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.** The phaser reports have a new format:
   players with v1.6.0 or older keep their own phaser decisions.

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.7.0.zip`** into the game folder.

### What is new

- **Phaser beams start without the delay (issue #23).** Since v1.6.0 the shooter reports its phaser
  banks and hits, and the other computers follow. In six-player tests with 60 ms latency a bank
  still started a median 223 ms after the shooter's on the other screens. Two causes:
  - The game only fires at targets in a list that it rebuilds for each ship inside that ship's own
    weapon update. bcmpfix started a reported bank outside it, the check failed, and the bank waited
    for the weapon system's next attempt (every 0.33 s). A reported bank now starts at once.
  - The game's network is a star: a client's report reached the other clients only through the
    host, one latency later. Now every player's bcmpfix also sends its reports straight to every
    other bcmpfix player, as a small UDP packet on the game port (`DirectPhaserReports`, default 1).
    The host tells each player the others' IP addresses and ports for this. The reliable report
    through the host stays; whichever copy arrives first counts.

  Release check, six players, 60 ms, three runs each, v1.6.0 → v1.7.0:

  | | v1.6.0 | v1.7.0 |
  |---|---|---|
  | beam start on another screen after the shooter's, median | 223–224 ms | 57–58 ms |
  | … 90th percentile | 336–353 ms | 91–94 ms |
  | bank firings of the shooter missing on another screen | 5.2–6.3 % | 3.5–3.9 % |
  | beams the shooter hit and another screen did not show | 3.3–4.7 % | 3.6–4.4 % |
  | client's beams without damage on the host | 0.0 % | 0.0–0.5 % |
  | host's phaser hits (0.5 s bins) also on a client | 99–100 % | 100 % |

  With `DirectPhaserReports=0` (reports only through the host) the median was 137 ms.
- **Test tools.** `tools/phasertiming.py` shows where a phaser start or hit waits: the report's send,
  its arrival on each other machine, and the bank's start and hit there.

Release check with the exact v1.7.0 build (simulated network, 180 s each, mean position error / 95th
percentile in game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.13, 0.0 % | 0.04 / 0.09, 0.0 % | – | 8 of 8 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.20, 0.0 % | 0.07 / 0.14, 0.1 % | – | 9 of 9 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.11–0.12, 0.0–0.2 % | 0.05 / 0.09–0.10, 0.0–0.3 % | 0.07 / 0.14–0.15, 0.0–0.3 % | 68 of 70, 79 of 79 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.11, 0.0 % | 0.05 / 0.09, 0.0 % | 0.07 / 0.14, 0.0 % | 49 of 49 |

Clients showed torpedo hits 68–72 ms before the host (median). Kick test: peak error after a kick
median 3.4 units, max 5.4; join test: every client saw every ship. Update from v1.6.0 with the
installer tested (the kept `bcmpfix.ini` has no `DirectPhaserReports` line, so it is on). No hangs,
no crashes, no script errors.

### Known open points

- Tested in one PC with simulated network conditions, up to six players; no dedicated server, no real
  internet games yet. The direct phaser reports ran on loopback addresses only: behind NAT routers or
  firewalls they may not get through, then the reports come through the host as in v1.6.0.
- A phaser beam lasts about 50 ms longer on the other screens than on the shooter's.

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
