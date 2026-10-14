/* Latency compensation for remote ships.
 *
 * Remote ships are driven by NetworkAI (vtable 0x0088BD04): each state update
 * (ship reader 0x005B21C0) sets the AI target position (+0x30) and the AI then
 * extrapolates it along forward (+0x3C) * speed (+0x54) from the moment of
 * arrival. The update is one network hop old on arrival, so the proxy always
 * trails the real ship by the one-way latency. After each position update we
 * advance the target by speed * one-way latency of the peer we got it from;
 * the script layer (bcmpfix_mp.py) measures that latency and sets it per ship. */
#include "common.h"

#define ADDR_SHIP_READER 0x005B21C0
#define ADDR_GET_AI 0x005A1720       /* thiscall(object) -> AI */
#define ADDR_AS_NETWORK_AI 0x0047DE50 /* cdecl(AI) -> NetworkAI or 0 */

typedef void(__fastcall *PFN_Reader)(void *ship, void *edx, void *msg);
typedef void *(__fastcall *PFN_GetAI)(void *obj, void *edx);
typedef void *(__cdecl *PFN_AsNetAI)(void *ai);

static BYTE *g_tramp;

#define MAXLEAD 64
static struct {
    DWORD id;
    int ms;
} g_lead[MAXLEAD];

void lead_set(DWORD objId, int ms)
{
    int i, free_ = -1;
    if (ms > g_cfg.maxLeadMs)
        ms = g_cfg.maxLeadMs;
    if (ms < 0)
        ms = 0;
    for (i = 0; i < MAXLEAD; i++) {
        if (g_lead[i].id == objId) {
            g_lead[i].ms = ms;
            return;
        }
        if (free_ < 0 && g_lead[i].id == 0)
            free_ = i;
    }
    if (free_ < 0)
        free_ = (int)(objId % MAXLEAD);
    g_lead[free_].id = objId;
    g_lead[free_].ms = ms;
}

static int lead_get(DWORD objId)
{
    int i;
    for (i = 0; i < MAXLEAD; i++)
        if (g_lead[i].id == objId)
            return g_lead[i].ms;
    return 0;
}

static void __fastcall hk_Reader(void *ship, void *edx, void *msg)
{
    BYTE *ai = NULL;
    float t0 = 0.0f;
    (void)edx;
    if (ship) {
        void *a = ((PFN_GetAI)ADDR_GET_AI)(ship, 0);
        if (a)
            ai = (BYTE *)((PFN_AsNetAI)ADDR_AS_NETWORK_AI)(a);
        if (ai)
            t0 = *(float *)(ai + 0x2C);
    }
    ((PFN_Reader)g_tramp)(ship, 0, msg);
    if (ai && *(float *)(ai + 0x2C) != t0) {
        int ms = lead_get(*(DWORD *)((BYTE *)ship + 4));
        if (ms > 0) {
            float s = *(float *)(ai + 0x54) * (ms / 1000.0f);
            float *target = (float *)(ai + 0x30), *fwd = (float *)(ai + 0x3C);
            target[0] += fwd[0] * s;
            target[1] += fwd[1] * s;
            target[2] += fwd[2] * s;
        }
    }
}

void lead_install(void)
{
    static const BYTE prologue[7] = {0x6a, 0xff, 0x68, 0xd8, 0xb9, 0x87, 0x00}; /* push -1; push 0x87b9d8 */
    BYTE jmp[5];
    LONG rel;
    if (!g_cfg.latencyComp)
        return;
    if (memcmp((void *)ADDR_SHIP_READER, prologue, 7) != 0) {
        logf("PATCH latency compensation: prologue differs, NOT applied");
        return;
    }
    g_tramp = (BYTE *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(g_tramp, prologue, 7);
    g_tramp[7] = 0xE9;
    rel = (LONG)(ADDR_SHIP_READER + 7) - (LONG)(g_tramp + 12);
    memcpy(g_tramp + 8, &rel, 4);
    jmp[0] = 0xE9;
    rel = (LONG)hk_Reader - (LONG)(ADDR_SHIP_READER + 5);
    memcpy(jmp + 1, &rel, 4);
    mem_write((void *)ADDR_SHIP_READER, jmp, 5);
    logf("PATCH latency compensation (state update reader): applied");
}
