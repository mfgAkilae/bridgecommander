/* Network trust boundaries for the original multiplayer implementation.
 *
 * The stock transport trusts lengths, sender IDs and object/event classes read
 * from UDP.  This module validates the serialized transport before the stock
 * parser and validates complete GAME messages before any native/Python handler
 * or host relay sees them.  Unknown mod GAME opcodes remain compatible; strict
 * rules are applied only where the stock contract is known. */
#include "common.h"
#include <float.h>
#include <stdio.h>
#include <string.h>

#define ADDR_UTOPIA             0x0097FA00
#define ADDR_NETWORK_PTR        0x0097FA78
#define ADDR_GAME_PTR           0x0097E238
#define ADDR_FIND_OBJECT        0x006F0EE0
#define ADDR_DISPATCH           0x006DA300
#define ADDR_RECEIVE_CALL       0x006B5CA7
#define ADDR_GS_ALLOC_CALL      0x006B9694
#define ADDR_MALLOC             0x00718CB0
#define ADDR_FREE               0x00718CF0

#define TYPE_PHYSICS            0x8006
#define TYPE_DAMAGEABLE         0x8007
#define TYPE_SHIP               0x8008
#define TYPE_SHIP_SUBSYSTEM     0x801B
#define TYPE_POWERED_SUBSYSTEM  0x801C
#define TYPE_TORPEDO_SYSTEM     0x801E
#define TYPE_PHASER_SYSTEM      0x801F
#define TYPE_TRACTOR_SYSTEM     0x8021
#define TYPE_CLOAK_SYSTEM       0x8024
#define TYPE_REPAIR_SYSTEM      0x8029
#define TYPE_PULSE_WEAPON       0x802D
#define TYPE_TORPEDO_TUBE       0x802F

#define EVT_BASE                0x0101
#define EVT_BOOL                0x0104
#define EVT_CHAR                0x0105
#define EVT_OBJPTR              0x010C
#define EVT_PLAYER              0x0866
#define EVT_COLLISION           0x8124
#define EVT_START_FIRING        0x8128
#define EVT_EXPLODING           0x8129
#define EVT_WARP                0x812A

#define MAX_ENDPOINTS           64
#define ENDPOINT_STALE_MS       90000u
#define MAX_REORDER_AHEAD       256
#define MAX_PENDING_ACKS        0x1000
#define MAX_FRAGMENT_TRACKS     16
#define MAX_PENDING_RECEIVES    8

#define STATE_READER_BASE       0x0056D390
#define STATE_READER_POWERED    0x005629D0
#define STATE_READER_BATTERY    0x00564530
#define STATE_READER_SHIELD     0x0056AE80
#define ADDR_SUBSYSTEM_CHILD    0x0056C570

typedef BYTE *(__cdecl *PFN_FindObject)(DWORD id);
typedef int(__fastcall *PFN_IsA)(void *obj, void *edx, int type);
typedef int(__fastcall *PFN_MsgType)(void *msg, void *edx);
typedef unsigned char(__fastcall *PFN_Dispatch)(void *manager, void *edx, BYTE *event);
typedef void(__fastcall *PFN_Delete)(void *obj, void *edx, int deleting);
typedef void *(__cdecl *PFN_Malloc)(int size);
typedef void(__cdecl *PFN_Free)(void *ptr);
typedef char *(__fastcall *PFN_ReceiveDecoded)(void *network, void *edx,
                                               int *length, DWORD *source);
typedef void(__fastcall *PFN_Crypt)(void *crypt, void *edx, char *data, int length);
typedef BYTE *(__fastcall *PFN_SubsystemChild)(BYTE *subsystem, void *edx, int index);

typedef struct MsgSlice {
    unsigned short off;
    unsigned short len;
    unsigned short hdr;
    unsigned short seq;
    unsigned char type;
    unsigned char haveSeq;
    unsigned char isFrag;
    unsigned char fragIndex;
    unsigned char fragTotal;
} MsgSlice;

typedef struct FragTrack {
    unsigned short seq;
    unsigned char active;
    unsigned char total;
    unsigned char count;
    unsigned char maxIndex;
    unsigned char complete;
    unsigned char seen[32];
    DWORD touched;
} FragTrack;

typedef struct Endpoint {
    SOCKET socket;
    ULONG ip;
    USHORT port;
    DWORD uid;
    int playerId;
    int state; /* 1=pending connect, 2=bound player, 3=expected host */
    DWORD seen;
    FragTrack fragments[MAX_FRAGMENT_TRACKS];
} Endpoint;

typedef struct PendingReceive {
    SOCKET socket;
    DWORD threadId;
    struct sockaddr_in from;
    unsigned long serial;
    int used;
} PendingReceive;

static Endpoint g_endpoints[MAX_ENDPOINTS];
static PendingReceive g_pendingReceives[MAX_PENDING_RECEIVES];
static CRITICAL_SECTION g_guardLock;
static int g_guardReady;
static int g_receiveHooked;
static unsigned long g_pendingSerial;
static BYTE *g_dispatchTramp;
static void *o_gsAlloc;
static unsigned long g_transportDrops;
static unsigned long g_gameDrops;

static int readable(const void *ptr, size_t len)
{
    const BYTE *p = (const BYTE *)ptr;
    const BYTE *end;
    MEMORY_BASIC_INFORMATION mbi;
    if (!ptr || !len)
        return 0;
    end = p + len;
    if (end < p)
        return 0;
    while (p < end) {
        const BYTE *regionEnd;
        if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return 0;
        regionEnd = (const BYTE *)mbi.BaseAddress + mbi.RegionSize;
        if (regionEnd <= p)
            return 0;
        p = regionEnd < end ? regionEnd : end;
    }
    return 1;
}

static unsigned short u16(const BYTE *p)
{
    return (unsigned short)(p[0] | ((unsigned short)p[1] << 8));
}

static short i16(const BYTE *p)
{
    return (short)u16(p);
}

