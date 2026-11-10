/* msoobe.exe - replacement launcher for Windows 10 OOBE that runs the Vista wizard.
 *
 * Installed as System32\oobe\msoobe.exe in a disposable VM. It starts
 * C:\vista-oobe\oobe\vistamsoobe.exe with the original arguments, waits for it and
 * returns its exit code (oobeldr.exe reads that: done / restart / switch to audit mode).
 *
 * Safety net: on failure, the preserved Windows binary at
 * C:\vista-oobe\original-oobe\msoobe.exe runs with the original command line.
 * Creating C:\vista-oobe\disable.txt selects that native wizard directly.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -Wall -mwindows -municode -static -o oobelaunch.exe oobelaunch.c -lwtsapi32 -luserenv
 */
#include <windows.h>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

static void LogTo(const wchar_t* path, const char* u8, int n) {
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wr; WriteFile(f, u8, n, &wr, NULL); CloseHandle(f);
}
static void Log(const wchar_t* fmt, ...) {
    static wchar_t msg[8200], line[8300];
    va_list ap; va_start(ap, fmt); _vsnwprintf(msg, 8199, fmt, ap); va_end(ap); msg[8199] = 0;
    SYSTEMTIME st; GetLocalTime(&st);
    _snwprintf(line, 8299, L"%02u:%02u:%02u.%03u oobelaunch(%lu) %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(), msg);
    line[8299] = 0;
    OutputDebugStringW(line);
    static char u8[26000]; int k = WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, sizeof(u8), NULL, NULL);
    if (k <= 1) return;
    wchar_t p[MAX_PATH];
    if (GetWindowsDirectoryW(p, MAX_PATH - 24)) { wcscat(p, L"\\Temp\\vista-oobe.log"); LogTo(p, u8, k - 1); }
    LogTo(L"C:\\vista-oobe\\vista-oobe.log", u8, k - 1);           /* second copy, easy to find */
}

static int g_directReplacement;

/* skips one (possibly quoted) token of a command line */
static const wchar_t* SkipToken(const wchar_t* s) {
    while (*s == L' ') s++;
    if (*s == L'"') { s++; while (*s && *s != L'"') s++; if (*s) s++; }
    else while (*s && *s != L' ') s++;
    while (*s == L' ') s++;
    return s;
}

/* Runs the preserved Windows wizard in direct-replacement mode, or removes the old IFEO redirect in legacy mode. */
static int RunWindowsOwn(const wchar_t* original, const wchar_t* why) {
    Log(L"FALLBACK to Windows' own msoobe.exe: %s", why);
    { HANDLE bf = CreateFileW(L"C:\\vista-oobe\\bridge-stop.flag", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); if (bf != INVALID_HANDLE_VALUE) CloseHandle(bf); }
    wchar_t line[4096], native[MAX_PATH];
    LPCWSTR app = NULL;
    if (g_directReplacement) {
        _snwprintf(native, MAX_PATH, L"C:\\vista-oobe\\original-oobe\\msoobe.exe");
        native[MAX_PATH - 1] = 0;
        const wchar_t* args = SkipToken(original);
        _snwprintf(line, 4095, L"\"%s\"%s%s", native, *args ? L" " : L"", args);
        line[4095] = 0;
        app = native;
        if (GetFileAttributesW(native) == INVALID_FILE_ATTRIBUTES) {
            Log(L"ERROR: preserved Windows msoobe.exe is missing: %s", native);
            return 1;
        }
    } else {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\msoobe.exe", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            LONG r = RegDeleteValueW(k, L"Debugger"); RegCloseKey(k);
            Log(L"legacy IFEO redirect removed from the registry (result %ld)", r);
        } else Log(L"could not open the legacy IFEO key (error %lu)", GetLastError());
        wcsncpy(line, original, 4095); line[4095] = 0;
    }
    STARTUPINFOW si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si); GetStartupInfoW(&si);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(app, line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        Log(L"ERROR: could not start Windows' msoobe.exe (%lu)", GetLastError());
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    Log(L"Windows' msoobe.exe exited with code 0x%lX", code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
}


/* ---- Basic frame, done like BasicThemer2 does it: from OUTSIDE the wizard's process, repeatedly, on every visible top-level
   window of the wizard: DwmSetWindowAttribute(DWMWA_NCRENDERING_POLICY = DWMNCRP_DISABLED).
   Create C:\vista-oobe\aero.txt to keep the Aero (DWM) frame. ---- */
typedef HRESULT (WINAPI *DwmSetWA_t)(HWND, DWORD, LPCVOID, DWORD);
static DwmSetWA_t g_dwmSet; static DWORD g_wizPid; 
static BOOL CALLBACK BasicEnum(HWND h, LPARAM lp) {
    (void)lp; DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != g_wizPid || !IsWindowVisible(h)) return TRUE;
    HRESULT hr = 0;
    if (g_dwmSet) { DWORD policy = 1; hr = g_dwmSet(h, 2, &policy, sizeof(policy)); }
    static HWND seen[16]; static int ns; int isNew = 1;
    for (int i = 0; i < ns; i++) if (seen[i] == h) isNew = 0;
    if (isNew && ns < 16) {
        seen[ns++] = h;
        /* keep the wizard (its full-screen background "msoobe" with the performance screen, and its dialog) in front of Windows 10's own
           full-screen "FirstUX" window, which otherwise covers it */
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        wchar_t cn[64] = L"?"; GetClassNameW(h, cn, 64);
        Log(L"wizard window %p class=%s style=0x%08lX: topmost, basic frame -> hr=0x%08lX", h, cn, (unsigned long)GetWindowLongW(h, GWL_STYLE), (unsigned long)hr);
    }
    return TRUE;
}

/* hide Windows 10's full-screen "FirstUX" window (winlogon.exe) - the boot-screen-like screen. Only hidden, never closed or destroyed
   (it belongs to winlogon). C:\vista-oobe\showfirstux.txt keeps it visible. */
