/* Host-authoritative torpedo hits.
 *
 * Stock behaviour: every peer flies its own copy of each torpedo and runs its own
 * hit test (Torpedo::DetectCollision 0x00579010), but only the host applies damage
 * (0x005AED90 checks IsHost). Visible hits/misses therefore differ between peers.
 *
 * With TorpedoHitAuthority=1, MP clients ignore torpedo-vs-ship collisions unless
 * the script layer (bcmpfix_mp.py) has authorised that torpedo because the host
 * reported a hit; the script then places the torpedo at the host's hit point and
 * the stock collision code produces the hit visuals locally. */
#include "common.h"

#define ADDR_DETECT_COLLISION 0x00579010
#define ADDR_IS_MULTIPLAYER 0x0097FA89
#define ADDR_IS_HOST 0x0097FA8A
#define CT_SHIP 0x8007

typedef void(__fastcall *PFN_Detect)(void *torp, void *edx, void *other);
typedef char(__fastcall *PFN_IsTypeOf)(void *self, void *edx, int type);

static BYTE *g_tramp;
static unsigned long g_suppressed, g_allowed, g_pushedOut;
/* Set by the script layer once the host has answered a ping, i.e. runs bcmpfix
 * too. Without a modded host nobody would report hits, so keep stock behaviour. */
static unsigned long g_predicted, g_advanced, g_bystander;
static int g_active;

void torpedo_set_active(int on)
{
    if (on != g_active)
        logf("torpedo hit authority %s", on ? "active (host runs bcmpfix)" : "inactive");
    g_active = on;
}

#define MAXAUTH 64
static struct {
    DWORD id;
    DWORD expires;
    int mode; /* 0 = local decision, 1 = host says shield hit, 2 = host says hull hit */
    int hasStart;
    float start[3]; /* replay start point outside the target (segment start for the hit test) */
    DWORD target;   /* replay: the ship the host reported the hit on (0 = any ship) */
} g_auth[MAXAUTH];

/* The hit test (ship vtable +0x140, 0x005AE730) checks the segment from the
 * torpedo's previous position (+0x150) to its current one. After the script moved
 * the torpedo for a replay, +0x150 still holds where the local copy was, often
 * inside the target's shield ellipsoid: then the stock code tests the hull first. */
#define TORP_PREV_POS 0x150

void torpedo_authorize_mode(DWORD torpId, int mode);

void torpedo_authorize(DWORD torpId)
{
    torpedo_authorize_mode(torpId, 0);
}

static int auth_slot(DWORD torpId);

void torpedo_authorize_start(DWORD torpId, int mode, float x, float y, float z, DWORD target)
{
    int i;
    torpedo_authorize_mode(torpId, mode);
    i = auth_slot(torpId);
    if (i >= 0) {
        g_auth[i].target = target;
        g_auth[i].hasStart = 1;
        g_auth[i].start[0] = x;
        g_auth[i].start[1] = y;
        g_auth[i].start[2] = z;
    }
}

void torpedo_authorize_mode(DWORD torpId, int mode)
{
    int i, slot = 0;
    DWORD now = GetTickCount();
    for (i = 0; i < MAXAUTH; i++) {
        if (g_auth[i].id == torpId || (int)(now - g_auth[i].expires) > 0) {
            slot = i;
            break;
        }
        if (g_auth[i].expires < g_auth[slot].expires)
            slot = i;
    }
    g_auth[slot].id = torpId;
    g_auth[slot].expires = now + 2000;
    g_auth[slot].mode = mode;
    g_auth[slot].hasStart = 0;
    g_auth[slot].target = 0;
}

/* slot of a valid authorisation, or -1 */
static int auth_slot(DWORD torpId)
{
    int i;
    DWORD now = GetTickCount();
    for (i = 0; i < MAXAUTH; i++)
        if (g_auth[i].id == torpId && (int)(now - g_auth[i].expires) < 0)
            return i;
    return -1;
}

/* returns -1 if not authorised, else the mode */
static int is_authorized(DWORD torpId)
{
    int i = auth_slot(torpId);
    return i < 0 ? -1 : g_auth[i].mode;
}

/* Shield subsystem of a ship: ship+0x2C0; per-facing charge fraction at +0xC0,
 * six facings; the hit test (0x005AE730) treats <= 0.1 as "shield down". */
