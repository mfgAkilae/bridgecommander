/* BC_MP_fix.exe - installer for bcmpfix, the Star Trek: Bridge Commander 1.1 multiplayer fix.
 *
 * Finds the game folder, checks stbc.exe and installs or removes the mod files, which are
 * embedded as resources (setup.rc).
 *
 *   BC_MP_fix.exe [<game folder or stbc.exe>] [--install | --uninstall] [--quiet] [--force]
 *                 [--log <file>]
 *
 * Without a folder it looks next to itself, in the current folder and in the registry, and
 * asks otherwise. Without --install/--uninstall it asks what to do. --quiet shows no dialogs;
 * --force installs even if stbc.exe is not the supported version.
 * Exit codes: 0 done, 1 error, 2 cancelled, 3 unsupported stbc.exe (quiet mode without --force).
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "setup_res.h"
#include "version.h"

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)
#define VERSION_W WIDEN(BCMPFIX_VERSION)
#define TITLE L"Bridge Commander multiplayer fix " VERSION_W

#define EXIT_OK 0
#define EXIT_ERROR 1
#define EXIT_CANCEL 2
#define EXIT_UNSUPPORTED 3

#define BACKUP_DLL L"dinput.dll.bcmpfix_backup"
#define PATHLEN 1024

/* Code locations the fix patches, with the bytes stbc.exe 1.1 has there (from src/). */
static const struct {
    DWORD va;
    const char *hex;
} k_sites[] = {
    {0x0069EDC9, "8b ce e8 80 00 00 00"}, /* SendStateUpdates call site (DLL signature check) */
    {0x005B18D7, "75 0c"},                /* absolute position check */
    {0x005B24C6, "d8 86 9c 00 00 00"},    /* ship reader delta base */
    {0x005A2E62, "d8 86 9c 00 00 00"},    /* base reader delta base */
    {0x0047DD06, "d9 5e 2c"},             /* NetworkAI first-frame time */
    {0x0047E20E, "d8 1d a8 87 88 00"},    /* NetworkAI dead band */
    {0x005A23BD, "cd cc cc 3d"},          /* state update interval */
    {0x006B82D4, "b9 00 00 80 3f"},       /* resend interval */
    {0x00579010, "83 ec 28 53 55"},       /* torpedo DetectCollision prologue */
    {0x005B21C0, "6a ff 68 d8 b9 87 00"}, /* state update reader prologue */
};

static int g_quiet;
static FILE *g_log;
static wchar_t g_out[8192]; /* messages for the final dialog */

/* ---- output ---------------------------------------------------------------- */

static void say(const wchar_t *fmt, ...)
{
    wchar_t line[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(line, 1023, fmt, ap);
    line[1023] = 0;
    va_end(ap);
    if (wcslen(g_out) + wcslen(line) + 2 < sizeof(g_out) / sizeof(g_out[0])) {
        wcscat(g_out, line);
        wcscat(g_out, L"\n");
    }
    if (g_log) {
        char u[4096];
        int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, sizeof(u), NULL, NULL);
        if (n > 0) {
            fputs(u, g_log);
            fputc('\n', g_log);
            fflush(g_log);
        }
    }
}

static void dialog(const wchar_t *heading, const wchar_t *text, PCWSTR icon)
{
    TASKDIALOGCONFIG tc;
    if (g_quiet)
        return;
    memset(&tc, 0, sizeof(tc));
    tc.cbSize = sizeof(tc);
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    tc.dwCommonButtons = TDCBF_OK_BUTTON;
    tc.pszWindowTitle = TITLE;
    tc.pszMainIcon = icon;
    tc.pszMainInstruction = heading;
    tc.pszContent = text;
    TaskDialogIndirect(&tc, NULL, NULL, NULL);
}

/* ---- files ------------------------------------------------------------------- */

static void join(wchar_t *out, const wchar_t *dir, const wchar_t *rel)
{
    _snwprintf(out, PATHLEN - 1, L"%s\\%s", dir, rel);
    out[PATHLEN - 1] = 0;
}

static int file_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int is_game_dir(const wchar_t *dir)
{
    wchar_t p[PATHLEN];
    join(p, dir, L"stbc.exe");
    return file_exists(p);
}

