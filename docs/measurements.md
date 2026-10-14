# Measurements

## Method

- Two instances of `stbc.exe` 1.1 in one Windows Sandbox (dgVoodoo2 for Direct3D), connected over
  loopback: the host listens on 127.0.0.2, the client joins with `-Client -IP 127.0.0.2`.
- Each instance is driven by the scripted pilot `tools/game_scripts/bcmptest.py`: chase, jink and
  brake manoeuvres, phaser and torpedo volleys every 1.2–2.5 s. Duel in Multiplayer Mission1
  ("Ace" = host's ship, "Bravo" = client's ship), 180 s per run.
- The DLL's test mode delays and drops packets in `sendto`/`recvfrom` (latency, uniform jitter and loss
  per direction).
- Both instances log every ship every 100 ms with the shared QPC clock. `tools/analyze.py` compares the
  remote view of a ship with its owner's position at the same moment.
- "original" runs use the same DLL with all fixes turned off (`FIXES_OFF` in `tools/mptest.py`), so
  only the network emulation and the test pilot are active. "bcmpfix" runs use the release defaults.

Metrics:

- **Position error**: distance between the remote view and the owner's position, in game units. The
  ships fly at about 4–6 units/s. Samples within 16 s after a death are skipped (respawn).
- **Along-track offset**: mean signed error along the owner's direction of flight; negative = the
  remote view lags behind.
- **Torpedo hits**: a torpedo hit on host and client counts as "both" when the same target is hit within
  1.5 s on both instances; otherwise it is seen by one player only.

## Results

Final matrix, 14.10.2026, one 180 s run per row. "Host ship" = Ace as seen by the client,
"client ship" = Bravo as seen by the host. Error in game units: mean / 95th percentile / maximum.

| Network (per direction) | Fixes | Host ship | Client ship | Along-track offset (host / client ship) | Torpedo hits seen by both / by one only |
|---|---|---|---|---|---|
| 0 ms | original | 3.03 / 5.66 / 7.53 | 1.48 / 2.30 / 2.54 | −1.83 / −0.79 | 2 / 3 |
| 0 ms | bcmpfix | 0.59 / 1.23 / 1.61 | 0.26 / 0.52 / 1.42 | −0.14 / −0.05 | 7 / 0 |
| 60 ms ±20 ms, 1 % loss | original | 3.40 / 6.64 / 17.11 | 2.44 / 4.91 / 11.56 | −2.28 / −1.21 | 8 / 7 |
| 60 ms ±20 ms, 1 % loss | bcmpfix | 0.53 / 0.96 / 1.23 | 0.18 / 0.37 / 0.45 | −0.11 / −0.03 | 5 / 0 |
| 100 ms ±40 ms, 3 % loss | original | 4.38 / 8.76 / 15.47 | 2.10 / 3.78 / 11.69 | −2.87 / −1.20 | 4 / 8 |
| 100 ms ±40 ms, 3 % loss | bcmpfix | 0.45 / 0.80 / 1.58 | 0.23 / 0.39 / 0.56 | −0.02 / −0.07 | 5 / 0 |
| 150 ms ±50 ms, 5 % loss | original | 4.55 / 8.00 / 40.98 | 2.49 / 4.45 / 10.58 | −2.48 / −1.36 | 3 / 4 |
| 150 ms ±50 ms, 5 % loss | bcmpfix | 0.60 / 1.27 / 1.52 | 0.32 / 0.83 / 1.02 | −0.21 / −0.10 | 4 / 0 |

- Mean error −80 % to −93 %. The original run at 150 ms contains one teleport (41 units); no bcmpfix
  run exceeds 1.61 units.
- The along-track offset (lag) of remote ships drops from 0.8–2.9 units to 0.02–0.21 units.
- Torpedo hits: original 17 consistent, 22 seen by one player only; bcmpfix 21 consistent, 0 one-sided,
  no shield/hull mismatch. The client shows a hit 126 / 231 / 256 / 305 ms (mean, by row) after the host.
- The host ship (Ace) shows a larger error than the client ship in all runs. It is a different ship
  class and flies faster on average (5.2–5.6 vs. 3.7–4.1 units/s).