#define SHIP_SHIELDS 0x2C0
#define SHIELD_FACING_PCT 0xC0

#define ADDR_HIT_LOOP_COUNT 0x005791BA /* cmp eax,2 in the hit loop (imm8) */

/* Shield ellipsoid of a ship (hit test 0x005AE730): semi-axes at ship+0x24C/0x250/0x254
 * (local, divided out before the unit-sphere test), centre offset at +0x258. When the
 * replay start lies inside the ellipsoid, the stock test checks the hull first and a
 * host-reported shield hit shows as a hull hit (the host's hit point is on the host's
 * ellipsoid, the client's ship is oriented a bit differently). Move the start out to
 * 1.3 times the largest semi-axis from the ship's position, on the same ray. */
#define SHIP_SHIELD_AXES 0x24C
typedef float *(__fastcall *PFN_ShipPos)(void *obj, void *edx);
double __cdecl sqrt(double);

static void push_outside_shield(void *ship, float *st)
{
    float *ax = (float *)((BYTE *)ship + SHIP_SHIELD_AXES), r, d[3], n;
    float *c = ((PFN_ShipPos)(*(void ***)ship)[0x94 / 4])(ship, 0);
    int i;
    r = ax[0] > ax[1] ? ax[0] : ax[1];
    if (ax[2] > r)
        r = ax[2];
    if (!(r > 0.0f && r < 1000.0f))
        return;
    for (i = 0; i < 3; i++)
        d[i] = st[i] - c[i];
    n = (float)sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (n < 1e-3f || n >= 1.3f * r)
        return;
    for (i = 0; i < 3; i++)
        st[i] = c[i] + d[i] / n * 1.3f * r;
    g_pushedOut++;
}

/* A ship that is exploding (DamageableObject::IsDying: death time at +0x14C below
 * FLT_MAX) may already have freed subsystems: its shield pointer then points into
 * reused memory. Forcing the shield/hull decision on such a ship crashed every client
 * at once (read at 0x3F8000C0, the shield pointer was 1.0f) when a replayed hit and the
 * explosion coincided. Use the shields only of a living ship and only if the pointer
 * looks like an object of the game (vtable in .rdata). */
#define SHIP_DEATH_TIME 0x14C

/* committed, writable memory for [p, p + n) - without touching it (IsBadReadPtr raises
 * first-chance exceptions, which the crash log would report) */
static int writable(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)))
        return 0;
    return (const BYTE *)p + n <= (const BYTE *)mbi.BaseAddress + mbi.RegionSize;
}

static float *shield_facings(void *ship)
{
    BYTE *sh;
    DWORD vt;
    if (*(float *)((BYTE *)ship + SHIP_DEATH_TIME) < 3.0e38f)
        return NULL;
    sh = *(BYTE **)((BYTE *)ship + SHIP_SHIELDS);
    if (!sh || !writable(sh, SHIELD_FACING_PCT + 24))
        return NULL;
    vt = *(DWORD *)sh;
    if (vt < 0x00888000 || vt >= 0x008BB000)
        return NULL;
    return (float *)(sh + SHIELD_FACING_PCT);
}

/* A facing close to the "shield down" limit (0.1) decides shield or hull by a hair: our
 * copy of the charge comes from the host's health sync with 1/255 resolution and some
 * delay (host 306/3000, client 294/3000 in a test). Do not predict such hits; the host's
 * report decides them. */
static int shields_near_threshold(void *ship)
{
    float *f = shield_facings(ship);
    int i;
    if (!f)
        return 0;
    for (i = 0; i < 6; i++)
        if (f[i] > 0.08f && f[i] < 0.125f)
            return 1;
    return 0;
}

/* Spawn points of advanced torpedoes (advance_torpedo): the first collision test of such
 * a torpedo uses the segment from its spawn point, so it does not start inside a ship's
 * shield ellipsoid at short range (a hull hit through full shields). */
#define MAXADV 64
static struct {
    DWORD id;
    float p[3];
} g_adv[MAXADV];
static int g_advNext;

static void adv_remember(DWORD id, const float *p)
{
    g_adv[g_advNext].id = id;
    memcpy(g_adv[g_advNext].p, p, 12);
    g_advNext = (g_advNext + 1) % MAXADV;
}