static BYTE *read_file(const wchar_t *p, DWORD *size)
{
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    BYTE *buf;
    DWORD n = 0;
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    *size = GetFileSize(h, NULL);
    buf = (BYTE *)malloc(*size + 1);
    if (!buf || !ReadFile(h, buf, *size, &n, NULL) || n != *size) {
        free(buf);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    return buf;
}

/* write via a temporary file so a failed write never leaves half a file behind */
static DWORD write_file(const wchar_t *p, const void *data, DWORD size)
{
    wchar_t tmp[PATHLEN];
    HANDLE h;
    DWORD n = 0, err;
    _snwprintf(tmp, PATHLEN - 1, L"%s.bcmpfix_tmp", p);
    tmp[PATHLEN - 1] = 0;
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!WriteFile(h, data, size, &n, NULL) || n != size) {
        err = GetLastError();
        CloseHandle(h);
        DeleteFileW(tmp);
        return err ? err : ERROR_WRITE_FAULT;
    }
    CloseHandle(h);
    if (!MoveFileExW(tmp, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        err = GetLastError();
        DeleteFileW(tmp);
        return err;
    }
    return 0;
}

/* the payload as <p>.bcmpfix_tmp, for a later commit (MoveFileEx onto p) */
static DWORD stage_file(const wchar_t *p, const void *data, DWORD size, wchar_t *tmp)
{
    HANDLE h;
    DWORD n = 0, err;
    _snwprintf(tmp, PATHLEN - 1, L"%s.bcmpfix_tmp", p);
    tmp[PATHLEN - 1] = 0;
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!WriteFile(h, data, size, &n, NULL) || n != size) {
        err = GetLastError();
        CloseHandle(h);
        DeleteFileW(tmp);
        return err ? err : ERROR_WRITE_FAULT;
    }
    CloseHandle(h);
    return 0;
}

static const void *resource(int id, DWORD *size)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    HGLOBAL g;
    if (!r)
        return NULL;
    g = LoadResource(NULL, r);
    *size = SizeofResource(NULL, r);
    return g ? LockResource(g) : NULL;
}

/* our dinput.dll carries the name of its INI file; other mods' proxies do not */
static int is_our_dll(const wchar_t *p)
{
    DWORD size, i;
    BYTE *b = read_file(p, &size);
    static const char mark[] = "bcmpfix.ini";
    int found = 0;
    if (!b)
        return 0;
    for (i = 0; i + sizeof(mark) - 1 <= size && !found; i++)
        found = memcmp(b + i, mark, sizeof(mark) - 1) == 0;
    free(b);
    return found;
}

static const wchar_t *errtext(DWORD err)
{
    static wchar_t buf[512];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, buf, 511, NULL);
    while (n > 0 && (buf[n - 1] == L'\n' || buf[n - 1] == L'\r' || buf[n - 1] == L'.'))
        buf[--n] = 0;
    if (!n)
        _snwprintf(buf, 511, L"error %lu", err);
    return buf;
}

/* ---- checks ------------------------------------------------------------------ */

/* Returns the number of patch sites that hold the expected bytes (all = supported). */
static int check_exe(const wchar_t *dir, int *total)
{
    wchar_t p[PATHLEN];
    DWORD size, pe, base, i;
    WORD nsec, optsz;
    BYTE *d;
    int ok = 0;
    *total = sizeof(k_sites) / sizeof(k_sites[0]);
    join(p, dir, L"stbc.exe");
    d = read_file(p, &size);
    if (!d)
        return -1;
    if (size < 0x40 || d[0] != 'M' || d[1] != 'Z')
        goto out;
    pe = *(DWORD *)(d + 0x3c);
    if (pe + 24 + 96 > size || memcmp(d + pe, "PE\0\0", 4) != 0 || *(WORD *)(d + pe + 24) != 0x10b)
        goto out;
    nsec = *(WORD *)(d + pe + 6);
    optsz = *(WORD *)(d + pe + 20);
    base = *(DWORD *)(d + pe + 24 + 28);
    for (i = 0; i < (DWORD)*total; i++) {
        DWORD rva = k_sites[i].va - base, s;
        for (s = 0; s < nsec; s++) {
            BYTE *sh = d + pe + 24 + optsz + s * 40;
            DWORD vs, va, rs, rp;
            if ((DWORD)(sh - d) + 40 > size)
                break;
            vs = *(DWORD *)(sh + 8);
            va = *(DWORD *)(sh + 12);
            rs = *(DWORD *)(sh + 16);
            rp = *(DWORD *)(sh + 20);
            if (rva >= va && rva < va + (vs > rs ? vs : rs)) {
                DWORD off = rp + (rva - va);
                const char *h = k_sites[i].hex;
                int match = 1;
                while (*h && match) {
                    unsigned v = (unsigned)strtoul(h, NULL, 16);
                    if (off >= size || d[off] != (BYTE)v)
                        match = 0;
                    off++;
                    h += 2;
                    while (*h == ' ')
                        h++;
                }
                ok += match;
                break;
            }
        }
    }
out:
    free(d);
    return ok;
}

