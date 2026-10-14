#include "common.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

Config g_cfg;
HMODULE g_self;
char g_gameDir[MAX_PATH];
static FILE *g_log;
static CRITICAL_SECTION g_logLock;
static LARGE_INTEGER g_qpf, g_qpc0;

double now_sec(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)(c.QuadPart - g_qpc0.QuadPart) / (double)g_qpf.QuadPart;
}

static int ini_int(const char *ini, const char *sec, const char *key, int def)
{
    return (int)GetPrivateProfileIntA(sec, key, def, ini);
}

void config_load(void)
{
    char ini[MAX_PATH];
    const char *env = getenv("BCMPFIX_INI");
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_qpc0);
    if (env && *env)
        lstrcpynA(ini, env, MAX_PATH);
    else
        _snprintf(ini, MAX_PATH, "%s\\bcmpfix.ini", g_gameDir);

    g_cfg.stateUpdateHz = ini_int(ini, "Fixes", "StateUpdateHz", 0);
    g_cfg.alwaysAbsolutePos = ini_int(ini, "Fixes", "AlwaysAbsolutePosition", 1);
    g_cfg.fixDeltaBase = ini_int(ini, "Fixes", "FixDeltaBase", 1);
    g_cfg.fixProxyInit = ini_int(ini, "Fixes", "FixProxyFirstFrame", 1);
    g_cfg.noDeadband = ini_int(ini, "Fixes", "NoCorrectionDeadband", 1);
    g_cfg.correctionGain = ini_int(ini, "Fixes", "CorrectionGainPercent", 500) / 100.0f;
    g_cfg.smoothCorrection = ini_int(ini, "Fixes", "SmoothCorrection", 1);
    g_cfg.orientationCorrection = ini_int(ini, "Fixes", "OrientationCorrection", 1);
    g_cfg.sendInterval = ini_int(ini, "Fixes", "StateUpdateIntervalMs", 50) / 1000.0f;
    g_cfg.retransmitMs = ini_int(ini, "Fixes", "ResendIntervalMs", 300);
    g_cfg.torpedoAuthority = ini_int(ini, "Fixes", "TorpedoHitAuthority", 1);
    g_cfg.latencyComp = ini_int(ini, "Fixes", "LatencyCompensation", 1);
    g_cfg.maxLeadMs = ini_int(ini, "Fixes", "MaxLatencyCompensationMs", 300);
    g_cfg.hostRelay = ini_int(ini, "Fixes", "HostRelayReceivedState", 1);
    g_cfg.noScriptConsole = ini_int(ini, "Fixes", "NoScriptErrorConsole", 1);
    g_cfg.mpScript = ini_int(ini, "Fixes", "ScriptLayer", 1);
    if (!g_cfg.mpScript)
        g_cfg.torpedoAuthority = g_cfg.latencyComp = 0;
    g_cfg.timerResolution1ms = ini_int(ini, "Fixes", "TimerResolution1ms", 1);
    g_cfg.qpcRebase = ini_int(ini, "Fixes", "ClockFromProcessStart", 1);
    g_cfg.maxFps = ini_int(ini, "Fixes", "MaxFPS", 0);
    g_cfg.inputOnlyFocused = ini_int(ini, "Fixes", "InputOnlyWhenFocused", 0);

    g_cfg.testMode = ini_int(ini, "Test", "Enabled", 0);
    g_cfg.inputIsolation = ini_int(ini, "Test", "InputIsolation", 1);
    g_cfg.noInput = ini_int(ini, "Test", "NoInput", 1);
    g_cfg.noMovies = ini_int(ini, "Test", "NoMovies", 1);
    GetPrivateProfileStringA("Test", "BindAddress", "", g_cfg.bindAddress, sizeof(g_cfg.bindAddress), ini);
    g_cfg.clientBindPort = ini_int(ini, "Test", "ClientBindPort", 0);
    g_cfg.windowX = ini_int(ini, "Test", "WindowX", -1);
    g_cfg.windowY = ini_int(ini, "Test", "WindowY", -1);
    g_cfg.emuLatencyMs = ini_int(ini, "Test", "LatencyMs", 0);
    g_cfg.emuJitterMs = ini_int(ini, "Test", "JitterMs", 0);
    g_cfg.emuLossPct10 = ini_int(ini, "Test", "LossPerMille", 0);
    GetPrivateProfileStringA("Test", "PythonTick", "", g_cfg.pythonTick, sizeof(g_cfg.pythonTick), ini);
    g_cfg.pythonTickMs = ini_int(ini, "Test", "PythonTickMs", 100);
    g_cfg.pythonRedirect = ini_int(ini, "Test", "PythonRedirect", 1);
    g_cfg.pythonTickDelay = ini_int(ini, "Test", "PythonTickDelayMs", 6000) / 1000.0;
    g_cfg.statsIntervalSec = ini_int(ini, "Debug", "NetStatsSec", 0);
    g_cfg.traceSec = ini_int(ini, "Debug", "TraceStateSec", ini_int(ini, "Test", "TraceStateSec", 0));
    g_cfg.verbose = ini_int(ini, "Debug", "Verbose", 0);

    logf("config %s: StateUpdateHz=%d AlwaysAbsolute=%d DeltaBase=%d ProxyInit=%d NoDeadband=%d Gain=%.2f "
         "Interval=%.3f Timer1ms=%d MaxFPS=%d InputOnlyFocused=%d",
         ini, g_cfg.stateUpdateHz, g_cfg.alwaysAbsolutePos, g_cfg.fixDeltaBase, g_cfg.fixProxyInit, g_cfg.noDeadband,
         g_cfg.correctionGain, g_cfg.sendInterval, g_cfg.timerResolution1ms, g_cfg.maxFps, g_cfg.inputOnlyFocused);
    if (g_cfg.testMode)
        logf("TEST MODE: isolation=%d noinput=%d bind=%s win=%d,%d lat=%d jit=%d loss=%d/1000",
             g_cfg.inputIsolation, g_cfg.noInput, g_cfg.bindAddress, g_cfg.windowX, g_cfg.windowY,
             g_cfg.emuLatencyMs, g_cfg.emuJitterMs, g_cfg.emuLossPct10);
}

