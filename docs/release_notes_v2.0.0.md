# bcmpfix v2.0.0

This release hardens the original Bridge Commander 1.1 multiplayer protocol at
the narrowest shared receive boundaries while retaining original-client
compatibility.

## Security and network reliability

- Validates UDP framing, endpoint/UID/sender binding, transport types, sequence
  numbers, Queue bounds and fragment reassembly before decoded data reaches the
  original parser.
- Validates native GAME messages before host relay or native/Python dispatch:
  direction, class, length, Object ownership, STATE tails, create/fire/collision
  events and host-only script opcodes.
- Bounds chat and player-name inputs and adds targeted Safety Patches for the
  original signed-length/index and formatting paths reached by network data.
- Keeps guaranteed messages until ACK, bounds disconnect waits, retires stale
  sequence-wrap copies and resolves a respawned Player to the living Ship.
- Security-critical Hook or Safety-Patch mismatch now fails startup closed. GAME
  validation cannot run without authenticated transport identity.
- Collision reports cannot target another Player's Ship and extreme forged Force
  is rejected using a mass/contact-normalized limit. Legitimate AI ramming remains
  supported.

## Verification

- `security-final-baseline26`: current host/client, guard enabled, normal combat.
- `security-final-attack30`: malformed transport, sender spoofing, forged
  native/script GAME messages, invalid create/STATE/collision and duplicate
  fragment inputs were rejected; the Harness explicitly required the forged
  Collision reject (`opcode=0x15 len=26`), and both processes remained in game
  until normal test shutdown.
- `security-final-dependency28`: the formerly unsafe combination
  `GuardNetwork=0` with both GAME guards enabled still installed transport
  authentication and rejected the same Attack suite.
- Compatibility and switch coverage from the candidate remained green:
  original client against v2 host, all guards off, GAME validation off and
  host-only script validation off.

All listed Sandbox runs completed with process return codes `[0, 0]`. The final
MSVC x86 Build, Python Harness compile and `git diff --check` also passed.

## Scope

The score, scoreboard, team and wider game-script work researched for a broader
v2 line is not registered by this release. The existing `FixGameScripts`
Scaffolding remains inert until those fixes receive their own implementation and
runtime verification.
