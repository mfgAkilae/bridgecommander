/* Input/focus isolation (test mode) and optional "input only when focused" fix. */
#include "common.h"
#include <string.h>

#define DISCL_EXCLUSIVE 0x1
#define DISCL_NONEXCLUSIVE 0x2
#define DISCL_FOREGROUND 0x4
#define DISCL_BACKGROUND 0x8

static HWND g_gameWnd;
static WNDPROC g_origWndProc;

/* ---- DirectInput COM vtable wrapping ------------------------------------ */
typedef HRESULT(WINAPI *PFN_SetCoop)(void *self, HWND, DWORD);
typedef HRESULT(WINAPI *PFN_GetState)(void *self, DWORD, void *);
typedef HRESULT(WINAPI *PFN_GetData)(void *self, DWORD, void *, DWORD *, DWORD);
typedef HRESULT(WINAPI *PFN_CreateDev)(void *self, const GUID *, void **, void *);
typedef HRESULT(WINAPI *PFN_CreateDevEx)(void *self, const GUID *, const IID *, void **, void *);

#define MAXVT 4
static struct {
    void **vt;
    PFN_SetCoop setCoop;
    PFN_GetState getState;
    PFN_GetData getData;
} g_dev[MAXVT];
static int g_ndev;
static PFN_CreateDev g_origCreateDev;
static PFN_CreateDevEx g_origCreateDevEx;

static int dev_index(void *self)
{
    void **vt = *(void ***)self;
    int i;
    for (i = 0; i < g_ndev; i++)
        if (g_dev[i].vt == vt)
            return i;
    return 0;
}

static int input_blocked(void)
{
    if (g_cfg.testMode && g_cfg.noInput)
        return 1;
    if (g_cfg.inputOnlyFocused && g_gameWnd) {
        HWND fg = GetForegroundWindow();
        if (fg != g_gameWnd)
            return 1;
    }
    return 0;
}

static HRESULT WINAPI hk_SetCoop(void *self, HWND w, DWORD flags)
{
    int i = dev_index(self);
    DWORD nf = flags;
    if (g_cfg.testMode && g_cfg.inputIsolation)
        nf = DISCL_NONEXCLUSIVE | DISCL_BACKGROUND;
    logf("DInput SetCooperativeLevel(%p, %lx -> %lx)", (void *)w, flags, nf);
    return g_dev[i].setCoop(self, w, nf);
}

static HRESULT WINAPI hk_GetState(void *self, DWORD cb, void *data)
{
    int i = dev_index(self);
    HRESULT r = g_dev[i].getState(self, cb, data);
    if (input_blocked() && data)
        memset(data, 0, cb);
    return r;
}

static HRESULT WINAPI hk_GetData(void *self, DWORD cbObj, void *rg, DWORD *inout, DWORD flags)
{
    int i = dev_index(self);
    HRESULT r = g_dev[i].getData(self, cbObj, rg, inout, flags);
    if (input_blocked() && inout)
        *inout = 0;
    return r;
}

static void wrap_device(void *dev)
{
    void **vt = *(void ***)dev;
    void *p;
    int i;
    for (i = 0; i < g_ndev; i++)
        if (g_dev[i].vt == vt)
            return;
    if (g_ndev >= MAXVT)
        return;
    g_dev[g_ndev].vt = vt;
    g_dev[g_ndev].getState = (PFN_GetState)vt[9];
    g_dev[g_ndev].getData = (PFN_GetData)vt[10];
    g_dev[g_ndev].setCoop = (PFN_SetCoop)vt[13];
    g_ndev++;
    p = (void *)hk_GetState;
    mem_write(&vt[9], &p, 4);
    p = (void *)hk_GetData;
    mem_write(&vt[10], &p, 4);
    p = (void *)hk_SetCoop;
    mem_write(&vt[13], &p, 4);
    logf("DInput device vtable %p wrapped", (void *)vt);
}

static HRESULT WINAPI hk_CreateDev(void *self, const GUID *g, void **out, void *outer)
{
    HRESULT r = g_origCreateDev(self, g, out, outer);
    if (SUCCEEDED(r) && out && *out)
        wrap_device(*out);
    return r;
}

static HRESULT WINAPI hk_CreateDevEx(void *self, const GUID *g, const IID *iid, void **out, void *outer)
{
    HRESULT r = g_origCreateDevEx(self, g, iid, out, outer);
    if (SUCCEEDED(r) && out && *out)
        wrap_device(*out);
    return r;
}

HRESULT input_wrap_dinput(void *pDI)
{
    void **vt = *(void ***)pDI;
    void *p;
    if (!(g_cfg.testMode || g_cfg.inputOnlyFocused))
        return S_OK;
    if (!g_origCreateDev) {
        g_origCreateDev = (PFN_CreateDev)vt[3];
        g_origCreateDevEx = (PFN_CreateDevEx)vt[9];
        p = (void *)hk_CreateDev;
        mem_write(&vt[3], &p, 4);
        p = (void *)hk_CreateDevEx;
        mem_write(&vt[9], &p, 4);
    }
    return S_OK;
}

