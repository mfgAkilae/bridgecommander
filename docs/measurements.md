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
