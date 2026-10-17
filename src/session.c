/* Session and transport fixes of the original game (v2.0).
 *
 * Guaranteed messages that expire (FixReliableExpiry, original-game bug): TGNetwork resends a
 * guaranteed message until it is acknowledged, but SendOutgoingPackets (0x006B55B0) deletes it once
 * it is older than the send timeout (network+0xB4, 360 s) without telling the receiver. The
 * receiver delivers guaranteed messages in order (expected sequence per peer, +0x24 control and
 * +0x28 game messages) and keeps waiting for the deleted one: every later guaranteed message from
 * that peer is held back while the connection stays alive. Fix: the message is kept until it is
 * acknowledged or the connection times out (connection timeout +0xB8, 45 s, removes the peer).
 *
 * Leaving a game (FixDisconnectWait): TGNetwork::Disconnect (0x006B4060) sends the disconnect
 * message and waits up to 15 s for its acknowledgement; with a host that no longer answers the game
 * hangs that long. The wait is capped at 2 s (the shared 15.0 constant 0x008958CC stays; the two
 * compares use the existing 2.0 at 0x008898AC).
 *
 * Sequence wrap (FixSequenceWrap): the receivers (game messages 0x006B52B0, control messages
 * 0x006B5F70) delete a queued guaranteed message whose sequence lies behind the expected one, with
 * a plain 32-bit difference of the two 16-bit numbers. A second copy of message 65535 still queued
 * when the expected sequence wraps to 0 has the difference 65535 and stays; it is delivered again
 * when the sequence comes round. The compares use 16-bit (modular) arithmetic now; the window for
 * "behind" (0x4000) is unchanged.
 *
 * Player ship lookup (FixPlayerShipLookup): MultiplayerGame::GetShipFromPlayerID (0x006A1AA0)
 * returns the first ship of that player in any set. During a fast respawn the previous ship still
 * explodes and may come first: self-destruct (opcode 0x13) and the scripts then act on the wreck.
 * The lookup prefers a ship that is not dying (death time +0x14C still FLT_MAX); a dying ship is
 * returned only when the player has no other one (the kill subtitle names the ship that died). */
#include "common.h"

#define ADDR_SHIP_FROM_PLAYER 0x006A1AA0
#define ADDR_SETS 0x0097E9C8      /* array of sets */
#define ADDR_NSETS 0x0097E9CC
#define ADDR_SET_SHIPS 0x005AB550 /* cdecl(set, int *count) -> ship array */
#define FLT_MAX_BITS 0x7F7FFFFFu

typedef BYTE **(__cdecl *PFN_SetShips)(void *set, int *count);

static BYTE *__fastcall hk_ShipFromPlayer(void *game, void *edx, int id)
{
    void **sets = *(void ***)ADDR_SETS;
    int nsets = *(int *)ADDR_NSETS, i, j, n;
    BYTE *dying = NULL;
    (void)game;
    (void)edx;
    for (i = 0; sets && i < nsets; i++) {
        BYTE **ships = ((PFN_SetShips)ADDR_SET_SHIPS)(sets[i], &n);
        for (j = 0; ships && j < n; j++) {
            BYTE *s = ships[j];
            if (!s || *(int *)(s + 0x2E4) != id)
                continue;
            if (*(DWORD *)(s + 0x14C) == FLT_MAX_BITS && !s[0x150])
                return s;
            if (!dying)
                dying = s;
        }
    }
    return dying;
}

void session_install(void)
{
    if (g_cfg.fixReliableExpiry)
        patch_bytes(0x006B57AE, "75 1b", "eb 1b", "guaranteed messages are kept until acknowledged (no silent expiry)");
    if (g_cfg.fixDisconnectWait) {
        patch_bytes(0x006B4102, "d8 1d cc 58 89 00", "d8 1d ac 98 88 00", "leaving a game: wait at most 2 s (1/2)");
        patch_bytes(0x006B42EB, "d8 1d cc 58 89 00", "d8 1d ac 98 88 00", "leaving a game: wait at most 2 s (2/2)");
    }
    if (g_cfg.fixSequenceWrap) {
        /* sub cx,ax; cmp cx,4000h; jae keep; 12 x nop - delete when (expected - received) mod 2^16 < 0x4000 */
        patch_bytes(0x006B5353, "25 ff ff 00 00 81 e1 ff ff 00 00 2b c1 79 1d 3d 00 c0 ff ff 7e 16",
                    "66 2b c8 66 81 f9 00 40 73 22 90 90 90 90 90 90 90 90 90 90 90 90",
                    "game messages: old queued copies are dropped across the sequence wrap");
        patch_bytes(0x006B6045, "25 ff ff 00 00 81 e1 ff ff 00 00 2b c1 79 2b 3d 00 c0 ff ff 7e 24",
                    "66 2b c8 66 81 f9 00 40 73 30 90 90 90 90 90 90 90 90 90 90 90 90",
                    "control messages: old queued copies are dropped across the sequence wrap");
    }
    if (g_cfg.fixPlayerShipLookup) {
        char repl[32];
        LONG rel = (LONG)hk_ShipFromPlayer - (LONG)(ADDR_SHIP_FROM_PLAYER + 5);
        BYTE *r = (BYTE *)&rel;
        wsprintfA(repl, "e9 %02x %02x %02x %02x", r[0], r[1], r[2], r[3]);
        patch_bytes(ADDR_SHIP_FROM_PLAYER, "83 ec 08 a1 c8", repl, "player ship lookup prefers the living ship");
    }
}
