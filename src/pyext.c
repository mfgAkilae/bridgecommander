/* Tiny Python 1.5 extension module "bcmpdll" registered from the DLL, using the
 * interpreter statically linked into stbc.exe. Gives scripts a reliable output
 * channel (the game's Python cannot write files / stdout is swallowed). */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADDR_PY_INITMODULE4 0x0074D140
#define ADDR_PYARG_PARSETUPLE 0x0074E310
#define ADDR_PY_NONE 0x0095DD20
#define PYTHON_API_VERSION 1007

typedef void PyObject;
typedef PyObject *(__cdecl *PyCFunction)(PyObject *self, PyObject *args);
typedef struct {
    const char *ml_name;
    PyCFunction ml_meth;
    int ml_flags;
    const char *ml_doc;
} PyMethodDef;

typedef PyObject *(__cdecl *PFN_InitModule4)(const char *, PyMethodDef *, const char *, PyObject *, int);
typedef int(__cdecl *PFN_ParseTuple)(PyObject *, const char *, ...);

static FILE *g_tlog;
static FILE *g_pyout;

static PyObject *py_none(void)
{
    (*(int *)ADDR_PY_NONE)++;
    return (PyObject *)ADDR_PY_NONE;
}

static FILE *open_env(const char *var, const char *def)
{
    const char *p = getenv(var);
    char path[MAX_PATH];
    if (p && *p)
        lstrcpynA(path, p, MAX_PATH);
    else
        _snprintf(path, MAX_PATH, "%s\\%s", g_gameDir, def);
    return fopen(path, "a");
}

/* bcmpdll.log(str): append one line to the telemetry log */
static PyObject *__cdecl m_log(PyObject *self, PyObject *args)
{
    const char *s = NULL;
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "s", &s))
        return NULL;
    if (!g_tlog)
        g_tlog = open_env("BCMP_TLOG", "bcmp_telemetry.txt");
    if (g_tlog) {
        fputs(s, g_tlog);
        fputc('\n', g_tlog);
        fflush(g_tlog);
    }
    return py_none();
}

/* bcmpdll.write(str): raw text, used as sys.stdout/sys.stderr replacement */
static PyObject *__cdecl m_write(PyObject *self, PyObject *args)
{
    const char *s = NULL;
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "s", &s))
        return NULL;
    if (!g_pyout)
        g_pyout = open_env("BCMPFIX_PYLOG", "bcmpfix_python.log");
    if (g_pyout) {
        fputs(s, g_pyout);
        fflush(g_pyout);
    }
    return py_none();
}

/* bcmpdll.dlllog(str): line into the DLL log */
static PyObject *__cdecl m_dlllog(PyObject *self, PyObject *args)
{
    const char *s = NULL;
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "s", &s))
        return NULL;
    logf("py: %s", s);
    return py_none();
}

#define ADDR_PYSTRING_FROMSTRING 0x007506C0
typedef PyObject *(__cdecl *PFN_StrFromStr)(const char *);

/* bcmpdll.getenv(name): process environment variable or None */
static PyObject *__cdecl m_getenv(PyObject *self, PyObject *args)
{
    const char *s = NULL;
    char buf[1024];
    DWORD n;
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "s", &s))
        return NULL;
    n = GetEnvironmentVariableA(s, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
        return py_none();
    return ((PFN_StrFromStr)ADDR_PYSTRING_FROMSTRING)(buf);
}

#define ADDR_PYINT_FROMLONG 0x0074C6E0
typedef PyObject *(__cdecl *PFN_IntFromLong)(long);

/* bcmpdll.ms(): QPC milliseconds since boot as an exact integer. Python floats are
 * computed with the x87 FPU in single precision after D3D init, so time.time() is
 * useless for fine timing; QPC is shared by all processes on the machine. */
static PyObject *__cdecl m_ms(PyObject *self, PyObject *args)
{
    LARGE_INTEGER c, f;
    (void)self;
    (void)args;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return ((PFN_IntFromLong)ADDR_PYINT_FROMLONG)((long)((c.QuadPart / (f.QuadPart / 1000)) & 0x7fffffff));
}

/* bcmpdll.quit(code): terminate the process (test harness end) */
static PyObject *__cdecl m_quit(PyObject *self, PyObject *args)
{
    int code = 0;
    (void)self;
    ((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "|i", &code);
    logf("quit requested by script (%d)", code);
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    return py_none();
}

/* bcmpdll.allowhit(torpedoObjID, mode=0[, x, y, z]): let this torpedo hit ships on an
 * MP client; x, y, z = replay start point (start of the hit-test segment) */
static PyObject *__cdecl m_allowhit(PyObject *self, PyObject *args)
{
    int id = 0, mode = 0;
    float x = 1e30f, y = 0.0f, z = 0.0f; /* x stays 1e30 without a start point */
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "i|ifff", &id, &mode, &x, &y, &z))
        return NULL;
    if (x < 1e29f)
        torpedo_authorize_start((DWORD)id, mode, x, y, z);
    else
        torpedo_authorize_mode((DWORD)id, mode);
    return py_none();
}