static BOOL CALLBACK HideFirstUXEnum(HWND h, LPARAM lp) {
    (void)lp; wchar_t cn[64];
    if (GetClassNameW(h, cn, 64) && !wcscmp(cn, L"FirstUXWndClass") && IsWindowVisible(h)) {
        ShowWindow(h, SW_HIDE);
        static volatile LONG k = 0;
        if (InterlockedIncrement(&k) <= 8) Log(L"hid FirstUXWndClass window %p (still visible afterwards: %d)", h, IsWindowVisible(h) ? 1 : 0);
    }
    return TRUE;
}
static int g_hideFirstUX = -1;
static void HideFirstUX(void) {
    if (g_hideFirstUX < 0) g_hideFirstUX = GetFileAttributesW(L"C:\\vista-oobe\\showfirstux.txt") == INVALID_FILE_ATTRIBUTES;
    if (g_hideFirstUX) EnumWindows(HideFirstUXEnum, 0);
}
/* screen watcher: logs what is visible on screen whenever it changes (all visible top-level windows with class, owner process, rect;
   which one is in front) - to see which window covers the wizard's "performance" screen and what is shown between the phases */
static wchar_t g_sig[8192], g_cur[8192];
static BOOL CALLBACK ListEnum(HWND h, LPARAM lp) {
    (void)lp;
    if (!IsWindowVisible(h)) return TRUE;
    RECT r; GetWindowRect(h, &r);
    if (r.right - r.left < 50 || r.bottom - r.top < 50) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    wchar_t cn[48] = L"?"; GetClassNameW(h, cn, 48);
    wchar_t exe[MAX_PATH] = L"?";
    HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (ph) { DWORD n = MAX_PATH; wchar_t full[MAX_PATH]; if (QueryFullProcessImageNameW(ph, 0, full, &n)) { wchar_t* s = wcsrchr(full, L'\\'); wcsncpy(exe, s ? s + 1 : full, MAX_PATH - 1); } CloseHandle(ph); }
    wchar_t line[300];
    _snwprintf(line, 300, L"  %p %s [%s] %ldx%ld at %ld,%ld ex=0x%lX\r\n", h, cn, exe, r.right - r.left, r.bottom - r.top, r.left, r.top, (unsigned long)GetWindowLongW(h, GWL_EXSTYLE));
    line[299] = 0;
    if (wcslen(g_cur) + wcslen(line) < 8000) wcscat(g_cur, line);
    return TRUE;
}
static DWORD WINAPI BasicThread(LPVOID p) {
    HANDLE proc = (HANDLE)p;
    HMODULE d = NULL;
    if (GetFileAttributesW(L"C:\\vista-oobe\\aero.txt") == INVALID_FILE_ATTRIBUTES) {
        d = LoadLibraryW(L"dwmapi.dll");
        if (d) g_dwmSet = (DwmSetWA_t)GetProcAddress(d, "DwmSetWindowAttribute");
    }
    int logs = 0;
    while (WaitForSingleObject(proc, 250) == WAIT_TIMEOUT) {
        EnumWindows(BasicEnum, 0);
        HideFirstUX();
        g_cur[0] = 0; EnumWindows(ListEnum, 0);
        HWND fg = GetForegroundWindow(); wchar_t fl[100]; _snwprintf(fl, 100, L"  foreground=%p\r\n", fg); fl[99] = 0; wcscat(g_cur, fl);
        if (wcscmp(g_cur, g_sig) && logs < 150) {
            wcscpy(g_sig, g_cur); logs++;
            Log(L"[screen] visible windows now:\r\n%s", g_cur);
        }
    }
    return 0;
}


/* ---- "bridge": a full-screen window with Vista's background that stays up from the start of the wizard until Windows 10's own
   full-screen "FirstUX" window (the boot-screen-like blue/spinner screen) is gone, so the gaps between the phases and the move to the
   logon screen show the Vista background instead. Started as a separate program ("oobelaunch.exe /bridge") so it outlives the wizard.
   Create C:\vista-oobe\nobridge.txt to switch it off. ---- */
