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

## Issue #11: velocity tracking (v1.4.0)

`tools/lateral.py` splits each remote view's error along the owner's velocity (+ = ahead) and
laterally, by the owner's turn rate, and compares the remote ship's nose with the owner's (the test
pilot logs the forward vector since this version).

**Cause.** With v1.3.0 the remote ship's nose lagged the owner's by a median of 29–33° (90th
percentile 34–38°). The distribution is bimodal: a few degrees in straight flight, 30–40° in every turn.
NetworkAI turns the proxy physically and slowly, and the threshold correction of v1.2.1 only acts
beyond 30°. The proxy flies along its own nose, so in turns it flew a different path, and the position
correction pulled it sideways: in turns of 0.3–0.6 rad/s the lateral error was 0.30–0.33 units for the
host's ship (outside the curve) and 0.22–0.46 for the client's ship (inside the curve; that ship class
drifts 50° in such turns).

**Steps** (two players, 60 ms ±20 ms, 1 % loss, 180 s; mean error / 95th percentile; lateral error in
turns of 0.3–0.6 rad/s; nose angle median):

| Variant | Host ship | Client ship | Lateral in turns, host / client ship | Nose |
|---|---|---|---|---|
| v1.3.0 (2 runs) | 0.36 / 0.45–0.53 | 0.18–0.23 / 0.32–0.43 | 0.30–0.33 / 0.22–0.26 | 30–33° |
| tracking: velocity target + feed-forward (2 runs) | 0.26–0.28 / 0.38–0.41 | 0.21 / 0.29–0.31 | 0.14–0.15 / 0.07 | 27–31° |
| + latency compensation −30 ms | 0.11 / 0.19 | 0.07 / 0.12 | 0.11 / 0.05 | 28–31° |
| + latency compensation −50 ms | 0.09 / 0.16 | 0.07 / 0.12 | 0.10 / 0.05 | 30–32° |
| + nose turned fully to the reported one | 0.10 / 0.17 | 0.06 / 0.11 | 0.10 / 0.05 | 4° |
| + extrapolation along the estimated turn | 0.05 / 0.11 | 0.03 / 0.07 | 0.05 / 0.02 | 3° |

- Feed-forward: besides the proportional correction, the proxy is moved each frame by the difference
  between the target's velocity and its own physical velocity, so it no longer flies its own path
  (the same mechanism as the knockback correction, now all the time).
- With the lag gone, the latency compensation (the script's RTT/2 + 30 ms; the 30 ms made up for the
  correction lag) put the ship 0.20–0.27 units ahead; −45 ms (`TrackingLeadOffsetMs`) removes it.
- The nose can follow the reported one fully once it no longer steers the position.
- The velocity estimate from received positions lags in continuous turns; extrapolating along its
  estimated turn (`TrackingArc`) halved the remaining lateral error.

Six players (60 ms) and two players at 150 ms ±50 ms, 5 % loss:

| Run | v1.3.0 | tracking, straight | tracking + turn |
|---|---|---|---|
| 6 players: client views host | 0.37 / 0.53 | 0.08–0.09 / 0.13 | 0.06 / 0.11 |
| 6 players: host views client | 0.23 / 0.38 | 0.06–0.07 / 0.12–0.13 | 0.05 / 0.10 |
| 6 players: client views client | 0.40 / 0.70 | 0.09 / 0.17 | 0.07 / 0.15 |
| 6 players: lateral in turns, host ship / client ships (direct / relayed) | 0.33 / 0.15 / 0.24 | 0.09–0.10 / 0.07–0.08 / 0.09 | 0.04 / 0.04 / 0.05 |
| 2 players 150 ms: host ship | 0.30 / 0.51 | 0.17 / 0.27 | 0.08 / 0.20 |
| 2 players 150 ms: client ship | 0.33 / 0.61 | 0.11 / 0.16 | 0.07 / 0.15 |

The smoothness of the remote tracks (RMS of the second difference of positions 100 ms apart) changed
by −13 % to +18 % against v1.3.0, without a consistent direction. Torpedo replays: 0–7 of about 470 expired per six-player run with tracking,
0–2 without; 82–92 of 84–94 host hits were seen by all five clients. Knockback test with tracking (no
turn): peak error after a kick median 3.4 units, max 5.8 (v1.3.0: 5.3 / 7.9).

## Replays without a hit and a client crash (v1.4.0)

**Replays without a hit.** A client's replay of a host-confirmed torpedo hit gives up after three
approaches. In six-player runs this happened 0–9 times per game (sum over the five clients), mostly
for the same hit on every client. The log of such cases showed the copy stopped next to or inside the
ship without a collision, for example for hull hits that the host reported 1.2 units from the ship's
centre (deep inside the hull). Since v1.4.0 the copy is then put just inside the impact point, then at
the centre, then treated as a shield hit, and the collision is run at once; the copy is retired
afterwards like after a normal replay. Nine six-player runs: 0–14 such forced hits per game, no replay
left without a hit, 98–100 % of the host's torpedo hits seen by all five clients.

**Client crash.** Twice (once with a v1.2.1 build, once during this work) every client crashed at the
same moment, each time within a second of a ship's death: access violation reading 0x3F8000C0 in
`hk_DetectCollision`, called from the proximity pass (return address 0x5A88D4). The replay forces the
host's shield/hull decision by changing the target's shield facings, read through the ship's shield
pointer (+0x2C0). For an exploding ship that pointer held 1.0f (0x3F800000). The facings are now only
used for a living ship (death time +0x14C still FLT_MAX) whose shield pointer is committed, writable
memory with a vtable in the game's `.rdata`. In the release check the check caught such a pointer
twice; no crash in any run since, including runs where all hulls were set to 5 % at 40 s.

## v1.4.0 release check

Exact release build, default settings. Mean / 95th percentile, backward steps:

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.15, 0.0 % | 0.05 / 0.09, 0.0 % | – | 8 of 8 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.05 / 0.10, 0.0 % | 0.05 / 0.10, 0.0 % | 0.08 / 0.16, 0.0 % | 93 of 93 seen by all |
| 6 players, 60 ms (second run) | 0.05 / 0.09, 0.0 % | 0.05 / 0.10, 0.0 % | 0.08 / 0.16, 0.0 % | 48 of 48 |
| 6 players, all hulls at 5 % after 40 s | 0.05 / 0.10, 0.0 % | 0.05 / 0.11, 0.0 % | 0.09 / 0.18, 0.0 % | 61 of 61 |

Lateral error in turns of 0.3–0.6 rad/s 0.035–0.048 units; nose angle median 2.9–4.8°. Replays:
0–10 forced hits per game, none left without a hit. The build before the last change (pointer check
with `VirtualQuery` instead of `IsBadReadPtr`, otherwise the same): 2 players at 150 ms 0.10 / 0.21
and 0.06 / 0.13; kick test peak error median 3.2, 90th percentile 4.2, max 7.5 units; two join tests
with every ship visible on every client. No crashes in any of these runs.

## Issue #12: torpedo hits without the latency (v1.5.0)

`tools/hitlag.py` matches every torpedo hit of the host with the hit each client shows (same
target, within 1.5 s) and with the hit on the shooter's own machine (`src=` in the hit lines of the
test pilot). Six players, 180 s, 60 ms ±20 ms, 1 % loss; median (10th / 90th percentile).

**Before (v1.4.0).** Clients suppress their own torpedo/ship collisions and replay the host's
report: a client showed a hit 158–161 ms after the host, 190–201 ms for torpedoes the host fired.
A client that fired the torpedo saw its own hit just as late (it waited for the report too).

