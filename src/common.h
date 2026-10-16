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
    int symmetricSpawn;     /* remote torpedo copies keep the shooter's guidance values and direction */
    int fixTorpedoFlags;    /* torpedo fire message: sound/skew bits in the order the receiver reads them */
    float correctionGain;   /* NetworkAI position correction gain (1 = original 1/s) */
    int smoothCorrection;   /* never correct a remote ship backwards faster than half its speed */
    int orientationCorrection; /* turn remote ships towards the received orientation (time constant as position) */
    int velocityExtrapolation; /* target along the velocity estimated from received positions (1 straight, 2 arc) */
    int arcExtrapolation;   /* with orientationCorrection 2: extrapolate the position along the predicted turn */
    float sendInterval;     /* near-range state update interval in s (original 0.1) */
    int retransmitMs;       /* resend interval for guaranteed messages (original 1000) */
    int torpedoAuthority;   /* MP clients show torpedo hits only when the host reports them */
    int latencyComp;        /* extrapolate remote ships by measured one-way latency */
    int maxLeadMs;          /* cap for latency compensation */
    int hostRelay;          /* host writes client ships from the received state, not its proxy */
    int noScriptConsole;    /* script errors: log and continue instead of the blocking debug console */
    float lodScale;         /* state-update distance LOD thresholds x this (1 = stock) */
    int velocityTracking;   /* remote ships follow the measured velocity (velo.c) with feed-forward */
    int trackLeadOffsetMs;
    int trackArc;           /* VelocityTracking: extrapolate along the estimated turn of the velocity */  /* added to the latency compensation of the velocity target (velo.c) */
    int knockbackCorrection; /* follow the measured velocity while a ship is knocked off its nose */
    int torpedoLead;        /* advance remotely spawned torpedoes by the fire message's latency */
    int predictHits;        /* clients show their own torpedo/ship collisions at once (host confirms) */
    int phaserSync;         /* remote phaser banks fire only while their owner fires them */
    int directReports;      /* phaser reports also go straight to every modded peer (direct.c) */
    int fixGhostShip;       /* re-place a ship created before the joining client had its mission set */
    int fixDedicatedIds;    /* a dedicated server numbers its objects from slot 15, not the first client's slot 0 */
    int fixConditionInRange; /* script: ConditionInRange registers its group handlers once (issue #27) */
    int fixRestartCrashes;  /* two original-game crashes when a mission restarts (join.c) */
    int fixRammingDamage;   /* a client's collision report keeps its ship as the source (join.c) */
    int replayAnyShip;      /* test switch: a replayed torpedo may hit any ship, not only the reported one */
    int predictAnyHit;      /* test switch: PredictTorpedoHits also for ships the torpedo was not fired at */
    int phaserOwnerTarget;  /* test switch: remote banks the owner fires aim at the owner's target (issue #30) */
    int snapNewShips;       /* a new remote ship starts at its first received position, not the create message's */
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
void torpedo_trace_install(void);
void torpedo_authorize(DWORD torpId);
void torpedo_authorize_mode(DWORD torpId, int mode);
void torpedo_authorize_start(DWORD torpId, int mode, float x, float y, float z, DWORD target);
int torpedo_force(void *torp, void *target);
void lead_set_fire(DWORD objId, int ms);
int lead_get_fire(DWORD objId);
int torpedo_force_replay(void *torp, void *target);
void torpedo_retire(void *torp);
void torpedo_set_active(int on);
void torpedo_stats(unsigned long *suppressed, unsigned long *allowed);
unsigned long torpedo_pushed_out(void);
unsigned long torpedo_predicted(void);
unsigned long torpedo_bystander(void);
unsigned long torpedo_advanced(void);
void lead_install(void);
unsigned long lead_snapped(void);
extern int g_pythonUp;
int lead_take_snap(DWORD id);
void lead_set(DWORD objId, int ms);
void relay_install(void);
unsigned long relay_count(void);
void orient_on_update(BYTE *ai, int leadMs, const float *p0);
void orient_forget(BYTE *ai);
void velo_forget(BYTE *ai);
int orient_predict(BYTE *ai, float *f, float *u);
int orient_target(BYTE *ai, float *out);
void velo_on_update(BYTE *ai, int leadMs, const float *p0);
int velo_target(BYTE *ai, float *target, float *dir);
int velo_knocked(BYTE *ai, float proxySpeed);
void scripterr_install(void);
void join_install(void);
void phaser_install(void);
void phaser_frame(int pythonReady);
long tlog_ms(void);
void join_frame(void);
int net_sendto(SOCKET s, const char *buf, int len, const struct sockaddr *to, int tolen);
int direct_enabled(void);
void direct_init(void);
void direct_set_role(int isHost, int hostDirect);
void direct_note_game_send(SOCKET s, const struct sockaddr *to);
int direct_filter(SOCKET s, const char *buf, int len, const struct sockaddr *from);
void direct_send_banks(DWORD ship, DWORD seq, DWORD bits, DWORD target, DWORD hits, DWORD on);
void direct_frame(void);
void direct_stats(unsigned long *tx, unsigned long *rx, unsigned long *peers);
void phaser_set_remote_seq(DWORD ship, DWORD seq, DWORD bits, DWORD target, DWORD hits, DWORD on, int path);
void tlog_printf(const char *fmt, ...);
int phaser_take(DWORD *ship, DWORD *bits, DWORD *target, DWORD *hits, DWORD *on, DWORD *seq);
void phaser_set_owned(const DWORD *ids, int n);
void phaser_stats2(unsigned long *arc, unsigned long *wait, unsigned long *driven, unsigned long *held,
                   unsigned long *stopHits, unsigned long *list);
void phaser_stats(unsigned long *gated, unsigned long *charged, unsigned long *sent, unsigned long *started,
                  unsigned long *stopped);
unsigned long scripterr_count(void);
