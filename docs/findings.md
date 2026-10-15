# BC 1.1 multiplayer desync – findings

All addresses refer to `stbc.exe` version 1.1 (image base 0x400000).
[V] = verified in disassembly or decompiled code, [I] = inference, [R] = confirmed at runtime.

## Globals

- 0x97FA88 IsClient, 0x97FA89 IsMultiplayer, 0x97FA8A IsHost [V] (set by the app start routine 0x004388A0 and SetIsHost/SetIsClient).
- 0x97FAA2 IsModemConnection (throttles health sync) [V].
- 0x97FA78 network object, 0x9A09D0 app (game time at +0x90).

## Movement and state updates (opcode 0x1C, unreliable)

- Writer 0x005B17F0 (ship vtable +0x120), reader 0x005B21C0 (+0x124). Called every frame from SendStateUpdates 0x0069EE50 (call site 0x0069EDCB in 0x0069EDC0) [V].
- Per-(object, peer) rate limiter vtable +0x13C = 0x005A22A0: 0.1 s near (0.125–0.5 s by mode and player count), distance LOD 2x/5x/1 s at 170/228/343 units [V].
- Absolute position only if more than 1.0 s passed since the last absolute position (jnz at 0x005B18D7), otherwise a delta relative to the last absolute position [V].
- **Bug:** the receiver accumulates deltas into the dead-reckoning base (+0x9C += d in 0x005B24C2..EE and in the base reader 0x005A2E64..84). After one lost or late absolute update, dead reckoning starts from a base that is about 4 s of travel ahead: the ship surges forward and is then yanked back (teleport, rubber-banding). The host regenerates updates from its own proxy, so it re-broadcasts the glitch [V, R].
- Remote proxies are driven by NetworkAI (type 9, vtable 0x88BD04, ctor 0x0047DAB0, Update 0x0047DE70). It extrapolates the target along the received forward vector × speed, commands impulse, pulls the position towards the target with gain 1/s (0x0047E180), has a dead band of 0.5 units while moving (compare at 0x0047E20E), and turns physically [V].
- **Bug:** NetworkAI +0x28 (last update time) is not initialised. The first Update uses the absolute game time as dt and flings the ship ("first-frame fling") [V]. Fix: 0x0047DD06 `D9 5E 2C` → `D9 5E 28`.
- No latency or clock offset handling. AI+0x2C (sender time) is written but never read. The client copies the host game time once at join [V].
- The host relays its smoothed proxy, not the state the client reported last [V]. The writer 0x005B17F0 reads position, two orientation vectors (vtable +0xAC/+0xB0) and speed from the live object, unless the snapshot flag ship+0x2FC is set: then it uses the snapshot at ship+0x300 (position), +0x30C/+0x318 (vectors), +0x324 (speed) and clears the flag. The stock code fills the snapshot only for collisions of the local player ship (0x005AF890) [V]. In a 6-player game other clients saw a client's ship with 1.1 units mean error, the host 0.3 [R].
- The correction 0x0047E180 sets the position directly (vtable +0x78) to pos + (target − pos) × min(dt, 1). When the proxy is ahead of the target, that move points against the direction of flight; with a higher gain the ship visibly jumps back (rubber-banding) [V, R].
- NetworkAI::Update drives the proxy along its own nose (ShipClass SetSpeed with the model forward vector) and turns it physically towards the received forward/up vectors; only below speed 0.5 it snaps the orientation (AlignToVectors 0x004311F0 at 0x0047DF20, arguments forward, up). When the real ship turns sharply at low speed, the proxy's nose lags by more than 90° for seconds: the proxy flies the wrong way and the position correction drags it back. Traced on clients: the proxy's nose · received forward fell to −0.3…−0.6 exactly while 100 % of the 100 ms steps went backwards (issue #17) [R].
- The state update carries a forward and an up vector; the reader stores them at NetworkAI +0x3C and +0x48. In the writer's snapshot they are +0x30C (vtable +0xAC) and +0x318 (vtable +0xB0) [V].
- NetworkAI::Update advances the target (+0x30) every frame by (now − last) × speed (+0x54) × forward (+0x3C), so the target is the dead-reckoned received position; the proxy follows it with the correction lag [V].
- The speed in the state update is the magnitude of the velocity, not the speed along the nose; the proxy's engine flies at the owner's throttle speed. In normal flight both agree within 0.05 units/s [R]. After a hit's impulse or an explosion the real ship moves sideways or backwards relative to its nose for seconds, while the receiver extrapolates along the nose at |v|. With the backward-limited correction the proxy then fell up to 43 units behind (one knockback in a 6-player run, 11.5 s above 5 units on every viewer); with the stock correction a scripted kick of 20 units/s gave 25–112 units [R].
- Distance LOD: beyond 343 units a peer gets one update per second, beyond 228 units four. In 6-player fights 9–28 % of the ship pairs are beyond 228 units, after knockbacks more. One second of extrapolation along an outdated course costs 20–30 units [R].
- **Timing:** frame dt comes from QPC since boot, stored as a float (0x007B7B90). dt jitters by ±½ ulp of the OS uptime: ±7.8 ms after 1.5 days, ±15.6 ms after 3 days. Windows Fast Startup keeps the uptime across shutdowns. The game time is a float accumulator as well [V mechanism].

## Weapons, damage and death

- The owner fires: WeaponSystem_Fire 0x584600 → tube 0x57C770/0x57C9E0 → spawn 0x57CD90. In MP it sends opcode 0x19 (guaranteed) via 0x57CB10: tube id, player id, flags, normalised velocity, target id + offset. No spawn position, time or torpedo id [V].
- Receivers spawn the torpedo via 0x0069F930 → 0x57D110 from their own, lagged view of the shooter [V].
- MP-only receiver asymmetry: guided torpedoes get MaxAngularAccel ×1.5 and GuidanceLifetime ×1.25 (0x57D4DF); unguided torpedoes with a ship target are re-aimed at the predicted intercept (0x57D540–0x57D75A) [V].
- The host is a receiver too: its copy of a client's torpedo, which decides the hit, also gets the receiver values and the re-aim. With the shooter's values (`SymmetricTorpedoSpawn`) the clients' replays had to move their copy 8.4–9.7 units on average instead of 11.2–11.4 (largest 323–477 instead of 818–965), and 97–100 % of the host's torpedo hits were seen by all five clients instead of 94–97 % [R].
- Phaser hits are shown by every peer from its own simulation of the beam. Very short hits (taps, the edge of a firing arc) that the shooter's own machine shows are often missing on the others: per 0.5 s bin, each peer had 1–5 % of the phaser hits that no other peer showed. Damage is still applied once, by the host [R].
- **Bug:** flag bits are swapped (sender bit0 = sound, bit1 = skew; receiver bit0 = skew, bit1 = sound) [V].
- Every peer runs hit detection: ProximityManager 0x5A8740 → 0x5A8810 → Torpedo::DetectCollision 0x579010 (thiscall, `RET 4`) → ship +0x140 (0x5AE730, shield/hull test) and +0x144 (0x5AED90, apply hit) [V]. 0x5AE730 reads the shield subsystem at ship+0x2C0, per-facing charge fraction at +0xC0 + i*4, "shield down" at ≤ 0.1 [V]. DetectCollision applies hits in a loop of two passes (`cmp eax, 2` at 0x5791BA) [V].
- DetectCollision passes the segment from the torpedo's previous position (+0x150) to its current position (vtable +0x94) to the ship's hit test. 0x5AE730 tests the hull mesh first (0x594310) when the segment starts inside the shield ellipsoid; otherwise it intersects the ellipsoid and checks the facing [V].
- Pulse weapons (disruptors) fire Torpedo objects (payload WP_PULSE). WeaponHitEvent knows only PHASER, TORPEDO and TRACTOR_BEAM, so pulse hits are reported as TORPEDO [V]. Disruptor cannons fire in pairs; both projectiles often hit within a few ms [R].
- Damage is applied only if `IsHost || !IsMP` (shields 0x5AEF5B, hull 0x5AEF9A): the host is authoritative for damage, clients only show effects [V].
- Death is decided only on the host (0x5AFEA0 → ET_OBJECT_EXPLODING → handler 0x6A1240 → opcode 0x06 to "NoMe") [V].
- Host health sync: StateUpdate flag 0x20, round robin over ship+0x284 (includes the hull), 1 byte per subsystem, written directly to the condition (+0x30) [V].
- **Result:** the visible hit or miss differs between peers, while the host's outcome decides damage and death. This is the "hits on one screen, misses on the other, explodes anyway" effect [V, R: about 40 % of torpedo hits were seen by only one peer].

## Transport (TGNetwork / TGWinsockNetwork, UDP 22101)

- Reliable messages are delivered in order; one lost packet holds back all following guaranteed messages (head-of-line blocking) [V].
- TGMessage ctor 0x006B82A0 sets the resend interval to 1.0 s (`B9 00 00 80 3F` at 0x6B82D4), with linear backoff. SendOutgoingPackets 0x006B55B0 [V].

## Python layer

- Embedded Python 1.5.2, SWIG 1.1 pointer strings (`_<hex>_p_Type`).
- Builtin modules: array binascii cmath errno math new nt operator regex strop struct time thread cStringIO cPickle _locale marshal imp (no traceback).
- `Autoexec` does `try: import Local`.
- Python C API in the exe: PyRun_SimpleString 0x0074AE80, Py_InitModule4 0x0074D140 (API 1007), PyArg_ParseTuple 0x0074E310, Py_None 0x0095DD20, PyInt_FromLong 0x0074C6E0, PyString_FromString 0x007506C0.
- An uncaught exception in an event handler goes to the dispatcher 0x006F9B80. In fullscreen it prints the traceback (PyErr_Print 0x0074AF10) and continues. In windowed mode (app+0x2C == 0, copied to 0x0099ADD6 in 0x004388A0; `-ForceWindowed` clears it) it calls the blocking "TG Python Exception Debug Console" (0x006F9470): AllocConsole, PyErr_Print into the script's stderr (a StringIO, read back with `getvalue`/`close`), then wait for `resume` or `abort` typed into the console. The game freezes meanwhile [V, R]. Every handler in the mod catches all exceptions; since v1.2.0 the DLL replaces the console call (0x006F9B9B) with PyErr_Print + log.
- **Bug (join race):** `RebuildInfoPane` in Mission1/2/3/5Menus runs from `ObjectCreatedHandler` when a player ship is created and uses `MissionMenusShared.g_pInfoPane` without a check. If a ship arrives while a client joins, before the pane exists: `AttributeError: 'None' object has no attribute 'KillChildren'` (Mission1.py line 881 → Mission1Menus.py line 841). Windowed: the game freezes (5 of 40 client joins in 6-player tests). Fullscreen: the handler stops before `ResetEnemyFriendlyGroups`, and the small default font set before the failing line stays [V, R].
- **Bug (ghost ship, #18):** when a player spawns a ship while another client is joining, the host forwards the "object created" message (opcode 3) to every connected player at once (0x0069F620 loops over the player slots). The object reader 0x005A2060 reads the object's name and the name of its set and adds the object only if that set exists (binary search in the set list 0x0097E9C8). On the joining client the mission set does not exist yet: the ship is created with its NetworkAI but stays outside every set (+0x20 = NULL), is never drawn and `GetShipFromPlayerID` (0x006A1AA0, searches the sets) does not find it. Moments later the host's catch-up list contains the same ship again; the factory 0x005A1F50 finds the object ID (0x00430730) and drops the message. The joining player never sees that ship. Seen in 4 of 6 six-player joins [V, R].
- The debug console calls `getvalue()` and `close()` on the script's stdout/stderr. A replacement object without them (the test harness's output redirect) makes the console fail with AttributeError (seen once in a 6-player join) [R].
- After D3D init the x87 FPU runs in single precision; `time.time()` then advances in steps of 128 s. The mod gives scripts an integer millisecond clock (`bcmpdll.ms()`).
- Python file I/O from game scripts does not produce files (cause unknown); the mod's `bcmpdll` module provides logging.
- Hidden command line options parsed in 0x004388A0: `-Client -IP <ip> -Name <name>`, `-Listener`, `-Dedicated`, `-Mission <m>`, `-Password`, `-SkipChecksum`, `-TestMode`, `-ForceWindowed`. The client refuses an IP equal to its own resolved host IP (0x006B9460, error 0xB).

## Fixes in bcmpfix

| Problem | Fix | Where |
|---|---|---|
| Delta accumulation, teleports | absolute position in every update; receiver uses the last absolute position as delta base | 0x005B18D7, 0x005B24C6/D8/E8, 0x005A2E62/72/82 |
| First-frame fling | initialise the last update time | 0x0047DD06 |
| Slow correction, dead band | gain ×5 (call hook at 0x0047E01D), no dead band | 0x0047E20E |
| Rubber-banding from the correction | correction replaced: lateral and forward parts as before, a backward part limited to half the distance flown in the frame | `src/netfix.c` |
| Proxy nose lags in sharp turns (#17) | each frame the proxy is turned towards the received forward/up vectors with the position correction's time constant (AlignToVectors) | `src/netfix.c` |
| Update rate | 50 ms instead of 100 ms at close range | immediates at 0x5A23BD, 0x5A2418, 0x5A244A, 0x5A2480, 0x5A2499, 0x5A24CB |
| No latency compensation | ping/pong RTT per peer in `bcmpfix_mp.py`; reader detour at 0x005B21C0 extrapolates the NetworkAI target by RTT/2 + 30 ms | `src/lead.c` |
| Divergent torpedo hits | host reports hits; clients suppress their own torpedo/ship collisions and replay the host's hit with their torpedo copy, forcing the host's shield/hull decision; the hit-test segment starts at the replay start point (+0x150), and each report takes a different local copy | `src/torpedo.c`, `scripts/bcmpfix_mp.py` |
| Blocking debug console in windowed mode | console call replaced: traceback to `bcmpfix.log`, game continues | `src/scripterr.c` |
| Join race in `RebuildInfoPane` | skipped while `g_pInfoPane` is None; patched when the menus module is imported (`__import__` wrapper) | `scripts/bcmpfix_mp.py` |
| Host relays its proxy (3+ players) | the host writes client ships from the NetworkAI target and the received forward/up vectors through the writer's snapshot (ship+0x2FC..0x324) | `src/relay.c` |
| Head-of-line blocking | resend interval 300 ms | 0x006B82D4 |
| Ghost ship after a join race (#18) | a create message for an existing ship that is in no set is read into that ship again (object reader without the model setup), which adds it to the set that now exists | `src/join.c` (call 0x0069F6AF) |
| Knockback: ship drifts off its nose (#19) | detection: reported speed (= \|v\|) differs from the proxy's own speed by more than max(3, 30 %); then the target is extrapolated along the velocity measured from received positions and the proxy is moved by the velocity difference. Backward errors beyond 30 % of the speed are corrected at full gain | `src/velo.c`, `src/netfix.c` |
| Swapped torpedo fire flags (#8) | the sender writes the launch-sound flag to bit1 and the skew flag to bit0, as receivers read them | 0x0057CBA8, 0x0057CBB7 |
| Receiver torpedo values and re-aim (#9) | remote spawns keep the shooter's turn rate, guidance time and direction (`SymmetricTorpedoSpawn`) | 0x0057D519, 0x0057D523, 0x0057D540 |
| Duplicate host hit reports (mod bug, #15) | the host remembers every reported torpedo for 1 s, not only the last one: the shield and bleed-through events of disruptor pairs interleave, and the second report made clients replay a hull hit with another copy | `scripts/bcmpfix_mp.py` |
| Distance LOD of state updates (#20) | thresholds ×3 (510/684/1029 units) | fcom operands at 0x5A25D2, 0x5A25E7, 0x5A25FE, 0x5A2617, 0x5A262C |
| Float QPC clock | QPC rebased to process start | IAT hook |