void log_open(void)
{
    char path[MAX_PATH];
    const char *env = getenv("BCMPFIX_LOG");
    InitializeCriticalSection(&g_logLock);
    if (env && *env)
        lstrcpynA(path, env, MAX_PATH);
    else
        _snprintf(path, MAX_PATH, "%s\\bcmpfix.log", g_gameDir);
    g_log = fopen(path, "w");
}

void logf(const char *fmt, ...)
{
    va_list ap;
    if (!g_log)
        return;
    EnterCriticalSection(&g_logLock);
    fprintf(g_log, "[%9.3f] ", g_qpf.QuadPart ? now_sec() : 0.0);
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}

/* ---- state update trace (TraceStateSec) ---------------------------------- */
/* Lines start with QPC milliseconds since boot (same clock as bcmpdll.ms() and the
 * test telemetry). The trace runs for TraceStateSec seconds after its first line. */
static FILE *g_trace;
static int g_traceState; /* 0 = not started, 1 = running, 2 = done */
static DWORD g_traceEnd;

DWORD qpc_ms(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (DWORD)((c.QuadPart / (f.QuadPart / 1000)) & 0x7fffffff);
}

int trace_active(void)
{
    if (g_cfg.traceSec <= 0 || g_traceState == 2)
        return 0;
    if (g_traceState == 0) {
        const char *p = getenv("BCMPFIX_TRACE");
        char path[MAX_PATH];
        if (p && *p)
            lstrcpynA(path, p, MAX_PATH);
        else
            _snprintf(path, MAX_PATH, "%s\\bcmpfix_trace.log", g_gameDir);
        g_trace = fopen(path, "w");
        g_traceState = g_trace ? 1 : 2;
        g_traceEnd = qpc_ms() + (DWORD)g_cfg.traceSec * 1000;
        return g_traceState == 1;
    }
    if ((int)(qpc_ms() - g_traceEnd) > 0) {
        fclose(g_trace);
        g_trace = NULL;
        g_traceState = 2;
        return 0;
    }
    return 1;
}

void tracef(const char *fmt, ...)
{
    va_list ap;
    if (!g_trace)
        return;
    fprintf(g_trace, "%lu ", qpc_ms());
    va_start(ap, fmt);
    vfprintf(g_trace, fmt, ap);
    va_end(ap);
    fputc('\n', g_trace);
}

/* ---- memory patching ---------------------------------------------------- */
int mem_write(void *addr, const void *data, int len)
{
    DWORD old;
    if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy(addr, data, len);
    VirtualProtect(addr, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, len);
    return 1;
}

static int hex_decode(const char *hex, unsigned char *out, int max)
{
    int n = 0;
    while (*hex && n < max) {
        unsigned v;
        while (*hex == ' ')
            hex++;
        if (!*hex)
            break;
        if (sscanf(hex, "%2x", &v) != 1)
            return -1;
        out[n++] = (unsigned char)v;
        hex += 2;
    }
    return n;
}

int patch_bytes(DWORD addr, const char *expectHex, const char *replHex, const char *what)
{
    unsigned char exp[64], rep[64];
    int ne = hex_decode(expectHex, exp, 64), nr = hex_decode(replHex, rep, 64);
    if (ne <= 0 || ne != nr) {
        logf("PATCH %s: bad spec", what);
        return 0;
    }
    if (memcmp((void *)addr, exp, ne) != 0) {
        if (memcmp((void *)addr, rep, nr) == 0) {
            logf("PATCH %s @%08lx: already applied", what, addr);
            return 1;
        }
        logf("PATCH %s @%08lx: bytes differ, NOT applied (unknown exe version?)", what, addr);
        return 0;
    }
    mem_write((void *)addr, rep, nr);
    logf("PATCH %s @%08lx: applied", what, addr);
    return 1;
}