**Two causes, two changes:**
- *Prediction* (`PredictTorpedoHits`): a client shows the collision of its own copy at once and
  remembers it; the host's report for that shooter and target confirms it (no replay). Only reports
  without a prediction are replayed.
- *Copies start late* (`TorpedoLatencyCompensation`): a torpedo spawned from a remote fire message
  starts one message latency after the shooter's, so the copy hits later too. It is moved ahead along
  its velocity by the latency of fire messages from that ship (RTT/2 − 15 ms per hop; other clients'
  fire messages come through the host, two hops). Pulse weapons (disruptors) have their own fire
  message (opcode 0x1A, 0x0069FBB0 → 0x005762B0); without them the copies still trailed by 88 ms
  (host) and 163 ms (clients), measured with the torpedo positions every peer logged (`Q` lines,
  `tools/torptrack.py`).

| Variant | Client − host | Client − host, host's torpedoes | Predicted / confirmed / not confirmed | Replays | Shield/hull differs |
|---|---|---|---|---|---|
| v1.4.0 (2 runs) | +158, +161 ms | +201, +190 ms | – | 538, 383 | 5, 0 |
| prediction only | +74 ms | | 293 / 269 / 24 | 135 | 1 |
| prediction + advancing on all peers (3 runs) | +1 … +5 ms | | 335–427 / 315–414 / 5–20 | 15–31 | 5–19 |
| prediction + advancing on the clients (2 runs) | −68 ms | −14, +54 ms | 324–437 / 313–428 / 9–11 | 35 | 6, 10 |

With advancing on the host too, every machine showed the hit at the same moment, but the host's
advanced copy decided shield or hull differently from the shooter's in 2–6 % of the hits (a segment
that starts close to the target). Since the host's decision applies the damage, the host keeps its
unadvanced copy (default; `TorpedoLatencyCompensation=2` advances it too). The host then applies a
client's hit about 90 ms after the client saw it; the clients show it with the shooter (+20 … +32 ms).
"Not confirmed": the client showed a hit the host did not have (2–3 %), about as often as a client
showed a hit of its own before (analysis: 7–11 such hits per run before, 9–21 after).

## Issue #22: phaser banks (experimental `PhaserSync`, off by default)

`tools/banks.py` compares the firing intervals of every phaser bank (the test pilot logs every change
of every bank's firing state on every peer, `B` lines) with the shooter's own; `tools/phasers.py` does
the same for the phaser hits per shooter and target (beams). Six players, 180 s, 60 ms, same build,
PhaserSync off / on (1: other peers start and stop the banks as the owner reports them) / blocking
only (2: other peers only block and stop banks the owner does not fire):

| | off | 1 (2 runs) | 2 (2 runs) |
|---|---|---|---|
| bank firings of the shooter missing on another peer | 16.5 % | 8.1–8.8 % | 8.5–11.9 % |
| bank firings only on another peer | 170 | 0–2 | 0 |
| start delay on the other peer (median) | 281 ms | 226–244 ms | 276–290 ms |
| beams the shooter hit and another peer did not show | 15.9 % | 14.6–17.7 % | 17.7–20.2 % |
| beams another peer showed and the shooter did not | 71 | 1–3 | 1–4 |
| client shooters' beams the host did not show (no damage) | 8.1 % | 8.0–10.4 % | 8.9–13.1 % |

The banks then fire as on the shooter's machine, and the beams no other machine should show are gone.
The hits do not follow: another peer's beam still starts about 280 ms late and ends about 100 ms
earlier, and a beam that touches its target for less than about 300 ms shows no hit (missed hits came
with a median firing time of 280–320 ms on that peer, shown hits with 490–500 ms). The hits that
peers showed from banks the shooter did not fire had covered part of these misses before. A fix
needs the hits themselves from the shooter or the host, as for torpedoes. Therefore off by default.

**Shield or hull of a predicted hit.** A predicted hit uses the client's own decision. Two causes
made it differ from the host's in up to 25 of about 450 client hits per run:
- A shield facing near the "shield down" limit (0.1): the host had 306/3000, the client 294/3000 (the
  health sync has a resolution of 1/255). A client now does not predict hits on a ship with a facing
  between 8 % and 12.5 %; the host's report decides those (blocking 5–16 % instead cost too many late
  hits: 40 % replays in one run).
- An advanced copy at short range: the copy's hit-test segment started inside the target's shield
  ellipsoid, which tests the hull first (a hull hit through full shields). The first test of an
  advanced copy now starts at its spawn point.

Three six-player runs with both: shield/hull differs on 1–4 matches (before 15–25), client − host
−51 … −71 ms (median), 61–98 of 370–407 client hits replayed (late), 1–25 predictions not confirmed
(25 in one run, all from one ship's disruptors on the host's ship).

## v1.5.0 release check

Exact release build, default settings (`PredictTorpedoHits=1`, `TorpedoLatencyCompensation=1`,
`PhaserSync=0`). Mean / 95th percentile, backward steps:

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.15, 0.0 % | 0.05 / 0.10, 1.2 % | – | 4 of 4 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.21, 0.0 % | 0.07 / 0.14, 0.0 % | – | 3 of 3 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.13, 0.0 % | 0.05 / 0.11, 0.0 % | 0.07 / 0.16, 0.0 % | 80 of 81 |
| 6 players, 60 ms (second run) | 0.06 / 0.13, 0.0 % | 0.05 / 0.10, 0.0 % | 0.07 / 0.14, 0.0 % | 54 of 54 |
| 6 players, all hulls at 5 % after 40 s | 0.07 / 0.15, 0.0 % | 0.05 / 0.12, 0.0 % | 0.07 / 0.17, 0.1 % | 74 of 74 |

Torpedo hit timing (six players): client − host −65, −67, −71 ms; clients − shooter +12 … +25 ms;
host − shooter +79 … +85 ms (the host applies the damage with its own copy). Predicted hits 282–382 per
run, not confirmed 2–11, replays 30–71. Kick test: peak error median 3.4 units, max 6.0. Join test:
every client saw every ship. Installer update from v1.4.0 tested. No crashes.

## Issue #22: phaser hit authority (v1.6.0, `PhaserHitAuthority=1`)

Bank sync alone (v1.5.0, `PhaserSync=1`) matched the banks but not the hits. With hit authority the
owner also reports its beams' hit state, and the other peers' beams follow: no local arc test, no
start wait, no stock stop while the owner fires, and the bank is driven with the frame time when the
weapon system does not feed it (the host's damage). `tools/banks.py`, `tools/phasers.py` and
`tools/combat.py`; six players, 180 s, 60 ms ±20 ms, 1 % loss, development builds in steps:

1. original decisions (v1.5.0 default), 2 runs;
2. hit authority, the beam hits by the local growth timer shortened by the latency, 2 runs;
3. the beam hits when the owner's is in state 3, 4 runs;
4. as 3, state 3 only with the owner's ray on an object, and the hit that StopFiring applies only if
   the owner's ray was on an object at its stop (v1.6.0), 2 runs.

| | 1 | 2 | 3 | 4 (v1.6.0) |
|---|---|---|---|---|
| bank firings of the shooter missing on another peer | 22.1–23.1 % | 6.1–6.6 % | 5.4–8.2 % | 6.9–7.9 % |
| bank firings only on another peer | 213–228 | 1–5 | 0–10 | 0–3 |
| start delay on the other peer (median) | 273–279 ms | 222–225 ms | 222–234 ms | 219–221 ms |
| beams the shooter hit and another peer did not show | 14.1–15.6 % | 3.3–3.7 % | 3.5–4.8 % | 2.5–4.1 % |
| client shooters' beams the host did not show (no damage) | 6.4–10.9 % | 0.0 % | 0.0–0.9 % | 0.0–1.3 % |
| host's beams a client did not show | 13.8–19.4 % | 2.7–5.3 % | 4.1–7.8 % | 5.5 % |
| beams another peer showed and the shooter did not | 58–70 | 103–117 | 43–118 | 3–10 |
| of these with more than one hit event | 4–6 | 55–61 | 0–5 | 0–3 |
| of these on the host (damage) | 5–9 | 13–15 | 8–22 | 1–3 |
| host's phaser hits (0.5 s bins) also on a client | 93–95 % | 99–100 % | 96–100 % | 100 % |

Step 2: a forced beam also hit where the shooter's beam did not (longer beams only other peers
showed). Step 3 removed those, but left single hit events that only other peers showed, up to twice
as many as with the original decisions. Their cause: StopFiring applies a last hit to the object the
beam's ray touches (+0xFC), also while the beam still grows (state 2). The other peers skip the
start wait, so their ray touches the target earlier than the shooter's; a beam the shooter stopped
during its start wait (or with its ray beside the target) showed one hit on the other peers and none
on the shooter. Step 4 reports the ray's object as a third bit mask and applies the stop's hit only
when the owner's ray was on an object.

