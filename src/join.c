/* Joining clients: the ghost-ship fix, plus logs of dropped and created objects.
 *
 * Ghost ship (original-game bug): when player B spawns a ship while player C is still
 * joining, the host forwards B's "object created" message (opcode 3) to C at once
 * (0x0069F620 relays it to every connected player). If that message arrives before C
 * has created the mission set, the object reader (0x005A2060) finds no set with the
 * stored name and leaves the ship outside every set: it exists (with its NetworkAI)
 * but is never drawn and GetShipFromPlayerID does not find it. A moment later the
 * host's catch-up list for C contains the same ship again; the factory (0x005A1F50)
 * sees that the object ID already exists and drops the message. C never sees B.
 *
 * Fix (FixGhostShip=1): when a create message names an existing ship that is in no
 * set, the message is read into that ship again (the game's own reader, without the
 * Python model setup), which also puts it into the set that now exists.
 *
 * Dedicated server object IDs (original-game bug, issue #24): every machine numbers the
 * objects it creates from a range of its player slot, (slot << 18) + 0x3FFFFFFF, with the
 * counter in UtopiaModule+0x8C (0x00445F70 skips IDs in use) and the slot in +0x84. A
 * dedicated server has no player slot and keeps slot 0, which the first client gets. The
 * host's own objects (the torpedo copies of everybody's shots) then use the first client's
 * range, and a ship that client creates later is refused by the host's object reader when
 * its ID is in use there: nobody sees the ship, and the client gets no state updates.
 * Fix (FixDedicatedObjectIds=1): a dedicated server uses slot 15 (the game has 16 player
 * slots, 0x006A19C0; the menu allows 8 players), with its counter moved into that range.
 *
 * Mission restart crashes (original-game bugs; seen on clients when the starbase mission
 * restarts after the starbase was destroyed, both on the frame the player gets the new ship):
 * - Game::HandleSetPlayer (0x00406AE0) -> WeaponsDisplay::SetShipID (0x0054B950) removes the
 *   charge range checks it put on the previous ship's weapons (0x0054B6E0). For a weapon whose
 *   FloatRangeWatcher has no checks left, FloatRangeWatcher::RemoveRangeCheck (0x0044C3F0)
 *   reads the second chain through the empty list head (read at 0x18, EIP 0x0044C413).
 * - Mission::PlayerEnteredSet (0x004090D0) reads the set's name through the object's set
 *   (+0x20) without a check; for an object that left its set before the queued event ran,
 *   that is NULL (read at 0x74, EIP 0x004090EB).
 * Fix (FixRestartCrashes=1): RemoveRangeCheck returns on an empty list (nothing to remove),
 * and PlayerEnteredSet passes such an event on as it does for an object that is not an
 * ObjectClass.
 *
 * Ramming damage (original-game bug): in multiplayer only the host applies collision damage.
 * ShipClass::CollisionEffectHandler (0x005AF9C0) on a client sends a collision of its own ship to the
 * host (message 0x15, 0x006A17C0): the event whose destination is its own ship, and, when the other
 * object is not a player's ship (an AI ship, the starbase), also the event whose destination is that
 * object and whose source is its own ship. Before sending it sets the event's source to NULL
 * (0x005AFA6C). The host's handler (0x006A2470) accepts a report only when the sender's ship is the
 * destination or the source, so the second report was always dropped: a client that rammed an AI
 * ship took damage, the AI ship none. Fix (FixRammingDamage=1): the source stays when it is a ship;
 * the host's own checks remain (the sender's ship is the source, the destination is no player's
 * ship, the two objects are less than 26 units apart). A source that is no ship is still cleared
 * (0x005AFA64).
 *
 * Logs (test mode / Verbose): MultiplayerGame::ReceiveMessageHandler (0x0069F2A0)
 * processes game messages only once the game has its mission (game+0x70, and +0x3C of
 * that) and drops earlier ones without a trace; this module logs them, the object
 * messages, the objects created from them and the host's broadcasts. */
#include <stdio.h>
#include "common.h"

#define ADDR_MP_RECV 0x0069F2A0
#define ADDR_MSG_GETDATA 0x006B8530 /* TGMessage::GetData(int *len) -> data */

