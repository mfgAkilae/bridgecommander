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
    double used; /* a full table drops the entry set longest ago (respawns are new ships, issue #25) */
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
            g_lead[i].used = now_sec();
            return;
        }
        if (free_ < 0 && g_lead[i].id == 0)
            free_ = i;
    }
    if (free_ < 0)
        for (i = free_ = 0; i < MAXLEAD; i++)
            if (g_lead[i].used < g_lead[free_].used)
                free_ = i;
    g_lead[free_].id = objId;
    g_lead[free_].ms = ms;
    g_lead[free_].used = now_sec();
}

/* Latency of torpedo fire messages from a ship (script: RTT/2 per hop), used to advance
 * torpedoes spawned from them (torpedo.c) */
static struct {
    DWORD id;
    int ms;
    double used;
} g_fireLead[MAXLEAD];

void lead_set_fire(DWORD objId, int ms)
{
    int i, free_ = -1;
    if (ms < 0)
        ms = 0;
    if (ms > g_cfg.maxLeadMs)
        ms = g_cfg.maxLeadMs;
    for (i = 0; i < MAXLEAD; i++) {
        if (g_fireLead[i].id == objId) {
            g_fireLead[i].ms = ms;
            g_fireLead[i].used = now_sec();
            return;
        }
        if (free_ < 0 && g_fireLead[i].id == 0)
            free_ = i;
    }
    if (free_ < 0)
        for (i = free_ = 0; i < MAXLEAD; i++)
            if (g_fireLead[i].used < g_fireLead[free_].used)
                free_ = i;
    g_fireLead[free_].id = objId;
    g_fireLead[free_].ms = ms;
    g_fireLead[free_].used = now_sec();
}

int lead_get_fire(DWORD objId)
{
    int i;
    for (i = 0; i < MAXLEAD; i++)
        if (g_fireLead[i].id == objId)
            return g_fireLead[i].ms;
    return 0;
}

static int lead_get(DWORD objId)
{
    int i;
    for (i = 0; i < MAXLEAD; i++)
        if (g_lead[i].id == objId)
            return g_lead[i].ms;
    return 0;
}

typedef float *(__fastcall *PFN_GetVec)(void *obj, void *edx, float *out);
typedef float *(__fastcall *PFN_GetPos)(void *obj, void *edx);
/* SnapNewShips: a new proxy starts where the "object created" message put it. That position
 * is as old as the message: a player who joins gets every ship at the place it was created,
 * seconds ago, and a resent or late message has the same effect. NetworkAI then pulls the
 * proxy to the received position with its correction lag, so the ship slid into place for up
 * to 1 s (measured up to 110 units, in about 4 % of the ships that appeared on a screen with
 * ten players). The reader marks a proxy's first state update (NetworkAI +0x2C, the sender's
 * time, is still 0 from the constructor 0x0047DAB0); the next correction (netfix.c) places
 * the proxy at the target. Setting the position here does not last: the correction later in
 * the frame starts from the position the scene graph still has. */
#define MAXSNAP 32
static DWORD g_snapIds[MAXSNAP];
static int g_snapNext;
static unsigned long g_snapped;

static void snap_mark(DWORD id)
{
    g_snapIds[g_snapNext] = id;
    g_snapNext = (g_snapNext + 1) % MAXSNAP;
}

/* netfix.c: 1 once for a proxy whose first state update just arrived */
int lead_take_snap(DWORD id)
{
    int i;
    for (i = 0; i < MAXSNAP; i++)
        if (g_snapIds[i] == id) {
            g_snapIds[i] = 0;
            g_snapped++;
            return 1;
        }
    return 0;
}

unsigned long lead_snapped(void)
{
    return g_snapped;
}

/* trace line for one received state update: which NetworkAI fields the reader
 * changed (1 target, 2 forward, 4 up, 8 speed, 16 sender time) and the proxy */
