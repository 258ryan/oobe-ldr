/* impcheck - lists the imports of a Vista PE file that THIS Windows cannot resolve.
 * Reads the file from disk (it is never run or loaded as code): for every imported DLL it loads the
 * Windows copy (first from the file's own folder, then System32) with DONT_RESOLVE_DLL_REFERENCES - no
 * DllMain runs - and checks each imported name / ordinal with GetProcAddress.
 *   impcheck.exe file1 [file2 ...]      -> prints to the console and to impcheck.txt next to the .exe
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static FILE* g_out;
static void out(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    if (g_out) { va_start(ap, fmt); vfprintf(g_out, fmt, ap); va_end(ap); fflush(g_out); }
}
static unsigned char* g_img; static DWORD g_size; static IMAGE_NT_HEADERS64* g_nt;
static IMAGE_SECTION_HEADER* g_sec; static int g_nsec;

static unsigned char* Rva(DWORD rva, DWORD need) {          /* RVA -> pointer into the file image */
    for (int i = 0; i < g_nsec; i++) {
        DWORD va = g_sec[i].VirtualAddress, sz = g_sec[i].Misc.VirtualSize > g_sec[i].SizeOfRawData ? g_sec[i].Misc.VirtualSize : g_sec[i].SizeOfRawData;
        if (rva >= va && rva < va + sz) {
            DWORD off = g_sec[i].PointerToRawData + (rva - va);
            if ((unsigned long long)off + need > g_size) return NULL;
            return g_img + off;
        }
    }
    if (rva + need <= g_nt->OptionalHeader.SizeOfHeaders && (unsigned long long)rva + need <= g_size) return g_img + rva;
    return NULL;
}

static HMODULE g_dirmods[64]; static char g_dirnames[64][260]; static int g_ndir;
static WCHAR g_folder[MAX_PATH];

static HMODULE LoadFor(const char* dll, int* fromLocal) {
    *fromLocal = 0;
    for (int i = 0; i < g_ndir; i++) if (!_stricmp(g_dirnames[i], dll)) { *fromLocal = (g_dirmods[i] != NULL) && g_dirmods[i] != (HMODULE)1; return g_dirmods[i] == (HMODULE)1 ? NULL : g_dirmods[i]; }
    WCHAR w[260], full[MAX_PATH * 2];
    MultiByteToWideChar(CP_ACP, 0, dll, -1, w, 260);
    HMODULE m = NULL;
    swprintf(full, MAX_PATH * 2, L"%s\\%s", g_folder, w);
    if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) { m = LoadLibraryExW(full, NULL, DONT_RESOLVE_DLL_REFERENCES); if (m) *fromLocal = 1; }
    if (!m) m = LoadLibraryExW(w, NULL, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) m = LoadLibraryExW(w, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (g_ndir < 64) { strncpy(g_dirnames[g_ndir], dll, 259); g_dirmods[g_ndir++] = m ? m : (HMODULE)1; }
    return m;
}

static int CheckThunks(const char* dll, DWORD intRva, int delay, int* missingOut) {
    HMODULE m = NULL; int local = 0, loaded = 0, total = 0, miss = 0;
    m = LoadFor(dll, &local); loaded = m != NULL;
    if (!loaded) { out("  NODLL   %s%s (this Windows does not have it)\n", dll, delay ? " [delay]" : ""); }
    for (DWORD i = 0;; i++) {
        ULONGLONG* t = (ULONGLONG*)Rva(intRva + i * 8, 8);
        if (!t || !*t) break;
        total++;
        char name[300]; const char* fn = NULL; WORD ord = 0;
        if (*t & IMAGE_ORDINAL_FLAG64) { ord = (WORD)(*t & 0xFFFF); snprintf(name, sizeof(name), "#%u", ord); }
        else { IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)Rva((DWORD)*t, 4); if (!ibn) break; fn = (const char*)ibn->Name; snprintf(name, sizeof(name), "%.250s", fn); }
        if (!loaded) { miss++; continue; }
        FARPROC p = fn ? GetProcAddress(m, fn) : GetProcAddress(m, (LPCSTR)(ULONG_PTR)ord);
        if (!p) { miss++; out("  MISSING %s!%s%s\n", dll, name, delay ? " [delay]" : ""); }
    }
    *missingOut += miss;
    return total;
}

static int CheckFile(const WCHAR* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { out("cannot open %ls\n", path); return 1; }
    g_size = GetFileSize(f, NULL); g_img = (unsigned char*)malloc(g_size);
    DWORD rd; ReadFile(f, g_img, g_size, &rd, NULL); CloseHandle(f);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_img;
    if (g_size < sizeof(*dos) || dos->e_magic != IMAGE_DOS_SIGNATURE) { out("%ls: not a PE file\n", path); free(g_img); return 1; }
    g_nt = (IMAGE_NT_HEADERS64*)(g_img + dos->e_lfanew);
    if (g_nt->Signature != IMAGE_NT_SIGNATURE || g_nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) { out("%ls: not a 64-bit PE file (this tool is 64-bit)\n", path); free(g_img); return 1; }
    g_sec = IMAGE_FIRST_SECTION(g_nt); g_nsec = g_nt->FileHeader.NumberOfSections;
    wcsncpy(g_folder, path, MAX_PATH - 1); g_folder[MAX_PATH - 1] = 0;
    WCHAR* sl = wcsrchr(g_folder, L'\\'); if (sl) *sl = 0; else wcscpy(g_folder, L".");
    out("\n=== %ls ===\n", path);
    int missing = 0, total = 0;
    DWORD ir = g_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (ir) for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)Rva(ir, sizeof(*d)); d && d->Name; d++) {
        const char* dll = (const char*)Rva(d->Name, 2); if (!dll) break;
        total += CheckThunks(dll, d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk, 0, &missing);
    }
    DWORD dr = g_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    if (dr) for (IMAGE_DELAYLOAD_DESCRIPTOR* d = (IMAGE_DELAYLOAD_DESCRIPTOR*)Rva(dr, sizeof(*d)); d && d->DllNameRVA; d++) {
        const char* dll = (const char*)Rva(d->DllNameRVA, 2); if (!dll) break;
        total += CheckThunks(dll, d->ImportNameTableRVA, 1, &missing);
    }
    out("  -> %d imports checked, %d unresolved\n", total, missing);
    free(g_img); g_img = NULL;
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    WCHAR me[MAX_PATH]; GetModuleFileNameW(NULL, me, MAX_PATH);
    WCHAR* sl = wcsrchr(me, L'\\'); if (sl) wcscpy(sl + 1, L"impcheck.txt"); else wcscpy(me, L"impcheck.txt");
    g_out = _wfopen(me, L"w");
    OSVERSIONINFOW vi; (void)vi;
    out("impcheck: imports that this Windows cannot resolve\n");
    if (argc < 2) { out("usage: impcheck.exe file1 [file2 ...]   (Vista .exe/.dll files)\n"); return 1; }
    for (int i = 1; i < argc; i++) {
        WCHAR full[MAX_PATH * 2]; GetFullPathNameW(argv[i], MAX_PATH * 2, full, NULL);
        CheckFile(full);
    }
    out("\nresults also saved in impcheck.txt next to impcheck.exe\n");
    return 0;
}