static int adv_take(DWORD id, float *p)
{
    int i;
    for (i = 0; i < MAXADV; i++)
        if (g_adv[i].id == id) {
            memcpy(p, g_adv[i].p, 12);
            g_adv[i].id = 0;
            return 1;
        }
    return 0;
}

/* The ship a torpedo was fired at (0 = none): the fire message's target ID, set by the spawn
 * (0x0057D110 for remote copies) */
#define TORP_TARGET 0x118

/* PredictTorpedoHits only for the ship the torpedo was fired at. A torpedo that touches another
 * ship on its way (issue #31: the defenders' torpedoes pass close to their own starbase) hits it
 * on some computers and flies past on others, because every computer flies its own copy. A copy
 * that predicted such a hit was spent, so the host's real hit further on could not be replayed.
 * Such collisions now wait for the host's report like before PredictTorpedoHits. */
#define MAXBY 64
static DWORD g_byIds[MAXBY]; /* torpedoes that touched a ship they were not fired at */
static int g_byNext;

static int by_seen(DWORD id)
{
    int i;
    for (i = 0; i < MAXBY; i++)
        if (g_byIds[i] == id)
            return 1;
    return 0;
}

static int predict_on(BYTE *torp, BYTE *ship)
{
    DWORD target = *(DWORD *)(torp + TORP_TARGET), id = *(DWORD *)(torp + 4);
    if (g_cfg.predictAnyHit)
        return 1;
    if (target && target != *(DWORD *)(ship + 4)) {
        g_bystander++;
        if (!by_seen(id)) {
            g_byIds[g_byNext] = id;
            g_byNext = (g_byNext + 1) % MAXBY;
        }
        return 0;
    }
    /* a torpedo that brushed past another ship may have hit that ship on the host: its hit on
     * its target waits for the host's report too */
    if (by_seen(id)) {
        g_bystander++;
        return 0;
    }
    return 1;
}

static void __fastcall hk_DetectCollision(void *torp, void *edx, void *other)
{
    (void)edx;
    if (g_active && other && *(BYTE *)ADDR_IS_MULTIPLAYER && !*(BYTE *)ADDR_IS_HOST) {
        void **vt = *(void ***)other;
        if (((PFN_IsTypeOf)vt[2])(other, 0, CT_SHIP)) {
            DWORD id = *(DWORD *)((BYTE *)torp + 4);
            BYTE one = 1, two = 2;
            int slot = auth_slot(id), elsewhere = 0;
            int mode;
            float *pct = NULL, saved[6];
            int i;
            /* a replay is released for the ship the host reported only: the copy may still
             * overlap another ship (issue #31: it flew through the starbase), which would
             * take the hit. Nor may that other ship take a predicted hit: the copy belongs
             * to the replay. */
            if (slot >= 0 && g_auth[slot].target && g_auth[slot].target != *(DWORD *)((BYTE *)other + 4) &&
                !g_cfg.replayAnyShip) {
                slot = -1;
                elsewhere = 1;
            }
            mode = slot < 0 ? -1 : g_auth[slot].mode;
            if (mode < 0) {
                /* PredictTorpedoHits: show our own collision at once instead of waiting one
                 * latency for the host's report; the script matches the report to it. Not on
                 * an exploding ship (see shield_facings). One effect, then the copy is spent. */
                if (g_cfg.predictHits && !elsewhere && *(float *)((BYTE *)other + SHIP_DEATH_TIME) >= 3.0e38f &&
                    !shields_near_threshold(other) && predict_on((BYTE *)torp, (BYTE *)other)) {
                    BYTE *t = (BYTE *)torp;
                    float dmgBefore = *(float *)(t + 0x144), sp[3];
                    if (adv_take(*(DWORD *)(t + 4), sp))
                        memcpy(t + TORP_PREV_POS, sp, 12);
                    mem_write((void *)ADDR_HIT_LOOP_COUNT, &one, 1);
                    ((PFN_Detect)g_tramp)(torp, 0, other);
                    mem_write((void *)ADDR_HIT_LOOP_COUNT, &two, 1);
                    if (t[0x24] || *(float *)(t + 0x144) != dmgBefore) {
                        g_predicted++;
                        torpedo_retire(torp);
                    }
                    return;
                }
                g_suppressed++;
                return;
            }
            g_allowed++;
            /* replay: the hit segment starts at the replay start point outside the target */
            if (g_auth[slot].hasStart) {
                float st[3];
                memcpy(st, g_auth[slot].start, 12);
                push_outside_shield(other, st);
                memcpy((BYTE *)torp + TORP_PREV_POS, st, 12);
            }
            /* make the local shield/hull decision follow the host's */
            if (mode > 0) {
                pct = shield_facings(other);
                if (pct) {
                    for (i = 0; i < 6; i++) {
                        saved[i] = pct[i];
                        if (mode == 1 && pct[i] <= 0.1f)
                            pct[i] = 0.5f;
                        else if (mode == 2)
                            pct[i] = 0.0f;
                    }
                }
            }
            /* Clients consume no damage, so the stock loop would apply the same
             * hit twice (two effects): run it once for host-confirmed hits. */
            mem_write((void *)ADDR_HIT_LOOP_COUNT, &one, 1);
            ((PFN_Detect)g_tramp)(torp, 0, other);
            mem_write((void *)ADDR_HIT_LOOP_COUNT, &two, 1);
            if (pct)
                for (i = 0; i < 6; i++)
                    pct[i] = saved[i];
            return;
        }
    }
    ((PFN_Detect)g_tramp)(torp, 0, other);
}

