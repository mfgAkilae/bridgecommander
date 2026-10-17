/* bcmpfix: dinput.dll proxy that loads the real system dinput.dll and patches stbc.exe */
#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static HMODULE g_real;

static FARPROC real_proc(const char *name)
{
    if (!g_real) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        lstrcatA(path, "\\dinput.dll");
        g_real = LoadLibraryA(path);
        if (!g_real) {
            logf("FATAL: cannot load %s (err %lu)", path, GetLastError());
            return NULL;
        }
    }
    return GetProcAddress(g_real, name);
}

typedef HRESULT(WINAPI *PFN_DICreateEx)(HINSTANCE, DWORD, REFIID, LPVOID *, void *);
typedef HRESULT(WINAPI *PFN_DICreate)(HINSTANCE, DWORD, LPVOID *, void *);
typedef HRESULT(WINAPI *PFN_DllGetClassObject)(REFCLSID, REFIID, LPVOID *);
typedef HRESULT(WINAPI *PFN_Void)(void);

HRESULT WINAPI DirectInputCreateEx(HINSTANCE h, DWORD ver, REFIID riid, LPVOID *ppv, void * outer)
{
    PFN_DICreateEx f = (PFN_DICreateEx)real_proc("DirectInputCreateEx");
    HRESULT r;
    if (!f)
        return E_FAIL;
    r = f(h, ver, riid, ppv, outer);
    logf("DirectInputCreateEx(ver=%lx) = %lx", ver, r);
    if (SUCCEEDED(r) && ppv && *ppv)
        input_wrap_dinput(*ppv);
    return r;
}

HRESULT WINAPI DirectInputCreateA(HINSTANCE h, DWORD ver, LPVOID *ppv, void * outer)
{
    PFN_DICreate f = (PFN_DICreate)real_proc("DirectInputCreateA");
    return f ? f(h, ver, ppv, outer) : E_FAIL;
}

HRESULT WINAPI DirectInputCreateW(HINSTANCE h, DWORD ver, LPVOID *ppv, void * outer)
{
    PFN_DICreate f = (PFN_DICreate)real_proc("DirectInputCreateW");
    return f ? f(h, ver, ppv, outer) : E_FAIL;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    PFN_Void f = (PFN_Void)real_proc("DllCanUnloadNow");
    return f ? f() : S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID i, LPVOID *p)
{
    PFN_DllGetClassObject f = (PFN_DllGetClassObject)real_proc("DllGetClassObject");
    return f ? f(c, i, p) : E_FAIL;
}

HRESULT WINAPI DllRegisterServer(void)
{
    PFN_Void f = (PFN_Void)real_proc("DllRegisterServer");
    return f ? f() : E_FAIL;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    PFN_Void f = (PFN_Void)real_proc("DllUnregisterServer");
    return f ? f() : E_FAIL;
}

static int is_stbc_11(void)
{
    /* identify the exe by bytes at the patched sites rather than by file hash */
    const unsigned char sig[] = {0x8b, 0xce, 0xe8, 0x80, 0x00, 0x00, 0x00}; /* 0x0069edc9 */
    __try {
        return memcmp((void *)0x0069edc9, sig, sizeof(sig)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        char *slash;
        g_self = h;
        DisableThreadLibraryCalls(h);
        GetModuleFileNameA(NULL, g_gameDir, MAX_PATH);
        slash = strrchr(g_gameDir, '\\');
        if (slash)
            *slash = 0;
        log_open();
        config_load();
        crashlog_install();
        logf("bcmpfix loaded into pid %lu, game dir %s", GetCurrentProcessId(), g_gameDir);
        if (!is_stbc_11()) {
            logf("stbc.exe signature mismatch - only proxying dinput, no patches");
            return TRUE;
        }
        if (g_cfg.testMode || g_cfg.inputOnlyFocused)
            input_install();
        if (!guard_install()) {
            logf("SECURITY FATAL: required network guard hook or patch failed; refusing startup");
            return FALSE;
        }
        net_install();
        netfix_install();
        torpedo_install();
        torpedo_trace_install();
        lead_install();
        relay_install();
        scripterr_install();
        join_install();
        session_install();
        phaser_install();
    }
    return TRUE;
}