The remaining missing beams are mostly on clients: a client's report reaches the other clients
through the host, one latency later, and beams up to 1 s long make up most of them.

Hull differences of the clients against the host (`combat.py`, samples with more than 5 % of the
maximum hull apart) varied from 4 to 89 per run without a clear difference between the steps; viewer
stalls in the sandbox (frame gaps of 0.5–1 s, all ships jump at once) cause part of them.

Settings: `PhaserHitAuthority=1` (default) uses mode 3 of the phaser sync; with
`PhaserHitAuthority=0`, `PhaserSync=1` or `2` select the v1.5.0 bank-only modes. The report uses a
new message type (`MSG_BASE + 4`, five integers: ship, firing banks, target, banks that hit, banks
whose ray is on an object): v1.5.0 peers ignore it, and v1.6.0 ignores v1.5.0's bank reports.

## v1.6.0 release check

Exact release build, default settings (`PhaserHitAuthority=1`, `PredictTorpedoHits=1`,
`TorpedoLatencyCompensation=1`). Mean / 95th percentile, backward steps:

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.14, 0.0 % | 0.04 / 0.09, 0.0 % | – | 4 of 4 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.11 / 0.22, 0.0 % | 0.07 / 0.13, 0.0 % | – | 3 of 3 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.13, 0.0 % | 0.05 / 0.11, 0.0 % | 0.07 / 0.14, 0.1 % | 94 of 94 |
| 6 players, 60 ms (second run) | 0.06 / 0.11, 0.0 % | 0.05 / 0.09, 0.0 % | 0.07 / 0.15, 0.0 % | 89 of 90 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.11, 0.0 % | 0.05 / 0.09, 0.0 % | 0.07 / 0.14, 0.0 % | 61 of 61 |

Phasers against the v1.5.0 release check (the same three six-player runs each: two plain, one with
all hulls at 5 % after 40 s):

| | v1.5.0 | v1.6.0 |
|---|---|---|
| bank firings of the shooter missing on another peer | 18.7–22.7 % | 5.2–6.3 % |
| bank firings only on another peer | 154–258 | 0–5 |
| start delay on the other peer (median) | 271–279 ms | 223–224 ms |
| beams the shooter hit and another peer did not show | 13.0–15.7 % | 3.3–4.7 % |
| client shooters' beams the host did not show (no damage) | 7.1–12.0 % | 0.0 % |
| host's beams a client did not show | 14.3–19.5 % | 5.5–6.4 % |
| beams another peer showed and the shooter did not (on the host) | 44–70 (8–12) | 1–11 (0–1) |
| host's phaser hits (0.5 s bins) also on a client | 91–97 % | 99–100 % |
| hull divergences > 5 % of the maximum (`combat.py`) | 18–90 | 11–36 |

Torpedo hits: client − host −71, −66, −65 ms (median); shield/hull differed on 10, 1 and 1 matched
hits (v1.5.0 release check: 16, 9, 19). Kick test: peak error median 3.6 units, max 5.7. Join test:
every client saw every ship. Installer update from v1.5.0 tested; the kept `bcmpfix.ini` has
`PhaserSync=0` and no `PhaserHitAuthority`, and a run with that setting logged every hit authority
hook as applied. No crashes.

## Issue #23: phaser start delay (v1.7.0)