static int game_running(void)
{
    PROCESSENTRY32W pe;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int found = 0;
    if (s == INVALID_HANDLE_VALUE)
        return 0;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(s, &pe)) {
        do {
            if (!_wcsicmp(pe.szExeFile, L"stbc.exe"))
                found = 1;
        } while (!found && Process32NextW(s, &pe));
    }
    CloseHandle(s);
    return found;
}

/* 0 if we can create files in the folder and in its scripts subfolder */
static DWORD can_write(const wchar_t *dir)
{
    const wchar_t *sub[2] = {L"bcmpfix_write_test.tmp", L"scripts\\bcmpfix_write_test.tmp"};
    int i;
    for (i = 0; i < 2; i++) {
        wchar_t p[PATHLEN];
        HANDLE h;
        join(p, dir, sub[i]);
        if (i == 1) {
            wchar_t sd[PATHLEN];
            join(sd, dir, L"scripts");
            if (!dir_exists(sd))
                continue; /* created during install */
        }
        h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return GetLastError();
        CloseHandle(h);
    }
    return 0;
}

static int is_elevated(void)
{
    HANDLE t;
    TOKEN_ELEVATION e;
    DWORD n = 0;
    int r = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
        if (GetTokenInformation(t, TokenElevation, &e, sizeof(e), &n))
            r = e.TokenIsElevated != 0;
        CloseHandle(t);
    }
    return r;
}

/* ---- finding the game -------------------------------------------------------- */

static void normalize(wchar_t *dir)
{
    wchar_t full[PATHLEN];
    size_t n;
    DWORD r = GetFullPathNameW(dir, PATHLEN, full, NULL);
    if (r > 0 && r < PATHLEN) /* a longer result did not fit and is not in full */
        wcscpy(dir, full);
    n = wcslen(dir);
    while (n > 3 && dir[n - 1] == L'\\')
        dir[--n] = 0;
}