static DWORD u32(const BYTE *p)
{
    DWORD v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static int i32(const BYTE *p)
{
    int v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static int finite_float(const BYTE *p)
{
    float v;
    memcpy(&v, p, sizeof(v));
    return _finite((double)v);
}

static BYTE *network_ptr(void)
{
    if (!readable((void *)ADDR_NETWORK_PTR, sizeof(BYTE *)))
        return NULL;
    return *(BYTE **)ADDR_NETWORK_PTR;
}

static SOCKET game_socket(void)
{
    BYTE *network = network_ptr();
    if (!network || !readable(network + 0x194, sizeof(SOCKET)))
        return INVALID_SOCKET;
    return *(SOCKET *)(network + 0x194);
}

static BYTE *game_ptr(void)
{
    if (!readable((void *)ADDR_GAME_PTR, sizeof(BYTE *)))
        return NULL;
    return *(BYTE **)ADDR_GAME_PTR;
}

static int is_host(void)
{
    return readable((void *)(ADDR_UTOPIA + 0x8A), 1) && *(BYTE *)(ADDR_UTOPIA + 0x8A) != 0;
}

static int is_multiplayer(void)
{
    return readable((void *)(ADDR_UTOPIA + 0x89), 1) && *(BYTE *)(ADDR_UTOPIA + 0x89) != 0;
}

static BYTE *net_player_by_id(int id)
{
    BYTE *net = network_ptr();
    BYTE **players;
    int count, i;
    if (!readable(net, 0x34))
        return NULL;
    players = *(BYTE ***)(net + 0x2C);
    count = *(int *)(net + 0x30);
    if (count < 0 || count > 256 || (count && !readable(players, count * sizeof(*players))))
        return NULL;
    for (i = 0; i < count; i++) {
        BYTE *p = players[i];
        if (readable(p, 0x24) && *(int *)(p + 0x18) == id)
            return p;
    }
    return NULL;
}

static int game_player_record(int id, int *slotOut, DWORD *shipOut)
{
    BYTE *game = game_ptr();
    int slot;
    if (!readable(game, 0x200))
        return 0;
    for (slot = 0; slot < 16; slot++) {
        BYTE *rec = game + 0x78 + slot * 0x18;
        if (rec[0] && *(int *)(rec + 4) == id) {
            if (slotOut)
                *slotOut = slot;
            if (shipOut)
                *shipOut = *(DWORD *)(rec + 8);
            return 1;
        }
    }
    return 0;
}

static BYTE *find_object(DWORD id)
{
    BYTE *obj;
    if (!id || id == 0xFFFFFFFF)
        return NULL;
    obj = ((PFN_FindObject)ADDR_FIND_OBJECT)(id);
    return readable(obj, 12) ? obj : NULL;
}

static int object_is(BYTE *obj, int type)
{
    void **vt;
    if (!readable(obj, 12))
        return 0;
    vt = *(void ***)obj;
    if (!readable(vt, 3 * sizeof(void *)) || !vt[2])
        return 0;
    return ((PFN_IsA)vt[2])(obj, 0, type) != 0;
}

static BYTE *ship_from_subsystem(BYTE *obj)
{
    BYTE *ship;
    if (!obj || !object_is(obj, TYPE_SHIP_SUBSYSTEM) || !readable(obj + 0x40, sizeof(ship)))
        return NULL;
    ship = *(BYTE **)(obj + 0x40);
    return object_is(ship, TYPE_SHIP) ? ship : NULL;
}

static BYTE *ship_from_tube(BYTE *tube)
{
    BYTE *system, *ship;
    if (!object_is(tube, TYPE_TORPEDO_TUBE) || !readable(tube + 0x24, sizeof(system)))
        return NULL;
    system = *(BYTE **)(tube + 0x24);
    if (!object_is(system, TYPE_TORPEDO_SYSTEM) || !readable(system + 0x40, sizeof(ship)))
        return NULL;
    ship = *(BYTE **)(system + 0x40);
    return object_is(ship, TYPE_SHIP) ? ship : NULL;
}

static BYTE *ship_from_pulse(BYTE *weapon)
{
    BYTE *ship;
    if (!object_is(weapon, TYPE_PULSE_WEAPON) || !readable(weapon + 0x40, sizeof(ship)))
        return NULL;
    ship = *(BYTE **)(weapon + 0x40);
    return object_is(ship, TYPE_SHIP) ? ship : NULL;
}

static int ship_owned(BYTE *ship, int sender, int current)
{
    DWORD currentId = 0;
    if (!object_is(ship, TYPE_SHIP) || !readable(ship + 0x2E4, sizeof(int)) ||
        *(int *)(ship + 0x2E4) != sender)
        return 0;
    if (!current)
        return 1;
    return game_player_record(sender, NULL, &currentId) && currentId == *(DWORD *)(ship + 4);
}

static int ship_alive(BYTE *ship)
{
    return readable(ship + 0x14C, sizeof(DWORD)) && *(DWORD *)(ship + 0x14C) == 0x7F7FFFFF;
}

static void endpoints_expire(DWORD now)
{
    int i;
    for (i = 0; i < MAX_ENDPOINTS; i++)
        if (g_endpoints[i].state && now - g_endpoints[i].seen > ENDPOINT_STALE_MS)
            memset(&g_endpoints[i], 0, sizeof(g_endpoints[i]));
}

static Endpoint *endpoint_find(SOCKET s, const struct sockaddr_in *from)
{
    int i;
    endpoints_expire(GetTickCount());
    for (i = 0; i < MAX_ENDPOINTS; i++)
        if (g_endpoints[i].state && g_endpoints[i].socket == s &&
            g_endpoints[i].ip == from->sin_addr.s_addr && g_endpoints[i].port == from->sin_port)
            return &g_endpoints[i];
    return NULL;
}

static int endpoint_uid_in_use(DWORD uid)
{
    int i;
    for (i = 0; i < MAX_ENDPOINTS; i++)
        if ((g_endpoints[i].state == 1 || g_endpoints[i].state == 2) &&
            g_endpoints[i].uid == uid)
            return 1;
    return 0;
}

static Endpoint *endpoint_alloc(SOCKET s, const struct sockaddr_in *from)
{
    Endpoint *candidate = NULL;
    int i;
    endpoints_expire(GetTickCount());
    for (i = 0; i < MAX_ENDPOINTS; i++) {
        if (!g_endpoints[i].state) {
            candidate = &g_endpoints[i];
            break;
        }
        /* A connect flood may replace only another unauthenticated connect,
         * never a bound player or the client's expected host endpoint. */
        if (g_endpoints[i].state == 1 &&
            (!candidate || g_endpoints[i].seen < candidate->seen))
            candidate = &g_endpoints[i];
    }
    if (!candidate)
        return NULL;
    memset(candidate, 0, sizeof(*candidate));
    candidate->socket = s;
    candidate->ip = from->sin_addr.s_addr;
    candidate->port = from->sin_port;
    candidate->playerId = -1;
    candidate->seen = GetTickCount();
    return candidate;
}

static void endpoint_remove(SOCKET s, const struct sockaddr_in *from)
{
    Endpoint *ep;
    EnterCriticalSection(&g_guardLock);
    ep = endpoint_find(s, from);
    if (ep)
        memset(ep, 0, sizeof(*ep));
    LeaveCriticalSection(&g_guardLock);
}

static void pending_receive_add(SOCKET s, const struct sockaddr_in *from)
{
    PendingReceive *slot = NULL, *oldest = NULL;
    unsigned long serial;
    int i;
    EnterCriticalSection(&g_guardLock);
    serial = ++g_pendingSerial;
    if (!serial)
        serial = ++g_pendingSerial;
    for (i = 0; i < MAX_PENDING_RECEIVES; i++) {
        if (!g_pendingReceives[i].used) {
            slot = &g_pendingReceives[i];
            break;
        }
        if (!oldest || (LONG)(g_pendingReceives[i].serial - oldest->serial) < 0)
            oldest = &g_pendingReceives[i];
    }
    if (!slot)
        slot = oldest;
    if (slot) {
        memset(slot, 0, sizeof(*slot));
        slot->socket = s;
        slot->threadId = GetCurrentThreadId();
        slot->from = *from;
        slot->serial = serial;
        slot->used = 1;
    }
    LeaveCriticalSection(&g_guardLock);
}

static int pending_receive_take(SOCKET s, struct sockaddr_in *from)
{
    PendingReceive *oldest = NULL;
    DWORD threadId = GetCurrentThreadId();
    int i;
    EnterCriticalSection(&g_guardLock);
    for (i = 0; i < MAX_PENDING_RECEIVES; i++) {
        PendingReceive *pending = &g_pendingReceives[i];
        if (!pending->used || pending->socket != s || pending->threadId != threadId)
            continue;
        if (!oldest || (LONG)(pending->serial - oldest->serial) < 0)
            oldest = pending;
    }
    if (oldest) {
        *from = oldest->from;
        memset(oldest, 0, sizeof(*oldest));
    }
    LeaveCriticalSection(&g_guardLock);
    return oldest != NULL;
}

static int utf16_name_ok(const BYTE *p, int bytes)
{
    int chars = 0;
    while (bytes >= 2) {
        if (p[0] == 0 && p[1] == 0)
            return chars <= g_cfg.playerNameMaxLength;
        chars++;
        if (chars > g_cfg.playerNameMaxLength)
            return 0;
        p += 2;
        bytes -= 2;
    }
    return 0;
}

static int parse_transport(const BYTE *p, int bytes, MsgSlice *out, int *countOut)
{
    int count, off, i;
    if (bytes < 2 || bytes > 0x200)
        return 0;
    count = p[1];
    off = 2;
    for (i = 0; i < count; i++) {
        unsigned field;
        int type, len, hdr, haveSeq, frag;
        if (off >= bytes)
            return 0;
        memset(&out[i], 0, sizeof(out[i]));
        type = p[off];
        len = hdr = 0;
        if (type == 2) {
            len = hdr = 1;
        } else if (type == 1) {
            if (off + 4 > bytes || (p[off + 3] & ~3))
                return 0;
            len = hdr = 4 + (p[off + 3] & 1);
        } else if (type == 0 || type == 3 || type == 4 || type == 5 || type == 50) {
            if (off + 3 > bytes)
                return 0;
            field = u16(p + off + 1);
            haveSeq = (field & 0x8000) != 0;
            frag = type == 50 && (field & 0x2000) != 0;
            hdr = haveSeq ? 5 : 3;
            len = field & (type == 50 ? 0x1FFF : 0x3FFF);
            if (len < hdr || off + len > bytes)
                return 0;
            if (haveSeq) {
                out[i].haveSeq = 1;
                out[i].seq = (unsigned short)u16(p + off + 3);
            }
            if (frag) {
                int index;
                if (!haveSeq)
                    return 0;
                out[i].isFrag = 1;
                if (hdr >= len)
                    return 0;
                index = p[off + hdr++];
                out[i].fragIndex = (unsigned char)index;
                if (index == 0) {
                    int total;
                    if (hdr >= len)
                        return 0;
                    total = p[off + hdr++];
                    if (total < 1)
                        return 0;
                    out[i].fragTotal = (unsigned char)total;
                }
            }
            if (type == 4 && len < hdr + 1)
                return 0;
            if (type == 50 && len < hdr + 1)
                return 0;
        } else {
            return 0;
        }
        if (len <= 0 || off + len > bytes)
            return 0;
        out[i].off = (unsigned short)off;
        out[i].len = (unsigned short)len;
        out[i].hdr = (unsigned short)hdr;
        out[i].type = (unsigned char)type;
        off += len;
    }
    if (off != bytes)
        return 0;
    *countOut = count;
    return 1;
}

static int only_type(const MsgSlice *slices, int count, int type)
{
    int i;
    if (!count)
        return 0;
    for (i = 0; i < count; i++)
        if (slices[i].type != type)
            return 0;
    return 1;
}

static int connect_uid(const BYTE *packet, const MsgSlice *slice, DWORD *uid)
{
    const BYTE *body;
    int bodyLen;
    if (slice->type != 3)
        return 0;
    body = packet + slice->off + slice->hdr;
    bodyLen = slice->len - slice->hdr;
    if (bodyLen < 10 || !utf16_name_ok(body + 8, bodyLen - 8))
        return 0;
    *uid = u32(body + 4);
    return 1;
}

static int below_player_limit(void)
{
    BYTE *net = network_ptr();
    BYTE *game = game_ptr();
    int count, maxPlayers = 16;
    if (!readable(net, 0x34))
        return 0;
    count = *(int *)(net + 0x30);
    if (readable(game, 0x200) && *(int *)(game + 0x1FC) > 0)
        maxPlayers = *(int *)(game + 0x1FC);
    return count >= 0 && count < maxPlayers && count < 125;
}

static int player_transport_ok(BYTE *player, const MsgSlice *slices, int count)
{
    int i;
    if (!readable(player, 0x9C) || *(int *)(player + 0x98) >= MAX_PENDING_ACKS)
        return 0;
    for (i = 0; i < count; i++) {
        unsigned short expected;
        short ahead;
        if (!slices[i].haveSeq)
            continue;
        expected = *(unsigned short *)(player + (slices[i].type == 50 ? 0x28 : 0x24));
        ahead = (short)(slices[i].seq - expected);
        if (ahead > MAX_REORDER_AHEAD)
            return 0;
    }
    return 1;
}

static int endpoint_fragments_ok(Endpoint *ep, const MsgSlice *slices, int count)
{
    DWORD now = GetTickCount();
    int i, j;
    for (j = 0; j < MAX_FRAGMENT_TRACKS; j++) {
        FragTrack *track = &ep->fragments[j];
        if (track->active && now - track->touched > 30000u)
            memset(track, 0, sizeof(*track));
    }
    for (i = 0; i < count; i++) {
        FragTrack *track = NULL;
        unsigned index, mask;
        if (!slices[i].isFrag)
            continue;
        index = slices[i].fragIndex;
        for (j = 0; j < MAX_FRAGMENT_TRACKS; j++)
            if (ep->fragments[j].active && ep->fragments[j].seq == slices[i].seq) {
                track = &ep->fragments[j];
                break;
            }
        if (!track) {
            for (j = 0; j < MAX_FRAGMENT_TRACKS; j++)
                if (!ep->fragments[j].active) {
                    track = &ep->fragments[j];
                    memset(track, 0, sizeof(*track));
                    track->active = 1;
                    track->seq = slices[i].seq;
                    break;
                }
        }
        if (!track)
            return 0;
        if (slices[i].fragTotal) {
            if ((track->total && track->total != slices[i].fragTotal) ||
                track->maxIndex >= slices[i].fragTotal)
                return 0;
            track->total = slices[i].fragTotal;
        } else if (track->total && index >= track->total) {
            return 0;
        }
        mask = 1u << (index & 7);
        if (track->seen[index >> 3] & mask)
            return 0;
        track->seen[index >> 3] |= (unsigned char)mask;
        track->count++;
        if (index > track->maxIndex)
            track->maxIndex = (unsigned char)index;
        track->touched = now;
        if (track->total && track->count == track->total)
            track->complete = 1;
    }
    for (j = 0; j < MAX_FRAGMENT_TRACKS; j++)
        if (ep->fragments[j].complete)
            memset(&ep->fragments[j], 0, sizeof(ep->fragments[j]));
    return 1;
}

static int bind_sender(SOCKET s, const struct sockaddr_in *from, const BYTE *packet,
                       const MsgSlice *slices, int count, int sender, int *ownerOut)
{
    Endpoint *ep;
    BYTE *player;
    DWORD uid = 0;
    int host = is_host();
    int ok = 0;
    int fragmentsChecked = 0;
    EnterCriticalSection(&g_guardLock);
    ep = endpoint_find(s, from);
    if (host) {
        if (!ep) {
            int shapeOk = count == 1 && only_type(slices, count, 3);
            int uidOk = shapeOk && connect_uid(packet, &slices[0], &uid);
            int roomOk = below_player_limit();
            int uidInUse = uidOk && endpoint_uid_in_use(uid);
            if (sender != -1 || !shapeOk || !roomOk || !uidOk || uidInUse) {
                if (g_transportDrops < 32)
                    logf("network guard: connect rejected sender=%d count=%d shape=%d room=%d uid=%d duplicate=%d",
                         sender, count, shapeOk, roomOk, uidOk, uidInUse);
                goto done;
            }
            ep = endpoint_alloc(s, from);
            if (!ep)
                goto done;
            ep->state = 1;
            ep->uid = uid;
            if (g_transportDrops < 32)
                logf("network guard: connect pending source=%08X claimed=%08X uid=%08X",
                     (unsigned)from->sin_addr.s_addr,
                     (unsigned)u32(packet + slices[0].off + slices[0].hdr),
                     (unsigned)uid);
            ok = 1;
            goto done;
        }
        ep->seen = GetTickCount();
        if (ep->state == 1) {
            if (sender == -1) {
                if (count == 1 && only_type(slices, count, 3) &&
                    connect_uid(packet, &slices[0], &uid) && uid == ep->uid)
                    ok = 1;
                else if (only_type(slices, count, 1) || only_type(slices, count, 2))
                    ok = 1; /* pre-auth reliable ACK/control during the connect exchange */
                else if (g_transportDrops < 32)
                    logf("network guard: pending connect mismatch count=%d type=%d uid=%08X expected=%08X",
                         count, count == 1 ? slices[0].type : -1,
                         (unsigned)uid, (unsigned)ep->uid);
                goto done;
            }
            player = net_player_by_id(sender);
        if (!readable(player, 0x24) || *(DWORD *)(player + 0x1C) != ep->ip ||
            *(DWORD *)(player + 0x20) != ep->uid ||
            !player_transport_ok(player, slices, count) ||
            !endpoint_fragments_ok(ep, slices, count))
                goto done;
        fragmentsChecked = 1;
            ep->state = 2;
            ep->playerId = sender;
        }
        if (ep->state == 2 && (sender == ep->playerId ||
            (sender == -1 && only_type(slices, count, 5)) ||
            (sender == -1 && only_type(slices, count, 3) &&
             connect_uid(packet, &slices[0], &uid) && uid == ep->uid))) {
            player = net_player_by_id(ep->playerId);
        if (!player_transport_ok(player, slices, count) ||
            (!fragmentsChecked && !endpoint_fragments_ok(ep, slices, count)))
                goto done;
            *ownerOut = ep->playerId;
            ok = 1;
        }
    } else {
        BYTE *net = network_ptr();
        int hostId = readable(net, 0x24) ? *(int *)(net + 0x20) : -1;
        if (!ep)
            goto done;
        if (ep->state != 3)
            goto done;
        ep->seen = GetTickCount();
        if (sender == -1) {
            ok = only_type(slices, count, 3) || only_type(slices, count, 4) || only_type(slices, count, 5);
        } else if (hostId >= 0 && sender == hostId) {
            player = net_player_by_id(hostId);
        if (!player_transport_ok(player, slices, count) ||
            !endpoint_fragments_ok(ep, slices, count))
                goto done;
            ep->playerId = hostId;
            *ownerOut = hostId;
            ok = 1;
        }
    }
done:
    LeaveCriticalSection(&g_guardLock);
    return ok;
}

static int transport_contents(const BYTE *packet, const MsgSlice *slices, int count,
                              int owner, int host)
{
    int i;
    for (i = 0; i < count; i++) {
        const BYTE *body = packet + slices[i].off + slices[i].hdr;
        int bodyLen = slices[i].len - slices[i].hdr;
        switch (slices[i].type) {
        case 0:
            if (bodyLen < 7 || !utf16_name_ok(body + 5, bodyLen - 5) ||
                (host && owner >= 0 && (signed char)body[0] != owner)) {
                if (g_cfg.testMode && g_transportDrops < 32)
                    logf("network guard: invalid type 0 body=%d owner=%d first=%d",
                         bodyLen, owner, bodyLen ? (signed char)body[0] : -999);
                return 0;
            }
            break;
        case 3:
            if ((host && (bodyLen < 10 || !utf16_name_ok(body + 8, bodyLen - 8))) ||
                (!host && bodyLen != 1 &&
                 (bodyLen < 10 || !utf16_name_ok(body + 8, bodyLen - 8)))) {
                if (g_cfg.testMode && g_transportDrops < 32)
                    logf("network guard: invalid type 3 body=%d", bodyLen);
                return 0;
            }
            break;
        case 4:
            if (host)
                return 0;
            break;
        case 5:
            if (bodyLen < 1 || (host && owner >= 0 && (signed char)body[0] != owner))
                return 0;
            break;
        default:
            break;
        }
    }
    return 1;
}

static void normalize_connect_sources(BYTE *packet, const MsgSlice *slices, int count,
                                      const struct sockaddr_in *from)
{
    int i;
    for (i = 0; i < count; i++)
        if (slices[i].type == 3)
            memcpy(packet + slices[i].off + slices[i].hdr,
                   &from->sin_addr.s_addr, sizeof(from->sin_addr.s_addr));
}

int guard_datagram(SOCKET s, char *buf, int len, const struct sockaddr *from, int fromlen)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)from;
    const char *reason = "framing";
    MsgSlice slices[256];
    int count = 0, owner = -1, sender;
    if (!g_guardReady || !g_cfg.guardNetwork)
        return 1;
    if (s != game_socket())
        return 1;
    if (len == 0)
        goto reject;
    if (!buf || len < 0 || !from || fromlen < (int)sizeof(*in) || in->sin_family != AF_INET)
        goto reject;
    /* GameSpy query/reply traffic shares Winsock hooks but not TGNetwork framing. */
    if (buf[0] == '\\')
        return 1;
    if (!parse_transport((const BYTE *)buf, len, slices, &count))
        goto reject;
    sender = (signed char)buf[0];
    reason = "endpoint";
    if (!bind_sender(s, in, (const BYTE *)buf, slices, count, sender, &owner))
        goto reject;
    reason = "contents";
    if (!transport_contents((const BYTE *)buf, slices, count, owner, is_host()))
        goto reject;
    if (is_host())
        normalize_connect_sources((BYTE *)buf, slices, count, in);
    if (only_type(slices, count, 5))
        endpoint_remove(s, in);
    return 1;
reject:
    if (g_transportDrops++ < 32)
        logf("network guard: rejected UDP datagram socket=%u len=%d reason=%s host=%d",
             (unsigned)s, len, reason, is_host());
    return 0;
}