typedef void(__fastcall *PFN_Recv)(void *game, void *edx, void *event);
typedef BYTE *(__fastcall *PFN_GetData)(void *msg, void *edx, int *len);
typedef int(__fastcall *PFN_MsgType)(void *msg, void *edx);

static BYTE *g_tramp;
static unsigned long g_dropped[64];

/* object creation from a network message (0x0069F620 -> 0x005A1F50, cdecl) and the
 * host's broadcast of new objects (ObjectCreatedHandler 0x006A0F90 -> SendTGMessageToGroup) */
#define ADDR_CALL_CREATE 0x0069F6AF
#define ADDR_CREATE 0x005A1F50
#define ADDR_CALL_BCAST 0x006A1118
#define ADDR_BCAST 0x006B4DE0
typedef BYTE *(__cdecl *PFN_Create)(BYTE *data, int len);
typedef int(__fastcall *PFN_Bcast)(void *net, void *edx, const char *group, void *msg);
static void *o_Create, *o_Bcast;

/* restart crashes: RemoveRangeCheck's branch to the second chain when the list is empty, the
 * WeaponsDisplay cleanup's call of it (thiscall(watcher, id)), and PlayerEnteredSet's
 * ObjectClass cast (cdecl) */
#define ADDR_RRC_EMPTY 0x0044C3FF
#define ADDR_RRC 0x0044C3F0
#define ADDR_CALL_WD_RRC 0x0054B89C
#define ADDR_CALL_PES_CAST 0x004090DC
#define ADDR_OBJCLASS_CAST 0x00434ED0
typedef void(__fastcall *PFN_RemoveCheck)(BYTE *watcher, void *edx, int id);
typedef BYTE *(__cdecl *PFN_ObjCast)(void *obj);
static void *o_RemoveCheck, *o_ObjCast;
static unsigned long g_emptyWatchers, g_noSetEvents;

/* test telemetry of ship collisions in multiplayer: a client reports a collision of its own ship
 * as message 0x15; the host's handler (0x006A2470, called from the receive handler at 0x0069F494)
 * checks the sender and the distance and posts it as ET_HOST_OBJECT_COLLISION (AddEvent call at
 * 0x006A25F9). CR = report received (sender's player ID), CA = report applied (source and
 * destination object IDs). */
#define ADDR_CALL_COLL_RECV 0x0069F494
#define ADDR_COLL_RECV 0x006A2470
#define ADDR_CALL_COLL_ADD 0x006A25F9
#define ADDR_EVENT_ADD 0x006DA2A0
typedef void(__fastcall *PFN_CollRecv)(void *game, void *edx, BYTE *msg);
typedef void(__fastcall *PFN_EventAdd)(void *mgr, void *edx, BYTE *ev);
static void *o_CollRecv, *o_CollAdd;

/* pieces of the factory 0x005A1F50: object lookup by ID, TGBufferStream on the stack,
 * and the two ObjectClass readers (species byte; position, orientation, velocity, name
 * and set). ShipClass's own reader (vtable+0x118) is part 1 plus the Python model setup. */
#define ADDR_FIND_OBJECT 0x00430730 /* cdecl(0, id) -> object or NULL */
#define ADDR_STREAM_CTOR 0x006CEFE0
#define ADDR_STREAM_ATTACH 0x006CF180 /* (data, len) */
#define ADDR_STREAM_READINT 0x006CF670
#define ADDR_STREAM_DTOR 0x006CF120
#define ADDR_OBJ_READ1 0x005A2030
#define ADDR_OBJ_READ2 0x005A2060
#define TYPE_SHIP 0x8008
#define ADDR_SHIP_READ 0x005B0E80 /* ShipClass vtable+0x118: part 1 + model setup; the layout place_orphan replays */
typedef BYTE *(__cdecl *PFN_FindObject)(int kind, DWORD id);
typedef void(__fastcall *PFN_This)(void *self, void *edx);
typedef void(__fastcall *PFN_Attach)(void *stream, void *edx, BYTE *data, int len);
typedef int(__fastcall *PFN_ReadInt)(void *stream, void *edx);
typedef void(__fastcall *PFN_ObjRead)(void *obj, void *edx, void *stream);
typedef char(__fastcall *PFN_IsA)(void *obj, void *edx, int type);
typedef float *(__fastcall *PFN_GetPosJ)(void *obj, void *edx);