static int reg_path(HKEY root, const wchar_t *key, const wchar_t *value, REGSAM view, wchar_t *out)
{
    HKEY k;
    DWORD type, size = PATHLEN * sizeof(wchar_t);
    int ok = 0;
    if (RegOpenKeyExW(root, key, 0, KEY_READ | view, &k) != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueExW(k, value, NULL, &type, (BYTE *)out, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        out[PATHLEN - 1] = 0;
        normalize(out);
        ok = is_game_dir(out);
    }
    RegCloseKey(k);
    return ok;
}

static int find_gog(REGSAM view, wchar_t *out)
{
    HKEY games;
    DWORD i;
    int ok = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\GOG.com\\Games", 0, KEY_READ | view, &games) != ERROR_SUCCESS)
        return 0;
    for (i = 0; !ok; i++) {
        wchar_t sub[256], name[256], key[512];
        DWORD n = 256, size = sizeof(name), type;
        HKEY g;
        if (RegEnumKeyExW(games, i, sub, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        _snwprintf(key, 511, L"SOFTWARE\\GOG.com\\Games\\%s", sub);
        key[511] = 0;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | view, &g) != ERROR_SUCCESS)
            continue;
        if (RegQueryValueExW(g, L"gameName", NULL, &type, (BYTE *)name, &size) == ERROR_SUCCESS && type == REG_SZ) {
            name[255] = 0;
            _wcslwr(name);
            if (wcsstr(name, L"bridge commander"))
                ok = reg_path(HKEY_LOCAL_MACHINE, key, L"path", view, out);
        }
        RegCloseKey(g);
    }
    RegCloseKey(games);
    return ok;
}

static int find_game(wchar_t *out)
{
    static const wchar_t *k_bc = L"SOFTWARE\\Activision\\Star Trek: Bridge Commander\\V1.0";
    wchar_t *slash;
    GetModuleFileNameW(NULL, out, PATHLEN);
    slash = wcsrchr(out, L'\\');
    if (slash) {
        *slash = 0;
        if (is_game_dir(out))
            return 1;
    }
    GetCurrentDirectoryW(PATHLEN, out);
    normalize(out);
    if (is_game_dir(out))
        return 1;
    if (reg_path(HKEY_LOCAL_MACHINE, k_bc, L"INSTALLPATH", KEY_WOW64_32KEY, out) ||
        reg_path(HKEY_LOCAL_MACHINE, k_bc, L"INSTALLPATH", KEY_WOW64_64KEY, out) ||
        reg_path(HKEY_CURRENT_USER, k_bc, L"INSTALLPATH", 0, out))
        return 1;
    return find_gog(KEY_WOW64_32KEY, out) || find_gog(KEY_WOW64_64KEY, out);
}

static int pick_folder(wchar_t *out)
{
    IFileOpenDialog *dlg = NULL;
    IShellItem *item = NULL;
    PWSTR path = NULL;
    DWORD opts;
    int ok = 0;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&dlg)))
        return 0;
    IFileOpenDialog_GetOptions(dlg, &opts);
    IFileOpenDialog_SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    IFileOpenDialog_SetTitle(dlg, L"Select the Bridge Commander folder (the one with stbc.exe)");
    if (SUCCEEDED(IFileOpenDialog_Show(dlg, NULL)) && SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item))) {
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
            wcsncpy(out, path, PATHLEN - 1);
            out[PATHLEN - 1] = 0;
            normalize(out);
            ok = 1;
            CoTaskMemFree(path);
        }
        IShellItem_Release(item);
    }
    IFileOpenDialog_Release(dlg);
    return ok;
}

/* ---- install / uninstall ----------------------------------------------------- */

