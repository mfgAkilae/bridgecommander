/* bcmpfix - multiplayer robustness fixes for Star Trek: Bridge Commander 1.1 */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

/* ---- config ------------------------------------------------------------ */
typedef struct Config {
    /* fixes */
    int stateUpdateHz;      /* 0 = unlimited (original behaviour: every frame) */
    int alwaysAbsolutePos;  /* send absolute position in every state update */
    int fixDeltaBase;       /* receiver: delta relative to last absolute, not accumulated */
    int fixProxyInit;       /* NetworkAI first-frame time init */
    int noDeadband;         /* NetworkAI: correct small position errors too */
    float correctionGain;   /* NetworkAI position correction gain (1 = original 1/s) */
    int smoothCorrection;   /* never correct a remote ship backwards faster than half its speed */
    int orientationCorrection; /* turn remote ships towards the received orientation (time constant as position) */
    int arcExtrapolation;   /* with orientationCorrection 2: extrapolate the position along the predicted turn */
    float sendInterval;     /* near-range state update interval in s (original 0.1) */
    int retransmitMs;       /* resend interval for guaranteed messages (original 1000) */
    int torpedoAuthority;   /* MP clients show torpedo hits only when the host reports them */
    int latencyComp;        /* extrapolate remote ships by measured one-way latency */
    int maxLeadMs;          /* cap for latency compensation */
    int hostRelay;          /* host writes client ships from the received state, not its proxy */
    int noScriptConsole;    /* script errors: log and continue instead of the blocking debug console */
    int mpScript;           /* run scripts/bcmpfix_mp.py (needed by the two options above) */
    int timerResolution1ms; /* timeBeginPeriod(1) */
    int qpcRebase;          /* game clock relative to process start, not boot */
    int maxFps;             /* 0 = unlimited */
    int inputOnlyFocused;   /* ignore DirectInput when window is not foreground */
    /* test / diagnostics */
    int testMode;           /* master switch for the options below */
    int inputIsolation;     /* no exclusive input, no cursor clipping, no focus stealing */
    int noInput;            /* feed zero input to the game */
    int noMovies;           /* skip Bink movies */
    char bindAddress[32];   /* bind sockets to this address instead of INADDR_ANY */
    int clientBindPort;     /* port for implicitly bound (client) sockets, 0 = ephemeral */
    int windowX, windowY;   /* initial window position (-1 = default) */
    int emuLatencyMs;       /* outgoing one-way latency */
    int emuJitterMs;        /* +/- jitter */
    int emuLossPct10;       /* loss in 0.1 % */
    char pythonTick[64];    /* e.g. Local.Tick - called periodically from the frame loop */
    int pythonTickMs;
    int pythonRedirect;     /* test mode: send Python stdout/stderr to BCMPFIX_PYLOG */
    double pythonTickDelay; /* seconds after start before the first tick */
    int statsIntervalSec;   /* log network stats every N seconds (0 = off) */
    int traceSec;           /* trace state updates and corrections for N seconds (0 = off) */
    int verbose;
} Config;

extern Config g_cfg;
extern HMODULE g_self;
extern char g_gameDir[MAX_PATH];

void config_load(void);
void crashlog_install(void);

/* ---- log --------------------------------------------------------------- */
void log_open(void);
void logf(const char *fmt, ...);

/* ---- time -------------------------------------------------------------- */
double now_sec(void);
DWORD qpc_ms(void);

/* ---- state update trace ------------------------------------------------ */
int trace_active(void);
void tracef(const char *fmt, ...);

/* ---- hooking ----------------------------------------------------------- */
int mem_write(void *addr, const void *data, int len);
int patch_bytes(DWORD addr, const char *expectHex, const char *replHex, const char *what);
int redirect_call(DWORD callsite, void *newfn, void **orig, DWORD expectTarget, const char *what);
void **iat_slot(HMODULE mod, const char *dll, const char *name, WORD ordinal);
int iat_hook(HMODULE mod, const char *dll, const char *name, WORD ordinal, void *newfn, void **orig);

/* ---- modules ----------------------------------------------------------- */
void input_install(void);
HRESULT input_wrap_dinput(void *pDI);
void net_install(void);
void netfix_install(void);
int pyext_init(void);
void torpedo_install(void);
void torpedo_authorize(DWORD torpId);
void torpedo_authorize_mode(DWORD torpId, int mode);
void torpedo_authorize_start(DWORD torpId, int mode, float x, float y, float z);
int torpedo_force(void *torp, void *target);
void torpedo_retire(void *torp);
void torpedo_set_active(int on);
void torpedo_stats(unsigned long *suppressed, unsigned long *allowed);
void lead_install(void);
void lead_set(DWORD objId, int ms);
void relay_install(void);
unsigned long relay_count(void);
void orient_on_update(BYTE *ai, int leadMs, const float *p0);
int orient_predict(BYTE *ai, float *f, float *u);
int orient_target(BYTE *ai, float *out);
void scripterr_install(void);
unsigned long scripterr_count(void);