static int ExplorerInUserSession(void);
static HBITMAP g_bridgeBmp;
static LRESULT CALLBACK BridgeProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        if (g_bridgeBmp) {
            HDC mem = CreateCompatibleDC(dc); HGDIOBJ old = SelectObject(mem, g_bridgeBmp); BITMAP bm; GetObjectW(g_bridgeBmp, sizeof(bm), &bm);
            SetStretchBltMode(dc, HALFTONE); StretchBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            SelectObject(mem, old); DeleteDC(mem);
        } else { HBRUSH b = CreateSolidBrush(RGB(0x1F, 0x6E, 0x8C)); FillRect(dc, &rc, b); DeleteObject(b); }
        EndPaint(h, &ps); return 0;
    }
    if (m == WM_SETCURSOR) return TRUE;
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}
static HBITMAP LoadJpgBitmap(const wchar_t* path) {
    HMODULE g = LoadLibraryW(L"gdiplus.dll"); if (!g) return NULL;
    typedef int (WINAPI *Startup_t)(ULONG_PTR*, const void*, void*);
    typedef int (WINAPI *FromFile_t)(const WCHAR*, void**);
    typedef int (WINAPI *ToHBitmap_t)(void*, HBITMAP*, DWORD);
    Startup_t st = (Startup_t)GetProcAddress(g, "GdiplusStartup");
    FromFile_t ff = (FromFile_t)GetProcAddress(g, "GdipCreateBitmapFromFile");
    ToHBitmap_t th = (ToHBitmap_t)GetProcAddress(g, "GdipCreateHBITMAPFromBitmap");
    if (!st || !ff || !th) return NULL;
    struct { UINT32 v; void* a; BOOL b; BOOL c; } in = { 1, NULL, FALSE, FALSE }; ULONG_PTR tok;
    if (st(&tok, &in, NULL)) return NULL;
    void* bmp = NULL; HBITMAP hb = NULL;
    if (ff(path, &bmp) == 0 && bmp) th(bmp, &hb, 0xFF000000);
    return hb;
}
static BOOL CALLBACK FirstUXEnum(HWND h, LPARAM lp) {
    wchar_t cn[64]; if (GetClassNameW(h, cn, 64) && !wcscmp(cn, L"FirstUXWndClass")) *(int*)lp = 1;
    return TRUE;
}
static int RunBridge(void) {
    if (GetFileAttributesW(L"C:\\vista-oobe\\nobridge.txt") != INVALID_FILE_ATTRIBUTES) return 0;
    wchar_t jpg[MAX_PATH] = L"C:\\vista-oobe\\oobe\\msoobe.jpg";
    if (GetFileAttributesW(jpg) == INVALID_FILE_ATTRIBUTES) {
        GetWindowsDirectoryW(jpg, MAX_PATH - 40); wcscat(jpg, L"\\System32\\oobe\\msoobe.jpg");
    }
    g_bridgeBmp = LoadJpgBitmap(jpg);
    Log(L"bridge: started (background %s)", g_bridgeBmp ? L"loaded" : L"NOT loaded, plain colour");
    WNDCLASSW wc; memset(&wc, 0, sizeof(wc)); wc.lpfnWndProc = BridgeProc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"VistaOobeBridge"; wc.hCursor = NULL;
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"VistaOobeBridge", L"", WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, wc.hInstance, NULL);
    if (!w) { Log(L"bridge: window failed (%lu)", GetLastError()); return 1; }
    DWORD t0 = GetTickCount(), lastSeen = t0; int everSeen = 0, blogs = 0;
    for (;;) {
        MSG msg; while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        HideFirstUX();
        { g_cur[0] = 0; EnumWindows(ListEnum, 0); if (wcscmp(g_cur, g_sig) && blogs < 100) { wcscpy(g_sig, g_cur); blogs++; Log(L"[screen/bridge] visible windows now:\r\n%s", g_cur); } }
        int fx = 0; EnumWindows(FirstUXEnum, (LPARAM)&fx);
        DWORD now = GetTickCount();
        if (fx) { everSeen = 1; lastSeen = now; }
        static DWORD explorerSince = 0; if (ExplorerInUserSession()) { if (!explorerSince) explorerSince = now; } else explorerSince = 0;
        int explorerReady = explorerSince && now - explorerSince > 3000;     /* the new user's desktop is there */
        if ((everSeen && now - lastSeen > 6000 && explorerReady) || now - t0 > 600000 || GetFileAttributesW(L"C:\\vista-oobe\\bridge-stop.flag") != INVALID_FILE_ATTRIBUTES) break;
        Sleep(200);
    }
    Log(L"bridge: closing after %lu s", (GetTickCount() - t0) / 1000);
    DestroyWindow(w);
    RedrawWindow(NULL, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
    return 0;
}

/* ---- wallpaper: the wizard's wallpaper page stores the picked picture in HKLM\...\OOBE\Wallpaper, but Windows 10 puts its own Spotlight
   picture on the new user's desktop at first sign-in. After the wizard, a RunOnce entry starts "oobelaunch.exe /wallpaper" at the new user's
   first sign-in, which applies the picked picture (a few times, in case Windows 10 changes it back). Missing pictures can be put in
   C:\vista-oobe\wallpapers\. C:\vista-oobe\nowallpaper.txt switches this off. ---- */
static int RunGlassReload(void);
static int RunWallpaper(void) {
    { wchar_t me[MAX_PATH], cl[MAX_PATH + 40]; GetModuleFileNameW(NULL, me, MAX_PATH); _snwprintf(cl, MAX_PATH + 40, L"\"%s\" /screenlog", me); cl[MAX_PATH + 39] = 0;
      STARTUPINFOW s; memset(&s, 0, sizeof(s)); s.cb = sizeof(s); PROCESS_INFORMATION p;
      if (CreateProcessW(NULL, cl, NULL, NULL, FALSE, DETACHED_PROCESS, NULL, NULL, &s, &p)) { CloseHandle(p.hThread); CloseHandle(p.hProcess); } }
    { wchar_t me[MAX_PATH], cl[MAX_PATH + 40]; GetModuleFileNameW(NULL, me, MAX_PATH); _snwprintf(cl, MAX_PATH + 40, L"\"%s\" /glassreload", me); cl[MAX_PATH + 39] = 0;
      STARTUPINFOW s; memset(&s, 0, sizeof(s)); s.cb = sizeof(s); PROCESS_INFORMATION p;
      if (CreateProcessW(NULL, cl, NULL, NULL, FALSE, DETACHED_PROCESS, NULL, NULL, &s, &p)) { CloseHandle(p.hThread); CloseHandle(p.hProcess); } }
    wchar_t path[MAX_PATH * 2]; DWORD cb = sizeof(path); path[0] = 0;
    LONG rr = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OOBE", L"Wallpaper", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, path, &cb);
    Log(L"wallpaper: chosen in the wizard: [%s] (registry result %ld)", path, rr);
    if (rr != ERROR_SUCCESS || !path[0]) return 0;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        wchar_t alt[MAX_PATH * 2]; const wchar_t* base = wcsrchr(path, L'\\'); base = base ? base + 1 : path;
        _snwprintf(alt, MAX_PATH * 2, L"C:\\vista-oobe\\wallpapers\\%s", base); alt[MAX_PATH * 2 - 1] = 0;
        if (GetFileAttributesW(alt) != INVALID_FILE_ATTRIBUTES) wcscpy(path, alt);
        else { Log(L"wallpaper: picture %s not found (also not in C:\\vista-oobe\\wallpapers) - nothing applied", path); return 0; }
    }
    for (int pass = 0; pass < 4; pass++) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(k, L"Wallpaper", 0, REG_SZ, (BYTE*)path, (DWORD)((wcslen(path) + 1) * 2));
            RegSetValueExW(k, L"WallpaperStyle", 0, REG_SZ, (BYTE*)L"10", 6);
            RegSetValueExW(k, L"TileWallpaper", 0, REG_SZ, (BYTE*)L"0", 4);
            RegCloseKey(k);
        }
        DWORD zero = 0;
        RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Wallpapers", L"BackgroundType", REG_DWORD, &zero, sizeof(zero));
        BOOL ok = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, path, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        Log(L"wallpaper: pass %d applied %s (%d)", pass, path, ok);
        Sleep(pass == 0 ? 6000 : 12000);
    }
    return 0;
}

