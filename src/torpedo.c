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
static unsigned long g_suppressed, g_allowed;
/* Set by the script layer once the host has answered a ping, i.e. runs bcmpfix
 * too. Without a modded host nobody would report hits, so keep stock behaviour. */
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
} g_auth[MAXAUTH];

void torpedo_authorize_mode(DWORD torpId, int mode);

void torpedo_authorize(DWORD torpId)
{
    torpedo_authorize_mode(torpId, 0);
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
}

/* returns -1 if not authorised, else the mode */
static int is_authorized(DWORD torpId)
{
    int i;
    DWORD now = GetTickCount();
    for (i = 0; i < MAXAUTH; i++)
        if (g_auth[i].id == torpId && (int)(now - g_auth[i].expires) < 0)
            return g_auth[i].mode;
    return -1;
}

/* Shield subsystem of a ship: ship+0x2C0; per-facing charge fraction at +0xC0,
 * six facings; the hit test (0x005AE730) treats <= 0.1 as "shield down". */
#define SHIP_SHIELDS 0x2C0
#define SHIELD_FACING_PCT 0xC0

#define ADDR_HIT_LOOP_COUNT 0x005791BA /* cmp eax,2 in the hit loop (imm8) */

static void __fastcall hk_DetectCollision(void *torp, void *edx, void *other)
{
    (void)edx;
    if (g_active && other && *(BYTE *)ADDR_IS_MULTIPLAYER && !*(BYTE *)ADDR_IS_HOST) {
        void **vt = *(void ***)other;
        if (((PFN_IsTypeOf)vt[2])(other, 0, CT_SHIP)) {
            DWORD id = *(DWORD *)((BYTE *)torp + 4);
            BYTE one = 1, two = 2;
            int mode = is_authorized(id);
            float *pct = NULL, saved[6];
            int i;
            if (mode < 0) {
                g_suppressed++;
                return;
            }
            g_allowed++;
            /* make the local shield/hull decision follow the host's */
            if (mode > 0) {
                BYTE *sh = *(BYTE **)((BYTE *)other + SHIP_SHIELDS);
                if (sh) {
                    pct = (float *)(sh + SHIELD_FACING_PCT);
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