void guard_note_receive(SOCKET s, const char *buf, int len,
                        const struct sockaddr *from, int fromlen)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)from;
    if (!g_guardReady || !g_receiveHooked || !g_cfg.guardNetwork ||
        !buf || len <= 0 || buf[0] == '\\' || s != game_socket() ||
        !from || fromlen < (int)sizeof(*in) || in->sin_family != AF_INET)
        return;
    pending_receive_add(s, in);
}

int guard_note_send(SOCKET s, const char *buf, int len, const struct sockaddr *to, int tolen)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)to;
    Endpoint *ep;
    if (!g_guardReady || s != game_socket())
        return 0;
    if (!g_cfg.guardNetwork || is_host() || !buf || len <= 0 || buf[0] == '\\' ||
        !to || tolen < (int)sizeof(*in) || in->sin_family != AF_INET)
        return 1;
    EnterCriticalSection(&g_guardLock);
    ep = endpoint_find(s, in);
    if (!ep)
        ep = endpoint_alloc(s, in);
    if (ep) {
        ep->state = 3;
        ep->seen = GetTickCount();
    }
    LeaveCriticalSection(&g_guardLock);
    return 1;
}

int guard_encrypt_test(char *buf, int len)
{
    BYTE *network, *crypt;
    void **vt;
    if (!g_cfg.testMode || !buf || len < 0)
        return 0;
    if (len <= 1 || buf[0] == '\\')
        return 1;
    network = network_ptr();
    if (!network || !readable(network + 0xF0, sizeof(BYTE *)))
        return 0;
    crypt = *(BYTE **)(network + 0xF0);
    if (!crypt)
        return 1;
    if (!readable(crypt, sizeof(void *)))
        return 0;
    vt = *(void ***)crypt;
    if (!readable(vt, 2 * sizeof(void *)) || !vt[1])
        return 0;
    ((PFN_Crypt)vt[1])(crypt, 0, buf + 1, len - 1);
    return 1;
}