static int install(const wchar_t *dir)
{
    wchar_t p[PATHLEN], bak[PATHLEN], old[PATHLEN], sp[PATHLEN], sold[PATHLEN], tmpDll[PATHLEN], tmpScript[PATHLEN];
    const void *data;
    DWORD size, err;
    int movedForeign = 0, hadOld = 0, hadOldScript = 0;
    join(p, dir, L"scripts");
    if (!dir_exists(p) && !CreateDirectoryW(p, NULL)) {
        say(L"Cannot create %s: %s.", p, errtext(GetLastError()));
        return EXIT_ERROR;
    }
    /* write both payload files first; nothing in the game folder changes before both are there */
    join(p, dir, L"dinput.dll");
    join(bak, dir, BACKUP_DLL);
    join(old, dir, L"dinput.dll.bcmpfix_old");
    join(sp, dir, L"scripts\\bcmpfix_mp.py");
    join(sold, dir, L"scripts\\bcmpfix_mp.py.bcmpfix_old");
    data = resource(IDR_DLL, &size);
    if (!data || (err = stage_file(p, data, size, tmpDll)) != 0) {
        say(L"Cannot write %s: %s.", tmpDll, data ? errtext(err) : L"payload missing");
        return EXIT_ERROR;
    }
    data = resource(IDR_SCRIPT, &size);
    if (!data || (err = stage_file(sp, data, size, tmpScript)) != 0) {
        say(L"Cannot write %s: %s.", tmpScript, data ? errtext(err) : L"payload missing");
        DeleteFileW(tmpDll);
        return EXIT_ERROR;
    }
    /* dinput.dll: keep another mod's proxy as a backup, our older version until the end */
    if (file_exists(p) && !is_our_dll(p)) {
        if (file_exists(bak)) {
            say(L"%s belongs to another mod, and %s already exists. Move one of them away and try again.", p, bak);
            DeleteFileW(tmpDll);
            DeleteFileW(tmpScript);
            return EXIT_ERROR;
        }
        if (!MoveFileExW(p, bak, 0)) {
            say(L"Cannot back up %s: %s.", p, errtext(GetLastError()));
            DeleteFileW(tmpDll);
            DeleteFileW(tmpScript);
            return EXIT_ERROR;
        }
        movedForeign = 1;
    } else if (file_exists(p)) {
        DeleteFileW(old);
        hadOld = MoveFileExW(p, old, 0);
    }
    if (file_exists(sp)) {
        DeleteFileW(sold);
        hadOldScript = MoveFileExW(sp, sold, 0);
    }
    if (!MoveFileExW(tmpDll, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ||
        !MoveFileExW(tmpScript, sp, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        err = GetLastError();
        /* put everything back as it was */
        DeleteFileW(tmpDll);
        DeleteFileW(tmpScript);
        if (hadOld)
            MoveFileExW(old, p, MOVEFILE_REPLACE_EXISTING);
        else if (!movedForeign)
            DeleteFileW(p);
        if (hadOldScript)
            MoveFileExW(sold, sp, MOVEFILE_REPLACE_EXISTING);
        else
            DeleteFileW(sp);
        if (movedForeign) {
            DeleteFileW(p);
            MoveFileExW(bak, p, 0);
        }
        say(L"Cannot install into %s: %s. Nothing was changed.", dir, errtext(err));
        return EXIT_ERROR;
    }
    DeleteFileW(old);
    DeleteFileW(sold);
    if (movedForeign)
        say(L"Another mod's dinput.dll was saved as %s. Uninstalling bcmpfix puts it back.", BACKUP_DLL);
    join(p, dir, L"scripts\\bcmpfix_mp.pyc"); /* the game recompiles it */
    DeleteFileW(p);
    join(p, dir, L"bcmpfix.ini");
    if (file_exists(p)) {
        say(L"Kept your settings in bcmpfix.ini.");
    } else {
        data = resource(IDR_INI, &size);
        if (!data || (err = write_file(p, data, size)) != 0) {
            say(L"Cannot write %s: %s.", p, data ? errtext(err) : L"payload missing");
            return EXIT_ERROR;
        }
    }
    join(p, dir, L"bcmpfix_README.md");
    data = resource(IDR_README, &size);
    if (data)
        write_file(p, data, size);
    say(L"bcmpfix " VERSION_W L" is installed in %s.", dir);
    say(L"Start the game as usual. bcmpfix.log in the game folder lists every active fix.");
    say(L"All players of a game should install bcmpfix, the host above all.");
    return EXIT_OK;
}

static int uninstall(const wchar_t *dir)
{
    static const wchar_t *k_files[] = {L"scripts\\bcmpfix_mp.py", L"scripts\\bcmpfix_mp.pyc", L"bcmpfix.ini",
                                       L"bcmpfix_README.md", L"bcmpfix.log"};
    wchar_t p[PATHLEN], bak[PATHLEN];
    int i, rc = EXIT_OK;
    join(p, dir, L"dinput.dll");
    join(bak, dir, BACKUP_DLL);
    if (file_exists(p)) {
        if (is_our_dll(p)) {
            if (!DeleteFileW(p)) {
                say(L"Cannot delete %s: %s.", p, errtext(GetLastError()));
                return EXIT_ERROR;
            }
        } else {
            say(L"dinput.dll belongs to another mod and was left in place.");
        }
    }
    if (file_exists(bak)) {
        if (file_exists(p)) {
            say(L"The previous dinput.dll is still saved as %s.", BACKUP_DLL);
        } else if (MoveFileExW(bak, p, 0)) {
            say(L"Restored the previous dinput.dll.");
        } else {
            say(L"Cannot restore the previous dinput.dll from %s: %s.", BACKUP_DLL, errtext(GetLastError()));
            rc = EXIT_ERROR;
        }
    }
    for (i = 0; i < (int)(sizeof(k_files) / sizeof(k_files[0])); i++) {
        join(p, dir, k_files[i]);
        if (file_exists(p) && !DeleteFileW(p)) {
            say(L"Cannot delete %s: %s.", p, errtext(GetLastError()));
            rc = EXIT_ERROR;
        }
    }
    if (rc == EXIT_OK)
        say(L"bcmpfix was removed from %s.", dir);
    return rc;
}

static int installed(const wchar_t *dir)
{
    wchar_t p[PATHLEN];
    join(p, dir, L"dinput.dll");
    if (file_exists(p) && is_our_dll(p))
        return 1;
    join(p, dir, L"scripts\\bcmpfix_mp.py");
    return file_exists(p);
}

/* Restart elevated for the same folder and action; returns its exit code. */
static int run_elevated(const wchar_t *dir, int action, int force, const wchar_t *logpath)
{
    wchar_t exe[PATHLEN], args[PATHLEN * 2];
    SHELLEXECUTEINFOW sei;
    DWORD code = EXIT_ERROR;
    GetModuleFileNameW(NULL, exe, PATHLEN);
    _snwprintf(args, PATHLEN * 2 - 1, L"\"%s%s\" %s%s%s%s%s%s", dir, dir[wcslen(dir) - 1] == L'\\' ? L"." : L"",
               action == 2 ? L"--uninstall" : L"--install", force ? L" --force" : L"", g_quiet ? L" --quiet" : L"",
               logpath ? L" --log \"" : L"", logpath ? logpath : L"", logpath ? L"\"" : L"");
    args[PATHLEN * 2 - 1] = 0;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.lpParameters = args;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED)
            return EXIT_CANCEL;
        say(L"Cannot start the installer with administrator rights: %s.", errtext(err));
        return EXIT_ERROR;
    }
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
    }
    return (int)code;
}

