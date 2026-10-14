/* Multiplayer synchronisation fixes for stbc.exe 1.1 */
#include "common.h"
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

/* MultiplayerGame per-frame update (0x0069EDC0) calls SendStateUpdates (0x0069EE50)
 * every frame. Both are __thiscall without stack arguments. */
#define ADDR_CALL_SENDSTATE 0x0069EDCB
#define ADDR_SENDSTATE 0x0069EE50

/* Ship::WriteStateUpdate (0x005B17F0): absolute position only if more than 1.0 s
 * since the last absolute one for this (ship, peer); otherwise a delta relative to
 * that last absolute position. Receivers accumulate the deltas (0x005B21C0), so a
 * lost absolute update corrupts the dead-reckoning base and ships teleport. */
#define ADDR_ABS_CHECK 0x005B18D7 /* jnz 0x005b18e5 -> nop nop */

/* Receivers (ship reader 0x005B21C0 and base reader 0x005A2BF0) add each delta to
 * the dead-reckoning base (+0x9C += d) although the delta is relative to the last
 * absolute position (+0x90). Make it +0x9C = +0x90 + d (disp byte of FADD). */
static const struct {
    DWORD addr;
    const char *expect, *repl, *what;
} k_deltaBase[] = {
    {0x005B24C6, "d8 86 9c 00 00 00", "d8 86 90 00 00 00", "ship reader delta base x"},
    {0x005B24D8, "d8 86 a0 00 00 00", "d8 86 94 00 00 00", "ship reader delta base y"},
    {0x005B24E8, "d8 86 a4 00 00 00", "d8 86 98 00 00 00", "ship reader delta base z"},
    {0x005A2E62, "d8 86 9c 00 00 00", "d8 86 90 00 00 00", "base reader delta base x"},
    {0x005A2E72, "d8 86 a0 00 00 00", "d8 86 94 00 00 00", "base reader delta base y"},
    {0x005A2E82, "d8 86 a4 00 00 00", "d8 86 98 00 00 00", "base reader delta base z"},
};

/* Per (object, peer) send interval in the rate limiter 0x005A22A0: 0.1f immediates
 * for the near range. Replaced by the configured interval. */
static const DWORD k_sendInterval[] = {0x005A23BD, 0x005A2418, 0x005A244A, 0x005A2480, 0x005A2499, 0x005A24CB};

/* NetworkAI (remote ship proxy) ctor stores the creation time into +0x2C (sender
 * time, unused) instead of +0x28 (last update time); the first Update then uses
 * dt = absolute game time and flings the proxy away. */
#define ADDR_NETAI_CTOR_TIME 0x0047DD06
/* NetworkAI position correction (0x0047E180): no correction below 0.5 units while
 * moving; compare against 1e-6 (0x00888B58) instead. */
#define ADDR_NETAI_DEADBAND 0x0047E20E
/* NetworkAI::Update calls the correction with (ship, dt); the correction moves
 * min(dt,1) of the error per frame, i.e. a 1 s time constant. We scale dt. */
#define ADDR_CALL_NETAI_CORRECT 0x0047E01D
#define ADDR_NETAI_CORRECT 0x0047E180

typedef void(__fastcall *PFN_ThisVoid)(void *self, void *edx);
typedef void(__fastcall *PFN_Correct)(void *self, void *edx, void *ship, float dt);
static PFN_ThisVoid o_SendStateUpdates;
static PFN_Correct o_NetAICorrect;

double __cdecl sqrt(double); /* not <math.h>: its logf clashes with ours */
#define ABSF(x) ((x) < 0.0f ? -(x) : (x))

typedef float *(__fastcall *PFN_GetPos)(void *obj, void *edx);
typedef void(__fastcall *PFN_SetPos)(void *obj, void *edx, float *pos);
typedef float *(__fastcall *PFN_GetVecOut)(void *obj, void *edx, float *out);

static unsigned long g_heldBack; /* frames in which a backward correction was limited */