/* bcmpdll.setlead(shipObjID, ms): latency compensation for updates about this ship */
static PyObject *__cdecl m_setlead(PyObject *self, PyObject *args)
{
    int id = 0, ms = 0;
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "ii", &id, &ms))
        return NULL;
    lead_set((DWORD)id, ms);
    return py_none();
}

/* SWIG 1.1 pointer strings look like "_<hex>_p_Type" */
static void *swig_ptr(const char *s)
{
    unsigned long v = 0;
    if (!s || *s != '_')
        return NULL;
    for (s++; *s; s++) {
        int c = *s;
        if (c >= '0' && c <= '9')
            v = v * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f')
            v = v * 16 + (c - 'a' + 10);
        else
            break;
    }
    return (void *)v;
}

/* bcmpdll.forcehit(torpedo.this, ship.this): apply a host-confirmed torpedo hit
 * locally through the stock collision code (visuals); returns 1 if spent */
static PyObject *__cdecl m_forcehit(PyObject *self, PyObject *args)
{
    const char *t = NULL, *s = NULL;
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "ss", &t, &s))
        return NULL;
    return ((PFN_IntFromLong)ADDR_PYINT_FROMLONG)(torpedo_force(swig_ptr(t), swig_ptr(s)));
}

/* bcmpdll.authority(on): enable client-side torpedo hit suppression */
static PyObject *__cdecl m_authority(PyObject *self, PyObject *args)
{
    int on = 0;
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "i", &on))
        return NULL;
    torpedo_set_active(on && g_cfg.torpedoAuthority);
    return py_none();
}

/* bcmpdll.retire(torpedo.this): remove a replayed torpedo from play */
static PyObject *__cdecl m_retire(PyObject *self, PyObject *args)
{
    const char *t = NULL;
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "s", &t))
        return NULL;
    torpedo_retire(swig_ptr(t));
    return py_none();
}

/* bcmpdll.torpstats(): "suppressed allowed" counters of the torpedo authority hook */
static PyObject *__cdecl m_torpstats(PyObject *self, PyObject *args)
{
    unsigned long s, a;
    char buf[64];
    (void)self;
    (void)args;
    torpedo_stats(&s, &a);
    _snprintf(buf, sizeof(buf), "%lu %lu", s, a);
    return ((PFN_StrFromStr)ADDR_PYSTRING_FROMSTRING)(buf);
}

/* bcmpdll.config(key): integer value of a [Fixes] option (scripts follow the INI) */
static PyObject *__cdecl m_config(PyObject *self, PyObject *args)
{
    const char *k = NULL;
    long v = -1;
    (void)self;
    if (!((PFN_ParseTuple)ADDR_PYARG_PARSETUPLE)(args, "s", &k))
        return NULL;
    if (!lstrcmpiA(k, "TorpedoHitAuthority"))
        v = g_cfg.torpedoAuthority;
    else if (!lstrcmpiA(k, "LatencyCompensation"))
        v = g_cfg.latencyComp;
    else if (!lstrcmpiA(k, "TestMode"))
        v = g_cfg.testMode;
    else if (!lstrcmpiA(k, "Verbose"))
        v = g_cfg.verbose;
    return ((PFN_IntFromLong)ADDR_PYINT_FROMLONG)(v);
}

static PyMethodDef g_methods[] = {
    {"allowhit", m_allowhit, 1, NULL},
    {"forcehit", m_forcehit, 1, NULL},
    {"retire", m_retire, 1, NULL},
    {"authority", m_authority, 1, NULL},
    {"setlead", m_setlead, 1, NULL},
    {"torpstats", m_torpstats, 1, NULL},
    {"config", m_config, 1, NULL},
    {"ms", m_ms, 1, NULL},
    {"quit", m_quit, 1, NULL},
    {"getenv", m_getenv, 1, NULL},
    {"log", m_log, 1, NULL},
    {"write", m_write, 1, NULL},
    {"dlllog", m_dlllog, 1, NULL},
    {NULL, NULL, 0, NULL},
};

int pyext_init(void)
{
    PyObject *m = ((PFN_InitModule4)ADDR_PY_INITMODULE4)("bcmpdll", g_methods, NULL, NULL, PYTHON_API_VERSION);
    logf("python module bcmpdll %s", m ? "registered" : "FAILED");
    return m != NULL;
}
