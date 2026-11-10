/* winbrand.dll proxy for the Vista msoobe.exe on Windows 10.
 *
 * Vista's msoobe.exe imports ONE function from winbrand.dll (BrandingLoadBitmap). Put this DLL next to it:
 * Windows loads it first (application folder), so it runs inside msoobe.exe before anything else, and
 *   - passes BrandingLoadBitmap on to the real C:\Windows\System32\winbrand.dll,
 *   - fixes DUser's SetGadgetRootInfo for the old Vista 0x20-byte structure (Windows 10 wants 0x30),
 *   - traces the DUser calls msoobe.exe makes (arguments + result), to find which one fails,
 *   - logs crashes with a call stack, and leaves C:\vista-oobe\crash.flag so the launcher can fall back,
 *   - writes everything to C:\vista-oobe\vista-oobe.log and %SystemRoot%\Temp\vista-oobe.log.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -Wall -shared -o winbrand.dll winbrand_proxy.c winbrand.def -static-libgcc -lkernel32
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

void* g_real = 0;                       /* real winbrand!BrandingLoadBitmap (used by the asm stub below) */

static void Log(const wchar_t* fmt, ...);
/* ---- own branding: <oobe folder>\branding\<dll>_<id>.(bmp|png|jpg|gif), e.g. Basebrd_121.png replaces the banner
   the wizard asks Windows for with BrandingLoadBitmap(L"Basebrd", 121). Also accepted for id 121: banner.(bmp|png|jpg|gif) ---- */
typedef HBITMAP (WINAPI *BLB_t)(LPCWSTR, UINT);
typedef struct { UINT32 v; void* a; BOOL b; BOOL c; } GdipStartupInput_;
static HBITMAP LoadBrandingFile(const wchar_t* path) {
    wchar_t* dot = wcsrchr((wchar_t*)path, L'.');
    if (dot && !_wcsicmp(dot, L".bmp"))
        return (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    HMODULE g = LoadLibraryW(L"gdiplus.dll"); if (!g) return NULL;
    typedef int (WINAPI *Startup_t)(ULONG_PTR*, const void*, void*);
    typedef int (WINAPI *FromFile_t)(const WCHAR*, void**);
    typedef int (WINAPI *ToHBitmap_t)(void*, HBITMAP*, DWORD);
    typedef int (WINAPI *Dispose_t)(void*);
    Startup_t st = (Startup_t)GetProcAddress(g, "GdiplusStartup");
    FromFile_t ff = (FromFile_t)GetProcAddress(g, "GdipCreateBitmapFromFile");
    ToHBitmap_t th = (ToHBitmap_t)GetProcAddress(g, "GdipCreateHBITMAPFromBitmap");
    Dispose_t di = (Dispose_t)GetProcAddress(g, "GdipDisposeImage");
    if (!st || !ff || !th || !di) return NULL;
    static ULONG_PTR tok;
    if (!tok) { GdipStartupInput_ in = { 1, NULL, FALSE, FALSE }; if (st(&tok, &in, NULL)) { tok = 0; return NULL; } }
    void* bmp = NULL; HBITMAP hb = NULL;
    if (ff(path, &bmp) == 0 && bmp) { th(bmp, &hb, 0xFFFFFFFF); di(bmp); }   /* transparent parts become white */
    return hb;
}
HBITMAP WINAPI BrandingLoadBitmap(LPCWSTR name, UINT id) {
    wchar_t home[MAX_PATH]; GetModuleFileNameW(NULL, home, MAX_PATH);
    wchar_t* sl = wcsrchr(home, L'\\'); if (sl) *sl = 0;
    static const wchar_t* ext[] = { L"png", L"bmp", L"jpg", L"gif" };
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1 && id != 121) break;
        for (int k = 0; k < 4; k++) {
            wchar_t p[MAX_PATH * 2];
            if (pass == 0) _snwprintf(p, MAX_PATH * 2, L"%s\\branding\\%s_%u.%s", home, name ? name : L"", id, ext[k]);
            else _snwprintf(p, MAX_PATH * 2, L"%s\\branding\\banner.%s", home, ext[k]);
            p[MAX_PATH * 2 - 1] = 0;
            if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) continue;
            HBITMAP hb = LoadBrandingFile(p);
            Log(L"[branding] BrandingLoadBitmap(%s, %u): custom file %s -> %p", name ? name : L"(null)", id, p, hb);
            if (hb) return hb;
        }
    }
    HBITMAP r = g_real ? ((BLB_t)g_real)(name, id) : NULL;
    static volatile LONG n = 0;
    if (InterlockedIncrement(&n) <= 6) Log(L"[branding] BrandingLoadBitmap(%s, %u) from Windows -> %p", name ? name : L"(null)", id, r);
    return r;
}