/* ---- first-session fix: at the new user's first sign-in the corners (OpenGlass / theme) are wrong until the user signs out and in again.
   A scheduled task (runs as SYSTEM, at log on, 25 s delay, created by the launcher after a successful wizard run) starts "oobelaunch.exe /fixshell",
   which deletes its own task and restarts dwm.exe and then explorer.exe once (both restart by themselves; Windhawk / OpenGlass hook the new ones).
   C:\vista-oobe\nofixshell.txt = do nothing, skip-dwm.txt / skip-explorer.txt = skip that step. ---- */
static void RunHidden(const wchar_t* cmd) {
    wchar_t line[1024]; wcsncpy(line, cmd, 1023); line[1023] = 0;
    STARTUPINFOW si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { WaitForSingleObject(pi.hProcess, 15000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    else Log(L"fixshell: could not run %s (%lu)", cmd, GetLastError());
}
static int ExplorerInUserSession(void) {
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe); int found = 0;
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (sn == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(sn, &pe)) do {
        if (!_wcsicmp(pe.szExeFile, L"explorer.exe")) { DWORD sid = 0; if (ProcessIdToSessionId(pe.th32ProcessID, &sid) && sid != 0) found = 1; }
    } while (!found && Process32NextW(sn, &pe));
    CloseHandle(sn); return found;
}
/* start explorer.exe as the user who is signed in (we run as SYSTEM) */
static void StartExplorerAsUser(void) {
    DWORD sid = WTSGetActiveConsoleSessionId(); HANDLE tok = NULL;
    if (sid == 0xFFFFFFFF || !WTSQueryUserToken(sid, &tok)) { Log(L"fixshell: no user token (%lu)", GetLastError()); return; }
    void* env = NULL; CreateEnvironmentBlock(&env, tok, FALSE);
    wchar_t sysdir[MAX_PATH], exe[MAX_PATH]; GetWindowsDirectoryW(sysdir, MAX_PATH); _snwprintf(exe, MAX_PATH, L"%s\\explorer.exe", sysdir); exe[MAX_PATH - 1] = 0;
    STARTUPINFOW si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si); si.lpDesktop = L"winsta0\\default";
    PROCESS_INFORMATION pi; wchar_t cmd[MAX_PATH + 4]; _snwprintf(cmd, MAX_PATH + 4, L"\"%s\"", exe); cmd[MAX_PATH + 3] = 0;
    BOOL ok = CreateProcessAsUserW(tok, exe, cmd, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, env, sysdir, &si, &pi);
    Log(L"fixshell: explorer.exe started for session %lu -> %d (error %lu)", sid, ok, ok ? 0 : GetLastError());
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    if (env) DestroyEnvironmentBlock(env);
    CloseHandle(tok);
}
static int RunFixShell(void) {
    RunHidden(L"schtasks.exe /delete /tn VistaOobeFixShell /f");
    if (GetFileAttributesW(L"C:\\vista-oobe\\nofixshell.txt") != INVALID_FILE_ATTRIBUTES) { Log(L"fixshell: disabled (nofixshell.txt)"); return 0; }
    /* as soon as the user's explorer.exe is running (polled every 250 ms, max 90 s) */
    DWORD t0 = GetTickCount();
    while (!ExplorerInUserSession() && GetTickCount() - t0 < 90000) Sleep(250);
    Log(L"fixshell: explorer is up after %lu ms", GetTickCount() - t0);
    Sleep(1500);
    /* DWM restart is opt-in (create fix-dwm.txt): with OpenGlass hooked in, DWM did not come back after being ended */
    if (GetFileAttributesW(L"C:\\vista-oobe\\fix-dwm.txt") != INVALID_FILE_ATTRIBUTES) { Log(L"fixshell: restarting dwm.exe"); RunHidden(L"taskkill.exe /f /im dwm.exe"); Sleep(5000); }
    if (GetFileAttributesW(L"C:\\vista-oobe\\skip-explorer.txt") == INVALID_FILE_ATTRIBUTES) {
        Log(L"fixshell: restarting explorer.exe");
        { PROCESSENTRY32W pe; pe.dwSize = sizeof(pe); HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
          if (sn != INVALID_HANDLE_VALUE) { if (Process32FirstW(sn, &pe)) do {
              if (!_wcsicmp(pe.szExeFile, L"explorer.exe")) { HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID); if (ph) { TerminateProcess(ph, 0); CloseHandle(ph); } }
          } while (Process32NextW(sn, &pe)); CloseHandle(sn); } }
        /* Windows does not restart a killed explorer.exe by itself: wait a moment for it, then start it as the user */
        DWORD t1 = GetTickCount(); while (!ExplorerInUserSession() && GetTickCount() - t1 < 2500) Sleep(250);
        if (!ExplorerInUserSession()) StartExplorerAsUser(); else Log(L"fixshell: explorer.exe came back by itself");
    }
    Log(L"fixshell: done");
    return 0;
}