int redirect_call(DWORD callsite, void *newfn, void **orig, DWORD expectTarget, const char *what)
{
    unsigned char *p = (unsigned char *)callsite;
    DWORD target;
    LONG rel;
    if (p[0] != 0xE8) {
        logf("HOOK %s @%08lx: not a call, NOT applied", what, callsite);
        return 0;
    }
    target = callsite + 5 + *(LONG *)(p + 1);
    if (expectTarget && target != expectTarget) {
        logf("HOOK %s @%08lx: call target %08lx != %08lx, NOT applied", what, callsite, target, expectTarget);
        return 0;
    }
    if (orig)
        *orig = (void *)target;
    rel = (LONG)((DWORD)newfn - (callsite + 5));
    mem_write(p + 1, &rel, 4);
    logf("HOOK %s @%08lx: applied", what, callsite);
    return 1;
}

void **iat_slot(HMODULE mod, const char *dll, const char *name, WORD ordinal)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (!dir->VirtualAddress)
        return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *oft, *ft;
        if (_stricmp((char *)(base + imp->Name), dll) != 0)
            continue;
        oft = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        ft = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            if (IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal)) {
                if (ordinal && IMAGE_ORDINAL(oft->u1.Ordinal) == ordinal)
                    return (void **)&ft->u1.Function;
            } else if (name) {
                IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + oft->u1.AddressOfData);
                if (strcmp((char *)ibn->Name, name) == 0)
                    return (void **)&ft->u1.Function;
            }
        }
    }
    return NULL;
}

int iat_hook(HMODULE mod, const char *dll, const char *name, WORD ordinal, void *newfn, void **orig)
{
    void **slot = iat_slot(mod, dll, name, ordinal);
    if (!slot) {
        logf("IAT %s!%s(#%u): not found", dll, name ? name : "", ordinal);
        return 0;
    }
    if (orig)
        *orig = *slot;
    mem_write(slot, &newfn, sizeof(void *));
    if (g_cfg.verbose)
        logf("IAT %s!%s(#%u): hooked", dll, name ? name : "", ordinal);
    return 1;
}

/* ---- crash logging -------------------------------------------------------- */
static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    static int count;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    if (count++ < 20) {
        CONTEXT *c = ep->ContextRecord;
        DWORD *sp = (DWORD *)c->Esp;
        char buf[512];
        int n = 0, i, k = 0;
        for (i = 0; i < 512 && k < 16; i++) {
            DWORD v;
            if (IsBadReadPtr(sp + i, 4))
                break;
            v = sp[i];
            if ((v >= 0x401000 && v < 0x888000) || ((BYTE *)v >= (BYTE *)g_self && (BYTE *)v < (BYTE *)g_self + 0x40000)) {
                n += _snprintf(buf + n, sizeof(buf) - n, " %08lx", v);
                k++;
            }
        }
        logf("EXCEPTION %08lx at EIP=%08lx addr=%08lx EAX=%08lx ECX=%08lx EDX=%08lx ESI=%08lx EDI=%08lx stack:%s",
             code, c->Eip, ep->ExceptionRecord->NumberParameters > 1 ? (DWORD)ep->ExceptionRecord->ExceptionInformation[1] : 0,
             c->Eax, c->Ecx, c->Edx, c->Esi, c->Edi, buf);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void log_stack(const char *what, DWORD *sp)
{
    char buf[600];
    int n = 0, i, k = 0;
    for (i = 0; i < 1024 && k < 24; i++) {
        DWORD v;
        if (IsBadReadPtr(sp + i, 4))
            break;
        v = sp[i];
        if (v >= 0x401000 && v < 0x888000) {
            n += _snprintf(buf + n, sizeof(buf) - n, " %08lx", v);
            k++;
        }
    }
    buf[n] = 0;
    logf("%s stack:%s", what, buf);
}

static VOID(WINAPI *o_ExitProcess)(UINT);

static VOID WINAPI hk_ExitProcess(UINT code)
{
    DWORD marker;
    logf("ExitProcess(%u)", code);
    if (code != 0)
        log_stack("ExitProcess", &marker);
    o_ExitProcess(code);
}

void crashlog_install(void)
{
    AddVectoredExceptionHandler(1, veh);
    iat_hook(GetModuleHandleA(NULL), "KERNEL32.dll", "ExitProcess", 0, (void *)hk_ExitProcess,
             (void **)&o_ExitProcess);
}