void guard_forget_socket(SOCKET s)
{
    int i;
    if (!g_guardReady)
        return;
    EnterCriticalSection(&g_guardLock);
    for (i = 0; i < MAX_ENDPOINTS; i++)
        if (g_endpoints[i].state && g_endpoints[i].socket == s)
            memset(&g_endpoints[i], 0, sizeof(g_endpoints[i]));
    for (i = 0; i < MAX_PENDING_RECEIVES; i++)
        if (g_pendingReceives[i].used && g_pendingReceives[i].socket == s)
            memset(&g_pendingReceives[i], 0, sizeof(g_pendingReceives[i]));
    LeaveCriticalSection(&g_guardLock);
}

static int event_prefix(const BYTE *d, int len, DWORD cls, DWORD type, int exact)
{
    return len == exact && len >= 17 && u32(d + 1) == cls && u32(d + 5) == type;
}

static int id_in_slot(DWORD id, int slot)
{
    DWORD base = 0x3FFFFFFFu + ((DWORD)slot << 18);
    return (DWORD)(id - base) < 0x40000u;
}

static int validate_create(const BYTE *d, int len, int sender, int host, int playerCreate)
{
    int slot, senderSlot = -1, base, species, pos, n;
    DWORD cls, id, currentId = 0;
    if (len < (playerCreate ? 12 : 11))
        return 0;
    slot = (signed char)d[1];
    if (slot < 0 || slot >= 16)
        return 0;
    base = playerCreate ? 3 : 2;
    if (playerCreate && (signed char)d[2] < 0)
        return 0;
    cls = u32(d + base);
    id = u32(d + base + 4);
    species = (signed char)d[base + 8];
    if (cls != TYPE_PHYSICS && cls != TYPE_DAMAGEABLE && cls != TYPE_SHIP)
        return 0;
    if (playerCreate && (cls != TYPE_SHIP || species <= 0 || species >= 16))
        return 0;
    if (!playerCreate && cls == TYPE_SHIP && (species <= 0 || species >= 46))
        return 0;
    pos = base + 44;
    if (pos >= len)
        return 0;
    n = d[pos++];
    if (n > len - pos)
        return 0;
    pos += n;
    if (pos >= len)
        return 0;
    n = d[pos++];
    if (n > len - pos)
        return 0;
    if (host) {
        BYTE *current;
        if (!game_player_record(sender, &senderSlot, &currentId) ||
            senderSlot != slot || !id_in_slot(id, slot))
            return 0;
        if (playerCreate && (signed char)d[2] != sender)
            return 0;
        current = currentId ? find_object(currentId) : NULL;
        if (playerCreate && current && object_is(current, TYPE_SHIP) && ship_alive(current))
            return 0;
        if (find_object(id))
            return 0;
    }
    return 1;
}