/* ---- user32 hooks (test mode isolation) --------------------------------- */
typedef HWND(WINAPI *PFN_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static PFN_CreateWindowExA o_CreateWindowExA;
static BOOL(WINAPI *o_ShowWindow)(HWND, int);

static LRESULT CALLBACK sub_WndProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (g_cfg.testMode && g_cfg.inputIsolation) {
        switch (m) {
        case WM_ACTIVATEAPP:
            wp = TRUE;
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE)
                wp = MAKEWPARAM(WA_ACTIVE, HIWORD(wp));
            break;
        case WM_KILLFOCUS:
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        }
    }
    return CallWindowProcA(g_origWndProc, w, m, wp, lp);
}

static HWND WINAPI hk_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int cx, int cy,
                                      HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    HWND w;
    if (!parent && g_cfg.testMode) {
        if (g_cfg.windowX >= 0)
            x = g_cfg.windowX;
        if (g_cfg.windowY >= 0)
            y = g_cfg.windowY;
        if (g_cfg.inputIsolation)
            ex |= WS_EX_NOACTIVATE;
        ex &= ~WS_EX_TOPMOST;
    }
    w = o_CreateWindowExA(ex, cls, title, style, x, y, cx, cy, parent, menu, inst, param);
    if (w && !parent && !g_gameWnd) {
        g_gameWnd = w;
        logf("game window %p class=%s", (void *)w, (cls && ((DWORD)cls >> 16)) ? cls : "#atom");
        if (g_cfg.testMode && g_cfg.inputIsolation) {
            g_origWndProc = (WNDPROC)SetWindowLongPtrA(w, GWLP_WNDPROC, (LONG_PTR)sub_WndProc);
            PostMessageA(w, WM_ACTIVATEAPP, TRUE, 0);
            PostMessageA(w, WM_ACTIVATE, WA_ACTIVE, 0);
        }
    }
    return w;
}

static BOOL WINAPI hk_ShowWindow(HWND w, int cmd)
{
    if (g_cfg.testMode && g_cfg.inputIsolation) {
        if (cmd == SW_SHOW || cmd == SW_SHOWNORMAL || cmd == SW_RESTORE)
            cmd = SW_SHOWNOACTIVATE;
        else if (cmd == SW_SHOWMAXIMIZED)
            cmd = SW_SHOWNA;
    }
    return o_ShowWindow(w, cmd);
}

static BOOL WINAPI hk_ClipCursor(const RECT *r) { (void)r; return TRUE; }
static BOOL WINAPI hk_SetCursorPos(int x, int y) { (void)x; (void)y; return TRUE; }
static HWND WINAPI hk_SetCapture(HWND w) { (void)w; return NULL; }
static BOOL WINAPI hk_SetForegroundWindow(HWND w) { (void)w; return TRUE; }
static HWND WINAPI hk_SetActiveWindow(HWND w) { (void)w; return g_gameWnd; }
static HWND WINAPI hk_GetForegroundWindow(void) { return g_gameWnd ? g_gameWnd : GetForegroundWindow(); }
static HWND WINAPI hk_GetActiveWindow(void) { return g_gameWnd ? g_gameWnd : GetActiveWindow(); }

static void *WINAPI hk_BinkOpen(const char *name, unsigned flags)
{
    logf("movie skipped: %s", name ? name : "?");
    (void)flags;
    return NULL;
}

void input_install(void)
{
    HMODULE exe = GetModuleHandleA(NULL);
    if (g_cfg.testMode && g_cfg.noMovies)
        iat_hook(exe, "binkw32.dll", "_BinkOpen@8", 0, (void *)hk_BinkOpen, NULL);
    iat_hook(exe, "USER32.dll", "CreateWindowExA", 0, (void *)hk_CreateWindowExA, (void **)&o_CreateWindowExA);
    if (!(g_cfg.testMode && g_cfg.inputIsolation))
        return;
    iat_hook(exe, "USER32.dll", "ShowWindow", 0, (void *)hk_ShowWindow, (void **)&o_ShowWindow);
    iat_hook(exe, "USER32.dll", "ClipCursor", 0, (void *)hk_ClipCursor, NULL);
    iat_hook(exe, "USER32.dll", "SetCursorPos", 0, (void *)hk_SetCursorPos, NULL);
    iat_hook(exe, "USER32.dll", "SetCapture", 0, (void *)hk_SetCapture, NULL);
    iat_hook(exe, "USER32.dll", "SetForegroundWindow", 0, (void *)hk_SetForegroundWindow, NULL);
    iat_hook(exe, "USER32.dll", "SetActiveWindow", 0, (void *)hk_SetActiveWindow, NULL);
    iat_hook(exe, "USER32.dll", "GetForegroundWindow", 0, (void *)hk_GetForegroundWindow, NULL);
    iat_hook(exe, "USER32.dll", "GetActiveWindow", 0, (void *)hk_GetActiveWindow, NULL);
    logf("input isolation installed");
}