static int place_orphan(BYTE *obj, BYTE *data, int len)
{
    __declspec(align(8)) BYTE st[0x80];
    memset(st, 0, sizeof(st));
    ((PFN_This)ADDR_STREAM_CTOR)(st, 0);
    ((PFN_Attach)ADDR_STREAM_ATTACH)(st, 0, data, len);
    ((PFN_ReadInt)ADDR_STREAM_READINT)(st, 0); /* class */
    ((PFN_ReadInt)ADDR_STREAM_READINT)(st, 0); /* object ID */
    ((PFN_ObjRead)ADDR_OBJ_READ1)(obj, 0, st);
    (*(PFN_This *)(*(BYTE **)st + 0xd8))(st, 0); /* as ShipClass's reader does after the model setup */
    ((PFN_ObjRead)ADDR_OBJ_READ2)(obj, 0, st);
    ((PFN_This)ADDR_STREAM_DTOR)(st, 0);
    return *(void **)(obj + 0x20) != NULL;
}

#define ADDR_UTOPIA 0x0097FA00 /* UtopiaModule */
#define UT_SLOT 0x84           /* local player slot: the range of new object IDs */
#define UT_IS_CLIENT 0x88
#define UT_IS_MP 0x89
#define UT_IS_HOST 0x8A
#define UT_NEXT_ID 0x8C        /* next object ID */
#define DEDICATED_SLOT 15
#define SLOT_BASE(s) (((DWORD)(s) << 18) + 0x3FFFFFFFu)

/* every frame: a dedicated server numbers its objects from slot 15 */
void join_frame(void)
{
    static int s_logs;
    BYTE *u = (BYTE *)ADDR_UTOPIA;
    int slot = *(int *)(u + UT_SLOT);
    DWORD next, off;
    if (!g_cfg.fixDedicatedIds || !u[UT_IS_MP] || !u[UT_IS_HOST] || u[UT_IS_CLIENT] || slot == DEDICATED_SLOT)
        return;
    next = *(DWORD *)(u + UT_NEXT_ID);
    off = (slot >= 0 && slot < 16) ? next - SLOT_BASE(slot) : 0;
    if (off >= 0x40000)
        off = 0;
    *(int *)(u + UT_SLOT) = DEDICATED_SLOT;
    *(DWORD *)(u + UT_NEXT_ID) = SLOT_BASE(DEDICATED_SLOT) + off;
    {
        /* the server's range is slot 15: a sixteenth client would get it too */
        BYTE *game = *(BYTE **)0x0097E238;
        if (game && *(int *)(game + 0x1FC) > DEDICATED_SLOT) {
            *(int *)(game + 0x1FC) = DEDICATED_SLOT;
            logf("dedicated server: at most %d players (slot %d is the server's)", DEDICATED_SLOT, DEDICATED_SLOT);
        }
    }
    if (s_logs++ < 5)
        logf("dedicated server: object IDs from slot %d (was %d, next id %lu -> %lu)", DEDICATED_SLOT, slot, next,
             SLOT_BASE(DEDICATED_SLOT) + off);
}

