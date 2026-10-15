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

## Issue #17: proxy orientation in sharp turns (v1.2.1)

Traced on the clients (`TraceStateSec`, `tools/trace_view.py`): the backward steps of the host's ship
came in bursts of a few seconds in which the proxy's nose pointed more than 90° away from the
received forward vector (nose · forward −0.3 … −0.6). The host's ship had turned sharply at low speed;
NetworkAI turns the proxy physically and drives it along its own nose, so the proxy flew the wrong
way and the position correction dragged it back. In one run this happened right after the start,
in another at 60–80 s, at the same moment for all clients.

Variants of an orientation correction (`OrientationCorrection`), mean error / 95th percentile and
backward steps:

| Run | none (v1.2.0) | 1: threshold 30°/60° (v1.2.1) | full, no threshold | 2: turn-rate prediction |
|---|---|---|---|---|
| 2 players 60 ms, host ship | 0.57 / 0.93, 0.0 % | 0.35 / 0.45, 0.0 % | 0.32 / 0.73, 0.4 % | 0.33 / 0.85, 0.3 % |
| 2 players 60 ms, client ship | 0.24 / 0.64, 0.0 % | 0.20 / 0.36, 0.0 % | 0.55 / 1.31, 0.3 % | 0.40 / 0.66, 0.0 % |
| 2 players 150 ms, host ship | 0.47 / 0.97, 2.0 % | 0.30 / 0.53, 0.0 % | 0.24 / 0.72, 0.7 % | 0.49 / 1.32, 0.8 % |
| 2 players 150 ms, client ship | 0.32 / 0.63, 0.0 % | 0.38 / 0.66, 0.0 % | 0.57 / 0.95, 0.0 % | 0.67 / 1.11, 0.0 % |
| 6 players 60 ms, client views host | 0.58–0.62, 5.2–7.8 % | 0.37–0.38, 0.0 % | 0.30–0.91, 0.0 % | 0.38, 0.5–0.6 % |
| 6 players 60 ms, host views client | 0.32–0.35, 0.0 % | 0.26–0.27, 0.0–0.1 % | 0.28–0.37, 0.1–0.2 % | 0.39–0.44, 0.1–0.2 % |
| 6 players 60 ms, client views client | 0.44–0.48, 0.5–2.2 % | 0.44–0.51, 0.1–0.7 % | 0.52–0.66, 0.1–0.5 % | 0.53–0.68, 0.3 % |

(6 players: two or three runs each, ranges shown.)

- **Full correction** (always turn the proxy to the received vectors) removes the backward bursts but
  makes ordinary turns worse: the received vectors are one latency old, while the physical turning
  keeps rotating and so predicts the turn.
- **Threshold 30°/60°** (v1.2.1 default) leaves turns below 30° to the physical turning and corrects
  fully from 60°: no backward steps in any of these runs, and mostly lower errors.
- **Turn-rate prediction** (estimate the rate from consecutive received forward vectors and their
  sender times, rotate by latency + age of the update): the estimated rates match the real ones
  (median 0.26 rad/s on both sides), but the predicted nose makes the proxy fly an arc while NetworkAI
  still extrapolates the position in a straight line, so the correction pulls it sideways. Kept as
  experimental option 2.

**Arc extrapolation (`ArcExtrapolation=1` with option 2).** The position is extrapolated along the
predicted turn as well: p = p0 + speed · ∫ R(ω t) f dt over latency + age of the update (closed form,
checked against numerical integration). Same settings, mean error / 95th percentile and backward
steps:

| Run | threshold 30°/60° | prediction | prediction + arc |
|---|---|---|---|
| 2 players 60 ms, host ship | 0.35 / 0.45, 0.0 % | 0.33 / 0.85, 0.3 % | 0.32 / 0.66, 0.0 % |
| 2 players 60 ms, client ship | 0.20 / 0.36, 0.0 % | 0.40 / 0.66, 0.0 % | 0.44 / 0.72, 0.0 % |
| 2 players 150 ms, host ship | 0.30 / 0.53, 0.0 % | 0.49 / 1.32, 0.8 % | 0.47 / 1.00, 0.0 % |
| 2 players 150 ms, client ship | 0.38 / 0.66, 0.0 % | 0.67 / 1.11, 0.0 % | 0.62 / 1.03, 0.0 % |
| 6 players: client views host | 0.37–0.38, 0.0–0.5 % | 0.38, 0.5–0.6 % | 0.31–0.33, 0.0–1.2 % |
| 6 players: host views client | 0.25–0.27, 0.0–0.3 % | 0.39–0.44, 0.1–0.2 % | 0.34–0.35, 0.1–0.3 % |
| 6 players: client views client | 0.44–0.51, 0.1–0.7 % | 0.53–0.68, 0.3 % | 0.59–0.62, 0.2–0.3 % |