static int validate_slot_message(const BYTE *d, int len)
{
    int n, pos;
    DWORD chars;
    if (len < 9 || d[5] < 0x60 || d[5] > 0x67 || d[6] >= 16 || !finite_float(d + 1))
        return 0;
    n = i16(d + 7);
    if (n < 0 || n > 1023 || n > len - 9)
        return 0;
    pos = 9 + n;
    if (d[5] & 4) {
        if (len - pos < 4)
            return 0;
        chars = u32(d + pos);
        pos += 4;
        if (chars > 1023 || chars > (DWORD)(len - pos) / 2)
            return 0;
        pos += (int)chars * 2;
    }
    return pos == len;
}

typedef struct BoolCursor {
    int remain;
    unsigned values;
} BoolCursor;

static int take_bool(const BYTE *d, int len, int *pos, BoolCursor *bc, unsigned *value)
{
    unsigned b, count;
    if (!bc->remain) {
        if (*pos >= len)
            return 0;
        b = d[(*pos)++];
        count = b >> 5;
        if (count < 1 || count > 5)
            return 0;
        bc->remain = (int)count;
        bc->values = b & 0x1F;
    }
    *value = bc->values & 1;
    bc->values >>= 1;
    bc->remain--;
    return 1;
}

static int state_reader_address(BYTE *subsystem, DWORD *reader)
{
    BYTE *vt;
    if (!object_is(subsystem, TYPE_SHIP_SUBSYSTEM) || !readable(subsystem, sizeof(vt)))
        return 0;
    vt = *(BYTE **)subsystem;
    if (!readable(vt + 0x74, sizeof(*reader)))
        return 0;
    *reader = *(DWORD *)(vt + 0x74);
    return *reader == STATE_READER_BASE || *reader == STATE_READER_POWERED ||
           *reader == STATE_READER_BATTERY || *reader == STATE_READER_SHIELD;
}

static int parse_health_record(BYTE *subsystem, const BYTE *d, int len, int *pos,
                               BoolCursor *bc, BYTE **stack, int depth)
{
    DWORD reader;
    unsigned present;
    int children, i;
    if (depth >= 64 || !state_reader_address(subsystem, &reader) ||
        !readable(subsystem + 0x1C, sizeof(children)))
        return 0;
    for (i = 0; i < depth; i++)
        if (stack[i] == subsystem)
            return 0;
    stack[depth] = subsystem;
    if (*pos >= len)
        return 0;
    (*pos)++; /* condition byte */
    children = *(int *)(subsystem + 0x1C);
    if (children < 0 || children > 256)
        return 0;
    for (i = 0; i < children; i++) {
        BYTE *child = ((PFN_SubsystemChild)ADDR_SUBSYSTEM_CHILD)(subsystem, 0, i);
        if (!child || !parse_health_record(child, d, len, pos, bc, stack, depth + 1))
            return 0;
    }
    if (reader == STATE_READER_POWERED || reader == STATE_READER_SHIELD) {
        if (!take_bool(d, len, pos, bc, &present))
            return 0;
        if (present) {
            if (*pos >= len)
                return 0;
            (*pos)++; /* signed power-percent byte */
        }
    }
    if (reader == STATE_READER_BATTERY) {
        if (len - *pos < 2)
            return 0;
        *pos += 2;
    } else if (reader == STATE_READER_SHIELD) {
        if (len - *pos < 6)
            return 0;
        *pos += 6;
    }
    return 1;
}

static int subsystem_list_count(BYTE *ship, BYTE **headOut)
{
    BYTE *node;
    int count = 0;
    if (!readable(ship + 0x284, sizeof(node)))
        return 0;
    node = *(BYTE **)(ship + 0x284);
    *headOut = node;
    while (node && count < 256) {
        if (!readable(node, 2 * sizeof(void *)) || !*(BYTE **)node)
            return 0;
        node = *(BYTE **)(node + 4);
        count++;
    }
    if (node)
        return 0;
    return count;
}

static BYTE *subsystem_at(BYTE *head, unsigned index)
{
    BYTE *node = head;
    while (node && index--) {
        if (!readable(node + 4, sizeof(node)))
            return NULL;
        node = *(BYTE **)(node + 4);
    }
    if (!node || !readable(node, sizeof(BYTE *)))
        return NULL;
    return *(BYTE **)node;
}

static int validate_health_tail(BYTE *ship, const BYTE *d, int len, int *pos, BoolCursor *bc)
{
    BYTE *head, *node, *stack[64];
    unsigned start;
    int count, records = 0;
    if (*pos >= len)
        return 0;
    start = d[(*pos)++];
    count = subsystem_list_count(ship, &head);
    if (count <= 0 || start >= (unsigned)count)
        return 0;
    node = head;
    while (start--)
        node = *(BYTE **)(node + 4);
    while (*pos < len) {
        BYTE *subsystem;
        if (!node)
            node = head;
        if (records++ >= count || !readable(node, 2 * sizeof(void *)))
            return 0;
        subsystem = *(BYTE **)node;
        node = *(BYTE **)(node + 4);
        if (!parse_health_record(subsystem, d, len, pos, bc, stack, 0))
            return 0;
    }
    return *pos == len;
}

static int validate_power_tail(BYTE *ship, const BYTE *d, int len, int *pos)
{
    BYTE *head;
    int count = subsystem_list_count(ship, &head);
    if (count <= 0)
        return 0;
    while (*pos < len) {
        BYTE *subsystem;
        unsigned index;
        if (len - *pos < 2)
            return 0;
        index = d[(*pos)++];
        (*pos)++; /* power byte: all 0..255 are legitimate */
        if (index >= (unsigned)count)
            return 0;
        subsystem = subsystem_at(head, index);
        if (!subsystem || !object_is(subsystem, TYPE_POWERED_SUBSYSTEM))
            return 0;
    }
    return *pos == len;
}

static int validate_state(const BYTE *d, int len, int sender, int host)
{
    BYTE *obj;
    BoolCursor bc;
    unsigned value;
    int pos = 10, ship;
    BYTE flags;
    if (len < 10 || !finite_float(d + 5))
        return 0;
    obj = find_object(u32(d + 1));
    if (!obj || !object_is(obj, TYPE_PHYSICS))
        return 0;
    ship = object_is(obj, TYPE_SHIP);
    flags = d[9];
    if (host && (!ship || !ship_owned(obj, sender, 1) || (flags & 0x20)))
        return 0;
    if (!ship && (flags & 0xE0))
        return 0;
    memset(&bc, 0, sizeof(bc));
    if (flags & 0x01) {
        if (len - pos < 12 || !finite_float(d + pos) || !finite_float(d + pos + 4) ||
            !finite_float(d + pos + 8))
            return 0;
        pos += 12;
        if (ship) {
            if (!take_bool(d, len, &pos, &bc, &value))
                return 0;
            if (value) {
                if (len - pos < 2)
                    return 0;
                pos += 2;
            }
        }
    }
    if (flags & 0x02) { if (len - pos < 5) return 0; pos += 5; }
    if (flags & 0x04) { if (len - pos < 3) return 0; pos += 3; }
    if (flags & 0x08) { if (len - pos < 3) return 0; pos += 3; }
    if (flags & 0x10) { if (len - pos < 2) return 0; pos += 2; }
    if (flags & 0x40) {
        if (!take_bool(d, len, &pos, &bc, &value))
            return 0;
    }
    if ((flags & 0x20) && (flags & 0x80))
        return 0;
    if (flags & 0x20) {
        /* Mirror each native subsystem reader without mutating the ship. */
        return validate_health_tail(obj, d, len, &pos, &bc);
    }
    if (flags & 0x80)
        return validate_power_tail(obj, d, len, &pos);
    return pos == len;
}

static BYTE *event_ship(DWORD objectId, int requiredType)
{
    BYTE *obj = find_object(objectId);
    if (!obj || !object_is(obj, requiredType))
        return NULL;
    if (requiredType == TYPE_SHIP)
        return obj;
    return ship_from_subsystem(obj);
}

