## Bridge Commander multiplayer fix v1.5.0

Torpedo hits now show on every screen at the moment the torpedo hits, not a network latency later.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.**

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.5.0.zip`** into the game folder.

### What is new

- **Torpedo hits without the delay (issue #12).** Since v1.1.0 the host decides every torpedo hit and
  the other players replayed its report, so they saw each hit about 0.16 s after the host, also
  their own torpedoes. Now a player's computer shows the hit as soon as its own copy of the torpedo
  hits and only checks it against the host's report. Torpedoes and disruptor shots fired by another
  player are also moved ahead by the network latency (they used to start that much late).
  New INI options `PredictTorpedoHits` and `TorpedoLatencyCompensation` (default 1).

  Six players, 60 ms: clients showed a hit 158–161 ms after the host (median); now 65–71 ms before
  the host, together with the shooter (12–25 ms after the shooter's own screen). The host still applies the damage, from its own copy,
  about one latency after the shooter saw the hit. A client shows a hit the host does not confirm in
  1–4 % of the hits; when a shield facing is close to failing, the client waits for the host's
  decision, so shield or hull agree in about 96–98 % of the hits.
- **Phasers (issue #22, experimental, off).** Each computer decides on its own which phaser banks can
  fire, so beams differ between players. `PhaserSync=1` makes the other computers fire exactly the
  banks the shooter fires; the banks then match, but the hits do not get better yet (the beams start
  about 0.3 s late on the other screens and a short beam shows no hit). Details:
  `docs/measurements.md`.
- **Test tools.** `tools/hitlag.py` (when each machine shows a hit), `tools/torptrack.py` (torpedo
  copies against the shooter's), `tools/banks.py` and `tools/phasers.py` (phaser banks and beams per
  shooter); the test pilot logs the firing ship of every hit, torpedo positions and phaser bank states.

Release check with the exact v1.5.0 build (simulated network, 180 s each, mean position error / 95th
percentile in game units, backward steps):

| Run | Host ship seen by clients | Client ships seen by the host | Client ships seen by other clients | Torpedo hits seen by all clients |
|---|---|---|---|---|
| 2 players, 60 ms ±20 ms, 1 % loss | 0.07 / 0.15, 0.0 % | 0.05 / 0.10, 1.2 % | – | 4 of 4 |
| 2 players, 150 ms ±50 ms, 5 % loss | 0.10 / 0.21, 0.0 % | 0.07 / 0.14, 0.0 % | – | 3 of 3 |
| 6 players, 60 ms ±20 ms, 1 % loss | 0.06–0.07 / 0.13, 0.0 % | 0.05 / 0.10–0.11, 0.0 % | 0.07 / 0.14–0.16, 0.0 % | 80 of 81, 54 of 54 |
| 6 players, all hulls at 5 % after 40 s | 0.07 / 0.15, 0.0 % | 0.05 / 0.12, 0.0 % | 0.07 / 0.17, 0.1 % | 74 of 74 |

Clients showed torpedo hits 65–71 ms before the host (median). Kick test: peak error after a kick
median 3.4 units, max 6.0; join test: every client saw every ship. Update from v1.4.0 with the
installer tested. No hangs, no crashes, no script errors.

### Known open points

- Tested in one PC with simulated network conditions, up to six players; no dedicated server, no real
  internet games yet.
- Phaser hits differ between players in about 15 % of the beams, also for the host's damage (#22).

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