/* Smooth correction (SmoothCorrection=1): the stock code moves the proxy straight
 * towards the target. When the proxy is ahead of the target (extrapolation overshot,
 * the remote ship slowed down or turned), that move goes against the direction of
 * flight: the ship visibly jumps back (rubber-banding). Split the error into a part
 * along the direction of flight and a lateral part. Lateral and forward parts are
 * corrected as before; a backward part is limited to half the distance the ship flies
 * in this frame, so the ship only slows down until the target catches up. */
static void smooth_correct(BYTE *ai, void *ship, float dt)
{
    void **vt = *(void ***)ship;
    float *pos = ((PFN_GetPos)vt[0x94 / 4])(ship, 0);
    float *target = (float *)(ai + 0x30), *fwd = (float *)(ai + 0x3C), pf[3], pu[3];
    float speed = *(float *)(ai + 0x54);
    float e[3], m[3], n, along, k, back, newpos[3];
    int i;
    if (g_cfg.orientationCorrection == 2 && g_cfg.arcExtrapolation && orient_predict(ai, pf, pu))
        fwd = pf; /* direction of flight now, not one latency ago */
    for (i = 0; i < 3; i++)
        e[i] = target[i] - pos[i];
    n = (float)sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    k = dt * g_cfg.correctionGain;
    if (k > 1.0f)
        k = 1.0f;
    if (n < 1e-6f || ABSF(speed) < 0.5f) { /* (nearly) standing: plain correction */
        for (i = 0; i < 3; i++)
            newpos[i] = pos[i] + e[i] * k;
        ((PFN_SetPos)vt[0x78 / 4])(ship, 0, newpos);
        return;
    }
    for (i = 0; i < 3; i++) /* direction of flight */
        m[i] = fwd[i] / n * (speed < 0.0f ? -1.0f : 1.0f);
    along = e[0] * m[0] + e[1] * m[1] + e[2] * m[2];
    back = along * k; /* along-track move this frame (negative = backwards) */
    if (back < 0.0f) {
        float limit = 0.5f * ABSF(speed) * dt;
        if (-back > limit) {
            back = -limit;
            g_heldBack++;
        }
    }
    for (i = 0; i < 3; i++)
        newpos[i] = pos[i] + (e[i] - along * m[i]) * k + m[i] * back;
    if (trace_active()) {
        float tmp[3], *nose = ((PFN_GetVecOut)vt[0xAC / 4])(ship, 0, tmp);
        float lat2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2] - along * along;
        tracef("C obj=%lu dt=%.4f along=%.3f lat=%.3f back=%.4f spd=%.2f nose.fwd=%.3f", *(DWORD *)((BYTE *)ship + 4), dt,
               along, lat2 > 0.0f ? (float)sqrt(lat2) : 0.0f, back, speed,
               nose[0] * m[0] + nose[1] * m[1] + nose[2] * m[2]);
    }
    ((PFN_SetPos)vt[0x78 / 4])(ship, 0, newpos);
}

/* Orientation correction (OrientationCorrection=1): NetworkAI turns the proxy
 * physically towards the received forward/up vectors (+0x3C/+0x48) and drives it
 * along its own nose (SetSpeed). In sharp turns at low speed the proxy's nose lags
 * the real ship by more than 90 degrees for seconds; the proxy then flies the wrong
 * way and the position correction drags it back (rubber-banding, issue #17). Turn the
 * proxy towards the received orientation with the position correction's time
 * constant (AlignToVectors 0x004311F0, as NetworkAI does itself below 0.5 speed). */
#define ADDR_ALIGN_TO_VECTORS 0x004311F0
typedef void(__fastcall *PFN_Align)(void *obj, void *edx, float *fwd, float *up);

static int norm3(float *v)
{
    float n = (float)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n < 1e-4f)
        return 0;
    v[0] /= n;
    v[1] /= n;
    v[2] /= n;
    return 1;
}

