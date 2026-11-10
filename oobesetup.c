#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static void Log(const wchar_t* message, DWORD error) {
    wchar_t path[MAX_PATH], line[1024];
    char utf8[4096];
    if (!GetWindowsDirectoryW(path, MAX_PATH - 24)) return;
    wcscat(path, L"\\Temp\\vista-oobe.log");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    _snwprintf(line, 1023, L"oobesetup(%lu): %s (error %lu)\r\n", GetCurrentProcessId(), message, error);
    line[1023] = 0;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), NULL, NULL);
    DWORD written;
    if (bytes > 1) WriteFile(file, utf8, (DWORD)(bytes - 1), &written, NULL);
    CloseHandle(file);
}

static const wchar_t* SkipExecutable(const wchar_t* commandLine) {
    while (*commandLine == L' ' || *commandLine == L'\t') commandLine++;
    if (*commandLine == L'"') {
        commandLine++;
        while (*commandLine && *commandLine != L'"') commandLine++;
        if (*commandLine) commandLine++;
    } else {
        while (*commandLine && *commandLine != L' ' && *commandLine != L'\t') commandLine++;
    }
    while (*commandLine == L' ' || *commandLine == L'\t') commandLine++;
    return commandLine;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int show) {
    (void)instance;
    (void)previous;
    (void)commandLine;
    (void)show;

    wchar_t module[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, module, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        Log(L"could not resolve setup wrapper path", GetLastError());
        return 1;
    }

    wchar_t* slash = wcsrchr(module, L'\\');
    if (!slash) {
        Log(L"setup wrapper path has no parent directory", ERROR_BAD_PATHNAME);
        return 1;
    }
    *slash = 0;

    wchar_t vistaSetup[MAX_PATH];
    if (_snwprintf(vistaSetup, MAX_PATH, L"%s\\vista-setup.exe", module) < 0) {
        Log(L"Vista setup path is too long", ERROR_BUFFER_OVERFLOW);
        return 1;
    }
    vistaSetup[MAX_PATH - 1] = 0;
    if (GetFileAttributesW(vistaSetup) == INVALID_FILE_ATTRIBUTES) {
        Log(L"vista-setup.exe not found beside setup wrapper", GetLastError());
        return 1;
    }
    wchar_t winsetup[MAX_PATH];
    if (_snwprintf(winsetup, MAX_PATH, L"%s\\winsetup.dll", module) < 0) {
        Log(L"winsetup.dll path is too long", ERROR_BUFFER_OVERFLOW);
        return 1;
    }
    winsetup[MAX_PATH - 1] = 0;
    if (GetFileAttributesW(winsetup) == INVALID_FILE_ATTRIBUTES) {
        Log(L"winsetup.dll not found beside setup wrapper", GetLastError());
        return 1;
    }
    Log(L"winsetup.dll exists beside the setup host", 0);

    if (!SetCurrentDirectoryW(module) || !SetDllDirectoryW(module)) {
        Log(L"could not set OOBE directory as current directory and DLL search path", GetLastError());
        return 1;
    }

    wchar_t childCommandLine[32768];
    const wchar_t* args = SkipExecutable(GetCommandLineW());
    int formatted = _snwprintf(childCommandLine, 32767, L"\"%s\"%s%ls",
        vistaSetup, *args ? L" " : L"", args);
    if (formatted < 0 || formatted >= 32767) {
        Log(L"Vista setup command line is too long", ERROR_BUFFER_OVERFLOW);
        return 1;
    }
    childCommandLine[32767] = 0;

    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    GetStartupInfoW(&startup);

    Log(L"launching vista-setup.exe with OOBE directory as working directory and DLL search path", 0);
    if (!CreateProcessW(vistaSetup, childCommandLine, NULL, NULL, TRUE, 0, NULL, module, &startup, &process)) {
        DWORD error = GetLastError();
        Log(L"could not start vista-setup.exe", error);
        return (int)error;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        DWORD error = GetLastError();
        Log(L"could not retrieve Vista setup exit code", error);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return (int)error;
    }
    Log(L"vista-setup.exe exited", exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)exitCode;
}