/* Run the stock torpedo-vs-object collision for a host-confirmed hit right now.
 * Returns 1 if the torpedo is spent afterwards. */
typedef void(__fastcall *PFN_SetHidden)(void *node, void *edx, int hide);

int torpedo_force(void *torp, void *target)
{
    BYTE *t = (BYTE *)torp;
    BYTE one = 1, two = 2;
    float dmgBefore;
    int hit;
    if (!g_tramp || !torp || !target)
        return 0;
    if (t[0x24]) {
        if (g_cfg.verbose)
            logf("forcehit torp %08lx already spent", *(DWORD *)(t + 4));
        return 0;
    }
    dmgBefore = *(float *)(t + 0x144);
    torpedo_authorize(*(DWORD *)(t + 4));
    /* On clients no damage is consumed, so the stock loop would apply the same
     * hit twice (two effects). Run it once for the replayed hit. */
    mem_write((void *)ADDR_HIT_LOOP_COUNT, &one, 1);
    ((PFN_Detect)g_tramp)(torp, 0, target);
    mem_write((void *)ADDR_HIT_LOOP_COUNT, &two, 1);
    hit = t[0x24] || *(float *)(t + 0x144) != dmgBefore;
    if (g_cfg.verbose)
        logf("forcehit torp %08lx target %08lx dmg %.1f -> %.1f spent=%d", *(DWORD *)(t + 4),
             *(DWORD *)((BYTE *)target + 4), dmgBefore, *(float *)(t + 0x144), t[0x24]);
    return hit;
}

/* Last resort for a replay whose approaches produced no hit: the script has put the
 * torpedo just inside the host's impact point (SetTranslate + UpdateNodeOnly); run the
 * collision now, through the hook, so the authorised start point outside the ship and
 * the host's shield/hull decision apply. Needs a replay authorisation (allowhit).
 * Returns 1 if the torpedo hit. */
int torpedo_force_replay(void *torp, void *target)
{
    BYTE *t = (BYTE *)torp;
    float dmgBefore, *p, *c, *ax;
    int mode, hit;
    if (!g_tramp || !torp || !target)
        return 0;
    mode = is_authorized(*(DWORD *)(t + 4));
    if (t[0x24] || mode < 0) {
        logf("forced replay torp %lu: not possible (spent %d, mode %d)", *(DWORD *)(t + 4), t[0x24], mode);
        return 0;
    }
    dmgBefore = *(float *)(t + 0x144);
    hk_DetectCollision(torp, 0, target);
    hit = t[0x24] || *(float *)(t + 0x144) != dmgBefore;
    p = ((PFN_ShipPos)(*(void ***)torp)[0x94 / 4])(torp, 0);
    c = ((PFN_ShipPos)(*(void ***)target)[0x94 / 4])(target, 0);
    ax = (float *)((BYTE *)target + SHIP_SHIELD_AXES);
    if (!hit || g_cfg.verbose || g_cfg.testMode)
        logf("forced replay torp %lu mode %d: %s; prev %.1f %.1f %.1f pos %.1f %.1f %.1f ship %.1f %.1f %.1f axes %.1f %.1f %.1f "
         "dmg %.1f -> %.1f", *(DWORD *)(t + 4), mode, hit ? "hit" : "NO hit", ((float *)(t + TORP_PREV_POS))[0],
         ((float *)(t + TORP_PREV_POS))[1], ((float *)(t + TORP_PREV_POS))[2], p[0], p[1], p[2], c[0], c[1], c[2], ax[0],
         ax[1], ax[2], dmgBefore, *(float *)(t + 0x144));
    return hit;
}