static void align_orientation(BYTE *ai, void *ship, float dt)
{
    void **vt = *(void ***)ship;
    float *rf = (float *)(ai + 0x3C), *ru = (float *)(ai + 0x48);
    float t1[3], t2[3], pf[3], pu[3], *nose, *up, f[3], u[3], k, d;
    int i;
    if (rf[0] * rf[0] + rf[1] * rf[1] + rf[2] * rf[2] < 0.25f || ru[0] * ru[0] + ru[1] * ru[1] + ru[2] * ru[2] < 0.25f)
        return; /* nothing received yet */
    nose = ((PFN_GetVecOut)vt[0xAC / 4])(ship, 0, t1);
    up = ((PFN_GetVecOut)vt[0xB0 / 4])(ship, 0, t2);
    k = dt * g_cfg.correctionGain;
    if (k > 1.0f)
        k = 1.0f;
    if (g_cfg.orientationCorrection == 2 && orient_predict(ai, pf, pu)) {
        /* mode 2: follow the received orientation, dead-reckoned with the estimated
         * turn rate over latency + age of the update (orient.c) */
        rf = pf;
        ru = pu;
    } else {
        /* mode 1: only when the nose lags far behind. In ordinary turns the physical
         * turning keeps rotating and so predicts the turn, while the received vectors
         * are one latency old. No correction below 30 degrees, full from 60 degrees. */
        d = (nose[0] * rf[0] + nose[1] * rf[1] + nose[2] * rf[2]) /
            (float)sqrt((rf[0] * rf[0] + rf[1] * rf[1] + rf[2] * rf[2]) + 1e-12f);
        if (d >= 0.866f)
            return;
        if (d > 0.5f)
            k *= (0.866f - d) / 0.366f;
    }
    for (i = 0; i < 3; i++) {
        f[i] = nose[i] + (rf[i] - nose[i]) * k;
        u[i] = up[i] + (ru[i] - up[i]) * k;
    }
    if (!norm3(f)) /* nose exactly opposite: take the received vector */
        for (i = 0; i < 3; i++)
            f[i] = rf[i];
    d = u[0] * f[0] + u[1] * f[1] + u[2] * f[2];
    for (i = 0; i < 3; i++)
        u[i] -= f[i] * d; /* orthogonal to the new forward vector */
    if (!norm3(u) || !norm3(f))
        return;
    ((PFN_Align)ADDR_ALIGN_TO_VECTORS)(ship, 0, f, u);
}

static void __fastcall hk_NetAICorrect(void *self, void *edx, void *ship, float dt)
{
    (void)edx;
    if (g_cfg.orientationCorrection == 2 && g_cfg.arcExtrapolation && ship) {
        /* replace NetworkAI's straight-line target with the arc extrapolation */
        float t[3];
        if (orient_target((BYTE *)self, t))
            memcpy((BYTE *)self + 0x30, t, 12);
    }
    if (g_cfg.orientationCorrection && ship)
        align_orientation((BYTE *)self, ship, dt);
    if (g_cfg.smoothCorrection && ship) {
        smooth_correct((BYTE *)self, ship, dt);
        return;
    }
    o_NetAICorrect(self, 0, ship, dt * g_cfg.correctionGain);
}

unsigned long netfix_heldback(void)
{
    return g_heldBack;
}
static double g_nextSend;
static unsigned long g_frames, g_sends;
static double g_statT;

static void __fastcall hk_SendStateUpdates(void *self, void *edx)
{
    double t = now_sec();
    (void)edx;
    g_frames++;
    if (g_cfg.stateUpdateHz <= 0 || t >= g_nextSend) {
        double period = g_cfg.stateUpdateHz > 0 ? 1.0 / g_cfg.stateUpdateHz : 0.0;
        /* keep a steady cadence but never try to catch up a backlog */
        g_nextSend = (t - g_nextSend > period) ? t + period : g_nextSend + period;
        g_sends++;
        o_SendStateUpdates(self, 0);
    }
    if (g_cfg.statsIntervalSec > 0 && t - g_statT >= g_cfg.statsIntervalSec) {
        if (g_statT > 0)
            logf("MP frames %.1f/s, state-update rounds %.1f/s, relayed client-ship writes %lu, held-back corrections %lu",
                 g_frames / (t - g_statT), g_sends / (t - g_statT), relay_count(), g_heldBack);
        g_frames = g_sends = 0;
        g_statT = t;
    }
}

/* Main frame function (0x0043B4F0) starts with the frame timer update
 * (TGSystemWrapper 0x0071A9E0, ECX = 0x0099C6B0). Hooking that call gives us a
 * per-frame entry point on the main thread for the FPS limiter and the test
 * harness Python tick. */
