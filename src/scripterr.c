/* Python exceptions in game script handlers.
 *
 * After a script call raised, the dispatcher 0x006F9B80 checks the app's fullscreen
 * flag (app+0x2C, copied to 0x0099ADD6 at start-up). In fullscreen it prints the
 * traceback and continues. In windowed mode it calls the "TG Python Exception Debug
 * Console" (0x006F9470): AllocConsole, show the traceback that was written to
 * sys.stderr, then wait until somebody types 'resume' or 'abort' into that console.
 * The game freezes meanwhile; in a multiplayer game the player drops out.
 *
 * With NoScriptErrorConsole=1 the console call is replaced: the traceback goes to
 * bcmpfix.log and the game continues, as in fullscreen. */
#include "common.h"

#define ADDR_CALL_CONSOLE 0x006F9B9B /* call 0x006F9470 in the dispatcher */
#define ADDR_CONSOLE 0x006F9470
#define ADDR_GET_STDOUT 0x006F9390 /* cached sys.stdout */
#define ADDR_GET_STDERR 0x006F93B0 /* cached sys.stderr */
#define ADDR_CALLMETHOD 0x0075B0A0 /* PyObject_CallMethod(obj, name, fmt) */
#define ADDR_STR_ASSTRING 0x007507D0
#define ADDR_ERR_CLEAR 0x00753140
#define ADDR_ERR_PRINT 0x0074AF10 /* PyErr_Print, as in the fullscreen path */

typedef int *(__cdecl *PFN_GetObj)(void);
typedef int *(__cdecl *PFN_CallMethod)(int *obj, const char *name, const char *fmt);
typedef const char *(__cdecl *PFN_AsString)(int *obj);
typedef void(__cdecl *PFN_Void)(void);
typedef void(__cdecl *PFN_Dealloc)(int *obj);

static void *o_Console;
static unsigned long g_errors;
static size_t g_logged[2]; /* part of sys.stderr / sys.stdout already written to the log */

static void decref(int *o)
{
    if (o && --o[0] == 0)
        (*(PFN_Dealloc *)((BYTE *)o[1] + 0x18))(o);
}

/* new text in a StringIO-like stream since the last call; 0 if it has no getvalue() */
static int new_text(int *stream, size_t *logged, char *out, size_t outlen)
{
    int *v;
    const char *s;
    size_t n;
    if (!stream)
        return 0;
    v = ((PFN_CallMethod)ADDR_CALLMETHOD)(stream, "getvalue", NULL);
    if (!v) {
        ((PFN_Void)ADDR_ERR_CLEAR)();
        return 0;
    }
    s = ((PFN_AsString)ADDR_STR_ASSTRING)(v);
    n = s ? strlen(s) : 0;
    if (n < *logged) /* stream was reset */
        *logged = 0;
    lstrcpynA(out, s ? s + *logged : "", (int)outlen);
    *logged = n;
    decref(v);
    return 1;
}

static void __cdecl hk_Console(void)
{
    int *err = ((PFN_GetObj)ADDR_GET_STDERR)();
    int *out = ((PFN_GetObj)ADDR_GET_STDOUT)();
    char text[2048] = "";
    int got;
    g_errors++;
    /* the console would print the pending exception into the script's stderr
       (a StringIO) and show it; print it and log it instead */
    ((PFN_Void)ADDR_ERR_PRINT)();
    got = new_text(err, &g_logged[0], text, sizeof(text));
    if (!text[0] && out != err)
        got |= new_text(out, &g_logged[1], text, sizeof(text));
    logf("python exception in a game script (debug console suppressed, game continues)%s\n%s",
         got ? ":" : " - traceback went to the script's stderr", text);
}

unsigned long scripterr_count(void)
{
    return g_errors;
}

/* Join race in the mission menus (RebuildInfoPane before the info pane exists):
 * bcmpfix_mp.GuardInfoPane() wraps RebuildInfoPane. The failing handler often
 * imports the menus module itself right before the call, so the guard must run at
 * import time. Hooked: the game's __import__ (0x00753410) calling
 * PyImport_ImportModuleEx (0x0075BC40). */
#define ADDR_CALL_IMPORTEX 0x007534AE
#define ADDR_IMPORTEX 0x0075BC40
#define ADDR_PYRUN 0x0074AE80

typedef int *(__cdecl *PFN_ImportEx)(const char *name, int *globals, int *locals, int *fromlist);
typedef int(__cdecl *PFN_Run)(const char *code);

static void *o_ImportEx;
static int g_inImportHook;

int g_pythonUp; /* the game's __import__ ran: Python is initialised */

static int *__cdecl hk_ImportEx(const char *name, int *globals, int *locals, int *fromlist)
{
    int *m = ((PFN_ImportEx)o_ImportEx)(name, globals, locals, fromlist);
    g_pythonUp = 1;
    if (m && !g_inImportHook && name && strstr(name, "Menus")) {
        g_inImportHook = 1;
        ((PFN_Run)ADDR_PYRUN)("import bcmpfix_mp\nbcmpfix_mp.GuardInfoPane()\n");
        g_inImportHook = 0;
    }
    return m;
}

void scripterr_install(void)
{
    if (g_cfg.noScriptConsole)
        redirect_call(ADDR_CALL_CONSOLE, (void *)hk_Console, &o_Console, ADDR_CONSOLE,
                      "no blocking Python debug console in windowed mode");
    if (g_cfg.mpScript)
        redirect_call(ADDR_CALL_IMPORTEX, (void *)hk_ImportEx, &o_ImportEx, ADDR_IMPORTEX,
                      "guard mission menus at import (join race)");
}