static int validate_owned_event(const BYTE *d, int sender, int requiredType,
                                int current, int alive)
{
    BYTE *ship = event_ship(u32(d + 13), requiredType);
    return ship && ship_owned(ship, sender, current) && (!alive || ship_alive(ship));
}

static int other_player_ship(DWORD objectId, int sender)
{
    BYTE *game = game_ptr();
    int slot;
    if (!readable(game, 0x200))
        return 0;
    for (slot = 0; slot < 16; slot++) {
        BYTE *rec = game + 0x78 + slot * 0x18;
        if (rec[0] && *(int *)(rec + 4) != sender && *(DWORD *)(rec + 8) == objectId)
            return 1;
    }
    return 0;
}

static int validate_collision(const BYTE *d, int len, int sender)
{
    int count;
    DWORD source, dest, currentId = 0;
    BYTE *destObj, *sourceObj;
    float force, mass;
    if (len < 22 || u32(d + 1) != EVT_COLLISION || u32(d + 5) != 0x800050)
        return 0;
    count = (signed char)d[17];
    if (count < 1 || count > 127 || len != 22 + 4 * count ||
        !finite_float(d + 18 + 4 * count))
        return 0;
    memcpy(&force, d + 18 + 4 * count, sizeof(force));
    if (force < 0.0f)
        return 0;
    if (!game_player_record(sender, NULL, &currentId))
        return 0;
    source = u32(d + 9);
    dest = u32(d + 13);
    if (source != currentId && dest != currentId)
        return 0;
    destObj = find_object(dest);
    if (!destObj || !object_is(destObj, TYPE_PHYSICS))
        return 0;
    sourceObj = source ? find_object(source) : NULL;
    if (source && (!sourceObj || !object_is(sourceObj, TYPE_PHYSICS)))
        return 0;
    /* A client legitimately reports damage to its own ship, and v1.7.5 also
     * preserves its ship as source when an AI ship was rammed.  It never owns
     * the opposite-direction report for another player's ship. */
    if (dest != currentId &&
        (source != currentId || !object_is(destObj, TYPE_SHIP) ||
         other_player_ship(dest, sender)))
        return 0;
    if (!readable(destObj + 0xD8, sizeof(mass)) || !finite_float(destObj + 0xD8))
        return 0;
    memcpy(&mass, destObj + 0xD8, sizeof(mass));
    if (mass <= 0.0f ||
        (double)force > (double)mass * (double)count *
                        (double)g_cfg.collisionMaxSpecificForce)
        return 0;
    return 1;
}

static int validate_fire(const BYTE *d, int len, int sender, int host, int torpedo)
{
    BYTE *weapon, *ship, *target;
    int flags;
    if (torpedo) {
        if (len < 10)
            return 0;
        flags = d[6];
        if ((flags & ~7) || len != ((flags & 4) ? 10 : 18))
            return 0;
        weapon = find_object(u32(d + 1));
        ship = ship_from_tube(weapon);
        if (ship && readable(weapon + 0x24, sizeof(BYTE *))) {
            BYTE *system = *(BYTE **)(weapon + 0x24);
            int selected;
            if (!readable(system + 0x118, sizeof(int)))
                return 0;
            selected = *(int *)(system + 0x114);
            if (selected < 0 || selected >= 4 ||
                !readable(system + 0x104 + selected * sizeof(void *), sizeof(void *)) ||
                !*(void **)(system + 0x104 + selected * sizeof(void *)))
                return 0;
        }
    } else {
        if (len < 10)
            return 0;
        flags = d[9];
        if ((flags & ~3) || len != ((flags & 2) ? 14 : 10))
            return 0;
        weapon = find_object(u32(d + 1));
        ship = ship_from_pulse(weapon);
    }
    if (!weapon || !ship)
        return 0;
    if ((torpedo && !(flags & 4)) || (!torpedo && (flags & 2))) {
        target = find_object(u32(d + 10));
        if (target && !object_is(target, TYPE_PHYSICS))
            return 0;
    }
    if (host && ((signed char)d[5] != sender || !ship_owned(ship, sender, 1) ||
        (g_cfg.rejectDeadShipFire && !ship_alive(ship))))
        return 0;
    return 1;
}

static int host_only_native(int op)
{
    switch (op) {
    case 0x00: case 0x01: case 0x06: case 0x14: case 0x16: case 0x17:
    case 0x18: case 0x1D: case 0x1F: case 0x22: case 0x29:
        return 1;
    default:
        return 0;
    }
}

static int validate_native(const BYTE *d, int len, int sender, int host)
{
    int op = d[0];
    BYTE *obj, *ship, *other;
    if (host && host_only_native(op))
        return 0;
    if (!host && (op == 0x0D || op == 0x13 || op == 0x15 || op == 0x2A))
        return 0;
    switch (op) {
    case 0x00: return validate_slot_message(d, len);
    case 0x01: return len == 1;
    case 0x02:
        if (host)
            return 0; /* clients never legitimately create generic network objects */
        return validate_create(d, len, sender, host, 0);
    case 0x03: return validate_create(d, len, sender, host, 1);
    case 0x06:
        return (len == 17 && u32(d + 1) == EVT_BASE && u32(d + 5) == 0x8000DF) ||
               (len == 21 && u32(d + 1) == EVT_OBJPTR &&
                (u32(d + 5) == 0x800074 || u32(d + 5) == 0x800075)) ||
               (len == 25 && u32(d + 1) == EVT_EXPLODING && u32(d + 5) == 0x80004E);
    case 0x07:
        if (len != 25 || u32(d + 1) != EVT_START_FIRING)
            return 0;
        if (!host)
            return 1;
        obj = find_object(u32(d + 13));
        if (!obj || !(object_is(obj, TYPE_PHASER_SYSTEM) || object_is(obj, TYPE_TRACTOR_SYSTEM)))
            return 0;
        ship = ship_from_subsystem(obj);
        return ship_owned(ship, sender, 1) && (!g_cfg.rejectDeadShipFire || ship_alive(ship));
    case 0x08:
    case 0x09:
        if (len != (op == 0x08 ? 17 : 21) || u32(d + 1) != (op == 0x08 ? EVT_BASE : EVT_OBJPTR))
            return 0;
        if (!host)
            return 1;
        obj = find_object(u32(d + 13));
        if (!obj || !(object_is(obj, TYPE_PHASER_SYSTEM) || object_is(obj, TYPE_TRACTOR_SYSTEM)))
            return 0;
        return ship_owned(ship_from_subsystem(obj), sender, 0);
    case 0x0A:
        if (len != 18 || u32(d + 1) != EVT_BOOL || d[17] > 1)
            return 0;
        return !host || validate_owned_event(d, sender, TYPE_POWERED_SUBSYSTEM, 1, 0);
    case 0x0B:
        if (len != 17 || u32(d + 1) != EVT_BASE)
            return 0;
        if (!host)
            return 1;
        obj = find_object(u32(d + 9));
        other = find_object(u32(d + 13));
        if (!object_is(obj, TYPE_SHIP_SUBSYSTEM) || !object_is(other, TYPE_REPAIR_SYSTEM))
            return 0;
        ship = ship_from_subsystem(obj);
        return ship && ship == ship_from_subsystem(other) && ship_owned(ship, sender, 1);
    case 0x0C:
        /* Stock has no registered producer. Its arbitrary class/type passthrough
         * reaches the native factory and relay before any semantic checks. */
        return 0;
    case 0x0D:
        if (!event_prefix(d, len, EVT_OBJPTR, 0x800058, 21))
            return 0;
        return !host || validate_owned_event(d, sender, TYPE_SHIP, 1, 0);
    case 0x0E:
    case 0x0F:
        if (len != 17 || u32(d + 1) != EVT_BASE)
            return 0;
        return !host || validate_owned_event(d, sender, TYPE_CLOAK_SYSTEM, 1, 0);
    case 0x10:
        if (len < 34 || u32(d + 1) != EVT_WARP || len != 34 + d[17] ||
            !finite_float(d + 18 + d[17]) || !finite_float(d + 22 + d[17]) ||
            !finite_float(d + 26 + d[17]) || !finite_float(d + 30 + d[17]))
            return 0;
        return !host || validate_owned_event(d, sender, TYPE_SHIP, 1, 0);
    case 0x11:
        if (len != 21 || u32(d + 1) != EVT_OBJPTR)
            return 0;
        if (!host)
            return 1;
        ship = event_ship(u32(d + 13), TYPE_REPAIR_SYSTEM);
        other = find_object(u32(d + 17));
        return ship && object_is(other, TYPE_SHIP_SUBSYSTEM) && ship == ship_from_subsystem(other) &&
               ship_owned(ship, sender, 1);
    case 0x12:
        if (!event_prefix(d, len, EVT_CHAR, 0x8000E0, 18) || d[17] > 2)
            return 0;
        return !host || validate_owned_event(d, sender, TYPE_PHASER_SYSTEM, 1, 0);
    case 0x13:
        return len == 1 && (!host || game_player_record(sender, NULL, NULL));
    case 0x14: return len == 5;
    case 0x15: return host && validate_collision(d, len, sender);
    case 0x16: return len == 2 && (d[1] == 0x20 || d[1] == 0x21);
    case 0x17:
        return len == 18 && u32(d + 1) == EVT_PLAYER && u32(d + 5) == 0x8000F1;
    case 0x18:
        if (len < 5)
            return 0;
        return i32(d + 1) >= 0 && i32(d + 1) <= len - 5 && len == 5 + i32(d + 1);
    case 0x19: return validate_fire(d, len, sender, host, 1);
    case 0x1A: return validate_fire(d, len, sender, host, 0);
    case 0x1B:
        if (len != 6 || d[5] >= 4)
            return 0;
        obj = find_object(u32(d + 1));
        if (!object_is(obj, TYPE_TORPEDO_SYSTEM))
            return 0;
        return !host || ship_owned(ship_from_subsystem(obj), sender, 1);
    case 0x1C: return validate_state(d, len, sender, host);
    case 0x1D: return len == 5;
    case 0x1E: return len == 5 && (!host || net_player_by_id(sender) != NULL);
    case 0x1F:
        if (len < 10 || i32(d + 5) <= 0 || i32(d + 5) != len - 9 || d[len - 1] != 0)
            return 0;
        obj = find_object(u32(d + 1));
        return !obj || object_is(obj, TYPE_SHIP);
    case 0x20: {
        int p = 4, a, b;
        if (len < 7)
            return 0;
        a = i16(d + 2);
        if (a < 0 || a > 1023 || a > len - p)
            return 0;
        p += a;
        if (len - p < 3)
            return 0;
        b = i16(d + p);
        p += 2;
        if (b < 0 || b > 1023 || b > len - p - 1)
            return 0;
        p += b;
        return p + 1 == len && (d[p] == 0x20 || d[p] == 0x21);
    }
    case 0x22:
        return len >= 3 && i16(d + 1) >= 0 && i16(d + 1) <= 1023 && len == 3 + i16(d + 1);
    case 0x27:
    case 0x28: return len == 1;
    case 0x29:
        if (len != 14)
            return 0;
        obj = find_object(u32(d + 1));
        return !obj || object_is(obj, TYPE_DAMAGEABLE);
    case 0x2A: return len == 2 && (d[1] == 0x20 || d[1] == 0x21) && net_player_by_id(sender);
    default:
        return 1;
    }
}

