/* Phaser bank replication and hit authority (PhaserHitAuthority, issue #22).
 *
 * The network carries only the weapon system's start/stop firing commands (opcodes 7/8/9).
 * Every machine then decides per bank and frame whether it fires (PhaserBank::Fire
 * 0x00570FE0: CanFire 0x00571E60 = charge, then the target list 0x005859D0 and the arc
 * 0x005714A0 against its own, latency-shifted view of both ships). Near the edge of an arc, or after the charges have
 * drifted apart, the decisions differ: in six-player tests 22 % of a shooter's bank
 * firings did not happen on a given other machine, and other machines fired banks the
 * shooter did not fire. The host applies the damage, so the shooter's hit did no damage.
 *
 * Here the owner of a ship (its player; for an AI ship the host, which simulates it) reports
 * which of its phaser banks fire, which of their beams' rays are on an object (+0xFC) and
 * which of these hit (beam state 3), sent by the script as soon as that changes (and straight
 * to every modded machine, direct.c). Every other machine lets a bank of that ship fire only
 * while its owner fires it; while it does, a low local charge does not stop it.
 *
 * Mode 3 (PhaserHitAuthority=1, default) also makes the beam follow the owner's: the bank
 * fires without the local arc test, the target list check (the list is rebuilt for each
 * ship inside its weapon update, so a start from here saw another ship's list and the bank
 * started only with the weapon system's retry every 0.33 s: 76-155 ms after the report;
 * issue #23) and the start wait, only the owner's stop ends it, the
 * beam reaches its hit state when the owner's does (not by the local 0.25 s growth timer),
 * a bank the weapon system does not feed is driven with the frame time (the host
 * applies its damage with it), and the hit that StopFiring applies to the ray's object
 * is applied only if the owner's ray was on an object at its stop, and the bank aims at the
 * owner's reported target and the point on it from the start-firing command (issue #30).
 * Six players, 60 ms: bank firings only on the shooter 19-23 % -> 7-8 %; beams the shooter
 * hit and another machine did not show 13-16 % -> 2.5-4 %; beams other machines showed and
 * the shooter did not 44-70 -> 3-10; client shooters' beams without damage on the host
 * 7-12 % -> 0-1.3 %.
 *
 * Without it, PhaserSync=1 (experimental, v1.5.0) starts such banks with the stock checks,
 * 2 only blocks and stops them; the banks then match but the hits do not. */
#include "common.h"

double __cdecl sqrt(double); /* not <math.h>: its logf clashes with ours */

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
#define BANK_BEAM_STATE 0xF4 /* 1 wait, 2 growing, 3 on the target (hits) */
#define BANK_BEAM_OBJECT 0xFC /* ID of the object the beam's ray touches (states 2 and 3), else 0 */
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
#define REPORT_TIMEOUT_S 20.0 /* beams last up to about 9 s (AI ships): longer silence means the owner left */
static struct {
    DWORD ship;
    DWORD bits;
    DWORD target;
    DWORD hits;            /* banks whose beam hits (state 3, ray on an object) on the owner's machine */
    DWORD on;              /* banks whose beam's ray is on an object (states 2 and 3) there */
    DWORD stopOn;          /* banks whose beam was on an object when the owner stopped it */
    DWORD seq;             /* sequence number of the last report applied (direct or relayed) */
    int hasSeq;
    double used;           /* last report or frame with the ship alive: a full table drops the oldest */
    double reported;       /* last report: an owner that went silent (lost connection) no longer fires */
    DWORD firing;          /* test telemetry: banks firing at the end of the last frame */
    double retry[32];      /* next start attempt per bank (a failed Fire is retried every 100 ms) */
    unsigned long fed[32]; /* frame of the last Fire call (the weapon system feeds firing banks) */
} g_remote[MAXREMOTE];

/* PhaserSync=3 (hit authority): a bank of a remote ship that its owner fires is driven to
 * its hit state as on the owner's machine. Context for the hooks below while Fire runs. */