The arc helps the prediction, but the threshold variant stays better except for the clients' view of
the host's ship. The reason is drift: in turns a BC ship's velocity lags its nose. Angle between the
nose a peer reported and the owner's actual velocity (run r17a, speed above 2 units/s): median 6°
(Warbird) to 23° (Vor'cha), 90th percentile up to 56° (Ambassador) and 62° (Vor'cha). Any
extrapolation along the nose, straight or curved, is wrong in turns; the proxy's own physics (same
ship class) reproduces the drift as long as its orientation is left alone. A better position
extrapolation would follow the velocity estimated from successive received positions instead of the
nose.

## Issue #11: extrapolation along the estimated velocity

Instead of the reported nose × speed, the target follows the velocity estimated from successive
received positions and their sender times (`VelocityExtrapolation`, `src/velo.c`), straight (1) or
along the estimated turn of the velocity (2). In a first version the magnitude also came from the
positions; it scattered by about ±30 % (|v| / reported speed: 5th percentile 0.69, 95th 1.27), while
the direction was steady (median 1.1° between consecutive estimates, 50 ms apart). The hybrid version
takes the direction from the positions and the magnitude from the reported speed. Orientation
correction: threshold variant in all runs. Mean error / 95th percentile:

| Run | threshold only | velocity, straight | velocity, arc | hybrid, straight | hybrid, arc |
|---|---|---|---|---|---|
| 2 players 60 ms, host ship | 0.35 / 0.45 | 0.60 / 1.00 | 0.52 / 0.69 | 0.57 / 0.72 | 0.55 / 0.71 |
| 2 players 60 ms, client ship | 0.20 / 0.36 | 0.23 / 0.39 | 0.25 / 0.43 | 0.23 / 0.43 | 0.24 / 0.42 |
| 2 players 150 ms, host ship | 0.30 / 0.53 | 0.61 / 0.83 | 0.56 / 0.75 | 0.65 / 0.83 | 0.60 / 0.77 |
| 2 players 150 ms, client ship | 0.38 / 0.66 | 0.31 / 0.48 | 0.28 / 0.46 | 0.31 / 0.48 | 0.30 / 0.46 |
| 6 players: client views host | 0.37–0.38 / 0.45–0.68 | 0.52–0.60 / 0.77–0.81 | 0.52–0.53 / 0.71–0.78 | 0.47–0.56 / 0.71–0.85 | 0.53–0.55 / 0.69–0.71 |
| 6 players: host views client | 0.25–0.27 / 0.41–0.50 | 0.33–0.34 / 0.56–0.58 | 0.33–0.36 / 0.54–0.56 | 0.33–0.38 / 0.56–0.58 | 0.33–0.34 / 0.52–0.55 |
| 6 players: client views client | 0.44–0.51 / 0.80–1.11 | 0.46–0.47 / 0.74–0.75 | 0.48 / 0.73–0.75 | 0.46–0.49 / 0.70–0.74 | 0.44–0.45 / 0.67–0.70 |

Backward steps stayed at or below 1.1 % in all variants.

The velocity-based target only helps where the host relays a client's ship to the other clients
(smaller 95th percentile) and the client ship at 150 ms in 2-player games. For ships whose state
comes straight from their owner (the host's ship, and clients' ships as the host sees them) it is
clearly worse. The velocity from position differences describes the past update interval and is
smoothed, so in continuous turns it lags; the reported nose is the state at send time, and the proxy's
own physics adds the drift of its ship class. Not enabled by default (`VelocityExtrapolation=0`).

## v1.2.1 release check

Exact release build, default settings (`OrientationCorrection=1`, velocity and arc extrapolation off):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.34 / 0.46, 0.0 % | 0.19 / 0.34, 0.0 % | – | 6 of 6 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.30 / 0.50, 0.1 % | 0.31 / 0.61, 0.0 % | – | 14 of 14 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.39 / 0.67, 0.6 % | 0.23 / 0.40, 0.0 % | 0.39 / 0.79, 0.0 % | 72 of 75 seen by all, 1 by none |

(mean / 95th percentile, backward steps.) No hangs or script errors. The six-player run had 21
shield/hull mismatches among the matched torpedo hits (earlier runs: 0–12), see issue #15.

## Issue #18: ghost ship after a join race (v1.3.0)

