/* Phaser bank replication (PhaserSync, issue #22).
 *
 * The network carries only the weapon system's start/stop firing commands (opcodes 7/8/9).
 * Every machine then decides per bank and frame whether it fires (PhaserBank::Fire
 * 0x00570FE0: CanFire 0x00571E60 = charge, then range and arc 0x005714A0 against its own,
 * latency-shifted view of both ships). Near the edge of an arc, or after the charges have
 * drifted apart, the decisions differ: in six-player tests 17 % of a shooter's bank
 * firings did not happen on a given other machine, and other machines fired banks the
 * shooter did not fire. The host applies the damage, so the shooter's hit did no damage.
 *
 * Here the owner of a ship reports which of its phaser banks fire (bitmask, sent by the
 * script as soon as it changes), and every other machine lets a bank of that ship fire
 * only while its owner fires it; while it does, a low local charge does not stop it.
 * PhaserSync=1 also starts such banks itself, 2 only blocks and stops.
 *
 * Experimental, off by default: in six-player tests the banks matched (bank firings only
 * on the shooter 16.5 % -> 8.5-12 %, banks fired only on other machines 170 -> 0, beams
 * other machines showed and the shooter did not 71 -> 1-4), but the hits did not: the
 * other machines' beams still start about 280 ms late and end about 100 ms earlier, and a
 * beam that touches the target for less than about 300 ms shows no hit. Beams the shooter
 * hit and another machine did not show went from 15.9 % to 17.7-20.2 %. */
#include "common.h"

#define ADDR_CUR_GAME 0x0097E238 /* Game_GetCurrentGame */
#define GAME_PLAYER 0x54         /* the local player's ship */
#define SHIP_PHASERS 0x2B8       /* ShipClass::GetPhaserSystem */
#define SUB_NUM_CHILDREN 0x1C
#define SUB_CHILDREN 0x20
#define BANK_SHIP 0x40
#define BANK_PROPERTY 0x18 /* EnergyWeapon property; minimum firing charge at +0x74 */
#define BANK_FIRING 0x88   /* Weapon::IsFiring */
#define BANK_TARGET 0x8C   /* target object ID, then the offset on it (+0x90) */
#define BANK_CHARGE 0xA0   /* EnergyWeapon charge level */
#define ADDR_FIND_OBJECT 0x00430730 /* cdecl(0, id) */
#define TYPE_SHIP 0x8008
#define PHASERBANK_VTABLE 0x00893194
#define VT_STOP 0x78
#define VT_FIRE 0x7C
#define ADDR_FIRE 0x00570FE0
#define ADDR_PYRUN_SIMPLESTRING 0x0074AE80

typedef char(__fastcall *PFN_Fire)(void *bank, void *edx, int a1, int a2);
typedef void(__fastcall *PFN_Stop)(void *bank, void *edx);
typedef int(__cdecl *PFN_PyRunSimpleString)(const char *);

#define MAXREMOTE 32
static struct {
    DWORD ship;
    DWORD bits;
    DWORD target;
    double retry[32]; /* next start attempt per bank (a failed Fire is retried every 100 ms) */
} g_remote[MAXREMOTE];

static DWORD g_ownShip, g_ownBits, g_ownTarget, g_sentShip, g_sentBits;
static int g_pending;
static unsigned long g_gated, g_forcedCharge, g_sent;

static BYTE *local_ship(void);
typedef BYTE *(__cdecl *PFN_FindObject)(int kind, DWORD id);
typedef char(__fastcall *PFN_IsA)(void *obj, void *edx, int type);
static unsigned long g_started, g_stopped;

static void ensure_charge(BYTE *b)
{
    BYTE *prop = *(BYTE **)(b + BANK_PROPERTY);
    float minCharge = prop ? *(float *)(prop + 0x74) : 0.0f;
    if (*(float *)(b + BANK_CHARGE) <= minCharge) {
        *(float *)(b + BANK_CHARGE) = minCharge + 1.0f;
        g_forcedCharge++;
    }
}

/* every frame: the phaser banks of remote ships follow their owner's state. PhaserBank::Fire
 * runs only when a bank starts, so starting and stopping is done here. */
static void enforce_remote(void)
{
    double t = now_sec();
    int s, i, n;
    for (s = 0; s < MAXREMOTE; s++) {
        BYTE *ship, *sys, **arr;
        if (!g_remote[s].ship)
            continue;
        ship = ((PFN_FindObject)ADDR_FIND_OBJECT)(0, g_remote[s].ship);
        if (!ship || !((PFN_IsA)(*(void ***)ship)[2])(ship, 0, TYPE_SHIP) || ship == local_ship())
            continue;
        sys = *(BYTE **)(ship + SHIP_PHASERS);
        if (!sys)
            continue;
        n = *(int *)(sys + SUB_NUM_CHILDREN);
        arr = *(BYTE ***)(sys + SUB_CHILDREN);
        for (i = 0; i < n && i < 32 && arr; i++) {
            BYTE *b = arr[i];
            int want = (g_remote[s].bits >> i) & 1;
            if (!b)
                continue;
            if (!want && b[BANK_FIRING]) {
                ((PFN_Stop)(*(void ***)b)[VT_STOP / 4])(b, 0);
                g_stopped++;
            } else if (want && !b[BANK_FIRING] && g_cfg.phaserSync == 1 && g_remote[s].target && t >= g_remote[s].retry[i]) {
                g_remote[s].retry[i] = t + 0.1;
                *(DWORD *)(b + BANK_TARGET) = g_remote[s].target;
                memset(b + BANK_TARGET + 4, 0, 12); /* offset on the target: its centre */
                ensure_charge(b);
                if (((PFN_Fire)ADDR_FIRE)(b, 0, 0, 1))
                    g_started++;
            }
        }
    }
}

