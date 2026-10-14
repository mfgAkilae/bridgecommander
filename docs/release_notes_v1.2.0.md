## Bridge Commander multiplayer fix v1.2.0

Less rubber-banding, no more frozen games when a player joins, and no more frozen games on script
errors in windowed mode.

### How to install or update (Windows)

1. Download **`BC_MP_fix.exe`** and run it. It finds your Bridge Commander folder (or asks for it),
   checks that `stbc.exe` is version 1.1 and installs the fix. If an older version is installed,
   choose *Update*; your `bcmpfix.ini` is kept, and the new options are on by default.
2. Start the game as usual. `bcmpfix.log` in the game folder lists every fix with `applied`.
3. **Every player should install it, the host above all.**

The installer is not signed, so Windows SmartScreen may warn ("More info" → "Run anyway").
Manual alternative: unpack **`bcmpfix-1.2.0.zip`** into the game folder.

### What is new

- **Less rubber-banding.** v1.1.0 pulled remote ships quickly towards their network position, also
  backwards when a ship was ahead of it, and that showed as small jumps back. Now a ship that is ahead
  only slows down until its position catches up. With three or more players the host also passes on
  the direction a client's ship really flies, not its own lagging copy. Six players, 60 ms: other
  clients' ships moved back against their direction of flight in 9.3 % of all 100 ms steps with
  v1.1.0 and in 0.9 % with v1.2.0 (original game: 11.3 %); their mean position error dropped from
  0.73 to 0.44 units. New INI option `SmoothCorrection` (default 1).
- **Joining a running game no longer freezes the game.** When a player's ship arrived while a client
  was still joining, the game's own mission script failed (`RebuildInfoPane` used the player list
  panel before it existed). In windowed mode the game then stopped in its "TG Python Exception Debug
  Console" and waited for keyboard input in a separate console window; in fullscreen the error skipped
  the update of the friend/foe groups. In six-player tests this hit 5 of 40 joining players. bcmpfix
  now skips that panel update until the panel exists.
- **Script errors no longer freeze windowed games.** Any error in a game script used to open that
  blocking debug console in windowed mode. bcmpfix writes the error to `bcmpfix.log` and lets the
  game continue, as the game itself does in fullscreen. New INI option `NoScriptErrorConsole`
  (default 1).

In tests that call the failing panel update on purpose, the original game raises the error and
v1.2.0 skips it; a script error forced on purpose froze the original client and left the v1.2.0
client running, with the error in `bcmpfix.log`.

### Known open points

- Tested in one PC with simulated network conditions, up to six players; no dedicated server, no real
  internet games yet.
- A small lateral error remains during hard turns.
- Rarely a client does not show a hit the host reported; damage is not affected.
- In some test games the host's ship still jerks back in the clients' view during the first 40 s
  (issue #17).

Details, evidence and tools: see the README and `docs/measurements.md` in the repository.