/* ---- OpenGlass first-session fix. OpenGlass (injected into dwm.exe by its service) gets the signed-in user's HKCU keys when DWM receives
   WM_WTSSESSION_CHANGE / WTS_SESSION_LOGON on its notification window (class "DWM") and then calls LoadRegistry(). At the first sign-in after setup that
   reload does not get the new user's keys, so the first session runs on machine defaults until the user signs out and in. This mode runs as the new user at first
   sign-in (RunOnce) and sends the same message again a few times, later, when the session is settled. C:\vista-oobe\noglassreload.txt switches it off. ---- */
static HWND FindDwmWindow(void) {
    HWND h = FindWindowW(L"DWM", NULL);
    if (!h) h = FindWindowExW(HWND_MESSAGE, NULL, L"DWM", NULL);
    return h;
}
static void ForceRedraw(void) {
    RedrawWindow(NULL, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
}
/* Win+Ctrl+Shift+B: the Windows shortcut that resets the graphics stack; DWM re-creates its surfaces and everything is redrawn */
static void GraphicsReset(void) {
    keybd_event(VK_LWIN, 0, 0, 0); keybd_event(VK_CONTROL, 0, 0, 0); keybd_event(VK_SHIFT, 0, 0, 0);
    keybd_event('B', 0, 0, 0); keybd_event('B', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
}
static int RunGlassReload(void) {
    if (GetFileAttributesW(L"C:\\vista-oobe\\noglassreload.txt") != INVALID_FILE_ATTRIBUTES) return 0;
    HANDLE once = CreateMutexW(NULL, FALSE, L"Local\\VistaOobeGlassReloadOnce");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) return 0;     /* started twice (by the wallpaper step and by RunOnce): only one runs */
    DWORD sid = 0; ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    static const DWORD waits[] = { 4000, 6000, 10000, 15000 };
    for (int i = 0; i < 4; i++) {
        Sleep(waits[i]);
        HWND h = FindDwmWindow(); DWORD_PTR res = 0; LRESULT r = 0; DWORD le = 0;
        if (h) { SetLastError(0); r = SendMessageTimeoutW(h, WM_WTSSESSION_CHANGE, 5 /* WTS_SESSION_LOGON */, (LPARAM)sid, SMTO_ABORTIFHUNG, 5000, &res); le = GetLastError(); }
        Log(L"glassreload: attempt %d, DWM window %p, session %lu -> %ld (error %lu)", i + 1, h, sid, (long)r, le);
    }
    ForceRedraw(); Log(L"glassreload: full redraw requested");
    if (GetFileAttributesW(L"C:\\vista-oobe\\gfxreset.txt") != INVALID_FILE_ATTRIBUTES) { Sleep(1500); GraphicsReset(); Log(L"glassreload: graphics reset (Win+Ctrl+Shift+B) sent"); }
    return 0;
}

/* ---- delayed OpenGlass: OpenGlassHost (service) injects OpenGlass into dwm.exe and gives it the signed-in user's registry keys at that moment.
   During setup no user is signed in, so the first session ran with machine defaults (wrong corners) until a sign-out. With C:\vista-oobe\glassdelay.txt
   (created by openglass-delay.bat, which also sets the service to manual start) the launcher registers a SYSTEM task at log on; it starts the service once the new
   user's desktop is up and sets it back to automatic start. ---- */
static int RunStartGlass(void) {
    RunHidden(L"schtasks.exe /delete /tn VistaOobeStartGlass /f");
    DWORD t0 = GetTickCount();
    while (!ExplorerInUserSession() && GetTickCount() - t0 < 120000) Sleep(250);
    Log(L"startglass: user desktop is up after %lu ms", GetTickCount() - t0);
    Sleep(2500);
    RunHidden(L"sc.exe config OpenGlassHost start= auto");
    RunHidden(L"sc.exe start OpenGlassHost");
    Log(L"startglass: OpenGlassHost set to automatic and started");
    return 0;
}

/* ---- /screenlog: runs as the new user at first sign-in (started by the wallpaper step) and logs every change of what is visible on the desktop
   (window, class, owning program, size, position, foreground) for 3 minutes - to identify an overlay that stays until Ctrl+Alt+Del. ---- */
static int RunScreenLog(void) {
    HANDLE once = CreateMutexW(NULL, FALSE, L"Local\\VistaOobeScreenLogOnce");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    DWORD t0 = GetTickCount(); int logs = 0; static wchar_t last[8192];
    DWORD sid = 0; ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    Log(L"screenlog: started in session %lu", sid);
    while (GetTickCount() - t0 < 180000 && logs < 250) {
        g_cur[0] = 0; EnumWindows(ListEnum, 0);
        HWND fg = GetForegroundWindow(); wchar_t fl[200] = L"  foreground=(none)\r\n";
        if (fg) { DWORD pid = 0; GetWindowThreadProcessId(fg, &pid); wchar_t cn[64] = L"?"; GetClassNameW(fg, cn, 64); _snwprintf(fl, 200, L"  foreground=%p %s pid %lu\r\n", fg, cn, pid); fl[199] = 0; }
        wcscat(g_cur, fl);
        if (wcscmp(g_cur, last)) { wcscpy(last, g_cur); logs++; Log(L"[screen/user] %lu s after start:\r\n%s", (GetTickCount() - t0) / 1000, g_cur); }
        Sleep(500);
    }
    return 0;
}

/* ---- AuthUX overlay fix. AuthUX (Win7-style logon UI) shows a full-screen window of class "AUTHUI.DLL: LogonUI Logon Window" in LogonUI.exe; its
   AuthUX::Hide() does nothing, so if Windows hides the logon UI after the first sign-in without stopping it, the window stays on screen until Ctrl+Alt+Del.
   SYSTEM task at log on (/authuxfix) -> waits for the new user's desktop -> starts "/hideauthux" as SYSTEM in the user's session -> that hides every visible window of that
   class on the Default and Winlogon desktops for 90 s. C:\vista-oobe\noauthuxfix.txt switches it off. ---- */
static void EnablePriv(const wchar_t* name) {
    HANDLE t; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &t)) return;
    TOKEN_PRIVILEGES tp; tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid)) AdjustTokenPrivileges(t, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(t);
}
static void InputDesktopName(wchar_t* out, int cch) {
    wcscpy(out, L"?");
    HDESK in = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!in) { _snwprintf(out, cch, L"(no access %lu)", GetLastError()); return; }
    DWORD need = 0; GetUserObjectInformationW(in, UOI_NAME, out, cch * 2, &need);
    CloseDesktop(in);
}
static BOOL CALLBACK DeskListEnum(HWND h, LPARAM lp) {
    RECT r; GetWindowRect(h, &r);
    if (r.right - r.left < 200 || r.bottom - r.top < 200) return TRUE;
    wchar_t cn[96] = L"?"; GetClassNameW(h, cn, 96);
    wchar_t* buf = (wchar_t*)lp;
    wchar_t line[200]; _snwprintf(line, 200, L"    %p %s %ldx%ld %s\r\n", h, cn, r.right - r.left, r.bottom - r.top, IsWindowVisible(h) ? L"visible" : L"hidden"); line[199] = 0;
    if (wcslen(buf) + wcslen(line) < 3000) wcscat(buf, line);
    return TRUE;
}
/* the input desktop stayed "Winlogon" after the first sign-in (the user's desktop never got the input): that is what Ctrl+Alt+Del + Cancel fixes.
   Runs as SYSTEM in the user's session for 2 minutes: logs the input desktop and the big windows on both desktops whenever it changes and, if the
   Winlogon desktop has been the input desktop for 6 s while the user's explorer is up, switches to the Default desktop (max 3 times).
   C:\vista-oobe\noswitchdesktop.txt only logs. */