static BYTE *__cdecl hk_Create(BYTE *data, int len)
{
    BYTE *o;
    if (g_cfg.fixGhostShip && data && len >= 8) {
        DWORD id = *(DWORD *)(data + 4);
        o = ((PFN_FindObject)ADDR_FIND_OBJECT)(0, id);
        if (o && !*(void **)(o + 0x20) && ((PFN_IsA)(*(void ***)o)[2])(o, 0, TYPE_SHIP) &&
            (*(DWORD **)o)[0x118 / 4] == ADDR_SHIP_READ) {
            int ok = place_orphan(o, data, len);
            logf("ghost ship fix: ship id=%lu was in no set (created before this client had its mission set); "
                 "read the resent create message into it: %s",
                 id, ok ? "now in its set" : "still in no set");
            return NULL; /* the object exists: same result as the game's own check */
        }
    }
    o = ((PFN_Create)o_Create)(data, len);
    if (!o && len >= 8)
        logf("object from network NOT created: id=%lu (%s)", *(DWORD *)(data + 4),
             ((PFN_FindObject)ADDR_FIND_OBJECT)(0, *(DWORD *)(data + 4)) ? "the ID is in use here" : "reader failed");
    if (g_cfg.testMode || g_cfg.verbose) {
        float *p = o ? ((PFN_GetPosJ)(*(void ***)o)[0x94 / 4])(o, 0) : NULL;
        logf("object from network: %d bytes -> %s id=%lu type=0x%x set=%s pos=%.1f,%.1f,%.1f", len,
             o ? "created" : "NOT created", o ? *(DWORD *)(o + 4) : 0, o ? ((PFN_MsgType)(*(void ***)o)[1])(o, 0) : 0,
             o ? (*(void **)(o + 0x20) ? "yes" : "NONE") : "-", p ? p[0] : 0.0f, p ? p[1] : 0.0f, p ? p[2] : 0.0f);
        if (o)
            tlog_printf("OC %ld %lu %.2f %.2f %.2f", tlog_ms(), *(DWORD *)(o + 4), p[0], p[1], p[2]);
    }
    return o;
}

static void __fastcall hk_CollRecv(void *game, void *edx, BYTE *msg)
{
    (void)edx;
    {
        /* the serialized event: 48 bytes as hex (finds the object IDs and the event type) */
        int len = 0, i, n;
        BYTE *d = msg ? ((PFN_GetData)ADDR_MSG_GETDATA)(msg, 0, &len) : NULL;
        char hex[2 * 48 + 1];
        n = d ? (len < 48 ? len : 48) : 0;
        for (i = 0; i < n; i++)
            _snprintf(hex + 2 * i, 3, "%02x", d[i]);
        hex[2 * n] = 0;
        tlog_printf("CR %ld %d %d %s", tlog_ms(), msg ? *(int *)(msg + 0xC) : -1, len, hex);
    }
    ((PFN_CollRecv)o_CollRecv)(game, 0, msg);
}

static void __fastcall hk_CollAdd(void *mgr, void *edx, BYTE *ev)
{
    BYTE *s = ev ? *(BYTE **)(ev + 8) : NULL, *d = ev ? *(BYTE **)(ev + 0xC) : NULL;
    (void)edx;
    tlog_printf("CA %ld %lu %lu", tlog_ms(), s ? *(DWORD *)(s + 4) : 0, d ? *(DWORD *)(d + 4) : 0);
    ((PFN_EventAdd)o_CollAdd)(mgr, 0, ev);
}

/* WeaponsDisplay cleanup: count the empty watchers (the byte patch covers every caller) */
static void __fastcall hk_RemoveCheck(BYTE *watcher, void *edx, int id)
{
    (void)edx;
    if (!*(void **)(watcher + 4)) {
        if (g_emptyWatchers++ < 5)
            logf("restart crash fix: weapons display removed a range check from an empty watcher (skipped)");
        return;
    }
    ((PFN_RemoveCheck)o_RemoveCheck)(watcher, 0, id);
}

static BYTE *__cdecl hk_EnteredSetCast(void *obj)
{
    BYTE *o = ((PFN_ObjCast)o_ObjCast)(obj);
    if (o && !*(void **)(o + 0x20)) {
        if (g_noSetEvents++ < 5)
            logf("restart crash fix: entered-set event for object id=%lu, which is in no set (skipped)", *(DWORD *)(o + 4));
        return NULL;
    }
    return o;
}

static int __fastcall hk_Bcast(void *net, void *edx, const char *group, void *msg)
{
    int len = 0;
    BYTE *d = msg ? ((PFN_GetData)ADDR_MSG_GETDATA)(msg, 0, &len) : NULL;
    (void)edx;
    if (d && len > 1)
        logf("host broadcasts new object: opcode %d slot %d, %d bytes to group %s", d[0], (signed char)d[1], len,
             group ? group : "?");
    return ((PFN_Bcast)o_Bcast)(net, 0, group, msg);
}