static BYTE *g_curBank;
static float g_curGrowth;
static int g_curSlot, g_curIdx;
static int g_allowStop;
static unsigned long g_frame;
static double g_lastFrameT;
static float g_dt = 0.016f;
static unsigned long g_forcedArc, g_skippedWait, g_driven, g_skippedStopHit;

/* the ships this machine simulates: its own and, on the host, the AI ships (the script names
 * them, phaser_set_owned); each reports its banks with its own sequence number */
#define MAXOWN 17
static struct {
    DWORD ship, bits, target, hits, on, seq;
    DWORD sentBits, sentHits, sentOn, sentTarget, other;
    int sentValid, pending;
} g_own[MAXOWN];
static DWORD g_aiShips[MAXOWN - 1];
static int g_nAiShips;
static unsigned long g_staleReports;
static unsigned long g_gated, g_forcedCharge, g_sent;

static BYTE *local_ship(void);
static char __fastcall hk_Fire(void *bank, void *edx, int a1, int a2);
/* test mode: why a start of an owner-fired bank failed (results of Fire's checks) */
static int g_diagTgt, g_diagRange;
static double g_diagLast[MAXREMOTE][32];
static DWORD g_diagWrongTgt[MAXREMOTE][32]; /* test telemetry: the last logged own target of a bank */
static unsigned long g_retargeted;
static double g_diagBeam[MAXREMOTE][32]; /* test telemetry: last PB line per remote bank */
typedef BYTE *(__cdecl *PFN_FindObject)(int kind, DWORD id);
typedef char(__fastcall *PFN_IsA)(void *obj, void *edx, int type);
static unsigned long g_started, g_stopped;

typedef float *(__fastcall *PFN_GetPosV)(void *obj, void *edx);

/* test telemetry: a remote bank's beam once a second (state, the object its ray touches, its
 * target and the distance to it, the beam length) */
static void diag_beam(int s, int i, BYTE *ship, BYTE *b)
{
    BYTE *tgt;
    float *p0, *p1, dx = 0, dy = 0, dz = 0;
    if (now_sec() - g_diagBeam[s][i] < 1.0)
        return;
    g_diagBeam[s][i] = now_sec();
    tgt = ((PFN_FindObject)ADDR_FIND_OBJECT)(0, *(DWORD *)(b + BANK_TARGET));
    if (tgt) {
        p0 = ((PFN_GetPosV)(*(void ***)ship)[0x94 / 4])(ship, 0);
        dx = p0[0];
        dy = p0[1];
        dz = p0[2];
        p1 = ((PFN_GetPosV)(*(void ***)tgt)[0x94 / 4])(tgt, 0);
        dx -= p1[0];
        dy -= p1[1];
        dz -= p1[2];
    }
    tlog_printf("PB %ld %lu %d st=%d obj=%lu tgt=%lu rep=%lu d=%.1f len=%.1f off=%.2f,%.2f,%.2f fed=%d", tlog_ms(),
                *(DWORD *)(ship + 4), i, *(int *)(b + BANK_BEAM_STATE), *(DWORD *)(b + BANK_BEAM_OBJECT),
                *(DWORD *)(b + BANK_TARGET), g_remote[s].target, tgt ? (float)sqrt(dx * dx + dy * dy + dz * dz) : -1.0f,
                *(float *)(b + 0xF0), *(float *)(b + BANK_TARGET + 4), *(float *)(b + BANK_TARGET + 8),
                *(float *)(b + BANK_TARGET + 12), g_remote[s].fed[i] + 1 >= g_frame);
}

/* aim a remote bank at the owner's target. The weapon system keeps a list of the targets its
 * start-firing commands named (+0xC4: nodes {entry, next}, entry {target ID, offset on it}); the
 * offset is the point the owner aims at (an AI picks a subsystem). Without an entry: the centre. */