static int host_only_script(int op)
{
    return op == 53 || op == 54 || op == 55 || op == 56 || op == 57 || op == 63 || op == 64;
}

static int validate_script(const BYTE *d, int len, int sender, int host)
{
    int op = d[0];
    if (host && host_only_script(op))
        return 0;
    if (op == 44 || op == 45) {
        int n;
        if (len < 7)
            return 0;
        n = i16(d + 5);
        if (n < 0 || n > g_cfg.chatMaxLength || n > len - 7)
            return 0;
        if (host && i32(d + 1) != sender)
            return 0;
        return 1;
    }
    if (op == 65) {
        if (len != 6 || d[5] > 1)
            return 0;
        return !host || i32(d + 1) == sender;
    }
    return 1;
}

static int validate_game_message(BYTE *msg)
{
    BYTE *data, *net;
    int len, sender, host, localId, hostId;
    if (!readable(msg, 0x10))
        return 0;
    data = *(BYTE **)(msg + 4);
    len = *(int *)(msg + 8);
    sender = *(int *)(msg + 0xC);
    if (len <= 0 || len > 0x100000 || !readable(data, (size_t)len))
        return 0;
    if (!is_multiplayer())
        return 1;
    net = network_ptr();
    if (!readable(net, 0x24))
        return 0;
    host = is_host();
    localId = *(int *)(net + 0x18);
    hostId = *(int *)(net + 0x20);
    if (!host && sender != hostId)
        return 0;
    if (host && sender != localId && !net_player_by_id(sender))
        return 0;
    if (g_cfg.validateGameMessages && data[0] <= 0x2A &&
        !validate_native(data, len, sender, host && sender != localId))
        return 0;
    if (g_cfg.hostOnlyScriptMessages && !validate_script(data, len, sender, host && sender != localId))
        return 0;
    return 1;
}

static unsigned char __fastcall hk_dispatch(void *manager, void *edx, BYTE *event)
{
    BYTE *msg;
    void **vt;
    (void)edx;
    if (!event || !readable(event, 0x2C) || *(DWORD *)(event + 0x10) != 0x60001)
        return ((PFN_Dispatch)g_dispatchTramp)(manager, 0, event);
    msg = *(BYTE **)(event + 0x28);
    if (!msg || !readable(msg, 0x10))
        return ((PFN_Dispatch)g_dispatchTramp)(manager, 0, event);
    vt = *(void ***)msg;
    if (!readable(vt, 2 * sizeof(void *)) || !vt[0] ||
        ((PFN_MsgType)vt[0])(msg, 0) != 0x32)
        return ((PFN_Dispatch)g_dispatchTramp)(manager, 0, event);
    if (validate_game_message(msg))
        return ((PFN_Dispatch)g_dispatchTramp)(manager, 0, event);
    if (g_gameDrops++ < 64) {
        BYTE *d = readable(msg + 4, sizeof(BYTE *)) ? *(BYTE **)(msg + 4) : NULL;
        int len = readable(msg + 8, sizeof(int)) ? *(int *)(msg + 8) : -1;
        logf("game guard: rejected sender=%d opcode=0x%02x len=%d", *(int *)(msg + 0xC),
             readable(d, 1) ? d[0] : 0xFF, len);
    }
    *(void **)(event + 0x28) = NULL;
    ((PFN_Delete)vt[1])(msg, 0, 1);
    return 1;
}

static char *__fastcall hk_receive_decoded_c(void *network, void *edx,
                                              int *length, DWORD *source)
{
    PFN_ReceiveDecoded receive;
    struct sockaddr_in from;
    void **vt;
    char *buf;
    SOCKET s;
    (void)edx;
    if (!network || !readable(network, sizeof(void *)))
        return NULL;
    vt = *(void ***)network;
    if (!readable(vt, 0x70) || !vt[0x6C / sizeof(void *)])
        return NULL;
    receive = (PFN_ReceiveDecoded)vt[0x6C / sizeof(void *)];
    buf = receive(network, 0, length, source);
    if (!buf || !g_cfg.guardNetwork || !length ||
        !readable(length, sizeof(*length)) ||
        !readable((BYTE *)network + 0x194, sizeof(SOCKET)))
        return buf;
    s = *(SOCKET *)((BYTE *)network + 0x194);
    if (!pending_receive_take(s, &from))
        return buf; /* Stock loopback/internal queue delivery did not use Winsock. */
    if (guard_datagram(s, buf, *length, (const struct sockaddr *)&from, sizeof(from)))
        return buf;
    ((PFN_Free)ADDR_FREE)(buf);
    return NULL;
}

__declspec(naked) static void hk_receive_decoded(void)
{
    __asm {
        xor esi, esi
        jmp hk_receive_decoded_c
    }
}

static void *__cdecl hk_gs_alloc(int size)
{
    return ((PFN_Malloc)o_gsAlloc)(size + 1);
}

static int __cdecl safe_duplicate_name(char *out, const char *fmt, const char *name, int serial)
{
    char suffix[32];
    int room, result;
    (void)fmt;
    _snprintf(suffix, sizeof(suffix), "_D%d", serial);
    suffix[sizeof(suffix) - 1] = 0;
    room = 255 - (int)strlen(suffix);
    if (room < 0)
        room = 0;
    result = _snprintf(out, 256, "%.*s%s", room, name, suffix);
    out[255] = 0;
    return result;
}