In v1.6.0 another peer started a reported phaser bank a median 223 ms after the shooter (60 ms
±20 ms per direction). Test-mode telemetry (`tools/phasertiming.py`: the owner's change, the report's
send and arrival, the bank's start and hit on each peer, all on one clock) split it up, median ms:

| path | v1.6.0: arrival / start after arrival / total | target list fix | + direct reports (v1.7.0) |
|---|---|---|---|
| host → client | 85 / 152 / 236 | 83 / 7 / 90 | 70–72 / 1 / 73 |
| client → host | 89 / 76 / 198 | 88–89 / 7 / 94–96 | 75 / 1 / 77–78 |
| client → other client | 174 / 97 / 290 | 170–172 / 7 / 177–178 | 76–77 / 1 / 77 |

The hit state followed the report within 2 ms in all versions, the start did not: Fire checks that
the target is in a global list (0x0098D32C) that the game rebuilds for each ship right before that
ship's weapon update. A start from the frame hook checked another ship's list and failed, so the bank
started only with the weapon system's own retry every 0.33 s. A bank the owner fires now passes that
check. The second part was the star network: a client's report went to the host and from there to
the other clients. With `DirectPhaserReports` every bcmpfix machine sends its reports straight to
every other one, as a UDP packet on the game socket, besides the reliable message through the host
(the first copy wins); that also skips the game's send queue, about 10–15 ms per hop.

Six players, 180 s, two runs each (`tools/banks.py`, `tools/phasers.py`, `tools/combat.py`):

| | v1.6.0 | target list fix | + direct reports (v1.7.0) |
|---|---|---|---|
| start delay on the other peer, median (10th, 90th) | 223 ms (111–118, 336–342) | 134–135 ms (55–56, 190–202) | 57 ms (33–34, 90–91) |
| stop delay on the other peer, median: host ↔ client, client → client | 117–129, 211–216 ms | 115–131, 212–215 ms | 103–110, 109–110 ms |
| beam duration on the other peer − shooter's (median) | −61 … −66 ms | +52 … +53 ms | +51 … +52 ms |
| bank firings of the shooter missing on another peer | 5.2–6.3 % | 3.6–4.8 % | 3.1–3.7 % |
| bank firings only on another peer | 5 | 13–18 | 12–17 |
| beams the shooter hit and another peer did not show | 3.6–4.7 % | 3.7–4.0 % | 3.2–4.0 % |
| client shooters' beams the host did not show (no damage) | 0.0 % | 0.0–0.5 % | 0.0–0.5 % |
| host's beams a client did not show | 6.2–6.4 % | 4.5–6.0 % | 4.9–5.6 % |
| beams another peer showed and the shooter did not | 1–11 | 3–5 | 2–5 |
| host's phaser hits (0.5 s bins) also on a client | 99–100 % | 100 % | 100 % |

`banks.py` reads the bank states the test pilot polls every 50 ms, so its delays differ from the
telemetry by a few tens of ms. Stop reports arrive as fast as start reports (median 74 ms in both
directions with direct reports), and the bank stops in the next frame; yet the polled stop comes
30–50 ms later than the polled start in every version (v1.6.0 too), so a beam now lasts about 50 ms
longer on the other screens than on the shooter's (before about 60 ms shorter).

Direct reports in the sandbox: every client got each other client's address from the host a few
seconds after that client joined; per run the host and a client each sent 270–330 reports and received 610–760. The sandbox
runs on loopback addresses, so NAT and firewalls are not tested; when the direct packets do not
arrive, the reliable report through the host still does.

## v1.7.0 release check

Exact release build, default settings (`PhaserHitAuthority=1`, `DirectPhaserReports=1`). Mean / 95th
percentile, backward steps:

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.13, 0.0 % | 0.04 / 0.09, 0.0 % | – | 8 of 8 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.20, 0.0 % | 0.07 / 0.14, 0.1 % | – | 9 of 9 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.11, 0.2 % | 0.05 / 0.09, 0.3 % | 0.07 / 0.14, 0.3 % | 68 of 70 |
| 6 players, 60 ms (second run) | 0.06 / 0.12, 0.0 % | 0.05 / 0.10, 0.0 % | 0.07 / 0.15, 0.0 % | 79 of 79 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.11, 0.0 % | 0.05 / 0.09, 0.0 % | 0.07 / 0.14, 0.0 % | 49 of 49 |

The two torpedo hits of the first six-player run that one client missed had no torpedo copy on that
client (`no_torpedo 2`); this also happened in the v1.5.0 release check and in a v1.6.0 development
run.

Phasers against the v1.6.0 release check (the same three six-player runs each):

| | v1.6.0 | v1.7.0 |
|---|---|---|
| start delay on the other peer, median (10th, 90th) | 223–224 ms (111–119, 336–353) | 57–58 ms (33–36, 91–94) |
| stop delay on the other peer, median: host ↔ client, client → client | 117–129, 211–216 ms | 107–108, 109 ms |
| beam duration on the other peer − shooter's (median) | −61 … −79 ms | +51 … +53 ms |
| bank firings of the shooter missing on another peer | 5.2–6.3 % | 3.5–3.9 % |
| bank firings only on another peer | 0–5 | 3–8 |
| beams the shooter hit and another peer did not show | 3.3–4.7 % | 3.6–4.4 % |
| client shooters' beams the host did not show (no damage) | 0.0 % | 0.0–0.5 % |
| host's beams a client did not show | 5.5–6.4 % | 5.4–7.2 % |
| beams another peer showed and the shooter did not | 0–11 | 0–6 |
| host's phaser hits (0.5 s bins) also on a client | 99–100 % | 100 % |
| hull divergences > 5 % of the maximum (`combat.py`) | 11–36 | 18–43 |

With `DirectPhaserReports=0` (120 s, six players): start delay median 137 ms (10th 57, 90th 203),
the reports came only through the host, no direct packets. Torpedo hits: client − host −72, −68,
−68 ms (median); shield/hull differed on 10, 3 and 13 matched hits. Kick test: peak error median 3.4
units, max 5.4. Join test: every client saw every ship. Installer update from v1.6.0 tested. No
crashes.

## Issue #13: dedicated server and ten clients (v1.7.0)

Eleven game instances in one Windows Sandbox: a dedicated server (`stbc.exe -Dedicated`, no ship of
its own) and ten clients, each with the scripted pilot, 180 s per run, mixed Federation, Klingon,
Romulan and Cardassian ships, exact v1.7.0 release build unless noted. The game's menu allows at
most eight players (`MissionMenusShared.MAX_PLAYER_LIMIT`); the test pilot raises the limit of the
host's game to 12 (`MultiplayerGame.SetMaxPlayers`, the field at +0x1FC), and all ten clients joined
and played. One listen-server run (the host plays too: eleven players) for comparison.

Test harness: `mptest.py --dedicated --max-players N` and up to 15 clients; `sbx_job.py` passes both.
The analysis tools now only count what happens while every instance is in the game: `analyze.py`
(positions) drops the samples after the first player quits, `banks.py`, `phasers.py` and
`phasertiming.py` only take events from 5 s after the last player joined to 1 s before the first
one quits. With ten clients that join and leave 3 s apart, 30 s at each end had distorted the
results: a ship that left is logged with stale or wrong positions, and a player who has not joined
yet cannot show a beam. The six-player release check runs of v1.7.0 are evaluated the same way
below (their phaser numbers are lower than in the v1.7.0 section, which counted the join and leave
phases).

Positions (mean / 95th percentile in game units, backward steps), torpedo hits:

| Run | Host sees clients | Clients see clients | Torpedo hits on the host seen by all ten clients | Clients − host (median) |
|---|---|---|---|---|
| dedicated, original game (`--fixes off`), 60 ms ±20 ms, 1 % loss | 2.38 / 5.38, 0.7 % | 6.24 / 12.15, 3.9 % | 72 of 141 | +176 ms |
| dedicated, v1.7.0, 60 ms (run a) | 0.08 / 0.17, 0.0 % | 0.10 / 0.23, 0.1 % | 125 of 127 | −70 ms |
| dedicated, v1.7.0, 60 ms (run b) | 0.09 / 0.18, 0.0 % | 0.55 / 0.27, 0.0 % (*) | 149 of 149 | −77 ms |
| dedicated, v1.7.0, all hulls at 5 % after 40 s | 0.11 / 0.23, 0.1 % | 0.16 / 0.34, 0.2 % | 133 of 140 | −88 ms |
| dedicated, v1.7.0, 150 ms ±50 ms, 5 % loss | 0.19 / 0.41, 0.2 % | 0.30 / 0.64, 1.0 % | 142 of 151 | −97 ms |
| listen server (eleven players), v1.7.0, 60 ms | 0.13 / 0.25, 0.0 % (clients see the host: 0.16 / 0.32) | 0.17 / 0.36, 0.2 % | 116 of 116 | −98 ms |
| six players (v1.7.0 release check, same evaluation) | 0.05 / 0.09–0.10, 0.0–0.4 % | 0.07 / 0.14–0.15, 0.0–0.4 % | 68 of 70, 77 of 77, 49 of 49 | −68 … −72 ms |

(*) One client's respawned ship was never created on the dedicated server (see below); that client
then got no position updates for 20 s, and its view of the others drifted by up to 117 units. Without
that client the other clients saw each other with 0.12 / 0.26.

Phasers (in the common phase):

| Run | bank firings missing on another peer | beams the shooter hit and another peer did not show | client beams without damage on the host | beams only other peers showed | start delay (median) |
|---|---|---|---|---|---|
| dedicated, original game | 69.6 % | 55.6 % | 42.5 % | 464 | 323 ms |
| dedicated, v1.7.0, 60 ms (a, b) | 0.2 %, 2.5 % | 0.2 %, 2.7 % | 0.3 %, 3.0 % | 10, 20 | 70–72 ms |
| dedicated, v1.7.0, hulls at 5 % | 2.1 % | 2.8 % | 0.6 % | 25 | 72 ms |
| dedicated, v1.7.0, 150 ms, 5 % loss | 2.0 % | 3.7 % | 0.4 % | 33 | 170 ms |
| listen server, v1.7.0, 60 ms | 0.3 % | 0.3 % | 0.6 % (host's beams a client did not show: 1.8 %) | 50 | 76 ms |
| six players (v1.7.0 release check) | 0.0–0.1 % | 0.0–0.3 % | 0.0–0.6 % | 0–6 | 57–58 ms |

The host's phaser hits (0.5 s bins) were also on the clients in 99–100 % (original: 73–74 %). The
direct phaser reports worked with ten clients: each client got the nine others from the host and sent
and received its reports directly. Telemetry: a report reached another peer a median 83–89 ms after
the owner's change (60 ms), and the bank started 1 ms after its arrival.

Load: the host's upload was 86–87 kB/s as a dedicated server and 96 kB/s (peak 113) as a listen
server with ten clients, against 26 kB/s with six players and 39 kB/s in the original game with ten
clients (bcmpfix sends state updates twice as often at close range and triples the distances of the
update tiers). Each client received about 9 kB/s. The jumps in the table's runs (13, 0, 82, 102 and
26 in the listen run, none with six players) came almost all in bursts: one instance saw all ships
jump in the same second, a stall of that instance while eleven games shared the sandbox's CPU.

Findings:

- **Dedicated server: object ID collision (stock bug, #24).** Network objects get IDs from a range per
  player slot: slot 0 from 1073741823, slot 1 from 1074003967, and so on (0x40000 apart). On a listen
  server the host is slot 0. On a dedicated server the first client gets slot 0, and the host gives
  its own local objects (the torpedo copies of every player's shots) IDs from the same range. In run b
  the first client respawned with ID 1073742099, which the host was using for a torpedo copy at that
  moment; the host's object reader refused the create message (`object from network: ... NOT
  created`). Nobody saw that client's new ship, and the host sent the client no more state updates
  (the client's input dropped from 9 to 0.9 kB/s). It happened once in about 1300 ships created from
  network messages in these runs. The original game has the same ID ranges; bcmpfix does not change
  them.
- **Tables per remote ship are never cleared (bcmpfix, #25).** The phaser reports (32 entries), the
  latency per ship (64), and the velocity and orientation tracking of remote ships (64 each) keep an
  entry for every ship ID or proxy they have seen; when a table is full, a new ship takes the entry
  at its ID modulo the table size, possibly a live ship's. A respawn creates a new ship. The runs here
  had at most 14 ship IDs with phaser reports, so it did not show; long games with many players and
  respawns can reach it.
- The original game with ten clients: half of the torpedo hits were missing on at least one client,
  and 43 % of the clients' phaser beams did no damage on the host.

## Issue #26: mixed games (v1.7.0, v1.6.0 and the original game)

Six players, 60 ms ±20 ms, 1 % loss, 180 s. The test harness now runs each instance with its own
version (`mptest.py --variant ROLE=NAME`): `vX.Y.Z` starts it from a copy of the game folder (hard
links) with that release's `dinput.dll` and `scripts/bcmpfix_mp.py`, taken from the release zip;
`off` is the original game as in every other "original" run, i.e. the DLL only serves the test
harness (loopback binding, simulated network, the test pilot's tick) with every fix and the script
layer off. `tools/mixed.py` reports positions, torpedo hits and phaser beams by the versions of the
two instances involved, while all six are in the game.

Clients: two with v1.7.0, two with v1.6.0, one with the original game; two runs per host version,
the second with the versions on other players (and so other ship classes). Mean / 95th percentile in
game units:

| Positions, owner → viewer | host v1.7.0 | host v1.6.0 | host original |
|---|---|---|---|
| bcmpfix → bcmpfix (either version) | 0.06–0.07 / 0.13–0.15 | 0.06–0.08 / 0.12–0.15 | 2.39–3.35 / 4.35–6.07 |
| original → bcmpfix | 0.51 / 0.76–0.77 | 0.51 / 0.73–0.76 | 1.57 / 4.12–4.13 |
| bcmpfix → original | 2.57–2.58 / 4.68–5.77 | 2.34–2.96 / 4.49–5.38 | 3.69–5.15 / 9.05–11.71 |
| original → original | – | – | 3.02 / 6.71 |

All six with the original game: 5.39 / 12.13.

| Torpedo hits of the host also shown by a client | host v1.7.0 | host v1.6.0 | host original |
|---|---|---|---|
| client v1.7.0 | 100 % | 99.7 % | 93.8 % |
| client v1.6.0 | 100 % | 100 % | 93.3 % |
| client original | 89.9 % | 69.6 % | 50.9 % |

All six with the original game: 83.9 %. Shield or hull agreed in 96–99 % of the shown hits.

| Phaser beams also shown by another peer, shooter → peer | host v1.7.0 | host v1.6.0 | host original |
|---|---|---|---|
| v1.7.0 → v1.7.0 | 99.8 % (banks 100 %) | 90.7 % | 70.0 % |
| v1.6.0 → v1.6.0 | 88.7 % | 100 % (banks 96.2 %) | 25.4 % |
| between v1.7.0 and v1.6.0 | 87.1–90.4 % | 89.9–91.3 % | 23.7–69.2 % |
| with the original game | 43.5–88.6 % | 69.9–88.3 % | 17.8–84.1 % |
| client beams shown by the host (damage): v1.7.0 / v1.6.0 / original shooter | 100 / 93.5 / 91.4 % | 94.8 / 100 / 89.0 % | 74.2 / 30.5 / 78.5 % |

All six with the original game: 69.5 % of the beams, 76.6 % of the client beams on the host.

What this shows:

- Mixed games run: no crash, hang, script error or exception in any of the seven runs. The phaser
  reports of v1.6.0 (`MSG_BASE + 4`) and v1.7.0 (`+ 5`) ignore each other, and the direct phaser
  reports only started between a v1.7.0 host and its v1.7.0 clients.
- Positions: a bcmpfix player sees every ship better, also an original player's (0.51 instead of
  about 5 with a bcmpfix host). An original player keeps the original errors, also for bcmpfix ships
  (2.3–3.0). A host without bcmpfix cannot relay the clients' received state, and everybody sees the
  other clients 1.6–5 units off.
- Torpedoes: v1.6.0 and v1.7.0 clients take the hit reports of either host version; with a host
  without bcmpfix there are no reports and each client decides itself (93 % shown). Original
  clients showed 51–90 % of the host's hits (all-original game: 84 %); with one original client per
  run these numbers rest on 112–148 hits each.
- Phasers: the hit authority works only among machines with the same version, including the host,
  which relays the clients' reports. With another host version or a host without bcmpfix, each
  machine decides itself, as in the original game.
- Recommendation: everybody, the host above all, should run the same bcmpfix version.

## Issues #24 and #25 (v1.7.1)

**#24, dedicated server object IDs.** `FixDedicatedObjectIds`: when the DLL finds the game running as
a dedicated server (UtopiaModule: multiplayer, host, not a client), it sets the server's slot to 15
and moves its object counter into that range. In a dedicated run with ten clients and all hulls at
5 % after 40 s the server's torpedo copies got IDs from 1077673983 on (slot 15) instead of 1073741823
(slot 0, the first client's), and all 197 ships from network messages were created; phaser beams
shown by the other peers 100 %, positions 0.11 / 0.24. The server switched twice at its start (the
game set the slot back to 0 once).

**#25, tables per remote ship.** The phaser reports, the latency per ship and the orientation
tracking now give a new ship the entry unused longest when the table is full (the velocity tracking
already did); the velocity and orientation tracking also start over when a proxy has had no update
for 10 s (a new proxy at the address of a deleted one). Six players with all hulls at 1 % every 30 s
from 40 s on (many respawns), with test builds whose tables hold 6 entries instead of 32 or 64,
positions per minute (mean / 95th percentile):

| | 0–60 s | 60–120 s | 120–180 s | 180–228 s | ship IDs |
|---|---|---|---|---|---|
| v1.7.0 code | 0.06 / 0.14 | 0.06 / 0.13 | 0.10 / 0.46 | 0.15 / 0.51 | 11 |
| v1.7.1 code | 0.06 / 0.14 | 0.06 / 0.13 | 0.06 / 0.13 | 0.08 / 0.17 | 14 |

Once the small tables were full, the old code gave new ships the entries of live ones (by ID modulo
the size), and their tracking started over again and again. With the real sizes a five-minute run of
ten clients with hull damage every 25 s reached only 13–16 ship IDs (the waves rarely killed), so the
tables did not fill; three instances of the v1.7.0 run stalled for about 9 s near its end, which
lowered its last minute.

## v1.7.1 release check

Exact release build, default settings. Mean / 95th percentile, backward steps:

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.14, 0.0 % | 0.04 / 0.09, 0.0 % | – | 4 of 4 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.20, 0.0 % | 0.07 / 0.15, 0.0 % | – | 4 of 4 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.14, 0.0 % | 0.05 / 0.11, 0.0 % | 0.07 / 0.15, 0.0 % | 56 of 56 |
| 6 players, 60 ms (second run) | 0.06 / 0.12, 0.0 % | 0.04 / 0.09, 0.0 % | 0.06 / 0.13, 0.0 % | 87 of 89 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.13, 0.0 % | 0.05 / 0.10, 0.0 % | 0.07 / 0.14, 0.0 % | 68 of 68 |
| dedicated server + 10 clients (run 1) | – | 0.12 / 0.23, 0.2 % | (one client stalled 47 s) | 77 of 145 |
| dedicated server + 10 clients (run 2) | – | 0.09 / 0.18, 0.2 % | 0.13 / 0.27, 0.3 % | 145 of 153 |

Phasers, six players: beams shown by the other peers 99.6–100 %, start delay 58–59 ms. Dedicated
runs: the server numbered its objects from slot 15, no object was refused. Mixed game (v1.7.1 host,
two v1.7.0 and two v1.7.1 clients, one original): v1.7.x players saw each other with 0.06–0.08,
phaser beams 100 %, torpedo hits 100 %; the original player as before. Torpedo hits: client − host
−71, −62, −72 ms (median). Kick test: peak error median 3.4 units, max 6.4. Join test: every client
saw every ship. Installer update from v1.7.0 tested. No crashes, no script errors.

Stalls (#27): in dedicated runs with ten clients one client instance stalled for seconds (under 20 frames
per 5 s) in 4 of 10 runs with the v1.7.1 code and in 4 of 9 runs with v1.7.0 (minimum 0–17.8 frames/s
per 5 s, against 50–60 normally). A watchdog of the test build (no frame for 2 s: sample the main thread)
found it busy in the game's code, not in a driver. The sandbox renders on the RTX 3080 Ti (DXGI
adapter 1 in the sandbox; 64–66 % 3D load with eleven instances), and 13 GB of 16 GB stayed free
(the sandbox had 8 GB before; the runs from the stall checks on use 16 GB).

## Issue #27: stalls with ten clients (v1.7.2)

The test build's watchdog (test mode: when no frame starts for 2 s, a thread samples the main thread;
`tools/stackdecode.py` keeps the stack values that are return addresses) found the stalled first
client deleting an object: main loop → event dispatch → 0x005A5800 → destructors 0x004350E0,
0x00430A90 → 0x006D9060 (event receiver) → 0x006DB590 → loop in 0x006E20B0, a pass over every
registered event handler. A counter of the handler registry (test mode, every 10 s) then showed the
first client's registry growing to 98667 handlers in three minutes, while the server had 280 and the
other clients 300–800; between two readings its 32839 handler removals took 11 s. At 6499 handlers
the largest event type, ET_OBJECT_GROUP_CHANGED, held 2052 of them, all the same Python method
(`GroupChanged`) of one receiver for the same group; with the ENTERED_SET and EXITED_SET handlers that
the same call registers, that is 3 × 2052 plus the about 350 handlers of a normal client. The
condition is `ConditionInRange`, which the combat music uses for the enemy group (see findings). With `FixConditionInRange` the first client kept 229–397 handlers.

Dedicated server with ten clients, 180 s, 60 ms ±20 ms, 1 % loss, three runs each, alternating:

| | v1.7.1 | v1.7.2 candidate (fix) |
|---|---|---|
| first client: lowest frame rate per 5 s | 3.3, 57.9, 51.8 | 50.8, 59.8, 57.4 |
| first client: longest gap between frames | 37 s, 0.2 s, 0.3 s | 0.15 s, 0.15 s, 0.16 s |
| clients see clients (mean / 95th), jumps | 0.26 / 0.47, 211; 0.12 / 0.26, 0; 0.14 / 0.30, 0 | 0.12 / 0.26, 0; 0.10 / 0.22, 0; 0.12 / 0.27, 0 |
| torpedo hits on the host seen by all ten clients | 60 of 88, 106 of 106, 147 of 150 | 159 of 160, 163 of 165, 118 of 123 |
| phaser beams also shown by the other peers | 96.1 %, 99.5 %, 100 % | 100 %, 99.8 %, 99.4 % |

Before the fix, 4 of 10 dedicated runs with the v1.7.1 code and 4 of 9 with v1.7.0 had such a stall
(under 20 frames per 5 s). The bursts of position jumps in the earlier ten-client runs (one instance
sees all ships jump at once) were the same stalls on a smaller scale: all four runs with the fix
had none.

## v1.7.2 release check

Exact release build, default settings (`FixConditionInRange=1`). Mean / 95th percentile, backward
steps:

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.13, 0.0 % | 0.04 / 0.09, 0.1 % | – | 9 of 9 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.20, 0.0 % | 0.07 / 0.15, 0.0 % | – | 5 of 5 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.10, 0.0 % | 0.04 / 0.08, 0.2 % | 0.06 / 0.14, 0.2 % | 82 of 83 |
| 6 players, 60 ms (second run) | 0.06 / 0.10, 0.0 % | 0.04 / 0.09, 0.0 % | 0.07 / 0.14, 0.0 % | 45 of 45 |
| 6 players, all hulls at 5 % after 40 s | 0.06 / 0.11, 0.0 % | 0.04 / 0.09, 0.0 % | 0.06 / 0.13, 0.0 % | 85 of 85 |
| dedicated server + 10 clients (run 1) | – | 0.13 / 0.37, 0.0 % | 0.16 / 0.44, 0.2 % | 171 of 173 |
| dedicated server + 10 clients (run 2, PC busy) | – | 0.24 / 0.45, 0.0 % | 0.27 / 0.55, 0.4 % | 112 of 115 |

The second dedicated run and the mixed game overlapped with other load on the PC (other game
processes): the instances ran at a median 20 (dedicated) and 39 (mixed) frames per second instead of
about 65 and 115, which raised the position errors. No stall in either dedicated run (first client:
longest gap 0.19–0.21 s, about 370 handlers at the end, no watchdog sample). Phasers: beams shown by
the other peers 99.8–100 % (six players), 99.4–99.8 % (dedicated). Mixed game (v1.7.2 host; v1.7.1,
v1.7.0 and two v1.7.2 clients; one original): among the v1.7.x players phasers 98.7–100 %, torpedo hits
97.8–100 %, positions 0.09–0.14. Torpedo hits: client − host −66, −63, −66 ms (median). Kick test:
the first run's ships drifted apart (mean distance 1067, up to 2278 units at the end, against about
150 normally), so they got fewer updates: peak error median 3.6, 90th percentile 12.2, max 58.9; two
repeats: median 3.3–3.4, max 5.1–5.2. Join test: every client saw every ship. Installer update from
v1.7.1 tested. No crashes, no script errors.

## Phaser beam length on the other screens (v1.7.3)

The v1.7.0 measurements found a beam lasting about 50 ms longer on the other screens than on the
shooter's. That came from the method: `banks.py` read the bank states the test pilot polls every
50 ms. On the shooter's machine the pilot's StartFiring does not start the bank itself: the bank
starts in the weapon system's next update, after the pilot's poll. Its StopFiring stops the bank at
once, inside the call. So the polled start on the shooter came about 40 ms late and the polled stop
about 10 ms early, and the shooter's beam looked about 50 ms shorter. On the other machines a bank starts
and stops in the frame the report arrives, and the poll sees both with the same lag.

The test build now logs each bank change in the frame it happens: `PO` when the owner's banks change,
`PS` / `PE` when another ship's bank starts / stops on this machine. `banks.py` uses these lines when
a run has them (`--polled` forces the old way). Six players, 60 ms ±20 ms, 1 % loss, two runs:

| | polled bank states | telemetry |
|---|---|---|
| start delay on the other peer, median (10th–90th percentile) | 60 ms (37–96), 60 ms (38–98) | 77 ms (58–100), 78 ms (59–101) |
| duration on the other peer minus the shooter's, median (10th–90th) | +50 ms (−2…+91), +52 ms (+2…+98) | +1 ms (−29…+27), +1 ms (−28…+30) |
| bank firings of the shooter also on the other peer | 100.0 %, 99.9 % | 99.8 %, 99.7 % |

A beam lasts as long on every screen; it starts and stops one network latency (plus up to a frame)
after the shooter's. No code change was needed.

## Issue #28: AI ships' phasers (v1.7.3)

New test: five players (host and four clients) against five AI ships that the host creates
(`mptest.py --ai-ships 5 --target ai`: the pilots attack only the AI ships, the AI ships attack the
players with the game's `BasicAttack` AI, a destroyed AI ship comes back after 15 s). 240 s,
60 ms ±20 ms, 1 % loss. `phasers.py` and `banks.py` take `--shooters ai|players`.

| | AI shooters, v1.7.2 | AI shooters, fix | players, v1.7.2 | players, fix |
|---|---|---|---|---|
| beams of the shooter also shown by the other peers | 91.7 % | 100.0 % | 99.9 % | 100.0 % |
| beams other peers showed and the shooter did not | 191 | 4 | 4 | 0 |
| bank firings also on the other peer (polled) | 70.1 % | 99.4 % | 100.0 % | 99.7 % |
| start delay on the other peer, median (polled) | 207 ms | 62 ms | 59 ms | 55 ms |

The host reported only its own ship's banks, so the AI ships it simulates kept the original
behaviour: every machine decided their firing arcs and hits itself. Now the host's script lists the
ships it simulates besides its own every second (each ship in the game's set that is no player's
ship, `bcmpdll.setowned`), and the DLL reports their banks like its own ship's, each with its own
sequence number (up to 16 ships). With the telemetry, AI ships' banks start on the other peers a
median 68 ms after the host's and last 3 ms longer (players: 73 ms, +1 ms). The host's phaser hits
(0.5 s bins) were on the clients in 94–97 % before, 100 % after.

Other results of the run with the fix: position errors 0.04 (clients see the host, mean),
0.04 (host sees clients) and 0.07 (clients see clients), no jumps; torpedo hits on the host seen by
all four clients 294 of 297; hull values on the clients a mean 0.11 % of the maximum off the host's
(six-player release runs 0.10 %). The flag "shield or hull" of a torpedo hit differed in 38 of 1230
matched hits (3.1 %; v1.7.2 52, 3.5 %; six-player release runs 1–2 %): the AI ships keep the
players' shields low, and a hit on a facing near zero counts as a shield hit on one machine and as a
hull hit on another. The damage is the host's.

## Thirty minutes with ten clients (v1.7.3)

Dedicated server, ten clients and two AI ships that the server creates (they attack the players; a
destroyed one comes back after 15 s), 1800 s, 60 ms ±20 ms, 1 % loss. At 120 s and then every
300 s the server set every hull to 5 %, so ships died and respawned (65 deaths, 8 new AI ships).
Another program (five game instances of a different project) loaded the PC during the run. The run
used the v1.7.3 code without `SnapNewShips` (next section).

- No instance stalled, crashed or logged a script error. Frames per second per 5 s on the clients:
  median 68–71, lowest 23–52 (the dips came with the mass deaths after the hull drops); the server
  105–122.
- Memory: the sandbox had 13.2–14.2 GB of 16 GB free during the whole run, at the end as much as
  at the start. Event handlers stayed at 400–470 per client and 618 on the server (issue #27).
- Positions (mean / 95th percentile, units): clients see the server's AI ships 0.05 / 0.12, the
  server sees the clients 0.08 / 0.17, clients see each other 0.10 / 0.23; backward steps 0.0–0.1 %.
  47 of the 67 jumps were one fast move of an AI ship that every client followed (the AI flew
  75 units/s for half a second, on the server too); the other 20 came from ships that appeared at
  an old position and slid into place.
- Torpedo hits on the server while all ten clients were in the game: 1566 of 1576 seen by all, 5 by
  none. The shield/hull flag differed in 1.9 % of the matched hits.
- Phasers: beams of the shooter also shown by the other peers 99.8 % for the players (37300) and
  99.8 % for the AI ships (28090); bank firings 99.7 % / 99.6 %, start delay median 77 / 84 ms,
  duration difference +1 ms. The server's phaser hits (0.5 s bins) were on every client in 100 %.
- Hull values on the clients were a mean 0.19 % of the maximum off the server's. 36 of 1486
  differences above 5 % lasted longer than 0.4 s (at most 1.1 s), all in fast series of hits
  (torpedo volleys, the forced hull drops), where the clients stay one latency behind.

## Issue #29: ships that appear at an old position (v1.7.3, `SnapNewShips`)

A ship that appears on a peer starts at the position of its "object created" message. A trace
(`TraceStateSec`) of a join showed the ship of the player who had joined 3 s earlier at its spawn
point, 11 units behind, until the first state update; then NetworkAI pulled it into place with its
correction lag (time constant 0.2 s). `tools/spawn.py` measures every ship that appears on a peer
within 5 s after its owner created it. In the 30-minute run 31 of 720 were more than 5 units off at
first and 13 more than 20, up to 110 units. A dedicated run of the original game had 9 of 76 more
than 5 units off and 7 more than 20 (up to 155 units; 90th percentile still 4.7 units after 1 s).

`SnapNewShips` places a proxy at its target in the first correction after its first state update
(setting the position in the reader does not last). Dedicated server, ten clients, two AI ships,
hulls at 3 % every 60 s, 300 s, 60 ms ±20 ms, 1 % loss:

| | without | with |
|---|---|---|
| ships appearing on a peer | 252 | 208 |
| more than 5 units off at the first sample (before the first update arrived) | 7 | 11 |
| 0.25 s later: 90th percentile / maximum | 0.2 / 10.7 | 0.2 / 1.5 |
| 0.5 s later: 90th percentile / maximum | 0.2 / 3.0 | 0.2 / 1.7 |

The ship still shows at the old position until its first update arrives (one latency plus the
update interval), and then is where its owner has it instead of sliding there. Positions of the run
with the fix: clients see the server's ships 0.05 / 0.13, the server sees the clients 0.10 / 0.19,
clients see each other 0.12 / 0.27, no jumps; torpedo hits seen by all clients 251 of 251.

## v1.7.3 release check

Exact release build, default settings (`SnapNewShips=1`). Mean / 95th percentile, backward steps:

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.06 / 0.13, 0.0 % | 0.04 / 0.09, 0.1 % | – | 4 of 4 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.11 / 0.22, 0.0 % | 0.07 / 0.15, 0.1 % | – | 4 of 4 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.14, 0.1 % | 0.05 / 0.11, 0.0 % | 0.08 / 0.18, 0.1 % | 73 of 73 |
| 6 players, all hulls at 5 % after 40 s | 0.05 / 0.10, 0.0 % | 0.04 / 0.09, 0.0 % | 0.07 / 0.15, 0.0 % | 58 of 58 |
| 5 players against 5 AI ships | 0.05 / 0.11, 0.0 % (with the AI ships) | 0.04 / 0.09, 0.0 % | 0.07 / 0.14, 0.0 % | 279 of 283 |
| dedicated server + 10 clients | – | 0.08 / 0.17, 0.0 % | 0.10 / 0.23, 0.0 % | 140 of 142 |

Phasers: beams shown by the other peers 99.9 % and 100 % (six players), 99.6 % (dedicated), 100 % of
the players' and 96.0 % of the AI ships' beams in the AI run: all 95 missing AI beams were one AI
ship firing at the host's ship at 162–175 units, where the clients showed the beam without hits
(#30). Bank firings 99.3–99.9 %, start delay median 76 ms. Ships that appeared on a peer were at
most 0.3–0.4 units off 0.25 s later. Every frame rate stayed above 24 per 5 s; no instance stalled
(no watchdog sample); in the dedicated run the first client had 366 event handlers at the end, the
server 281. In the six-player run the host took one 168 ms frame while it created a joining player's
ship, seen as four jumps of all ships at once. Mixed game (v1.7.3 host; v1.7.2, v1.7.0 and two
v1.7.3 clients; one original): among the v1.7.x players phasers 100 % (banks and beams), torpedo
hits 100 %, positions 0.05–0.09. Kick test: peak error median 3.4, 90th percentile 4.3, max 5.4
(v1.7.2: 3.3–3.4, 5.1–5.2). Join test: every client saw every ship. Installer update from v1.7.2:
settings kept, new files installed (`SnapNewShips` is on by default when the kept `bcmpfix.ini`
lacks it). No crashes, no script errors.

## Issue #30: remote phaser beams without hits (after v1.7.3)

In the v1.7.3 release check one AI ship fired long beams at the host's ship at 162–175 units: the
host showed hits, the four clients showed the beam without hits (95 of 2372 AI beams). It looked
like the edge of the phaser range, but it is not:

- Range test (`mptest.py --range`, `tools/rangetest.py`): the host's ship stands still and fires at
  a ship it moves to 100–250 units, 15 s each. The host and both clients showed the same hits at
  every distance, also beyond 175 units, with and without the fix.
- In the release check run the clients' beam passed beside the host's ship and hit Delta, which
  was behind it, 205 units away: the clients' copy of the bank aimed at a different ship.

Test telemetry (`PB`: once a second per remote bank its beam state, the object its ray touches, its
target, the owner's reported target and the point it aims at; `PG`: a remote bank's target differs
from the owner's) found two ways the copy of a bank aims elsewhere than its owner's:

- **Other target.** Each weapon system picks its banks' targets itself: when Fire fails for the
  current target (out of the bank's arc or range), it goes on with the next target in its list
  (0x00584E40), without stopping the bank. The receiver forces the arc test for banks the owner
  fires (v1.6.0), so the copy never fails it and keeps the old target while the owner's bank has
  switched. Caught once: a client's copy fired at Bravo for 4 s while the owner reported another
  target.
- **Other point.** The bank aims at its target plus an offset (bank +0x90; an AI picks a
  subsystem), which the start-firing command carries and the weapon system's target list keeps
  (+0xC4). A first fix aimed a retargeted bank at the target's centre; at close range (19–30
  units) its ray then missed the target for seconds.

Fix: every Fire of a bank its owner fires aims at the owner's reported target, at the point the
weapon system's list has for it (`aim_at` in `src/phaser.c`; `[Test] PhaserOwnerTarget=0` turns it
off for comparisons). Five players against five AI ships, 240 s, 60 ms ±20 ms, 1 % loss:

| | v1.7.3 behaviour | first fix (target's centre) | fix |
|---|---|---|---|
| runs | 6 | 5 | 4 |
| AI ships' beams the other peers did not show | 75 of 16308 (0.46 %) | 121 of 13088 (0.92 %) | 2 of 9080 (0.02 %) |
| per run: AI beams shown | 98.8–100 % | 96.2–100 % | 99.9–100 % |
| beams other peers showed and the shooter did not | 37 | 8 | 4 |
| a copy fired at another target for 3 s or more | 1 | 0 | 0 |
| a copy's ray missed its target for 3 s or more | 4 | 10 | 0 |
| host's phaser hits (0.5 s bins) also on a client | 99–100 % | 99–100 % | 100 % |

The v1.7.3 release check itself had 96.0 % (one long engagement) and the first 5-against-5 run with
the v1.7.3 code 100 %: single runs vary a lot, so the table sums several.

## The starbase mission ("Defend the Starbase", Mission 5)

The test pilot plays the stock starbase mission with `mptest.py --mission 5 --teams 1,0,1,0,1,0
--target team` (the host starts with `-Mission Mission5`; teams in role order, 1 = defenders,
0 = attackers; attackers go for the starbase, defenders for the attackers). The host creates the
starbase (`FedStarbase`, 220000 hull) with the mission's own `StarbaseAI`. Six players, 240–300 s,
60 ms ±20 ms, 1 % loss, two runs (the second with all hulls at 3 % every 45 s, 11 deaths):

| | run 1 | run 2 |
|---|---|---|
| position error, clients see the host's ships (mean / 95th) | 0.03 / 0.10 | 0.05 / 0.10 |
| position error, the host sees the clients | 0.05 / 0.10 | 0.04 / 0.09 |
| position error, clients see each other | 0.07 / 0.14 | 0.06 / 0.14 |
| the starbase's phaser beams shown by the other peers | 100 % (1575) | 100 % (1405) |
| the starbase's bank firings also on the other peers | 99.6 % | 100 % |
| players' phaser beams shown by the other peers | 99.0 % | 98.1 % |
| host's phaser hits (0.5 s bins) also on a client | 100 % | 100 % |
| torpedo hits on the host seen by all clients | 154 of 170 | 170 of 183 |
| torpedo hits a client showed and the host did not (all clients) | 55, 40 of them on the starbase | 32, 24 on the starbase |
| hull values on the clients off the host's (mean) | 0.11 % | 0.22 % |

The starbase is the host's ship like an AI ship, so since v1.7.3 its phasers are synchronised
(#28); its banks always fired at one target at a time. The missing players' beams were
mostly attackers' beams on the starbase and friendly fire. Torpedoes are less consistent than in
deathmatch (73 of 73, 58 of 58 in the v1.7.3 release check): the defenders fly close to their
starbase and their torpedoes pass right beside it. Each computer creates another player's torpedo
from that player's ship as it sees it, so near a large object the copies part ways: on a client its
copy hits the starbase, on the host it flies on and hits an attacker, and the host's hit cannot be
replayed on that client because its copy is gone (#31). The damage is the host's.

Ships that appear (`tools/spawn.py`): in one run a respawned ship came 375 units off on three
clients and slid into place within a second, although `SnapNewShips` placed it at its first
update: that update already carried the old position. In four more runs (183 ships appearing on
a peer, 33 deaths) two ships came 25 and 38 units off on one client each, the others at most 4
units. Holding back the host's relay of a client ship until the host had that ship's first update
did not help (one ship then stayed 25 units off for a second while the host held back about 69000
writes) and was dropped.