Six-player runs with 45 s, 100 ms ±40 ms latency and 3 % loss; the clients join one after another,
3 s apart. Without the fix 4 of 6 runs had one client that never saw the ship of the player who
joined just before it. The DLL log of that client always showed the same order: the forwarded create
message of the other ship arrived first and created the ship without a set, the host's catch-up copy
of the same ship followed 0.1–0.4 s later and was dropped ("object exists"). With `FixGhostShip=1`
the catch-up copy put the ship into its set (4 cases in 4 runs, each logged as "ghost ship fix ...
now in its set"); every client saw every ship, mean errors 0.2–1.0 units as in the other runs.

## Issues #19 and #20: knockback and distance LOD (v1.3.0)

`--kick 30:20:30`: from 30 s on every player adds 20 units/s to its own ship's velocity, half
sideways and half backwards, once every 30 s (player i at 30 + 4 i s). Six players, 150 s, 60 ms
±20 ms, 1 % loss. Each (kick, viewer) pair: peak error in the 15 s after the kick, and the time the
error stays above 5 units. Kicks around the ship's own death are left out.

| Build | Pairs | Peak error median / 90th / max | Time above 5 units, mean |
|---|---|---|---|
| v1.2.1 | 115 | 54.4 / 93.9 / 112.8 | 11.0 s |
| + large backward errors at full gain | 130 | 11.3 / 15.6 / 20.0 | 6.4 s |
| + `KnockbackCorrection` | 125–130 | 5.4 / 23–27 / 33–48 | 1.9–2.3 s |
| + LOD thresholds ×3 (v1.3.0 defaults) | 120 | 4.7 / 6.3 / 8.3 | 0.6 s |

v1.2.1 limited every backward correction to half the distance flown per frame, so a proxy that the
real ship's knockback left behind never caught up. With full gain beyond 30 % of the speed it caught
up, but slowly: the proxy kept flying along its nose at its engine speed (5.4 units/s in the trace)
while the target moved sideways at |v| (17.9), which settled at a constant lag of about 6 units. The
knockback path removes that lag. The remaining tail came from distant ships: beyond 343 units the stock
LOD sends one update per second (beyond 228 units four), and the knocked ships flew apart. With the
thresholds ×3 the client views in the kick run dropped from 1.93 / 10.52 (mean / 95th percentile) to
1.01 / 4.27, the host's upload from 12 to 24.5 kB/s.

Normal fights (no kicks, six players, 180 s, 60 ms): client views of other clients 0.43–0.47 / 0.81–0.89
with the new defaults, 0.38–0.46 / 0.65–0.90 with both features off, 0.39 / 0.79 for v1.2.1 (run to
run spread; the Delta ship alone varies between 0.4 and 0.8). The knockback path was active in at most
0.8 % of the correction calls.

## Issue #9: receiver torpedo values (v1.3.0)

Six players, 180 s, 60 ms ±20 ms, 1 % loss, two runs per setting. "Moved": how far a client's replay
had to move its own copy of the torpedo to the host's impact point (sum over the five clients).

| `SymmetricTorpedoSpawn` | Replays | Moved, mean / max | Host torpedo hits seen by all five clients | Client hits the host did not have |
|---|---|---|---|---|
| 0 (stock) | 342, 357 | 11.2 / 818, 11.4 / 965 | 60 of 62, 58 of 61 | 19, 20 |
| 1 | 528, 516 | 9.7 / 477, 8.4 / 323 | 93 of 93, 99 of 102 | 28, 11 |

The host decides every hit with its own copy of the torpedo, and for a client's torpedo that copy is a
remote spawn as well. With the shooter's values the host's copy flies the shooter's path, and all
clients' copies start closer to it. On by default since v1.3.0.

## Issue #15: duplicate hit reports (v1.3.0)

The host reports each torpedo hit once. It only compared a hit with the last reported torpedo, and
the stock collision code applies a hit that bleeds through the shield as two events (shield, then
hull). Disruptors fire in pairs, so the events of two projectiles interleave (A shield, B shield,
A hull, B hull) and the hull event of A was reported again. Clients replayed that second report with
another local copy as a hull hit, or found no copy (`no_torpedo`). Six players, 180 s, 60 ms:

| Build | Projectile hits on the host | Hit reports sent | Client replays (all five) | `no_torpedo` | `waiting` | Moved, mean |
|---|---|---|---|---|---|---|
| before (4 runs) | 93–104 | 105–118 | 460–528 | 32–89 | 44–96 | 8.4–9.7 (2 runs with this statistic) |
| v1.3.0 (2 runs) | 123, 87 | 123, 87 | 591 of 594, 391 of 395 | 0, 0 | 0, 0 | 7.0, 7.3 |

With one report per torpedo the replays no longer wait for a second copy, and shield/hull mismatches
between host and clients dropped to 4 and 0 (before: 0–25 per run).

## v1.3.0 release check

Exact release build, default settings. Mean / 95th percentile, backward steps:

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.33 / 0.45, 0.0 % | 0.21 / 0.36, 0.2 % | – | 4 of 4 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.31 / 0.53, 0.1 % | 0.31 / 0.59, 0.0 % | – | 4 of 4 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.34 / 0.44, 0.0 % | 0.25 / 0.39, 0.0 % | 0.41 / 0.76, 0.1 % | 64 of 67 seen by all, 0 by none |

A second six-player run with the same sources (development build): 0.37 / 0.46, 0.24 / 0.40,
0.44 / 0.83, 108 of 109 hits seen by all. Kick test (exact build): 120 (kick, viewer) pairs, peak error
median 5.3, 90th percentile 7.1, max 7.9 units, 0.8 s above 5 units on average. Join tests: the
ghost-ship fix fired in 2 of 3 runs, every client saw every ship. No hangs or script errors.
