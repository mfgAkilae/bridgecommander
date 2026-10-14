/* Host relay of client ships.
 *
 * The host writes state updates for every ship, including the ships owned by
 * clients (ship writer 0x005B17F0, vtable +0x120). For those ships it serialises
 * its own proxy, which NetworkAI pulls towards the received position with a time
 * constant of about 0.2 s (even with CorrectionGainPercent=500). Other clients
 * therefore see a client ship one correction lag behind, on top of both network
 * hops (measured: ~1 unit mean error at 0 ms in a 6-player game, vs. 0.3 units
 * for the host's own view).
 *
 * The NetworkAI target (+0x30) is the received position, dead-reckoned every frame
 * along the received forward vector * speed (and advanced by the latency
 * compensation). The writer already supports a state snapshot (used by the stock
 * code for collisions, 0x005AF890): flag ship+0x2FC, position +0x300, two
 * orientation vectors +0x30C/+0x318, speed +0x324. On the host we fill that
 * snapshot with the AI target and the received speed before each write. */
#include "common.h"

#define ADDR_SHIP_WRITER 0x005B17F0
#define ADDR_GET_AI 0x005A1720       /* thiscall(object) -> AI */
#define ADDR_AS_NETWORK_AI 0x0047DE50 /* cdecl(AI) -> NetworkAI or 0 */
#define ADDR_IS_MULTIPLAYER 0x0097FA89
#define ADDR_IS_HOST 0x0097FA8A

#define SHIP_SNAPSHOT_FLAG 0x2FC
#define SHIP_SNAPSHOT 0x300 /* pos[3], vec(+0xAC)[3], vec(+0xB0)[3], speed */

typedef int(__fastcall *PFN_Writer)(void *ship, void *edx, void *peer);
typedef void *(__fastcall *PFN_GetAI)(void *obj, void *edx);
typedef void *(__cdecl *PFN_AsNetAI)(void *ai);
typedef float *(__fastcall *PFN_GetVec)(void *obj, void *edx, float *out);

static BYTE *g_tramp;
static unsigned long g_relayed;

static int __fastcall hk_Writer(void *ship, void *edx, void *peer)
{
    BYTE *s = (BYTE *)ship, *ai = NULL;
    int r;
    (void)edx;
    if (s && *(BYTE *)ADDR_IS_MULTIPLAYER && *(BYTE *)ADDR_IS_HOST && !s[SHIP_SNAPSHOT_FLAG]) {
        void *a = ((PFN_GetAI)ADDR_GET_AI)(ship, 0);
        if (a)
            ai = (BYTE *)((PFN_AsNetAI)ADDR_AS_NETWORK_AI)(a);
    }
    if (ai) {
        void **vt = *(void ***)ship;
        float tmp[3], *snap = (float *)(s + SHIP_SNAPSHOT), *v;
        memcpy(snap, ai + 0x30, 12);
        /* orientation: the proxy's own vectors, read like the stock snapshot code does */
        v = ((PFN_GetVec)vt[0xAC / 4])(ship, 0, tmp);
        memcpy(snap + 3, v, 12);
        v = ((PFN_GetVec)vt[0xB0 / 4])(ship, 0, tmp);
        memcpy(snap + 6, v, 12);
        snap[9] = *(float *)(ai + 0x54);
        s[SHIP_SNAPSHOT_FLAG] = 1;
        r = ((PFN_Writer)g_tramp)(ship, 0, peer);
        s[SHIP_SNAPSHOT_FLAG] = 0; /* the writer clears it only when it really writes */
        g_relayed++;
        return r;
    }
    return ((PFN_Writer)g_tramp)(ship, 0, peer);
}

unsigned long relay_count(void)
{
    return g_relayed;
}

void relay_install(void)
{
    static const BYTE prologue[7] = {0x6a, 0xff, 0x68, 0xbb, 0xb9, 0x87, 0x00}; /* push -1; push 0x87b9bb */
    BYTE jmp[5];
    LONG rel;
    if (!g_cfg.hostRelay)
        return;
    if (memcmp((void *)ADDR_SHIP_WRITER, prologue, 7) != 0) {
        logf("PATCH host relay of client ships: prologue differs, NOT applied");
        return;
    }
    g_tramp = (BYTE *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(g_tramp, prologue, 7);
    g_tramp[7] = 0xE9;
    rel = (LONG)(ADDR_SHIP_WRITER + 7) - (LONG)(g_tramp + 12);
    memcpy(g_tramp + 8, &rel, 4);
    jmp[0] = 0xE9;
    rel = (LONG)hk_Writer - (LONG)(ADDR_SHIP_WRITER + 5);
    memcpy(jmp + 1, &rel, 4);
    mem_write((void *)ADDR_SHIP_WRITER, jmp, 5);
    logf("PATCH host relay of client ships (state update writer): applied");
}