/* ---- main --------------------------------------------------------------------- */

enum { BTN_INSTALL = 100, BTN_UNINSTALL, BTN_OTHER };

static int ask_action(const wchar_t *dir, int isInstalled, const wchar_t *versionNote)
{
    TASKDIALOGCONFIG tc;
    TASKDIALOG_BUTTON b[3];
    wchar_t text[2048];
    int n = 0, pressed = IDCANCEL;
    b[n].nButtonID = BTN_INSTALL;
    b[n++].pszButtonText = isInstalled ? L"Update / repair\nInstall bcmpfix " VERSION_W L" again" : L"Install";
    if (isInstalled) {
        b[n].nButtonID = BTN_UNINSTALL;
        b[n++].pszButtonText = L"Uninstall\nRemove bcmpfix and restore the original state";
    }
    b[n].nButtonID = BTN_OTHER;
    b[n++].pszButtonText = L"Choose another folder";
    _snwprintf(text, 2047, L"Game folder: %s\n%s\n\nbcmpfix fixes rubber-banding, teleporting ships and torpedo hits that only one player sees. All players should install it.",
               dir, versionNote);
    text[2047] = 0;
    memset(&tc, 0, sizeof(tc));
    tc.cbSize = sizeof(tc);
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_SIZE_TO_CONTENT;
    tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    tc.pszWindowTitle = TITLE;
    tc.pszMainIcon = TD_INFORMATION_ICON;
    tc.pszMainInstruction = isInstalled ? L"bcmpfix is installed" : L"Install the multiplayer fix?";
    tc.pszContent = text;
    tc.pButtons = b;
    tc.cButtons = n;
    tc.nDefaultButton = BTN_INSTALL;
    if (FAILED(TaskDialogIndirect(&tc, &pressed, NULL, NULL)))
        return IDCANCEL;
    return pressed;
}