/* Retire a torpedo like the stock code does when its damage is used up. */
void torpedo_retire(void *torp)
{
    BYTE *t = (BYTE *)torp;
    void *node;
    if (!t || t[0x24])
        return;
    node = *(void **)(t + 0x18);
    t[0x24] = 1;
    if (node)
        ((PFN_SetHidden)(*(void ***)node)[0x50 / 4])(node, 0, 1);
}

unsigned long torpedo_pushed_out(void)
{
    return g_pushedOut;
}

unsigned long torpedo_predicted(void)
{
    return g_predicted;
}

unsigned long torpedo_bystander(void)
{
    return g_bystander;
}

unsigned long torpedo_advanced(void)
{
    return g_advanced;
}

void torpedo_stats(unsigned long *suppressed, unsigned long *allowed)
{
    *suppressed = g_suppressed;
    *allowed = g_allowed;
}

void torpedo_install(void)
{
    static const BYTE prologue[5] = {0x83, 0xec, 0x28, 0x53, 0x55}; /* sub esp,28h; push ebx; push ebp */
    BYTE jmp[5];
    LONG rel;
    if (!g_cfg.torpedoAuthority)
        return;
    if (memcmp((void *)ADDR_DETECT_COLLISION, prologue, 5) != 0) {
        logf("PATCH torpedo authority: prologue differs, NOT applied");
        return;
    }
    g_tramp = (BYTE *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(g_tramp, prologue, 5);
    g_tramp[5] = 0xE9;
    rel = (LONG)(ADDR_DETECT_COLLISION + 5) - (LONG)(g_tramp + 10);
    memcpy(g_tramp + 6, &rel, 4);
    jmp[0] = 0xE9;
    rel = (LONG)hk_DetectCollision - (LONG)(ADDR_DETECT_COLLISION + 5);
    memcpy(jmp + 1, &rel, 4);
    mem_write((void *)ADDR_DETECT_COLLISION, jmp, 5);
    logf("PATCH torpedo authority (client torpedo/ship hits only on host confirmation): applied");
}

/* Diagnostics (TraceStateSec): torpedoes spawned from a remote fire message. The
 * receiver (0x0069F930) calls the spawn 0x0057D110 (thiscall, 7 arguments: skew,
 * sound, no target, tube index, target id, target offset, shooter's direction of
 * flight). Logs the flags and the angle between the new torpedo's velocity and the
 * shooter's direction of flight: a skewed launch leaves along the tube's right vector. */
#define ADDR_CALL_RECV_SPAWN 0x0069FB5C
#define ADDR_RECV_SPAWN 0x0057D110
#define ADDR_GET_VELOCITY 0x005A05A0

typedef BYTE *(__fastcall *PFN_RecvSpawn)(void *self, void *edx, char skew, char sound, char noTarget, int tube,
                                         int target, int *offset, float *dir);
typedef float *(__fastcall *PFN_GetVel)(void *obj, void *edx);
static void *o_RecvSpawn;
double __cdecl sqrt(double);
double __cdecl acos(double);

/* TorpedoLatencyCompensation: a torpedo spawned from a remote fire message starts one
 * message latency after the shooter's (and the shooter's ship is shown extrapolated to
 * now), so every copy - the host's, which decides the hit, too - trails the shooter's by
 * that latency and hits later. Move it ahead along its velocity by the latency of fire
 * messages from that ship (script: RTT/2 per hop). The hit-test segment still starts at
 * the spawn point (+0x150), so nothing on the skipped stretch is missed. */
typedef void(__fastcall *PFN_SetTranslatePtr)(void *obj, void *edx, float *pos);
typedef void(__fastcall *PFN_ObjVoid)(void *obj, void *edx);

static void advance_torpedo(BYTE *t, void *tube)
{
    BYTE *ship = tube ? *(BYTE **)((BYTE *)tube + 0x40) : NULL;
    float *v, *p, np[3], spawn[3], h;
    int ms;
    /* not on the host by default: its copy decides shield or hull, and an advanced copy
     * decided differently from the shooter's in 2-6 % of the hits (2 = also on the host) */
    if (!ship || (*(BYTE *)ADDR_IS_HOST && g_cfg.torpedoLead < 2))
        return;
    ms = lead_get_fire(*(DWORD *)(ship + 4));
    if (ms <= 0)
        return;
    h = ms / 1000.0f;
    v = ((PFN_GetVel)ADDR_GET_VELOCITY)(t, 0);
    p = ((PFN_ShipPos)(*(void ***)t)[0x94 / 4])(t, 0);
    memcpy(spawn, p, 12);
    np[0] = p[0] + v[0] * h;
    np[1] = p[1] + v[1] * h;
    np[2] = p[2] + v[2] * h;
    ((PFN_SetTranslatePtr)(*(void ***)t)[0x78 / 4])(t, 0, np);
    ((PFN_ObjVoid)(*(void ***)t)[0x58 / 4])(t, 0); /* UpdateNodeOnly */
    /* the hit-test segment must start at the spawn point: a segment that starts inside a
     * ship's shield ellipsoid is tested against the hull first (a hull hit through full
     * shields) */
    memcpy(t + TORP_PREV_POS, spawn, 12);
    adv_remember(*(DWORD *)(t + 4), spawn);
    g_advanced++;
    if (trace_active())
        tracef("T advance torp=%lu by %d ms (%.1f units)", *(DWORD *)(t + 4), ms,
               (float)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) * h);
}

