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
- The host relays its smoothed proxy, not the state the client reported last [V].
- **Timing:** frame dt comes from QPC since boot, stored as a float (0x007B7B90). dt jitters by ±½ ulp of the OS uptime: ±7.8 ms after 1.5 days, ±15.6 ms after 3 days. Windows Fast Startup keeps the uptime across shutdowns. The game time is a float accumulator as well [V mechanism].

## Weapons, damage and death

- The owner fires: WeaponSystem_Fire 0x584600 → tube 0x57C770/0x57C9E0 → spawn 0x57CD90. In MP it sends opcode 0x19 (guaranteed) via 0x57CB10: tube id, player id, flags, normalised velocity, target id + offset. No spawn position, time or torpedo id [V].
- Receivers spawn the torpedo via 0x0069F930 → 0x57D110 from their own, lagged view of the shooter [V].
- MP-only receiver asymmetry: guided torpedoes get MaxAngularAccel ×1.5 and GuidanceLifetime ×1.25 (0x57D4DF); unguided torpedoes with a ship target are re-aimed at the predicted intercept (0x57D540–0x57D75A) [V].
- **Bug:** flag bits are swapped (sender bit0 = sound, bit1 = skew; receiver bit0 = skew, bit1 = sound) [V].
- Every peer runs hit detection: ProximityManager 0x5A8740 → 0x5A8810 → Torpedo::DetectCollision 0x579010 (thiscall, `RET 4`) → ship +0x140 (0x5AE730, shield/hull test) and +0x144 (0x5AED90, apply hit) [V]. 0x5AE730 reads the shield subsystem at ship+0x2C0, per-facing charge fraction at +0xC0 + i*4, "shield down" at ≤ 0.1 [V]. DetectCollision applies hits in a loop of two passes (`cmp eax, 2` at 0x5791BA) [V].
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
- An uncaught exception in an event handler opens the blocking "TG Python Exception Debug Console" (0x006F9470). In a running MP game this hangs the process, so every handler in the mod catches all exceptions.
- After D3D init the x87 FPU runs in single precision; `time.time()` then advances in steps of 128 s. The mod gives scripts an integer millisecond clock (`bcmpdll.ms()`).
- Python file I/O from game scripts does not produce files (cause unknown); the mod's `bcmpdll` module provides logging.
- Hidden command line options parsed in 0x004388A0: `-Client -IP <ip> -Name <name>`, `-Listener`, `-Dedicated`, `-Mission <m>`, `-Password`, `-SkipChecksum`, `-TestMode`, `-ForceWindowed`. The client refuses an IP equal to its own resolved host IP (0x006B9460, error 0xB).

## Fixes in bcmpfix

| Problem | Fix | Where |
|---|---|---|
| Delta accumulation, teleports | absolute position in every update; receiver uses the last absolute position as delta base | 0x005B18D7, 0x005B24C6/D8/E8, 0x005A2E62/72/82 |
| First-frame fling | initialise the last update time | 0x0047DD06 |
| Slow correction, dead band | gain ×5 (call hook at 0x0047E01D), no dead band | 0x0047E20E |
| Update rate | 50 ms instead of 100 ms at close range | immediates at 0x5A23BD, 0x5A2418, 0x5A244A, 0x5A2480, 0x5A2499, 0x5A24CB |
| No latency compensation | ping/pong RTT per peer in `bcmpfix_mp.py`; reader detour at 0x005B21C0 extrapolates the NetworkAI target by RTT/2 + 30 ms | `src/lead.c` |
| Divergent torpedo hits | host reports hits; clients suppress their own torpedo/ship collisions and replay the host's hit with their torpedo copy, forcing the host's shield/hull decision | `src/torpedo.c`, `scripts/bcmpfix_mp.py` |
| Head-of-line blocking | resend interval 300 ms | 0x006B82D4 |
| Float QPC clock | QPC rebased to process start | IAT hook |