static void trace_update(void *ship, BYTE *ai, const BYTE *before, int lead)
{
    void **vt = *(void ***)ship;
    float tmp[3], *nose = ((PFN_GetVec)vt[0xAC / 4])(ship, 0, tmp);
    float *pos = ((PFN_GetPos)vt[0x94 / 4])(ship, 0);
    float *t = (float *)(ai + 0x30), *f = (float *)(ai + 0x3C), *u = (float *)(ai + 0x48);
    int mask = (memcmp(before, ai + 0x30, 12) ? 1 : 0) | (memcmp(before + 12, ai + 0x3C, 12) ? 2 : 0) |
               (memcmp(before + 24, ai + 0x48, 12) ? 4 : 0) | (memcmp(before + 36, ai + 0x54, 4) ? 8 : 0) |
               (memcmp(before + 40, ai + 0x2C, 4) ? 16 : 0);
    tracef("U obj=%lu st0=%.3f st=%.3f mask=%d tgt %.2f %.2f %.2f fwd %.3f %.3f %.3f up %.3f %.3f %.3f spd %.2f lead %d "
           "pos %.2f %.2f %.2f nose %.3f %.3f %.3f",
           *(DWORD *)((BYTE *)ship + 4), *(const float *)(before + 40), *(float *)(ai + 0x2C), mask, t[0], t[1], t[2], f[0], f[1], f[2], u[0], u[1],
           u[2], *(float *)(ai + 0x54), lead, pos[0], pos[1], pos[2], nose[0], nose[1], nose[2]);
}

static void __fastcall hk_Reader(void *ship, void *edx, void *msg)
{
    BYTE *ai = NULL;
    float t0 = 0.0f;
    BYTE before[44];
    float p0[3];
    int ms = 0;
    (void)edx;
    if (ship) {
        void *a = ((PFN_GetAI)ADDR_GET_AI)(ship, 0);
        if (a)
            ai = (BYTE *)((PFN_AsNetAI)ADDR_AS_NETWORK_AI)(a);
        if (ai) {
            t0 = *(float *)(ai + 0x2C);
            memcpy(before, ai + 0x30, 36); /* target, forward, up */
            memcpy(before + 36, ai + 0x54, 4);
            memcpy(before + 40, ai + 0x2C, 4);
        }
    }
    ((PFN_Reader)g_tramp)(ship, 0, msg);
    if (ai && *(float *)(ai + 0x2C) != t0) {
        memcpy(p0, ai + 0x30, 12); /* received position, before latency compensation */
        ms = lead_get(*(DWORD *)((BYTE *)ship + 4));
        if (ms > 0 && g_cfg.latencyComp) {
            float s = *(float *)(ai + 0x54) * (ms / 1000.0f);
            float *target = (float *)(ai + 0x30), *fwd = (float *)(ai + 0x3C);
            target[0] += fwd[0] * s;
            target[1] += fwd[1] * s;
            target[2] += fwd[2] * s;
        }
    }
    if (ai && t0 == 0.0f && *(float *)(ai + 0x2C) != 0.0f && g_cfg.snapNewShips)
        snap_mark(*(DWORD *)((BYTE *)ship + 4)); /* the proxy's first update */
    if (ai && *(float *)(ai + 0x2C) != t0 && g_cfg.orientationCorrection == 2)
        orient_on_update(ai, g_cfg.latencyComp ? ms : 0, p0);
    if (ai && *(float *)(ai + 0x2C) != t0 && (g_cfg.velocityExtrapolation || g_cfg.knockbackCorrection || g_cfg.velocityTracking))
        velo_on_update(ai, g_cfg.latencyComp ? ms : 0, p0);
    if (ai && trace_active())
        trace_update(ship, ai, before, ms);
}

void lead_install(void)
{
    static const BYTE prologue[7] = {0x6a, 0xff, 0x68, 0xd8, 0xb9, 0x87, 0x00}; /* push -1; push 0x87b9d8 */
    BYTE jmp[5];
    LONG rel;
    if (!g_cfg.latencyComp && g_cfg.traceSec <= 0 && g_cfg.orientationCorrection != 2 && !g_cfg.velocityExtrapolation &&
        !g_cfg.snapNewShips)
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
    if (g_cfg.snapNewShips)
        logf("PATCH new remote ships start at their first received position: applied");
}