#define ADDR_CALL_FRAMETIMER 0x0043B514
#define ADDR_FRAMETIMER 0x0071A9E0
#define ADDR_PYRUN_SIMPLESTRING 0x0074AE80

typedef int(__cdecl *PFN_PyRunSimpleString)(const char *);
static PFN_ThisVoid o_FrameTimer;
static double g_lastFrame, g_nextTick;
static char g_tickCode[256];

static void __fastcall hk_FrameTimer(void *self, void *edx)
{
    (void)edx;
    if (g_cfg.maxFps > 0) {
        double period = 1.0 / g_cfg.maxFps;
        double t = now_sec();
        double wait = g_lastFrame + period - t;
        if (wait > 0.002)
            Sleep((DWORD)((wait - 0.001) * 1000.0));
        while (now_sec() < g_lastFrame + period)
            ;
        t = now_sec();
        g_lastFrame = (t - g_lastFrame > 2 * period) ? t : g_lastFrame + period;
    }
    o_FrameTimer(self, 0);
    if (g_tickCode[0]) {
        double t = now_sec();
        static int ready;
        static int calls, fails;
        /* Python is initialised long before the main menu; wait a fixed delay */
        if (!ready && t >= g_cfg.pythonTickDelay) {
            ready = 1;
            logf("starting python tick: %s", g_tickCode);
            if (pyext_init() && g_cfg.testMode && g_cfg.pythonRedirect) {
                int r = ((PFN_PyRunSimpleString)ADDR_PYRUN_SIMPLESTRING)(
                    "import sys, bcmpdll\n"
                    "class _BcmpOut:\n"
                    "\tdef write(self, s):\n"
                    "\t\tbcmpdll.write(s)\n"
                    /* game scripts may flush/close stdout; a missing method raises and
                       opens the blocking Python debug console */
                    "\tdef flush(self):\n"
                    "\t\tpass\n"
                    "\tdef close(self):\n"
                    "\t\tpass\n"
                    "sys.stdout = _BcmpOut()\n"
                    "sys.stderr = sys.stdout\n"
                    "print 'bcmpdll: python output redirected'\n"
                    "print 'sys.path =', sys.path\n");
                logf("python stdout redirect: %d", r);
            }
        }
        if (ready && t >= g_nextTick) {
            int r;
            g_nextTick = t + g_cfg.pythonTickMs / 1000.0;
            r = ((PFN_PyRunSimpleString)ADDR_PYRUN_SIMPLESTRING)(g_tickCode);
            calls++;
            if (r != 0 && fails++ < 5)
                logf("python tick failed (%d) after %d calls", r, calls);
        }
    }
}

/* The game's clock (0x0086E3A0 -> 0x007B7B90) converts QueryPerformanceCounter
 * since boot to seconds and stores it as a float; frame dt is the difference of
 * two such floats. After days of uptime the float ulp is 8-16 ms, so frame dt
 * jitters by that much. Rebasing QPC to process start keeps the values small. */
static LARGE_INTEGER g_qpcBase;
static BOOL(WINAPI *o_QPC)(LARGE_INTEGER *);

static BOOL WINAPI hk_QPC(LARGE_INTEGER *c)
{
    BOOL r = o_QPC(c);
    if (r && c)
        c->QuadPart -= g_qpcBase.QuadPart;
    return r;
}