static void __fastcall hk_Recv(void *game, void *edx, void *event)
{
    BYTE *g = (BYTE *)game, *m = *(BYTE **)(g + 0x70);
    (void)edx;
    {
        void *msg = event ? *(void **)((BYTE *)event + 0x28) : NULL;
        if (msg && ((PFN_MsgType)(*(void ***)msg)[0])(msg, 0) == 0x32) {
            int len = 0;
            BYTE *d = ((PFN_GetData)ADDR_MSG_GETDATA)(msg, 0, &len);
            if (d && len > 1 && (d[0] == 2 || d[0] == 3))
                logf("object message received: opcode %d slot %d, %d bytes, game ready=%d", d[0], (signed char)d[1],
                     len, m && *(void **)(m + 0x3C) ? 1 : 0);
        }
    }
    if (!m || !*(void **)(m + 0x3C)) {
        void *msg = event ? *(void **)((BYTE *)event + 0x28) : NULL;
        if (msg && ((PFN_MsgType)(*(void ***)msg)[0])(msg, 0) == 0x32) {
            int len = 0;
            BYTE *d = ((PFN_GetData)ADDR_MSG_GETDATA)(msg, 0, &len);
            if (d && len > 0) {
                g_dropped[d[0] & 63]++;
                logf("game message dropped before the mission is ready: opcode 0x%02x, %d bytes", d[0], len);
            }
        }
    }
    ((PFN_Recv)g_tramp)(game, 0, event);
}

void join_install(void)
{
    static const BYTE prologue[6] = {0x57, 0x8b, 0xf9, 0x8b, 0x47, 0x70}; /* push edi; mov edi,ecx; mov eax,[edi+70h] */
    BYTE jmp[6];
    LONG rel;
    int logs = g_cfg.testMode || g_cfg.verbose;
    if (g_cfg.fixRammingDamage)
        patch_bytes(0x005AFA6C, "6a 00 8b ce e8 fb 67 12 00", "90 90 90 90 90 90 90 90 90",
                    "collision reports keep the reporting ship as the source (ramming damage)");
    if (g_cfg.fixRestartCrashes) {
        patch_bytes(ADDR_RRC_EMPTY, "74 12", "74 64", "range watcher: no read through an empty list (restart crash)");
        redirect_call(ADDR_CALL_WD_RRC, (void *)hk_RemoveCheck, &o_RemoveCheck, ADDR_RRC,
                      "weapons display: count empty range watchers (restart crash)");
        redirect_call(ADDR_CALL_PES_CAST, (void *)hk_EnteredSetCast, &o_ObjCast, ADDR_OBJCLASS_CAST,
                      "entered-set event for an object in no set (restart crash)");
    }
    if (g_cfg.fixGhostShip || logs)
        redirect_call(ADDR_CALL_CREATE, (void *)hk_Create, &o_Create, ADDR_CREATE,
                      g_cfg.fixGhostShip ? "ghost-ship fix (objects created from network)" : "log objects created from network");
    if (!logs)
        return;
    redirect_call(ADDR_CALL_BCAST, (void *)hk_Bcast, &o_Bcast, ADDR_BCAST, "log host object broadcasts");
    if (g_cfg.testMode) {
        redirect_call(ADDR_CALL_COLL_RECV, (void *)hk_CollRecv, &o_CollRecv, ADDR_COLL_RECV, "test: collision reports received");
        redirect_call(ADDR_CALL_COLL_ADD, (void *)hk_CollAdd, &o_CollAdd, ADDR_EVENT_ADD, "test: collision reports applied");
    }
    if (memcmp((void *)ADDR_MP_RECV, prologue, 6) != 0) {
        logf("HOOK dropped-message log: prologue differs, NOT applied");
        return;
    }
    g_tramp = (BYTE *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(g_tramp, prologue, 6);
    g_tramp[6] = 0xE9;
    rel = (LONG)(ADDR_MP_RECV + 6) - (LONG)(g_tramp + 11);
    memcpy(g_tramp + 7, &rel, 4);
    jmp[0] = 0xE9;
    rel = (LONG)hk_Recv - (LONG)(ADDR_MP_RECV + 5);
    memcpy(jmp + 1, &rel, 4);
    jmp[5] = 0x90;
    mem_write((void *)ADDR_MP_RECV, jmp, 6);
    logf("HOOK dropped-message log (MultiplayerGame receive): applied");
}