static int RunHideAuthUX(void) {
    DWORD t0 = GetTickCount(), winlogonSince = 0; int switches = 0; wchar_t last[64] = L"";
    int noSwitch = GetFileAttributesW(L"C:\\vista-oobe\\noswitchdesktop.txt") != INVALID_FILE_ATTRIBUTES;
    Log(L"deskfix: started (switching %s)", noSwitch ? L"off" : L"on");
    while (GetTickCount() - t0 < 120000) {
        wchar_t cur[64]; InputDesktopName(cur, 64);
        DWORD now = GetTickCount();
        if (wcscmp(cur, last)) {
            wcscpy(last, cur);
            wchar_t lst[3200]; lst[0] = 0;
            static const wchar_t* desks[] = { L"Default", L"Winlogon" };
            for (int i = 0; i < 2; i++) {
                HDESK d = OpenDesktopW(desks[i], 0, FALSE, DESKTOP_ENUMERATE | DESKTOP_READOBJECTS);
                wchar_t hdr[80]; _snwprintf(hdr, 80, L"  desktop %s:%s\r\n", desks[i], d ? L"" : L" (cannot open)"); hdr[79] = 0; wcscat(lst, hdr);
                if (d) { if (SetThreadDesktop(d)) EnumWindows(DeskListEnum, (LPARAM)lst); CloseDesktop(d); }
            }
            Log(L"deskfix: %lu s: input desktop is now [%s]\r\n%s", (now - t0) / 1000, cur, lst);
        }
        if (!_wcsicmp(cur, L"Winlogon")) { if (!winlogonSince) winlogonSince = now; } else winlogonSince = 0;
        if (!noSwitch && winlogonSince && now - winlogonSince > 6000 && ExplorerInUserSession() && switches < 3) {
            HDESK def = OpenDesktopW(L"Default", 0, FALSE, DESKTOP_SWITCHDESKTOP);
            BOOL ok = def ? SwitchDesktop(def) : FALSE; DWORD le = GetLastError();
            Log(L"deskfix: SwitchDesktop(Default) -> %d (error %lu)", ok, ok ? 0 : le);
            if (def) CloseDesktop(def);
            switches++; winlogonSince = 0;
        }
        Sleep(500);
    }
    Log(L"deskfix: done (%d switch(es))", switches);
    return 0;
}
static int RunAuthUXFix(void) {
    RunHidden(L"schtasks.exe /delete /tn VistaOobeAuthUXFix /f");
    if (GetFileAttributesW(L"C:\\vista-oobe\\noauthuxfix.txt") != INVALID_FILE_ATTRIBUTES) return 0;
    DWORD t0 = GetTickCount();
    while (!ExplorerInUserSession() && GetTickCount() - t0 < 120000) Sleep(250);
    Sleep(4000);
    EnablePriv(L"SeTcbPrivilege"); EnablePriv(L"SeAssignPrimaryTokenPrivilege"); EnablePriv(L"SeIncreaseQuotaPrivilege");
    DWORD sid = WTSGetActiveConsoleSessionId();
    HANDLE self = NULL, dup = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &self) || !DuplicateTokenEx(self, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &dup)) { Log(L"authuxfix: token failed (%lu)", GetLastError()); return 1; }
    if (!SetTokenInformation(dup, TokenSessionId, &sid, sizeof(sid))) { Log(L"authuxfix: session id failed (%lu)", GetLastError()); return 1; }
    wchar_t me[MAX_PATH], cl[MAX_PATH + 40]; GetModuleFileNameW(NULL, me, MAX_PATH); _snwprintf(cl, MAX_PATH + 40, L"\"%s\" /hideauthux", me); cl[MAX_PATH + 39] = 0;
    STARTUPINFOW si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si); si.lpDesktop = L"winsta0\\default"; PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessAsUserW(dup, NULL, cl, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    Log(L"authuxfix: /hideauthux started in session %lu as SYSTEM -> %d (error %lu)", sid, ok, ok ? 0 : GetLastError());
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    return 0;
}