static BYTE *local_ship(void)
{
    BYTE *game = *(BYTE **)ADDR_CUR_GAME;
    return game ? *(BYTE **)(game + GAME_PLAYER) : NULL;
}

static int bank_index(BYTE *ship, BYTE *bank)
{
    BYTE *sys = *(BYTE **)(ship + SHIP_PHASERS);
    BYTE **arr;
    int n, i;
    if (!sys)
        return -1;
    n = *(int *)(sys + SUB_NUM_CHILDREN);
    arr = *(BYTE ***)(sys + SUB_CHILDREN);
    for (i = 0; i < n && i < 32 && arr; i++)
        if (arr[i] == bank)
            return i;
    return -1;
}

static int remote_slot(DWORD ship, int create)
{
    int i, free_ = -1;
    for (i = 0; i < MAXREMOTE; i++) {
        if (g_remote[i].ship == ship)
            return i;
        if (free_ < 0 && !g_remote[i].ship)
            free_ = i;
    }
    if (!create)
        return -1;
    if (free_ < 0)
        free_ = (int)(ship % MAXREMOTE);
    memset(&g_remote[free_], 0, sizeof(g_remote[0]));
    g_remote[free_].ship = ship;
    return free_;
}

/* script: the owner of `ship` fires the banks in `bits` at `target` */
void phaser_set_remote(DWORD ship, DWORD bits, DWORD target)
{
    BYTE *own = local_ship();
    int i;
    if (own && *(DWORD *)(own + 4) == ship)
        return;
    i = remote_slot(ship, 1);
    g_remote[i].bits = bits;
    g_remote[i].target = target;
}

/* script: the next state of our own ship to send; 0 if nothing changed */
int phaser_take(DWORD *ship, DWORD *bits, DWORD *target)
{
    if (!g_pending)
        return 0;
    g_pending = 0;
    *ship = g_ownShip;
    *bits = g_ownBits;
    *target = g_ownTarget;
    g_sentShip = g_ownShip;
    g_sentBits = g_ownBits;
    g_sent++;
    return 1;
}

/* every frame: which banks of our own ship fire? Tell the script at once on a change. */
void phaser_frame(int pythonReady)
{
    BYTE *ship = local_ship(), *sys, **arr;
    DWORD bits = 0, id, target = 0;
    int n, i;
    if (!g_cfg.phaserSync)
        return;
    enforce_remote();
    if (!ship)
        return;
    sys = *(BYTE **)(ship + SHIP_PHASERS);
    if (!sys)
        return;
    n = *(int *)(sys + SUB_NUM_CHILDREN);
    arr = *(BYTE ***)(sys + SUB_CHILDREN);
    for (i = 0; i < n && i < 32 && arr; i++)
        if (arr[i] && arr[i][BANK_FIRING]) {
            bits |= 1u << i;
            if (!target)
                target = *(DWORD *)(arr[i] + BANK_TARGET);
        }
    id = *(DWORD *)(ship + 4);
    if (id == g_ownShip && bits == g_ownBits)
        return;
    g_ownShip = id;
    g_ownBits = bits;
    g_ownTarget = target;
    if (id == g_sentShip && bits == g_sentBits)
        return;
    g_pending = 1;
    if (pythonReady)
        ((PFN_PyRunSimpleString)ADDR_PYRUN_SIMPLESTRING)("import bcmpfix_mp\nbcmpfix_mp.SendBanks()\n");
}

static char __fastcall hk_Fire(void *bank, void *edx, int a1, int a2)
{
    BYTE *b = (BYTE *)bank, *ship = *(BYTE **)(b + BANK_SHIP);
    (void)edx;
    if (ship && ship != local_ship()) {
        int slot = remote_slot(*(DWORD *)(ship + 4), 0), idx;
        if (slot >= 0 && (idx = bank_index(ship, b)) >= 0) {
            if (!(g_remote[slot].bits & (1u << idx))) {
                /* the owner does not fire this bank */
                g_gated++;
                if (b[BANK_FIRING])
                    ((PFN_Stop)(*(void ***)bank)[VT_STOP / 4])(bank, 0);
                return 0;
            }
            /* the owner fires it: our copy's charge must not stop it */
            ensure_charge(b);
        }
    }
    return ((PFN_Fire)ADDR_FIRE)(bank, 0, a1, a2);
}

void phaser_stats(unsigned long *gated, unsigned long *charged, unsigned long *sent, unsigned long *started,
                  unsigned long *stopped)
{
    *gated = g_gated;
    *charged = g_forcedCharge;
    *sent = g_sent;
    *started = g_started;
    *stopped = g_stopped;
}

void phaser_install(void)
{
    void *slot = (void *)(PHASERBANK_VTABLE + VT_FIRE), *hook = (void *)hk_Fire;
    if (!g_cfg.phaserSync || !g_cfg.mpScript)
        return;
    if (*(DWORD *)slot != ADDR_FIRE) {
        logf("PATCH phaser sync: PhaserBank vtable differs, NOT applied");
        return;
    }
    mem_write(slot, &hook, 4);
    logf("PATCH phaser sync (remote phaser banks fire only when their owner fires them): applied");
}