void netfix_install(void)
{
    if (g_cfg.qpcRebase) {
        HMODULE exe = GetModuleHandleA(NULL);
        QueryPerformanceCounter(&g_qpcBase);
        g_qpcBase.QuadPart -= 1; /* never return 0 */
        if (iat_hook(exe, "KERNEL32.dll", "QueryPerformanceCounter", 0, (void *)hk_QPC, (void **)&o_QPC))
            logf("QueryPerformanceCounter rebased to process start");
    }
    /* script layer: bcmpfix_mp.py (torpedo hit sync, latency measurement) always,
     * plus the test harness tick in test mode */
    if (g_cfg.mpScript)
        lstrcpynA(g_tickCode, "import bcmpfix_mp\nbcmpfix_mp.Tick()\n", sizeof(g_tickCode));
    if (g_cfg.testMode && g_cfg.pythonTick[0]) {
        const char *dot = strrchr(g_cfg.pythonTick, '.');
        size_t n = strlen(g_tickCode);
        if (dot)
            _snprintf(g_tickCode + n, sizeof(g_tickCode) - 1 - n, "import %.*s\n%s()\n",
                      (int)(dot - g_cfg.pythonTick), g_cfg.pythonTick, g_cfg.pythonTick);
    }
    if (g_cfg.maxFps > 0 || g_tickCode[0]) {
        redirect_call(ADDR_CALL_FRAMETIMER, (void *)hk_FrameTimer, (void **)&o_FrameTimer, ADDR_FRAMETIMER,
                      "frame hook (FPS limit / python tick)");
    }
    if (g_cfg.timerResolution1ms) {
        timeBeginPeriod(1);
        logf("timeBeginPeriod(1)");
    }
    redirect_call(ADDR_CALL_SENDSTATE, (void *)hk_SendStateUpdates, (void **)&o_SendStateUpdates, ADDR_SENDSTATE,
                  "SendStateUpdates rate limiter");
    if (g_cfg.alwaysAbsolutePos)
        patch_bytes(ADDR_ABS_CHECK, "75 0c", "90 90", "always send absolute position");
    if (g_cfg.fixDeltaBase) {
        int i;
        for (i = 0; i < (int)(sizeof(k_deltaBase) / sizeof(k_deltaBase[0])); i++)
            patch_bytes(k_deltaBase[i].addr, k_deltaBase[i].expect, k_deltaBase[i].repl, k_deltaBase[i].what);
    }
    if (g_cfg.fixProxyInit)
        patch_bytes(ADDR_NETAI_CTOR_TIME, "d9 5e 2c", "d9 5e 28", "NetworkAI first-frame time");
    if (g_cfg.noDeadband)
        patch_bytes(ADDR_NETAI_DEADBAND, "d8 1d a8 87 88 00", "d8 1d 58 8b 88 00", "NetworkAI no 0.5 deadband");
    if ((g_cfg.correctionGain > 0 && g_cfg.correctionGain != 1.0f) || g_cfg.smoothCorrection ||
        g_cfg.orientationCorrection)
        redirect_call(ADDR_CALL_NETAI_CORRECT, (void *)hk_NetAICorrect, (void **)&o_NetAICorrect, ADDR_NETAI_CORRECT,
                      g_cfg.smoothCorrection ? "NetworkAI correction gain + no backward correction"
                                             : "NetworkAI correction gain");
    if (g_cfg.retransmitMs > 0 && g_cfg.retransmitMs != 1000) {
        /* TGMessage ctor (0x006B82A0): mov ecx,1.0f; mov [eax+0x34],ecx (backoff step);
         * mov [eax+0x30],ecx (first resend). A lost guaranteed packet otherwise stalls
         * all later guaranteed messages from that peer for >= 1 s (in-order delivery). */
        static float rt;
        char repl[32];
        unsigned char *b = (unsigned char *)&rt;
        rt = g_cfg.retransmitMs / 1000.0f;
        _snprintf(repl, sizeof(repl), "b9 %02x %02x %02x %02x", b[0], b[1], b[2], b[3]);
        patch_bytes(0x006B82D4, "b9 00 00 80 3f", repl, "guaranteed message resend interval");
    }
    if (g_cfg.sendInterval > 0.0f && g_cfg.sendInterval < 0.0999f) {
        static float interval;
        char expect[16], repl[16];
        unsigned char *b = (unsigned char *)&interval;
        int i;
        interval = g_cfg.sendInterval;
        _snprintf(expect, sizeof(expect), "cd cc cc 3d");
        _snprintf(repl, sizeof(repl), "%02x %02x %02x %02x", b[0], b[1], b[2], b[3]);
        for (i = 0; i < (int)(sizeof(k_sendInterval) / sizeof(k_sendInterval[0])); i++)
            patch_bytes(k_sendInterval[i], expect, repl, "state update interval");
    }
}