Caveats: one run per configuration; torpedo counts per run are small (4–15 hits), so the torpedo
figures show the trend, not exact rates. Only 1:1 games were measured.

## Release build check

The release build (DLL and script with host-mod detection) at 100 ms ±40 ms, 3 % loss:

| Run | Host ship | Client ship | Torpedo hits seen by both / host only / client only | Client log |
|---|---|---|---|---|
| host and client with bcmpfix, 180 s | 0.51 / 1.21 / 1.74 | 0.25 / 0.61 / 0.88 | 7 / 2 / 0 | `host runs bcmpfix (rtt 330 ms)` after 6.3 s |
| host without script layer, 120 s | 1.01 / 1.60 / 1.86 | 0.60 / 0.82 / 0.93 | 6 / 0 / 1 | torpedo authority never active, 0 hits suppressed |

- With a host that does not answer pings, the client keeps the original torpedo behaviour and has no
  latency compensation (no RTT). The DLL fixes still apply, so the error stays below the original.
- In the first run the client received 9 hit reports and replayed 7. Two replays on the client's own
  ship did not produce a local hit within three approaches; the client retired those torpedoes, so
  the hits were missing on its screen (damage is the host's anyway). Across all bcmpfix runs: 28 of
  30 hit reports replayed. See issue #15.

## Six players

14.10.2026, v1.1.0. One host and five clients in one sandbox, ship classes
Akira, Ambassador (Federation), Bird of Prey, Vor'cha (Klingon), Warbird (Romulan) and Galor
(Cardassian), so disruptor pulses are included. Free-for-all; each scripted pilot attacks the
nearest enemy and re-picks every 20 s. 180 s per run.

From here on the analysis skips the first 5 s after a ship appears for a viewer (proxies settle
after a join). Applied to the 2-player matrix above, this changes its means by at most 0.08 units.

Mean position error in game units (95th percentile / maximum in brackets). "Client views client"
is the case where the host relays a client's ship to the other clients.

| Network | Build | Client views host | Host views client | Client views client | Teleports | Torpedo hits seen by all clients / by none |
|---|---|---|---|---|---|---|
| 60 ms ±20 ms, 1 % loss | original¹ | 3.67 (6.38 / 16.3) | 2.69 (5.38 / 12.7) | 7.09 (12.5 / 18.2) | 2 | 36 of 68 / 16 |
| 60 ms ±20 ms, 1 % loss | v1.0.0 | 0.71 (1.35 / 1.61) | 0.32 (0.64 / 2.14) | 1.11 (2.14 / 3.81) | 0 | 36 of 72 / 10 |
| 60 ms ±20 ms, 1 % loss | v1.1.0 | 0.63 (1.12 / 1.95) | 0.31 (0.62 / 1.74) | 0.73 (1.38 / 2.11) | 0 | 80 of 87 / 1 |
| 100 ms ±40 ms, 3 % loss | original¹ | 4.59 (8.35 / 33.6) | 2.63 (5.48 / 17.0) | 6.57 (11.9 / 48.8) | 22 | 20 of 69 / 14 |
| 100 ms ±40 ms, 3 % loss | v1.1.0 | 0.51 (0.98 / 1.52) | 0.32 (0.60 / 1.44) | 0.81 (1.65 / 4.58) | 0 | 55 of 62 / 1 |

¹ In the original runs some clients hung while joining (see below), so these rows have 3 and 4
clients instead of 5.

Shield/hull decision of matched torpedo hits: v1.0.0 differed from the host on 52 matches,
v1.1.0 on 4 (60 ms) and 6 (100 ms).

What v1.1.0 changes:

- **Host relay (#10).** The host wrote client ships from its own smoothed proxy, which trails the
  received position by the correction lag. Now it writes them from the NetworkAI target, the
  received position dead-reckoned to the current time. Client-views-client error: 1.11 → 0.73.
- **Torpedo replay (#15).** The client's hit test runs on the segment from the torpedo's previous
  position to its current one. After the replay moved the torpedo, the previous position was still
  where the local copy had been, often inside the target's shield, so the game tested the hull first.
  The replay start point is now the segment start. Disruptors fire in pairs, and both hit reports
  picked the same local projectile; a projectile that already replays a hit is now skipped.

Hangs while joining: in the original runs 4 of 20 clients stopped at a blocking Python exception
dialog of the game within a few seconds after the host's first answer, before the mission started.
With bcmpfix 0 of 20 clients hung. The exception text could not be captured yet (one diagnostic
run with early logging did not reproduce it), so the cause is open.

Two-player check with v1.1.0 (100 ms ±40 ms, 3 % loss): host ship 0.55, client
ship 0.24, 6 of 6 torpedo hits seen by both, so no regression against v1.0.0 (0.46–0.58 / 0.23–0.25).

## Rubber-banding and v1.2.0

The mean error does not show small jumps back. From here on `analyze.py` also counts
**backward steps**: 100 ms steps in which a remote ship moves more than 0.05 units against its own
direction of flight (at more than 1 unit/s), i.e. visible rubber-banding.

v1.1.0 pulled remote ships straight towards their network position, five times faster than the
original. When a ship was ahead of that position (extrapolation overshot, the real ship slowed down or
turned), it was pulled backwards. v1.2.0 never corrects a ship backwards faster than half the
distance it flies in that frame (`SmoothCorrection`), so it slows down instead. The host also relays
the received forward and up vectors with a client's ship instead of its own lagging proxy
orientation; the other clients extrapolate along that forward vector.

Backward steps (and mean error) by viewer:

| Run | Original | v1.1.0 | v1.2.0 |
|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss (host ship / client ship) | 2.5 % / 2.5 % | 0.0 % / 0.0 % | 0.0 % / 0.0 % |
| 2 players, 150 ms ±50 ms, 5 % loss (host ship / client ship) | 2.8 % / 3.5 % | 4.2 % / 6.3 % | 2.0 % / 0.0 %¹ |
| 6 players, 60 ms: client views host | 3.3 % (3.67) | 5.9 % (0.63) | 5.5 % (0.57)² |
| 6 players, 60 ms: host views client | 0.6 % (2.69) | 0.7 % (0.31) | 1.0 % (0.32) |
| 6 players, 60 ms: client views client | 11.3 % (7.09) | 9.3 % (0.73) | 0.9 % (0.44) |
| 6 players, 100 ms ±40 ms, 3 % loss: client views host | 3.8 % (4.59) | 0.1 % (0.51) | 5.5 % (0.58)² |
| 6 players, 100 ms: host views client | 2.1 % (2.63) | 1.4 % (0.32) | 2.0 % (0.34) |
| 6 players, 100 ms: client views client | 5.3 % (6.57) | 7.9 % (0.81) | 2.4 % (0.54) |

¹ Build without the relay orientation change, which only matters for three or more players.
² Almost all of these steps fall into the first 40 s of the run (for example 99 of 105 for one client at 100 ms);
afterwards 0–6 per 20 s. The same start-phase pattern appears in one of the two v1.1.0 runs
(`p6_new`), not in the other. Cause not yet known (issue #17).

Torpedo hits seen by all five clients with v1.2.0: 58 of 61 (60 ms) and 46 of 52 (100 ms), none
missing on all clients; shield/hull differed from the host on 3 and 12 matches.

## Join race and script errors (v1.2.0)

- Original game, six-player joins: 5 of 40 joining clients hung in the Python debug console
  (`RebuildInfoPane` before the info pane exists, see `docs/findings.md`).
- Forced test (the test pilot calls `RebuildInfoPane` with no info pane): without bcmpfix the call
  raises `AttributeError: 'None' object has no attribute 'KillChildren'`; with v1.2.0 it is skipped.
- Forced script error in a windowed client (`--raise-once`): without `NoScriptErrorConsole` the client
  froze (no telemetry after the error); with it the client kept running (762 samples after the
  error) and `bcmpfix.log` contained the traceback.
- With bcmpfix's script layer active, 0 of 70 joining clients hung (the race was not observed in
  those runs).