static BYTE *__fastcall hk_RecvSpawn(void *self, void *edx, char skew, char sound, char noTarget, int tube, int target,
                                     int *offset, float *dir)
{
    BYTE *t = ((PFN_RecvSpawn)o_RecvSpawn)(self, edx, skew, sound, noTarget, tube, target, offset, dir);
    (void)edx;
    if (t && g_cfg.torpedoLead)
        advance_torpedo(t, self);
    if (t && dir && trace_active()) {
        float *v = ((PFN_GetVel)ADDR_GET_VELOCITY)(t, 0);
        float nv = (float)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        float nd = (float)sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        float c = (nv > 1e-4f && nd > 1e-4f) ? (v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2]) / (nv * nd) : 1.0f;
        if (c > 1.0f)
            c = 1.0f;
        if (c < -1.0f)
            c = -1.0f;
        tracef("T recv torp=%lu skew=%d sound=%d notgt=%d tube=%d ang=%.1f", *(DWORD *)(t + 4), skew, sound, noTarget,
               tube, nd > 1e-4f ? (float)(acos(c) * 57.29578) : -1.0f);
    }
    return t;
}

/* Pulse weapons (disruptors) are spawned from their own fire message (opcode 0x1A,
 * 0x0069FBB0 -> PulseWeapon fire 0x005762B0, thiscall, 4 arguments, returns the
 * projectile): advance those too. */
#define ADDR_CALL_PULSE_SPAWN 0x0069FD4E
#define ADDR_PULSE_SPAWN 0x005762B0
typedef BYTE *(__fastcall *PFN_PulseSpawn)(void *self, void *edx, int a1, int a2, int a3, float *dir);
static void *o_PulseSpawn;

static BYTE *__fastcall hk_PulseSpawn(void *self, void *edx, int a1, int a2, int a3, float *dir)
{
    BYTE *t = ((PFN_PulseSpawn)o_PulseSpawn)(self, edx, a1, a2, a3, dir);
    if (t && g_cfg.torpedoLead)
        advance_torpedo(t, self);
    return t;
}

void torpedo_trace_install(void)
{
    if (g_cfg.traceSec > 0 || g_cfg.torpedoLead)
        redirect_call(ADDR_CALL_RECV_SPAWN, (void *)hk_RecvSpawn, &o_RecvSpawn, ADDR_RECV_SPAWN,
                      g_cfg.torpedoLead ? "remote torpedo spawns: latency compensation" : "trace remote torpedo spawns");
    if (g_cfg.torpedoLead)
        redirect_call(ADDR_CALL_PULSE_SPAWN, (void *)hk_PulseSpawn, &o_PulseSpawn, ADDR_PULSE_SPAWN,
                      "remote pulse spawns: latency compensation");
}
