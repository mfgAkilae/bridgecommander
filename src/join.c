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
 * Logs (test mode / Verbose): MultiplayerGame::ReceiveMessageHandler (0x0069F2A0)
 * processes game messages only once the game has its mission (game+0x70, and +0x3C of
 * that) and drops earlier ones without a trace; this module logs them, the object
 * messages, the objects created from them and the host's broadcasts. */
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
    if (g_cfg.testMode || g_cfg.verbose)
        logf("object from network: %d bytes -> %s id=%lu type=0x%x set=%s", len, o ? "created" : "NOT created",
             o ? *(DWORD *)(o + 4) : 0, o ? ((PFN_MsgType)(*(void ***)o)[1])(o, 0) : 0,
             o ? (*(void **)(o + 0x20) ? "yes" : "NONE") : "-");
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
    if (g_cfg.fixGhostShip || logs)
        redirect_call(ADDR_CALL_CREATE, (void *)hk_Create, &o_Create, ADDR_CREATE,
                      g_cfg.fixGhostShip ? "ghost-ship fix (objects created from network)" : "log objects created from network");
    if (!logs)
        return;
    redirect_call(ADDR_CALL_BCAST, (void *)hk_Bcast, &o_Bcast, ADDR_BCAST, "log host object broadcasts");
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