static void Log(const wchar_t* fmt, ...) {
    wchar_t line[1000], msg[860];
    va_list ap; va_start(ap, fmt); _vsnwprintf(msg, 859, fmt, ap); va_end(ap); msg[859] = 0;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t exe[MAX_PATH] = L"?"; GetModuleFileNameW(NULL, exe, MAX_PATH);
    const wchar_t* b = wcsrchr(exe, L'\\'); b = b ? b + 1 : exe;
    _snwprintf(line, 999, L"%02u:%02u:%02u.%03u %s(%lu) %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, b, GetCurrentProcessId(), msg);
    line[999] = 0;
    OutputDebugStringW(line);
    char u8[3000]; int k = WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, sizeof(u8), NULL, NULL);
    if (k <= 1) return;
    wchar_t path[MAX_PATH];
    if (!GetWindowsDirectoryW(path, MAX_PATH - 24)) path[0] = 0; else wcscat(path, L"\\Temp\\vista-oobe.log");
    const wchar_t* targets[2] = { path, L"C:\\vista-oobe\\vista-oobe.log" };      /* second copy, easy to find */
    for (int i = 0; i < 2; i++) {
        if (!targets[i][0]) continue;
        HANDLE f = CreateFileW(targets[i], FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        DWORD wr; WriteFile(f, u8, k - 1, &wr, NULL); CloseHandle(f);
    }
}

/* memory check without exceptions (IsBadReadPtr raises a handled access violation that the crash logger would report) */
static int Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    const BYTE* a = (const BYTE*)p; const BYTE* end = a + n;
    while (a < end) {
        if (!VirtualQuery(a, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return 0;
        a = (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}
static int Writable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD | PAGE_READONLY | PAGE_EXECUTE_READ)) return 0;
    return (const BYTE*)p + n <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

/* ---- crash logger ---- */
static void Describe(ULONG_PTR a, wchar_t* out, size_t cch) {
    HMODULE m = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)a, &m) && m) {
        wchar_t p[MAX_PATH]; GetModuleFileNameW(m, p, MAX_PATH);
        const wchar_t* b = wcsrchr(p, L'\\'); b = b ? b + 1 : p;
        _snwprintf(out, cch, L"%s+0x%llX", b, (unsigned long long)(a - (ULONG_PTR)m));
    } else _snwprintf(out, cch, L"%p", (void*)a);
    out[cch - 1] = 0;
}
static ULONG_PTR c0_rip(PEXCEPTION_POINTERS ep){ return (ULONG_PTR)ep->ContextRecord->Rip; }
static LONG CALLBACK Crash(PEXCEPTION_POINTERS ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters > 1 && ep->ExceptionRecord->ExceptionInformation[1] == 0x78) {
        /* Windows 10 DUser's destroy-gadget function (duser+0x11A00) called with a null gadget during window clean-up:
           skip it (return 0 to its caller, undoing its 'push rbx / sub rsp,0x40'). */
        HMODULE du = GetModuleHandleW(L"duser.dll");
        if (du && c0_rip(ep) == (ULONG_PTR)du + 0x11A08) {
            CONTEXT* c = ep->ContextRecord;
            DWORD64* sp = (DWORD64*)c->Rsp;
            c->Rbx = sp[8];            /* saved rbx at [rsp+0x40] */
            c->Rip = sp[9];            /* return address at [rsp+0x48] */
            c->Rsp += 0x50; c->Rax = 0;
            Log(L"[guard] DUser clean-up on a null gadget skipped (the wizard's page set-up had failed)");
            HANDLE fl = CreateFileW(L"C:\\vista-oobe\\crash.flag", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (fl != INVALID_HANDLE_VALUE) CloseHandle(fl);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if (code != 0xC0000005 && code != 0xC0000409 && code != 0xC000001D && code != 0xC0000094 && code != 0xC0000374) return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG n = 0;
    if (InterlockedIncrement(&n) > 6) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord; wchar_t w[300];
    Describe((ULONG_PTR)c->Rip, w, 300);
    Log(L"[crash] exception 0x%08lX at %s  addr=%p", code, w, (void*)(ULONG_PTR)(ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0));
    Log(L"[crash]   rcx=%p rdx=%p r8=%p r9=%p rbx=%p rsi=%p rdi=%p", (void*)c->Rcx, (void*)c->Rdx, (void*)c->R8, (void*)c->R9, (void*)c->Rbx, (void*)c->Rsi, (void*)c->Rdi);
    CONTEXT cc = *c;
    for (int i = 0; i < 16; i++) {
        DWORD64 base = 0; RUNTIME_FUNCTION* fe = RtlLookupFunctionEntry(cc.Rip, &base, NULL);
        if (!fe) { if (!cc.Rsp) break; cc.Rip = *(DWORD64*)cc.Rsp; cc.Rsp += 8; }
        else { PVOID hd = NULL; DWORD64 ef = 0; RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, cc.Rip, fe, &cc, &hd, &ef, NULL); }
        if (!cc.Rip) break;
        Describe((ULONG_PTR)cc.Rip, w, 300);
        Log(L"[crash]   frame %d: %s", i, w);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- DUser SetGadgetRootInfo fix ---- */
typedef BOOL (WINAPI *SGRI_t)(HANDLE, void*);
static SGRI_t g_origSGRI;
static BOOL WINAPI Hook_SGRI(HANDLE g, void* info) {
    BOOL r = g_origSGRI(g, info);
    DWORD le = GetLastError(), cb = 0;
    if (info && Readable(info, 4)) cb = *(DWORD*)info;
    static volatile LONG n = 0;
    if (InterlockedIncrement(&n) <= 12) Log(L"[duser] SetGadgetRootInfo(gadget=%p, cbSize=0x%X) -> %d (error %lu)", g, cb, r, r ? 0 : le);
    if (r || !info || cb != 0x20) { SetLastError(le); return r; }
    __attribute__((aligned(16))) BYTE big[0x30]; memset(big, 0, sizeof(big));
    memcpy(big, info, 0x20); *(DWORD*)big = 0x30;
    BOOL r2 = g_origSGRI(g, big);
    static volatile LONG logged = 0;
    if (InterlockedExchange(&logged, 1) == 0) Log(L"[duser] SetGadgetRootInfo: old 0x20 structure retried as 0x30 -> %d", r2);
    if (!r2) SetLastError(le);
    return r2;
}

/* ---- trace of the other DUser calls: name, caller (offset inside msoobe.exe), arguments, result ---- */
typedef UINT_PTR (WINAPI *Fn8)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
#define NTRACE 40
static struct { const char* name; Fn8 orig; volatile LONG count; } g_tr[NTRACE];
static int g_ntr;
static volatile LONG g_totalTrace;
/* a readable wide string at p (printable ASCII, 2..40 chars), for the log */
static int WStr(UINT_PTR p, wchar_t* out, int cch) {
    out[0] = 0;
    if (p < 0x10000 || !Readable((void*)p, 4)) return 0;
    const wchar_t* w = (const wchar_t*)p; int n = 0;
    for (; n < cch - 1; n++) {
        if (((UINT_PTR)&w[n] & 0xFFF) < 2 && !Readable(&w[n], 2)) return 0;     /* page boundary */
        wchar_t c = w[n];
        if (c == 0) break;
        if (c < 0x20 || c > 0x7e) return 0;
        out[n] = c;
    }
    out[n] = 0;
    return n >= 2;
}
static void LogStack(const wchar_t* what) {
    void* fr[16]; USHORT n = RtlCaptureStackBackTrace(1, 14, fr, NULL);
    wchar_t line[900]; int k = 0; line[0] = 0;
    BYTE* exe = (BYTE*)GetModuleHandleW(NULL);
    for (USHORT i = 0; i < n && k < 800; i++) {
        wchar_t d[120]; Describe((ULONG_PTR)fr[i], d, 120);
        k += _snwprintf(line + k, 899 - k, L" %s", d);
    }
    (void)exe;
    Log(L"[stack] %s:%s", what, line);
}
/* which HWND-hosted gadget belongs to which window, and whether the wizard has deleted it already */
static struct { UINT_PTR gadget; UINT_PTR hwnd; volatile LONG deleted; } g_host[8];
static void TrackHosts(const char* name, UINT_PTR a, UINT_PTR b, UINT_PTR r) {
    if (!strcmp(name, "CreateGadget") && a && b == 1 && r) {                 /* flags 1 = HWND host */
        for (int k = 0; k < 8; k++) if (!g_host[k].gadget) { g_host[k].hwnd = a; g_host[k].deleted = 0; g_host[k].gadget = r; break; }
    } else if (!strcmp(name, "DeleteHandle")) {
        for (int k = 0; k < 8; k++) if (g_host[k].gadget == a) g_host[k].deleted = 1;
    }
}
static void TraceLog(int i, void* ret, UINT_PTR a, UINT_PTR b, UINT_PTR c, UINT_PTR d, UINT_PTR e, UINT_PTR f, UINT_PTR r) {
    TrackHosts(g_tr[i].name, a, b, r);
    LONG cnt = InterlockedIncrement(&g_tr[i].count);
    if (cnt > 8 || InterlockedIncrement(&g_totalTrace) > 200) return;
    ULONG_PTR rva = (ULONG_PTR)ret - (ULONG_PTR)GetModuleHandleW(NULL);
    wchar_t s1[48], s2[48];
    int h1 = WStr(a, s1, 48), h2 = WStr(b, s2, 48);
    Log(L"[duser] %S(%p%s%s%s, %p%s%s%s, %p, %p, %p, %p) from +0x%llX -> %p", g_tr[i].name,
        (void*)a, h1 ? L" \"" : L"", s1, h1 ? L"\"" : L"",
        (void*)b, h2 ? L" \"" : L"", s2, h2 ? L"\"" : L"",
        (void*)c, (void*)d, (void*)e, (void*)f, (unsigned long long)rva, (void*)r);
    /* who is deleting / creating gadgets: the call chain shows the function that gave up */
    if (!strcmp(g_tr[i].name, "DeleteHandle") && cnt <= 3) LogStack(L"DeleteHandle");
    if (!strcmp(g_tr[i].name, "CreateGadget") && cnt <= 3) LogStack(L"CreateGadget");
}
#define TW(i) static UINT_PTR WINAPI TW##i(UINT_PTR a, UINT_PTR b, UINT_PTR c, UINT_PTR d, UINT_PTR e, UINT_PTR f, UINT_PTR g, UINT_PTR h) { \
    void* ret = __builtin_return_address(0); \
    UINT_PTR r = g_tr[i].orig(a, b, c, d, e, f, g, h); \
    TraceLog(i, ret, a, b, c, d, e, f, r); return r; }
TW(0) TW(1) TW(2) TW(3) TW(4) TW(5) TW(6) TW(7) TW(8) TW(9) TW(10) TW(11) TW(12) TW(13) TW(14) TW(15) TW(16) TW(17) TW(18) TW(19) TW(20) TW(21) TW(22) TW(23) TW(24) TW(25) TW(26) TW(27) TW(28) TW(29) TW(30) TW(31) TW(32) TW(33) TW(34) TW(35) TW(36) TW(37) TW(38) TW(39)
#define TWP(i) (void*)&TW##i
static void* g_tw[NTRACE] = { TWP(0),TWP(1),TWP(2),TWP(3),TWP(4),TWP(5),TWP(6),TWP(7),TWP(8),TWP(9),TWP(10),TWP(11),TWP(12),TWP(13),TWP(14),TWP(15),TWP(16),TWP(17),TWP(18),TWP(19),
                              TWP(20),TWP(21),TWP(22),TWP(23),TWP(24),TWP(25),TWP(26),TWP(27),TWP(28),TWP(29),TWP(30),TWP(31),TWP(32),TWP(33),TWP(34),TWP(35),TWP(36),TWP(37),TWP(38),TWP(39) };
/* DUser functions that may take floating-point arguments are not traced (the pass-through only forwards integers) */
static int Traceable(const char* n) {
    static const char* skip[] = { "BuildInterpolation", "BuildAnimation", "GetGadgetAnimation", "CreateAction", "UtilDrawBlendRect", "SetGadgetBufferInfo", "GetStdColorBrushI", "SetGadgetRootInfo", NULL };
    for (int i = 0; skip[i]; i++) if (!strcmp(skip[i], n)) return 0;
    return 1;
}


/* ---- patch one import of msoobe.exe by name; returns the original function ---- */
static void* PatchImport(const char* dll, const char* fn, void* hook) {
    BYTE* b = (BYTE*)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    DWORD ir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(b + ir); ir && d->Name; d++) {
        if (_stricmp((const char*)(b + d->Name), dll)) continue;
        ULONGLONG* ilt = (ULONGLONG*)(b + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        ULONGLONG* iat = (ULONGLONG*)(b + d->FirstThunk);
        for (int i = 0; ilt[i]; i++) {
            if (ilt[i] & IMAGE_ORDINAL_FLAG64) continue;
            if (strcmp((const char*)((IMAGE_IMPORT_BY_NAME*)(b + ilt[i]))->Name, fn)) continue;
            DWORD op; if (!VirtualProtect(&iat[i], 8, PAGE_READWRITE, &op)) return NULL;
            void* orig = (void*)iat[i]; iat[i] = (ULONGLONG)hook;
            VirtualProtect(&iat[i], 8, op, &op);
            return orig;
        }
    }
    return NULL;
}

/* ---- the wizard's own debug output (DirectUI parser errors: "DUIXMLParserFailure ... DUI_E_...") ---- */
typedef void (WINAPI *ODSW_t)(LPCWSTR);
typedef void (WINAPI *ODSA_t)(LPCSTR);
static ODSW_t g_origODSW; static ODSA_t g_origODSA;
static volatile LONG g_odsCount;
static void WINAPI Hook_ODSW(LPCWSTR s) {
    if (s && Readable(s, 2) && InterlockedIncrement(&g_odsCount) <= 120) {
        wchar_t t[700]; size_t n = 0;
        while (n < 699 && Readable(&s[n], 2) && s[n]) { t[n] = (s[n] == L'\r' || s[n] == L'\n') ? L' ' : s[n]; n++; }
        t[n] = 0;
        Log(L"[exe-debug] %s", t);
    }
    g_origODSW(s);
}
static void WINAPI Hook_ODSA(LPCSTR s) {
    if (s && Readable(s, 1) && InterlockedIncrement(&g_odsCount) <= 120) {
        wchar_t t[700]; size_t n = 0;
        while (n < 699 && Readable(&s[n], 1) && s[n]) { t[n] = (s[n] == '\r' || s[n] == '\n') ? L' ' : (unsigned char)s[n]; n++; }
        t[n] = 0;
        Log(L"[exe-debug] %s", t);
    }
    g_origODSA(s);
}

/* ---- guard for a Windows 10 DUser bug (duser.dll 10.0.19041.x, checked by reading its code) ----
 * When a DUser-hosted window is destroyed, DUser reads the window's record (GWLP_USERDATA): record+0x10 = host object,
 * host+0x10 = the gadget, record+0x18 = flag. If the flag is 0 it calls its destroy-gadget function on the gadget WITHOUT
 * checking it. Vista's wizard deletes the gadget first and then calls DestroyWindow, so DUser uses a gadget pointer that is
 * null or already freed and crashes. Setting the flag to 1 first makes DUser skip that call. The guard applies to windows
 * whose host gadget the wizard has deleted (tracked from the DUser calls), or whose gadget pointer is null. */
typedef BOOL (WINAPI *DestroyWindow_t)(HWND);
static DestroyWindow_t g_origDW;
static BOOL WINAPI Hook_DestroyWindow(HWND h) {
    int deleted = 0;
    for (int k = 0; k < 8; k++) if (g_host[k].gadget && g_host[k].hwnd == (UINT_PTR)h && g_host[k].deleted) deleted = 1;
    BYTE* ud = (BYTE*)GetWindowLongPtrA(h, GWLP_USERDATA);
    if (ud && Readable(ud, 0x20) && Writable(ud + 0x18, 4) && *(DWORD*)(ud + 0x18) == 0) {
        BYTE* host = *(BYTE**)(ud + 0x10);
        int nullGadget = host && Readable(host, 0x18) && *(void**)(host + 0x10) == NULL;
        HMODULE du = GetModuleHandleW(L"duser.dll"); LONG_PTR wp = GetWindowLongPtrW(h, GWLP_WNDPROC);
        int inDuser = 0;
        if (du && wp) { BYTE* d0 = (BYTE*)du; DWORD dsz = ((IMAGE_NT_HEADERS64*)(d0 + ((IMAGE_DOS_HEADER*)d0)->e_lfanew))->OptionalHeader.SizeOfImage; inDuser = (BYTE*)wp >= d0 && (BYTE*)wp < d0 + dsz; }
        if (host && (deleted || (inDuser && nullGadget))) {
            *(DWORD*)(ud + 0x18) = 1;
            static volatile LONG n = 0;
            if (InterlockedIncrement(&n) <= 3) Log(L"[guard] DestroyWindow(%p): the DUser gadget is already deleted - DUser crash avoided (the wizard's page set-up had failed)", h);
            HANDLE fl = CreateFileW(L"C:\\vista-oobe\\crash.flag", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (fl != INVALID_HANDLE_VALUE) CloseHandle(fl);                 /* tell the launcher this run failed */
        }
    }
    return g_origDW(h);
}

/* ---- Vista's DUser has a default style sheet "common" as UIFILE 1010 (XML text); Windows 10's DUser has the same
   resource id but in a compiled binary format ("duib") that the wizard's own XML parser cannot read. The wizard looks up
   its sheet "common" there, fails, and the page set-up is abandoned. Serve an XML version of that resource instead. ---- */
static const char g_commonXml[] =
"<duixml>\r\n<stylesheets>\r\n<style resid=\"common\">\r\n"
"<element font=\"gtf(TextStyle, TEXT_BODYTEXT, 0)\" foreground=\"windowtext\"/>\r\n"
"<if class=\"para\"><element contentalign=\"wrapleft\" padding=\"rect(0,40rp,0,10rp)\"/></if>\r\n"
"<if class=\"step\"><element padding=\"rect(0,5rp,0,5rp)\"/></if>\r\n"
"</style>\r\n</stylesheets>\r\n</duixml>\r\n";
static char g_fakeRes[4];
typedef HRSRC (WINAPI *FindResW_t)(HMODULE, LPCWSTR, LPCWSTR);
typedef HGLOBAL (WINAPI *LoadRes_t)(HMODULE, HRSRC);
typedef LPVOID (WINAPI *LockRes_t)(HGLOBAL);
typedef DWORD (WINAPI *SizeRes_t)(HMODULE, HRSRC);
static FindResW_t g_origFR; static LoadRes_t g_origLR; static LockRes_t g_origKR; static SizeRes_t g_origSR;
static HRSRC WINAPI Hook_FindResourceW(HMODULE m, LPCWSTR n, LPCWSTR t) {
    if (m && m == GetModuleHandleW(L"duser.dll") && (ULONG_PTR)n == 1010) {
        static volatile LONG k = 0;
        if (InterlockedIncrement(&k) <= 3) Log(L"[resource] duser.dll UIFILE 1010 (default style sheet 'common') served as XML instead of Windows 10's binary version");
        return (HRSRC)g_fakeRes;
    }
    return g_origFR(m, n, t);
}
static HGLOBAL WINAPI Hook_LoadResource(HMODULE m, HRSRC r) { return r == (HRSRC)g_fakeRes ? (HGLOBAL)g_commonXml : g_origLR(m, r); }
static LPVOID WINAPI Hook_LockResource(HGLOBAL g) { return g == (HGLOBAL)g_commonXml ? (LPVOID)g_commonXml : g_origKR(g); }
static DWORD WINAPI Hook_SizeofResource(HMODULE m, HRSRC r) { return r == (HRSRC)g_fakeRes ? (DWORD)(sizeof(g_commonXml) - 1) : g_origSR(m, r); }

/* ---- "Basic" window frame (what BasicThemer2 does): turn off DWM non-client rendering for the wizard's top-level
   windows with DwmSetWindowAttribute(DWMWA_NCRENDERING_POLICY = DWMNCRP_DISABLED). Create C:\vista-oobe\aero.txt to keep Aero. ---- */
typedef HWND (WINAPI *CreateWindowExW_t)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef HRESULT (WINAPI *DwmSetWA_t)(HWND, DWORD, LPCVOID, DWORD);
static CreateWindowExW_t g_origCW;

/* Basic frame (like the Windhawk "Basic Themer" mod): DWMWA_NCRENDERING_POLICY = DISABLED, but ONLY for windows that have
   WS_CAPTION - on captionless windows it breaks the frame. Checked when the window is created and again when it is shown
   (the style may be set later). Also gives the transparent glass areas the Basic frame colour. */
static void ApplyBasic(HWND h) {
    static DwmSetWA_t fn; static int tried;
    static HWND done[32]; static int nd;
    if (!tried) {
        tried = 1;
        if (GetFileAttributesW(L"C:\\vista-oobe\\aero.txt") == INVALID_FILE_ATTRIBUTES) {
            HMODULE d = LoadLibraryW(L"dwmapi.dll");
            if (d) fn = (DwmSetWA_t)GetProcAddress(d, "DwmSetWindowAttribute");
        }
    }
    if (!fn || !h || (GetWindowLongW(h, GWL_STYLE) & WS_CHILD)) return;
    DWORD st = (DWORD)GetWindowLongW(h, GWL_STYLE);
    static HBRUSH basic;
    if (!basic) basic = CreateSolidBrush(RGB(0xBC, 0xD0, 0xE8));
    if (basic) SetClassLongPtrW(h, GCLP_HBRBACKGROUND, (LONG_PTR)basic);
    if ((st & WS_CAPTION) != WS_CAPTION) {
        static volatile LONG k1 = 0;
        if (InterlockedIncrement(&k1) <= 6) Log(L"[theme] window %p has no WS_CAPTION (style 0x%08lX): DWM frame setting not applied, background colour only", h, st);
        return;
    }
    for (int i = 0; i < nd; i++) if (done[i] == h) return;
    if (nd < 32) done[nd++] = h;
    DWORD policy = 1;                                      /* DWMNCRP_DISABLED */
    HRESULT hr = fn(h, 2, &policy, sizeof(policy));       /* DWMWA_NCRENDERING_POLICY */
    Log(L"[theme] basic frame applied to window %p (style 0x%08lX, hr=0x%08lX)", h, st, (unsigned long)hr);
}
static HWND WINAPI Hook_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    HWND r = g_origCW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    {
        static volatile LONG kw = 0;
        if (r && (((style & WS_CHILD) ? InterlockedIncrement(&kw) <= 25 : InterlockedIncrement(&kw) <= 400))) {
            wchar_t cn[80] = L"?"; if (!((ULONG_PTR)cls >> 16 == 0)) { wcsncpy(cn, cls, 79); cn[79] = 0; } else swprintf(cn, 80, L"#%u", (unsigned)(ULONG_PTR)cls);
            Log(L"[window] CreateWindowEx hwnd=%p class=%.60s title=%.40s style=0x%08lX ex=0x%08lX rect=%d,%d %dx%d parent=%p", r, cn, (name && ((ULONG_PTR)name >> 16)) ? name : L"", style, ex, x, y, w, h, parent);
        }
    }
    if (r && !(style & WS_CHILD) && !parent) ApplyBasic(r);
    return r;
}

/* ---- more diagnostics: which windows / libraries / processes the wizard uses (to find what happens after the last page) ---- */
typedef HMODULE (WINAPI *LoadLibW_t)(LPCWSTR);
typedef BOOL (WINAPI *ShowWin_t)(HWND, int);
typedef BOOL (WINAPI *CreateProcW_t)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
static LoadLibW_t g_origLL; static ShowWin_t g_origSW; static CreateProcW_t g_origCP;
static HMODULE WINAPI Hook_LoadLibraryW(LPCWSTR n) {
    HMODULE r = g_origLL(n); DWORD le = GetLastError();
    static volatile LONG k = 0;
    if (InterlockedIncrement(&k) <= 60) Log(L"[loadlib] %.120s -> %p%s", n ? n : L"(null)", r, r ? L"" : L" (FAILED)");
    SetLastError(le); return r;
}
static BOOL WINAPI Hook_ShowWindow(HWND h, int c) {
    static volatile LONG k = 0;
    if (h && !(GetWindowLongW(h, GWL_STYLE) & WS_CHILD) && InterlockedIncrement(&k) <= 200) { wchar_t cn[64] = L"?"; GetClassNameW(h, cn, 64); Log(L"[window] ShowWindow(%p, %d) class=%.60s", h, c, cn); }
    if (c != SW_HIDE && h) ApplyBasic(h);
    return g_origSW(h, c);
}
static BOOL WINAPI Hook_CreateProcessW(LPCWSTR a, LPWSTR cl, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD fl, LPVOID env, LPCWSTR cd, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    BOOL r = g_origCP(a, cl, pa, ta, inh, fl, env, cd, si, pi); DWORD le = GetLastError();
    Log(L"[process] CreateProcess app=%.100s cmd=%.160s -> %d (error %lu)", a ? a : L"", cl ? cl : L"", r, r ? 0 : le);
    SetLastError(le); return r;
}

/* ---- "DWM Unextend Frames" (Windhawk mod by aubymori): make the process believe DWM composition is off, so uxtheme / DWM APIs do not use glass.
   Tiny functions, replaced with  mov rax,hook / jmp rax  (the originals are never needed). aero.txt switches it off. ---- */
static BOOL WINAPI Hook_IsCompositionActive(void) { return FALSE; }
static HRESULT WINAPI Hook_DwmIsCompositionEnabled(BOOL* p) { if (p) *p = FALSE; return S_OK; }
static HRESULT WINAPI Hook_DwmDisabled(HWND h, const void* x) { (void)h; (void)x; return (HRESULT)0x80263001; }   /* DWM_E_COMPOSITIONDISABLED */
static void InlineReplace(HMODULE m, const char* name, void* hook) {
    BYTE* f = m ? (BYTE*)GetProcAddress(m, name) : NULL;
    if (!f) { Log(L"[unextend] %S: not found", name); return; }
    DWORD op; if (!VirtualProtect(f, 12, PAGE_EXECUTE_READWRITE, &op)) { Log(L"[unextend] %S: cannot change protection", name); return; }
    f[0] = 0x48; f[1] = 0xB8; *(void**)(f + 2) = hook; f[10] = 0xFF; f[11] = 0xE0;
    VirtualProtect(f, 12, op, &op); FlushInstructionCache(GetCurrentProcess(), f, 12);
    Log(L"[unextend] %S replaced", name);
}
static DWORD WINAPI UnextendThread(LPVOID p) {
    (void)p;
    if (GetFileAttributesW(L"C:\\vista-oobe\\aero.txt") != INVALID_FILE_ATTRIBUTES) return 0;
    HMODULE ux = LoadLibraryW(L"uxtheme.dll"), dw = LoadLibraryW(L"dwmapi.dll");
    InlineReplace(ux, "IsCompositionActive", (void*)&Hook_IsCompositionActive);
    InlineReplace(dw, "DwmIsCompositionEnabled", (void*)&Hook_DwmIsCompositionEnabled);
    InlineReplace(dw, "DwmExtendFrameIntoClientArea", (void*)&Hook_DwmDisabled);
    InlineReplace(dw, "DwmEnableBlurBehindWindow", (void*)&Hook_DwmDisabled);
    return 0;
}
static void PatchExtras(void) {
    g_origDW = (DestroyWindow_t)PatchImport("user32.dll", "DestroyWindow", (void*)&Hook_DestroyWindow);
    g_origODSW = (ODSW_t)PatchImport("kernel32.dll", "OutputDebugStringW", (void*)&Hook_ODSW);
    g_origODSA = (ODSA_t)PatchImport("kernel32.dll", "OutputDebugStringA", (void*)&Hook_ODSA);
    CreateThread(NULL, 0, UnextendThread, NULL, 0, NULL);
    g_origFR = (FindResW_t)PatchImport("kernel32.dll", "FindResourceW", (void*)&Hook_FindResourceW);
    g_origLR = (LoadRes_t)PatchImport("kernel32.dll", "LoadResource", (void*)&Hook_LoadResource);
    g_origKR = (LockRes_t)PatchImport("kernel32.dll", "LockResource", (void*)&Hook_LockResource);
    g_origSR = (SizeRes_t)PatchImport("kernel32.dll", "SizeofResource", (void*)&Hook_SizeofResource);
    g_origCW = (CreateWindowExW_t)PatchImport("user32.dll", "CreateWindowExW", (void*)&Hook_CreateWindowExW);
    g_origLL = (LoadLibW_t)PatchImport("kernel32.dll", "LoadLibraryW", (void*)&Hook_LoadLibraryW);
    g_origSW = (ShowWin_t)PatchImport("user32.dll", "ShowWindow", (void*)&Hook_ShowWindow);
    g_origCP = (CreateProcW_t)PatchImport("kernel32.dll", "CreateProcessW", (void*)&Hook_CreateProcessW);
    Log(L"[guard] basic-frame hook %s", g_origCW ? L"installed" : L"NOT installed");
    Log(L"[guard] resource hooks %s", (g_origFR && g_origLR && g_origKR && g_origSR) ? L"installed" : L"NOT installed");
    Log(L"[guard] DestroyWindow guard %s; wizard debug output %s", g_origDW ? L"installed" : L"NOT installed", (g_origODSW || g_origODSA) ? L"captured" : L"NOT captured");
}

/* 1 = patched, 0 = not bound yet (try again later), -1 = not found */
static int PatchExe(void) {
    BYTE* b = (BYTE*)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    DWORD ir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!ir) return -1;
    HMODULE du = GetModuleHandleW(L"duser.dll");
    if (!du) return 0;
    BYTE* d0 = (BYTE*)du; DWORD dsz = ((IMAGE_NT_HEADERS64*)(d0 + ((IMAGE_DOS_HEADER*)d0)->e_lfanew))->OptionalHeader.SizeOfImage;
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(b + ir); d->Name; d++) {
        if (_stricmp((const char*)(b + d->Name), "duser.dll")) continue;
        ULONGLONG* ilt = (ULONGLONG*)(b + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        ULONGLONG* iat = (ULONGLONG*)(b + d->FirstThunk);
        int found = 0;
        for (int i = 0; ilt[i]; i++) {
            if (ilt[i] & IMAGE_ORDINAL_FLAG64) continue;
            const char* name = (const char*)((IMAGE_IMPORT_BY_NAME*)(b + ilt[i]))->Name;
            ULONGLONG v = iat[i];
            if (v < (ULONGLONG)d0 || v >= (ULONGLONG)d0 + dsz) return 0;            /* still the unbound name RVA */
            void* hook = NULL;
            if (!strcmp(name, "SetGadgetRootInfo")) { g_origSGRI = (SGRI_t)v; hook = (void*)&Hook_SGRI; found = 1; }
            else if (Traceable(name) && g_ntr < NTRACE) { g_tr[g_ntr].name = name; g_tr[g_ntr].orig = (Fn8)v; hook = g_tw[g_ntr]; g_ntr++; }
            if (!hook) continue;
            DWORD op; if (!VirtualProtect(&iat[i], 8, PAGE_READWRITE, &op)) continue;
            iat[i] = (ULONGLONG)hook;
            VirtualProtect(&iat[i], 8, op, &op);
        }
        if (found) PatchExtras();
        return found ? 1 : -1;
    }
    return -1;
}
static DWORD WINAPI Later(LPVOID p) {
    (void)p;
    for (int i = 0; i < 2000; i++) {
        int r = PatchExe();
        if (r) { Log(r > 0 ? L"[duser] hooks installed (after the loader finished): SetGadgetRootInfo fix + %d traced DUser functions" : L"[duser] DUser imports not found", g_ntr); return 0; }
        Sleep(5);
    }
    Log(L"[duser] gave up waiting for DUser to be bound");
    return 0;
}

/* ordinal imports of msoobe.exe: where they land on THIS Windows (a wrong function at an ordinal is a classic crash) */
static void LogOrdinals(void) {
    BYTE* b = (BYTE*)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    DWORD ir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!ir) return;
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(b + ir); d->Name; d++) {
        ULONGLONG* ilt = (ULONGLONG*)(b + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        ULONGLONG* iat = (ULONGLONG*)(b + d->FirstThunk);
        for (int i = 0; ilt[i]; i++) {
            if (!(ilt[i] & IMAGE_ORDINAL_FLAG64)) continue;
            wchar_t w[300]; Describe((ULONG_PTR)iat[i], w, 300);
            Log(L"[import] %S!#%u -> %s", (const char*)(b + d->Name), (unsigned)(ilt[i] & 0xFFFF), w);
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)res;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    wchar_t real[MAX_PATH]; GetSystemDirectoryW(real, MAX_PATH - 16); wcscat(real, L"\\winbrand.dll");
    HMODULE m = LoadLibraryW(real);
    if (m) g_real = (void*)GetProcAddress(m, "BrandingLoadBitmap");
    Log(L"---- winbrand proxy loaded; command line: %s", GetCommandLineW());
    Log(L"real winbrand.dll: %s (BrandingLoadBitmap %s)", m ? L"loaded" : L"NOT loaded", g_real ? L"found" : L"NOT found");
    AddVectoredExceptionHandler(1, Crash);
    int r = PatchExe();
    if (r > 0) Log(L"[duser] hooks installed: SetGadgetRootInfo fix + %d traced DUser functions", g_ntr);
    else if (r == 0) CreateThread(NULL, 0, Later, NULL, 0, NULL);
    else Log(L"[duser] DUser imports not found in this exe");
    LogOrdinals();
    return TRUE;
}