#define BANK_SYSTEM 0x24
#define SYS_TARGETS 0xC4
static void aim_at(BYTE *b, DWORD target)
{
    BYTE *sys = *(BYTE **)(b + BANK_SYSTEM);
    DWORD **node = sys ? *(DWORD ***)(sys + SYS_TARGETS) : NULL;
    int guard = 0;
    *(DWORD *)(b + BANK_TARGET) = target;
    memset(b + BANK_TARGET + 4, 0, 12);
    while (node && guard++ < 64) {
        DWORD *e = node[0];
        if (e && e[0] == target) {
            memcpy(b + BANK_TARGET + 4, e + 1, 12);
            return;
        }
        node = (DWORD **)node[1];
    }
}

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
    g_frame++;
    if (g_lastFrameT > 0.0) {
        g_dt = (float)(t - g_lastFrameT);
        if (g_dt > 0.1f)
            g_dt = 0.1f;
    }
    g_lastFrameT = t;
    for (s = 0; s < MAXREMOTE; s++) {
        BYTE *ship, *sys, **arr;
        if (!g_remote[s].ship)
            continue;
        ship = ((PFN_FindObject)ADDR_FIND_OBJECT)(0, g_remote[s].ship);
        if (!ship || !((PFN_IsA)(*(void ***)ship)[2])(ship, 0, TYPE_SHIP) || ship == local_ship())
            continue;
        g_remote[s].used = t;
        sys = *(BYTE **)(ship + SHIP_PHASERS);
        if (!sys)
            continue;
        n = *(int *)(sys + SUB_NUM_CHILDREN);
        arr = *(BYTE ***)(sys + SUB_CHILDREN);
        if (g_remote[s].bits && t - g_remote[s].reported > REPORT_TIMEOUT_S)
            g_remote[s].bits = 0; /* no report for long: the owner is gone or lost its connection */
        for (i = 0; i < n && i < 32 && arr; i++) {
            BYTE *b = arr[i];
            int want = (g_remote[s].bits >> i) & 1;
            if (!b)
                continue;
            if (g_cfg.testMode && (g_remote[s].firing & (1u << i)) && !b[BANK_FIRING])
                tlog_printf("PE %ld %lu %d 0", tlog_ms(), g_remote[s].ship, i); /* stopped during the last frame */
            if (!want && b[BANK_FIRING]) {
                g_allowStop = 1;
                ((PFN_Stop)(*(void ***)b)[VT_STOP / 4])(b, 0);
                g_allowStop = 0;
                g_stopped++;
                if (g_cfg.testMode && !b[BANK_FIRING])
                    tlog_printf("PE %ld %lu %d 1", tlog_ms(), g_remote[s].ship, i); /* stopped here, at once */
            } else if (want && !b[BANK_FIRING] && g_cfg.phaserSync != 2 && g_remote[s].target &&
                       (g_cfg.phaserSync >= 3 || t >= g_remote[s].retry[i])) {
                float dt = g_dt;
                g_remote[s].retry[i] = t + 0.1;
                if (*(DWORD *)(b + BANK_TARGET) != g_remote[s].target)
                    aim_at(b, g_remote[s].target);
                if (hk_Fire(b, 0, *(int *)&dt, 1))
                    g_started++;
            } else if (want && b[BANK_FIRING] && g_cfg.phaserSync >= 3 && g_remote[s].fed[i] + 1 < g_frame) {
                /* the weapon system does not feed it (its start/stop command differs): keep
                 * the beam going, with the frame time (the host applies damage with it) */
                float dt = g_dt;
                hk_Fire(b, 0, *(int *)&dt, 1);
                g_driven++;
            }
            if (b[BANK_FIRING] && g_cfg.testMode)
                diag_beam(s, i, ship, b);
            if (b[BANK_FIRING])
                g_remote[s].firing |= 1u << i;
            else
                g_remote[s].firing &= ~(1u << i);
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
    int i, free_ = -1, oldest = 0;
    for (i = 0; i < MAXREMOTE; i++) {
        if (g_remote[i].ship == ship) {
            if (create)
                g_remote[i].used = now_sec();
            return i;
        }
        if (free_ < 0 && !g_remote[i].ship)
            free_ = i;
        if (g_remote[i].used < g_remote[oldest].used)
            oldest = i;
    }
    if (!create)
        return -1;
    if (free_ < 0) /* every respawn is a new ship: drop the entry unused longest (issue #25) */
        free_ = oldest;
    memset(&g_remote[free_], 0, sizeof(g_remote[0]));
    g_remote[free_].ship = ship;
    g_remote[free_].used = now_sec();
    return free_;
}

/* script: the owner of `ship` fires the banks in `bits` at `target` */
/* a report from the owner, through the host (path 0) or straight from its machine (1): the
 * first copy of each report wins */
void phaser_set_remote_seq(DWORD ship, DWORD seq, DWORD bits, DWORD target, DWORD hits, DWORD on, int path)
{
    BYTE *own = local_ship();
    int i;
    if (own && *(DWORD *)(own + 4) == ship)
        return;
    i = remote_slot(ship, 1);
    /* after a long silence the sender may be a new connection (a player who rejoined gets the
     * same object IDs again) whose sequence starts over */
    if (g_remote[i].hasSeq && now_sec() - g_remote[i].reported > REPORT_TIMEOUT_S)
        g_remote[i].hasSeq = 0;
    g_remote[i].reported = now_sec();
    if (g_remote[i].hasSeq && (long)(seq - g_remote[i].seq) <= 0 && (long)(g_remote[i].seq - seq) < 100000) {
        g_staleReports++;
        return;
    }
    g_remote[i].seq = seq;
    g_remote[i].hasSeq = 1;
    tlog_printf("PR %ld %lu %lu %lu %lu %d", tlog_ms(), ship, bits, hits, on, path);
    /* banks the owner stops now: was their beam on an object (the stop then applies a hit)? */
    g_remote[i].stopOn = (g_remote[i].on & g_remote[i].bits & ~bits) | (g_remote[i].stopOn & ~g_remote[i].bits & ~bits) |
                         (on & bits);
    g_remote[i].bits = bits;
    g_remote[i].target = target;
    g_remote[i].hits = hits;
    g_remote[i].on = on;
}

/* script: the next report to send (our own ship or, on the host, an AI ship); 0 if none waits */
int phaser_take(DWORD *ship, DWORD *bits, DWORD *target, DWORD *hits, DWORD *on, DWORD *seq)
{
    int k;
    for (k = 0; k < MAXOWN; k++)
        if (g_own[k].pending) {
            g_own[k].pending = 0;
            *ship = g_own[k].ship;
            *bits = g_own[k].bits;
            *target = g_own[k].target;
            *hits = g_own[k].hits;
            *on = g_own[k].on;
            *seq = g_own[k].seq;
            g_own[k].sentBits = g_own[k].bits;
            g_own[k].sentHits = g_own[k].hits;
            g_own[k].sentOn = g_own[k].on;
            g_own[k].sentTarget = g_own[k].target;
            g_own[k].sentValid = 1;
            g_sent++;
            return 1;
        }
    return 0;
}

/* script, host: the AI ships it simulates (their banks are reported like its own ship's) */
void phaser_set_owned(const DWORD *ids, int n)
{
    int i;
    if (n > MAXOWN - 1)
        n = MAXOWN - 1;
    for (i = 0; i < n; i++)
        g_aiShips[i] = ids[i];
    g_nAiShips = n;
}

/* one simulated ship: its banks' state; 1 if a new report waits for the script */
static int report_ship(int k, BYTE *ship)
{
    BYTE *sys = *(BYTE **)(ship + SHIP_PHASERS), **arr;
    DWORD bits = 0, id = *(DWORD *)(ship + 4), target = 0, hits = 0, on = 0, other = 0;
    int n, i;
    if (!sys)
        return 0;
    n = *(int *)(sys + SUB_NUM_CHILDREN);
    arr = *(BYTE ***)(sys + SUB_CHILDREN);
    for (i = 0; i < n && i < 32 && arr; i++)
        if (arr[i] && arr[i][BANK_FIRING]) {
            bits |= 1u << i;
            if (*(DWORD *)(arr[i] + BANK_BEAM_OBJECT)) {
                on |= 1u << i;
                if (*(int *)(arr[i] + BANK_BEAM_STATE) == 3)
                    hits |= 1u << i;
            }
            if (!target)
                target = *(DWORD *)(arr[i] + BANK_TARGET);
            else if (g_cfg.testMode && *(DWORD *)(arr[i] + BANK_TARGET) != target)
                other |= 1u << i; /* test telemetry: a firing bank on another target */
        }
    if (g_cfg.testMode && (other != g_own[k].other || (other && target != g_own[k].target))) {
        tlog_printf("PM %ld %lu %lu %lu %lu", tlog_ms(), id, bits, other, target);
        g_own[k].other = other;
    }
    if (id == g_own[k].ship && bits == g_own[k].bits && hits == g_own[k].hits && on == g_own[k].on &&
        (!bits || target == g_own[k].target))
        return 0;
    if (id != g_own[k].ship)
        g_own[k].sentValid = 0;
    g_own[k].ship = id;
    g_own[k].bits = bits;
    g_own[k].target = target;
    g_own[k].hits = hits;
    g_own[k].on = on;
    g_own[k].seq++;
    tlog_printf("PO %ld %lu %lu %lu %lu", tlog_ms(), id, bits, hits, on);
    if (direct_enabled())
        direct_send_banks(id, g_own[k].seq, bits, target, hits, on);
    if (g_own[k].sentValid && bits == g_own[k].sentBits && hits == g_own[k].sentHits && on == g_own[k].sentOn &&
        (!bits || target == g_own[k].sentTarget)) {
        g_own[k].pending = 0;
        return 0;
    }
    g_own[k].pending = 1;
    return 1;
}

/* every frame: which banks of our own ship fire? Tell the script at once on a change. */
/* every frame: the banks of the ships this machine simulates; tell the script at once on a change */
void phaser_frame(int pythonReady)
{
    BYTE *ship = local_ship();
    int i, any = 0;
    if (!g_cfg.phaserSync)
        return;
    if (direct_enabled())
        direct_frame();
    enforce_remote();
    if (ship)
        any |= report_ship(0, ship);
    for (i = 0; i < g_nAiShips; i++) {
        BYTE *ai = ((PFN_FindObject)ADDR_FIND_OBJECT)(0, g_aiShips[i]);
        int k;
        if (!ai || ai == ship || !((PFN_IsA)(*(void ***)ai)[2])(ai, 0, TYPE_SHIP))
            continue;
        /* its entry: the one of this ship, else one no listed ship uses */
        for (k = 1; k < MAXOWN && g_own[k].ship != g_aiShips[i]; k++)
            ;
        if (k == MAXOWN) {
            for (k = 1; k < MAXOWN; k++) {
                int j, listed = 0;
                for (j = 0; j < g_nAiShips; j++)
                    listed |= g_own[k].ship == g_aiShips[j];
                if (!listed)
                    break;
            }
            if (k == MAXOWN)
                continue;
        }
        any |= report_ship(k, ai);
    }
    if (any && pythonReady)
        ((PFN_PyRunSimpleString)ADDR_PYRUN_SIMPLESTRING)("import bcmpfix_mp\nbcmpfix_mp.SendBanks()\n");
}

static char __fastcall hk_Fire(void *bank, void *edx, int a1, int a2)
{
    BYTE *b = (BYTE *)bank, *ship = *(BYTE **)(b + BANK_SHIP);
    (void)edx;
    if (ship && ship != local_ship()) {
        int slot = remote_slot(*(DWORD *)(ship + 4), 0), idx;
        if (slot >= 0 && (idx = bank_index(ship, b)) >= 0) {
            char r;
            if (!(g_remote[slot].bits & (1u << idx))) {
                /* the owner does not fire this bank */
                g_gated++;
                if (b[BANK_FIRING]) {
                    g_allowStop = 1;
                    ((PFN_Stop)(*(void ***)bank)[VT_STOP / 4])(bank, 0);
                    g_allowStop = 0;
                }
                return 0;
            }
            /* the owner fires it: our copy's charge must not stop it */
            ensure_charge(b);
            /* ... and it aims where the owner's bank aims (issue #30). Each weapon system picks
             * its banks' targets itself: when Fire fails for the current target (out of the
             * bank's arc or range), it goes on with the next target in its list (0x00584E40)
             * without stopping the bank. Our copy passes the arc test (forced, hk_Arc), so it
             * kept the old target while the owner's bank had switched: the owner's beam hit one
             * ship and ours another or nothing. The point on the target comes with it (aim_at). */
            if (g_remote[slot].target && *(DWORD *)(b + BANK_TARGET) == g_remote[slot].target &&
                g_cfg.phaserOwnerTarget && g_cfg.phaserSync >= 3 && !*(DWORD *)(b + BANK_TARGET + 4) &&
                !*(DWORD *)(b + BANK_TARGET + 8) && !*(DWORD *)(b + BANK_TARGET + 12))
                aim_at(b, g_remote[slot].target); /* the centre: maybe the list has the point by now */
            else if (g_remote[slot].target && *(DWORD *)(b + BANK_TARGET) != g_remote[slot].target) {
                if (g_cfg.testMode && g_diagWrongTgt[slot][idx] != *(DWORD *)(b + BANK_TARGET)) {
                    g_diagWrongTgt[slot][idx] = *(DWORD *)(b + BANK_TARGET);
                    tlog_printf("PG %ld %lu %d %lu %lu %d", tlog_ms(), *(DWORD *)(ship + 4), idx,
                                *(DWORD *)(b + BANK_TARGET), g_remote[slot].target, b[BANK_FIRING]);
                }
                if (g_cfg.phaserOwnerTarget && g_cfg.phaserSync >= 3) {
                    aim_at(b, g_remote[slot].target);
                    g_retargeted++;
                }
            }
            g_remote[slot].fed[idx] = g_frame;
            if (g_cfg.phaserSync < 3)
                return ((PFN_Fire)ADDR_FIRE)(bank, 0, a1, a2);
            {
                int ms = lead_get_fire(*(DWORD *)(ship + 4));
                g_curGrowth = 0.25f - ms / 1000.0f;
                if (g_curGrowth < 0.02f)
                    g_curGrowth = 0.02f;
            }
            g_curBank = b;
            g_curSlot = slot;
            g_curIdx = idx;
            {
                char was = b[BANK_FIRING];
                int st = *(int *)(b + BANK_BEAM_STATE);
                g_diagTgt = g_diagRange = -1;
                r = ((PFN_Fire)ADDR_FIRE)(bank, 0, a1, a2);
                if (!was && b[BANK_FIRING])
                    tlog_printf("PS %ld %lu %d", tlog_ms(), *(DWORD *)(ship + 4), idx);
                if (!was && !b[BANK_FIRING] && g_cfg.testMode && now_sec() - g_diagLast[slot][idx] > 0.03) {
                    char cf = ((char(__fastcall *)(void *, void *))(*(void ***)bank)[0x84 / 4])(bank, 0);
                    g_diagLast[slot][idx] = now_sec();
                    tlog_printf("PF %ld %lu %d canfire=%d tgt=%d list=%d target=%lu charge=%.2f", tlog_ms(),
                                *(DWORD *)(ship + 4), idx, cf, g_diagTgt, g_diagRange, *(DWORD *)(b + BANK_TARGET),
                                *(float *)(b + BANK_CHARGE));
                }
                if (st != 3 && *(int *)(b + BANK_BEAM_STATE) == 3)
                    tlog_printf("PH %ld %lu %d", tlog_ms(), *(DWORD *)(ship + 4), idx);
            }
            g_curBank = NULL;
            return r;
        }
    }
    return ((PFN_Fire)ADDR_FIRE)(bank, 0, a1, a2);
}

/* PhaserSync=3 hooks, active only while Fire runs for a bank its owner fires (g_curBank) */
#define ADDR_CALL_ARC 0x00571134  /* PhaserBank::Fire -> arc test 0x005714A0 */
#define ADDR_ARC 0x005714A0
#define ADDR_CALL_WAIT 0x00572216 /* beam state 1: wait (0x004E7200) */
#define ADDR_WAIT 0x004E7200
#define ADDR_CALL_GROW 0x00572248 /* beam state 1 -> 2: growth timer of 0.25 s (0x004E75B0) */
#define ADDR_GROW 0x004E75B0
#define ADDR_STOP 0x00571200
#define ADDR_CALL_GROWN 0x005721D8 /* beam state 2: growth done? (0x004E7580) -> state 3, hit */
#define ADDR_GROWN 0x004E7580
#define ADDR_CALL_TGT 0x00571043 /* Fire: target position (0x005852A0, thiscall on the phaser system) */
#define ADDR_TGT 0x005852A0
#define ADDR_CALL_LIST 0x0057106E /* Fire: is the target in the global target list 0x0098D32C? */
#define ADDR_LIST 0x005859D0
#define ADDR_CALL_STOPHIT 0x00571374 /* StopFiring: apply the hit on the beam's object (0x005722B0) */
#define ADDR_HIT 0x005722B0
typedef char(__fastcall *PFN_Arc)(void *bank, void *edx, void *a1, void *a2, float *a3);
typedef char(__fastcall *PFN_Wait)(void *beam, void *edx, float t, float p);
typedef void(__fastcall *PFN_Grow)(void *beam, void *edx, float t, float dur);
typedef char(__fastcall *PFN_Grown)(void *beam, void *edx);
typedef void(__fastcall *PFN_Hit)(void *bank, void *edx);
static void *o_StopHit, *o_Tgt, *o_List;
static unsigned long g_forcedList;
typedef char(__fastcall *PFN_Tgt)(void *sys, void *edx, void *a1, void *a2, void *a3);
typedef char(__cdecl *PFN_List)(void *target);

static char __fastcall hk_Tgt(void *sys, void *edx, void *a1, void *a2, void *a3)
{
    char r = ((PFN_Tgt)o_Tgt)(sys, edx, a1, a2, a3);
    if (g_curBank)
        g_diagTgt = r;
    return r;
}

/* Fire only targets in a global list that 0x005856D0 rebuilds for each ship right before its
 * weapon systems' frame update (the targets of its weapon systems that its sensors see). Our
 * start of a bank runs outside that update and sees another ship's list, so the bank started
 * only with the weapon system's own retry (every 0.33 s). The owner fired at this target. */
static char __cdecl hk_List(void *target)
{
    char r = ((PFN_List)o_List)(target);
    if (g_curBank) {
        g_diagRange = r;
        if (!r && target) {
            g_forcedList++;
            return 1;
        }
    }
    return r;
}
static void *o_Arc, *o_Wait, *o_Grow, *o_Grown;
static unsigned long g_heldGrowth;

/* the owner fired this bank: its arc was clear on the owner's machine */
static char __fastcall hk_Arc(void *bank, void *edx, void *a1, void *a2, float *a3)
{
    char r = ((PFN_Arc)o_Arc)(bank, edx, a1, a2, a3);
    if (!r && bank == g_curBank) {
        g_forcedArc++;
        return 1;
    }
    return r;
}

/* skip the wait before the beam starts to grow */
static char __fastcall hk_Wait(void *beam, void *edx, float t, float p)
{
    if (g_curBank) {
        g_skippedWait++;
        return 1;
    }
    return ((PFN_Wait)o_Wait)(beam, edx, t, p);
}

/* shorten the 0.25 s growth by the latency, so the hit shows when it does for the owner */
static void __fastcall hk_Grow(void *beam, void *edx, float t, float dur)
{
    if (g_curBank)
        dur = g_curGrowth;
    ((PFN_Grow)o_Grow)(beam, edx, t, dur);
}

/* PhaserSync=3: the beam reaches the target (hit state) when it does on the owner's machine */
static char __fastcall hk_Grown(void *beam, void *edx)
{
    if (g_curBank) {
        if (g_remote[g_curSlot].hits & (1u << g_curIdx))
            return 1;
        g_heldGrowth++;
        return 0;
    }
    return ((PFN_Grown)o_Grown)(beam, edx);
}

/* A bank that stops with its beam's ray on an object applies a hit, also before the beam
 * reached it (state 2). Our copy skips the start wait, so its ray is on the target earlier
 * than the owner's: apply that hit only when the owner's beam was on an object at its stop. */
static void __fastcall hk_StopHit(void *bank, void *edx)
{
    BYTE *b = (BYTE *)bank, *ship = *(BYTE **)(b + BANK_SHIP);
    if (ship && ship != local_ship()) {
        int slot = remote_slot(*(DWORD *)(ship + 4), 0), idx;
        if (slot >= 0 && (idx = bank_index(ship, b)) >= 0 && !(g_remote[slot].stopOn & (1u << idx))) {
            g_skippedStopHit++;
            return;
        }
    }
    ((PFN_Hit)o_StopHit)(bank, edx);
}

/* while the owner fires a bank, only the owner's stop ends it */
static void __fastcall hk_Stop(void *bank, void *edx)
{
    BYTE *b = (BYTE *)bank, *ship = *(BYTE **)(b + BANK_SHIP);
    (void)edx;
    if (!g_allowStop && g_cfg.phaserSync >= 3 && ship && ship != local_ship()) {
        int slot = remote_slot(*(DWORD *)(ship + 4), 0), idx;
        if (slot >= 0 && (idx = bank_index(ship, b)) >= 0 && (g_remote[slot].bits & (1u << idx)))
            return;
    }
    ((PFN_Stop)ADDR_STOP)(bank, 0);
}

void phaser_stats2(unsigned long *arc, unsigned long *wait, unsigned long *driven, unsigned long *held,
                   unsigned long *stopHits, unsigned long *list)
{
    *list = g_forcedList;
    *arc = g_forcedArc;
    *wait = g_skippedWait;
    *driven = g_driven;
    *held = g_heldGrowth;
    *stopHits = g_skippedStopHit;
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
    if (g_cfg.phaserSync >= 3) {
        void *stopSlot = (void *)(PHASERBANK_VTABLE + VT_STOP), *stopHook = (void *)hk_Stop;
        redirect_call(ADDR_CALL_ARC, (void *)hk_Arc, &o_Arc, ADDR_ARC, "phaser hit authority: owner's arc");
        redirect_call(ADDR_CALL_WAIT, (void *)hk_Wait, &o_Wait, ADDR_WAIT, "phaser hit authority: no start wait");
        redirect_call(ADDR_CALL_GROW, (void *)hk_Grow, &o_Grow, ADDR_GROW, "phaser hit authority: growth minus latency");
        redirect_call(ADDR_CALL_GROWN, (void *)hk_Grown, &o_Grown, ADDR_GROWN, "phaser hit authority: hit when the owner hits");
        redirect_call(ADDR_CALL_LIST, (void *)hk_List, &o_List, ADDR_LIST, "phaser hit authority: owner's target");
        if (g_cfg.testMode)
            redirect_call(ADDR_CALL_TGT, (void *)hk_Tgt, &o_Tgt, ADDR_TGT, "phaser diagnostics: target");
        redirect_call(ADDR_CALL_STOPHIT, (void *)hk_StopHit, &o_StopHit, ADDR_HIT,
                      "phaser hit authority: hit at the stop only if the owner's beam was on an object");
        if (*(DWORD *)stopSlot == ADDR_STOP) {
            mem_write(stopSlot, &stopHook, 4);
            logf("PATCH phaser hit authority: stop only by the owner: applied");
        }
    }
}