int WINAPI wWinMain(HINSTANCE h, HINSTANCE p, LPWSTR cmd, int show) {
    (void)h; (void)p; (void)cmd; (void)show;
    const wchar_t* full = GetCommandLineW();
    if (!wcsncmp(SkipToken(full), L"/bridge", 7)) return RunBridge();
    if (!wcsncmp(SkipToken(full), L"/wallpaper", 10)) return RunWallpaper();
    if (!wcsncmp(SkipToken(full), L"/fixshell", 9)) return RunFixShell();
    if (!wcsncmp(SkipToken(full), L"/glassreload", 12)) return RunGlassReload();
    if (!wcsncmp(SkipToken(full), L"/startglass", 11)) return RunStartGlass();
    if (!wcsncmp(SkipToken(full), L"/authuxfix", 10)) return RunAuthUXFix();
    if (!wcsncmp(SkipToken(full), L"/hideauthux", 11)) return RunHideAuthUX();
    if (!wcsncmp(SkipToken(full), L"/screenlog", 10)) return RunScreenLog();
    Log(L"started with command line: %s", full);
    const wchar_t* afterMe = SkipToken(full);            /* the original command line: msoobe.exe path + its arguments */
    const wchar_t* args = SkipToken(afterMe);
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    const wchar_t* selfName = wcsrchr(self, L'\\');
    g_directReplacement = !_wcsicmp(selfName ? selfName + 1 : self, L"msoobe.exe");
    Log(L"original program: %.200s", afterMe);
    Log(L"arguments passed on: [%s]", args);

    wchar_t exe[MAX_PATH];
    _snwprintf(exe, MAX_PATH, L"C:\\vista-oobe\\oobe\\vistamsoobe.exe"); exe[MAX_PATH - 1] = 0;

    if (GetFileAttributesW(L"C:\\vista-oobe\\disable.txt") != INVALID_FILE_ATTRIBUTES)
        return RunWindowsOwn(afterMe, L"C:\\vista-oobe\\disable.txt exists");
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
        Log(L"ERROR: %s not found", exe);
        return RunWindowsOwn(afterMe, L"vistamsoobe.exe not found");
    }

    DeleteFileW(L"C:\\vista-oobe\\crash.flag");
    DeleteFileW(L"C:\\vista-oobe\\bridge-stop.flag");
    if (GetFileAttributesW(L"C:\\vista-oobe\\nobridge.txt") == INVALID_FILE_ATTRIBUTES) {
        wchar_t me[MAX_PATH], bl[MAX_PATH + 32]; GetModuleFileNameW(NULL, me, MAX_PATH);
        _snwprintf(bl, MAX_PATH + 32, L"\"%s\" /bridge", me); bl[MAX_PATH + 31] = 0;
        STARTUPINFOW bsi; memset(&bsi, 0, sizeof(bsi)); bsi.cb = sizeof(bsi); PROCESS_INFORMATION bpi;
        if (CreateProcessW(NULL, bl, NULL, NULL, FALSE, DETACHED_PROCESS, NULL, NULL, &bsi, &bpi)) { CloseHandle(bpi.hThread); CloseHandle(bpi.hProcess); Log(L"bridge window started (pid %lu)", bpi.dwProcessId); }
        else Log(L"bridge window could not be started (%lu)", GetLastError());
    }
    /* Windows 10 hides the mouse cursor during OOBE (cursor suppression); Vista's wizard needs it. */
    { DWORD zero = 0;
      LONG rc = RegSetKeyValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableCursorSuppression", REG_DWORD, &zero, sizeof(zero));
      Log(L"cursor suppression disabled (result %ld)", rc); }
    { DWORD zero = 0; LONG r1, r2;
      r1 = RegSetKeyValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableFirstLogonAnimation", REG_DWORD, &zero, sizeof(zero));
      r2 = RegSetKeyValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", L"EnableFirstLogonAnimation", REG_DWORD, &zero, sizeof(zero));
      Log(L"first sign-in animation disabled (results %ld, %ld)", r1, r2); }
    /* Skip the product-key page: Vista's wizard hides it when Setup already showed one (SetupDisplayedProductKey = 1).
       Activate later in Settings. Create C:\vista-oobe\askkey.txt to keep the page. */
    if (GetFileAttributesW(L"C:\\vista-oobe\\askkey.txt") == INVALID_FILE_ATTRIBUTES) {
        DWORD one = 1;
        LONG rr = RegSetKeyValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\OOBE", L"SetupDisplayedProductKey", REG_DWORD, &one, sizeof(one));
        Log(L"product-key page skipped via SetupDisplayedProductKey=1 (result %ld)", rr);
    }
    wchar_t line[4096];
    _snwprintf(line, 4095, L"\"%s\" %s", exe, args); line[4095] = 0;
    STARTUPINFOW si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    GetStartupInfoW(&si);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        Log(L"ERROR: CreateProcess failed (%lu)", GetLastError());
        return RunWindowsOwn(afterMe, L"the Vista wizard could not be started");
    }
    DWORD t0 = GetTickCount();
    Log(L"started Vista msoobe (pid %lu), waiting...", pi.dwProcessId);
    g_wizPid = pi.dwProcessId;
    HANDLE bt = CreateThread(NULL, 0, BasicThread, pi.hProcess, 0, NULL);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (bt) { WaitForSingleObject(bt, 2000); CloseHandle(bt); }
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    DWORD secs = (GetTickCount() - t0) / 1000;
    Log(L"Vista msoobe exited with code 0x%lX after %lu s", code, secs);
    /* the wizard writes its own messages (e.g. "Running WinSAT", errors) to the unattend log: keep a copy where it is easy to find */
    CopyFileW(L"C:\\Windows\\Panther\\UnattendGC\\setupact.log", L"C:\\vista-oobe\\unattendgc-setupact.log", FALSE);
    CopyFileW(L"C:\\Windows\\Panther\\UnattendGC\\setuperr.log", L"C:\\vista-oobe\\unattendgc-setuperr.log", FALSE);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    /* a crash (an NTSTATUS error as exit code) or an instant failure: let Windows' own wizard finish setup instead */
    /* the proxy leaves crash.flag when it sees a crash; Vista's wizard then exits with its own code (0xFF), not a crash code */
    if (code == 0 && GetFileAttributesW(L"C:\\vista-oobe\\nowallpaper.txt") == INVALID_FILE_ATTRIBUTES) {
        wchar_t me[MAX_PATH], rc[MAX_PATH + 32]; GetModuleFileNameW(NULL, me, MAX_PATH);
        _snwprintf(rc, MAX_PATH + 32, L"\"%s\" /wallpaper", me); rc[MAX_PATH + 31] = 0;
        LONG rw = RegSetKeyValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", L"VistaOobeWallpaper", REG_SZ, rc, (DWORD)((wcslen(rc) + 1) * 2));
        Log(L"wallpaper: first sign-in task registered (result %ld)", rw);
    }
    if (code == 0 && GetFileAttributesW(L"C:\\vista-oobe\\noauthuxfix.txt") == INVALID_FILE_ATTRIBUTES) {
        wchar_t me5[MAX_PATH], cl5[1024]; GetModuleFileNameW(NULL, me5, MAX_PATH);
        _snwprintf(cl5, 1024, L"schtasks.exe /create /tn VistaOobeAuthUXFix /tr \"\\\"%s\\\" /authuxfix\" /sc onlogon /ru SYSTEM /rl highest /f", me5); cl5[1023] = 0;
        STARTUPINFOW s5; memset(&s5, 0, sizeof(s5)); s5.cb = sizeof(s5); PROCESS_INFORMATION p5;
        if (CreateProcessW(NULL, cl5, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &s5, &p5)) {
            WaitForSingleObject(p5.hProcess, 15000); DWORD ec5 = 1; GetExitCodeProcess(p5.hProcess, &ec5); CloseHandle(p5.hThread); CloseHandle(p5.hProcess);
            Log(L"authuxfix: sign-in task created (schtasks exit code %lu)", ec5);
        } else Log(L"authuxfix: schtasks could not be started (%lu)", GetLastError());
    }
    if (code == 0 && GetFileAttributesW(L"C:\\vista-oobe\\glassdelay.txt") != INVALID_FILE_ATTRIBUTES) {
        wchar_t me4[MAX_PATH], cl4[1024]; GetModuleFileNameW(NULL, me4, MAX_PATH);
        _snwprintf(cl4, 1024, L"schtasks.exe /create /tn VistaOobeStartGlass /tr \"\\\"%s\\\" /startglass\" /sc onlogon /ru SYSTEM /rl highest /f", me4); cl4[1023] = 0;
        STARTUPINFOW s4; memset(&s4, 0, sizeof(s4)); s4.cb = sizeof(s4); PROCESS_INFORMATION p4;
        if (CreateProcessW(NULL, cl4, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &s4, &p4)) {
            WaitForSingleObject(p4.hProcess, 15000); DWORD ec4 = 1; GetExitCodeProcess(p4.hProcess, &ec4); CloseHandle(p4.hThread); CloseHandle(p4.hProcess);
            Log(L"startglass: sign-in task created (schtasks exit code %lu)", ec4);
        } else Log(L"startglass: schtasks could not be started (%lu)", GetLastError());
    }
    if (code == 0 && GetFileAttributesW(L"C:\\vista-oobe\\noglassreload.txt") == INVALID_FILE_ATTRIBUTES) {
        wchar_t me3[MAX_PATH], rc3[MAX_PATH + 40]; GetModuleFileNameW(NULL, me3, MAX_PATH);
        _snwprintf(rc3, MAX_PATH + 40, L"\"%s\" /glassreload", me3); rc3[MAX_PATH + 39] = 0;
        LONG r3 = RegSetKeyValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", L"VistaOobeGlassReload", REG_SZ, rc3, (DWORD)((wcslen(rc3) + 1) * 2));
        Log(L"glassreload: first sign-in task registered (result %ld)", r3);
    }
    if (code == 0 && GetFileAttributesW(L"C:\\vista-oobe\\fixshell.txt") != INVALID_FILE_ATTRIBUTES) {
        wchar_t me2[MAX_PATH], cl[1024]; GetModuleFileNameW(NULL, me2, MAX_PATH);
        _snwprintf(cl, 1024, L"schtasks.exe /create /tn VistaOobeFixShell /tr \"\\\"%s\\\" /fixshell\" /sc onlogon /ru SYSTEM /rl highest /f", me2); cl[1023] = 0;
        STARTUPINFOW s2; memset(&s2, 0, sizeof(s2)); s2.cb = sizeof(s2); PROCESS_INFORMATION p2;
        if (CreateProcessW(NULL, cl, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &s2, &p2)) {
            WaitForSingleObject(p2.hProcess, 15000); DWORD ec = 1; GetExitCodeProcess(p2.hProcess, &ec); CloseHandle(p2.hThread); CloseHandle(p2.hProcess);
            Log(L"fixshell: sign-in task created (schtasks exit code %lu)", ec);
        } else Log(L"fixshell: schtasks could not be started (%lu)", GetLastError());
    }
    int crashed = GetFileAttributesW(L"C:\\vista-oobe\\crash.flag") != INVALID_FILE_ATTRIBUTES;
    if (crashed) Log(L"the proxy recorded a crash (crash.flag)");
    if ((code & 0xF0000000) == 0xC0000000 || code == 0xFF || (code != 0 && (secs < 3 || crashed)))
        return RunWindowsOwn(afterMe, L"the Vista wizard crashed or failed");
    return (int)code;
}