static int __cdecl safe_player_left(char *out, const char *format, const char *name)
{
    int result = _snprintf(out, 256, format, name);
    out[255] = 0;
    return result;
}

static const BYTE g_receiveOriginal[5] = {0x33, 0xF6, 0xFF, 0x50, 0x6C};
static const BYTE g_dispatchOriginal[5] = {0xA1, 0xFC, 0xAD, 0x95, 0x00};

static int security_bytes_match(DWORD address, const BYTE *expected, int length,
                                const char *what)
{
    if (!readable((const void *)address, (size_t)length) ||
        memcmp((const void *)address, expected, (size_t)length) != 0) {
        logf("SECURITY PREFLIGHT %s: expected bytes differ", what);
        return 0;
    }
    return 1;
}

static int security_call_matches(DWORD callsite, DWORD target, const char *what)
{
    const BYTE *p = (const BYTE *)callsite;
    DWORD actual;
    if (!readable(p, 5) || p[0] != 0xE8) {
        logf("SECURITY PREFLIGHT %s: expected CALL differs", what);
        return 0;
    }
    actual = callsite + 5 + *(const LONG *)(p + 1);
    if (actual != target) {
        logf("SECURITY PREFLIGHT %s: call target %08lx, expected %08lx",
             what, actual, target);
        return 0;
    }
    return 1;
}

static int security_layout_ok(void)
{
    static const BYTE objectName[] = {0x0F, 0xBE, 0xF0};
    static const BYTE setName[] = {0x0F, 0xBE, 0xF0};
    static const BYTE healthIndex[] = {0x0F, 0xBE, 0xC0};
    static const BYTE powerIndex[] = {0x0F, 0xBE, 0xE8};
    static const BYTE readString[] = {0xC6, 0x04, 0x06, 0x00};
    static const BYTE warpSet[] = {0x33, 0xFF};
    static const BYTE phaserLevel[] = {0x75, 0x21};
    static const BYTE warpName[] = {0x0F, 0xBE, 0xE8};
    int ok = 1;

    if (g_cfg.guardNetwork) {
        if (!security_bytes_match(ADDR_RECEIVE_CALL, g_receiveOriginal,
                                  sizeof(g_receiveOriginal), "decoded receive hook"))
            ok = 0;
        if (!security_call_matches(ADDR_GS_ALLOC_CALL, ADDR_MALLOC,
                                   "GameSpy buffer allocation"))
            ok = 0;
    }
    if (g_cfg.validateGameMessages || g_cfg.hostOnlyScriptMessages) {
        if (!security_bytes_match(ADDR_DISPATCH, g_dispatchOriginal,
                                  sizeof(g_dispatchOriginal), "GAME dispatch hook"))
            ok = 0;
    }
    if (g_cfg.validateGameMessages) {
        if (!security_bytes_match(0x005A2151, objectName, sizeof(objectName), "object name length")) ok = 0;
        if (!security_bytes_match(0x005A2175, setName, sizeof(setName), "set name length")) ok = 0;
        if (!security_bytes_match(0x005B2692, healthIndex, sizeof(healthIndex), "STATE health index")) ok = 0;
        if (!security_bytes_match(0x005B2717, powerIndex, sizeof(powerIndex), "STATE power index")) ok = 0;
        if (!security_bytes_match(0x006CF449, readString, sizeof(readString), "ReadString termination")) ok = 0;
        if (!security_bytes_match(0x006A0752, warpSet, sizeof(warpSet), "missing warp set")) ok = 0;
        if (!security_bytes_match(0x00572A68, phaserLevel, sizeof(phaserLevel), "phaser level")) ok = 0;
        if (!security_bytes_match(0x0043FC36, warpName, sizeof(warpName), "warp name length")) ok = 0;
        if (!security_call_matches(0x005A2241, 0x008599B9, "duplicate-name formatter")) ok = 0;
        if (!security_call_matches(0x006A14CB, 0x008599B9, "player-left formatter")) ok = 0;
    }
    return ok;
}

static int install_receive_hook(void)
{
    BYTE patch[5];
    LONG rel;
    if (memcmp((void *)ADDR_RECEIVE_CALL, g_receiveOriginal, sizeof(g_receiveOriginal)) != 0) {
        logf("HOOK network guard: decoded receive call differs, NOT applied");
        return 0;
    }
    patch[0] = 0xE8;
    rel = (LONG)hk_receive_decoded - (LONG)(ADDR_RECEIVE_CALL + 5);
    memcpy(patch + 1, &rel, 4);
    if (!mem_write((void *)ADDR_RECEIVE_CALL, patch, sizeof(patch)))
        return 0;
    logf("HOOK network guard (decoded receive): applied");
    return 1;
}

static int install_dispatch_hook(void)
{
    BYTE patch[5];
    LONG rel;
    if (memcmp((void *)ADDR_DISPATCH, g_dispatchOriginal, sizeof(g_dispatchOriginal)) != 0) {
        logf("HOOK game message guard: dispatch prologue differs, NOT applied");
        return 0;
    }
    g_dispatchTramp = (BYTE *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_dispatchTramp) {
        logf("HOOK game message guard: trampoline allocation failed");
        return 0;
    }
    memcpy(g_dispatchTramp, g_dispatchOriginal, 5);
    g_dispatchTramp[5] = 0xE9;
    rel = (LONG)(ADDR_DISPATCH + 5) - (LONG)(g_dispatchTramp + 10);
    memcpy(g_dispatchTramp + 6, &rel, 4);
    patch[0] = 0xE9;
    rel = (LONG)hk_dispatch - (LONG)(ADDR_DISPATCH + 5);
    memcpy(patch + 1, &rel, 4);
    if (!mem_write((void *)ADDR_DISPATCH, patch, sizeof(patch)))
        return 0;
    logf("HOOK game message guard (TGEventManager dispatch): applied");
    return 1;
}

int guard_install(void)
{
    int ok = 1;
    InitializeCriticalSection(&g_guardLock);
    if (!security_layout_ok()) {
        logf("SECURITY PREFLIGHT failed; required guard layout is unavailable");
        return 0;
    }
    g_guardReady = 1;
    if (g_cfg.guardNetwork) {
        g_receiveHooked = install_receive_hook();
        if (!g_receiveHooked)
            ok = 0;
        if (!redirect_call(ADDR_GS_ALLOC_CALL, (void *)hk_gs_alloc, &o_gsAlloc, ADDR_MALLOC,
                           "GameSpy receive buffer includes terminator byte"))
            ok = 0;
    }
    if (!(g_cfg.validateGameMessages || g_cfg.hostOnlyScriptMessages))
        return ok;
    if (g_cfg.validateGameMessages) {
        if (!patch_bytes(0x005A2151, "0f be f0", "0f b6 f0", "object name length is unsigned")) ok = 0;
        if (!patch_bytes(0x005A2175, "0f be f0", "0f b6 f0", "set name length is unsigned")) ok = 0;
        if (!patch_bytes(0x005B2692, "0f be c0", "0f b6 c0", "STATE health index is unsigned")) ok = 0;
        if (!patch_bytes(0x005B2717, "0f be e8", "0f b6 e8", "STATE power index is unsigned")) ok = 0;
        if (!patch_bytes(0x006CF449, "c6 04 06 00", "c6 00 00 90", "negative ReadString terminates at destination start")) ok = 0;
        if (!patch_bytes(0x006A0752, "33 ff", "eb 20", "missing warp set retains old set")) ok = 0;
        if (!patch_bytes(0x00572A68, "75 21", "75 18", "invalid phaser level initializes safely")) ok = 0;
        if (!patch_bytes(0x0043FC36, "0f be e8", "0f b6 e8", "warp event name length is unsigned")) ok = 0;
        if (!redirect_call(0x005A2241, (void *)safe_duplicate_name, NULL, 0x008599B9,
                           "bounded duplicate object name formatter")) ok = 0;
        if (!redirect_call(0x006A14CB, (void *)safe_player_left, NULL, 0x008599B9,
                           "bounded player-left formatter")) ok = 0;
    }
    if (!install_dispatch_hook())
        ok = 0;
    if (!ok)
        g_guardReady = 0;
    return ok;
}