static int confirm_unsupported(int ok, int total)
{
    wchar_t text[1024];
    int pressed = IDNO;
    _snwprintf(text, 1023, L"This stbc.exe does not match Bridge Commander 1.1 (%d of %d code locations match). "
               L"It may be another version or a copy-protected executable.\n\n"
               L"bcmpfix checks every location before changing it, so the game will run, but the fixes stay "
               L"inactive where the code differs.\n\nInstall anyway?", ok, total);
    text[1023] = 0;
    if (FAILED(TaskDialog(NULL, NULL, TITLE, L"Unsupported game version", text, TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
                          TD_WARNING_ICON, &pressed)))
        return 0;
    return pressed == IDYES;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmdLine, int show)
{
    wchar_t dir[PATHLEN] = L"", versionNote[512];
    const wchar_t *logpath = NULL;
    int argc, i, action = 0, force = 0, haveDir = 0, rc = EXIT_ERROR, ok = 0, total = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    DWORD err;
    (void)hInst;
    (void)hPrev;
    (void)cmdLine;
    (void)show;

    for (i = 1; argv && i < argc; i++) {
        if (!_wcsicmp(argv[i], L"--install"))
            action = 1;
        else if (!_wcsicmp(argv[i], L"--uninstall") || !_wcsicmp(argv[i], L"--restore"))
            action = 2;
        else if (!_wcsicmp(argv[i], L"--quiet"))
            g_quiet = 1;
        else if (!_wcsicmp(argv[i], L"--force"))
            force = 1;
        else if (!_wcsicmp(argv[i], L"--log") && i + 1 < argc)
            logpath = argv[++i];
        else if (argv[i][0] != L'-') {
            wcsncpy(dir, argv[i], PATHLEN - 1);
            dir[PATHLEN - 1] = 0;
            haveDir = 1;
        }
    }
    if (logpath)
        g_log = _wfopen(logpath, L"a");
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    /* game folder: argument (folder or stbc.exe dropped on the installer), else search */
    if (haveDir) {
        normalize(dir);
        if (file_exists(dir)) {
            wchar_t *slash = wcsrchr(dir, L'\\');
            if (slash)
                *slash = 0;
        }
        if (!is_game_dir(dir)) {
            say(L"There is no stbc.exe in %s.", dir);
            if (g_quiet) {
                rc = EXIT_ERROR;
                goto done;
            }
            dialog(L"Bridge Commander not found", g_out, TD_WARNING_ICON);
            g_out[0] = 0;
            haveDir = 0;
        }
    } else {
        haveDir = find_game(dir);
    }
    if (!haveDir && g_quiet) {
        say(L"Bridge Commander was not found. Pass the game folder as the first argument.");
        rc = EXIT_ERROR;
        goto done;
    }

    for (;;) {
        if (!haveDir) {
            if (!pick_folder(dir)) {
                rc = EXIT_CANCEL;
                goto done;
            }
            if (!is_game_dir(dir)) {
                wchar_t t[PATHLEN + 128];
                _snwprintf(t, PATHLEN + 127, L"There is no stbc.exe in %s. Choose the folder Bridge Commander is installed in.", dir);
                t[PATHLEN + 127] = 0;
                dialog(L"Bridge Commander not found", t, TD_WARNING_ICON);
                continue;
            }
            haveDir = 1;
        }
        ok = check_exe(dir, &total);
        if (ok == total)
            _snwprintf(versionNote, 511, L"stbc.exe: version 1.1 (supported).");
        else
            _snwprintf(versionNote, 511, L"stbc.exe: NOT the supported version 1.1 (%d of %d code locations match).",
                       ok < 0 ? 0 : ok, total);
        versionNote[511] = 0;
        if (action || g_quiet)
            break;
        {
            int pressed = ask_action(dir, installed(dir), versionNote);
            if (pressed == BTN_INSTALL) {
                action = 1;
                break;
            }
            if (pressed == BTN_UNINSTALL) {
                action = 2;
                break;
            }
            if (pressed == BTN_OTHER) {
                haveDir = 0;
                continue;
            }
            rc = EXIT_CANCEL;
            goto done;
        }
    }
    if (!action)
        action = 1;
    say(L"%s", versionNote);

    if (action == 1 && ok != total && !force) {
        if (g_quiet) {
            say(L"Not installed. Use --force to install anyway.");
            rc = EXIT_UNSUPPORTED;
            goto done;
        }
        if (!confirm_unsupported(ok < 0 ? 0 : ok, total)) {
            rc = EXIT_CANCEL;
            goto done;
        }
        force = 1;
    }
    if (game_running()) {
        say(L"Bridge Commander (stbc.exe) is running. Close the game and run the installer again.");
        rc = EXIT_ERROR;
        goto report;
    }
    err = can_write(dir);
    if (err == ERROR_ACCESS_DENIED && !is_elevated()) {
        /* protected folder (e.g. Program Files): the elevated instance reports itself */
        g_out[0] = 0;
        rc = run_elevated(dir, action, force, logpath);
        if (rc == EXIT_CANCEL)
            say(L"Administrator rights were not granted. Nothing was changed.");
        if (g_out[0])
            goto report;
        goto done;
    }
    if (err) {
        say(L"Cannot write to %s: %s.", dir, errtext(err));
        rc = EXIT_ERROR;
        goto report;
    }
    rc = action == 2 ? uninstall(dir) : install(dir);

report:
    if (rc == EXIT_OK)
        dialog(action == 2 ? L"bcmpfix removed" : L"bcmpfix installed", g_out, TD_INFORMATION_ICON);
    else
        dialog(L"Nothing or not everything was done", g_out, TD_ERROR_ICON);
done:
    if (g_log) {
        if (rc != EXIT_OK && !g_out[0])
            say(L"exit %d", rc);
        fclose(g_log);
    }
    CoUninitialize();
    if (argv)
        LocalFree(argv);
    return rc;
}
