// Top-level Windows entry. Sits beside the ZhiLi folder and starts
// ZhiLi\simplemap.exe. The first launch writes a desktop shortcut that uses
// this exe's icon. A later launch from a moved copy rewrites it. Deleting the
// shortcut leaves the marker, so it stays deleted. No admin and no HKLM.

#define WIN32_LEAN_AND_MEAN
#define INITGUID
#include <windows.h>
#include <shlobj.h>

#include <stdio.h>
#include <string.h>

static void fail_box(const wchar_t *msg) {
    MessageBoxW(NULL, msg, L"枝理", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

static const wchar_t *skip_argv0(const wchar_t *p) {
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"') p++;
        if (*p == L'"') p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t') p++;
    }
    while (*p == L' ' || *p == L'\t') p++;
    return p;
}

static bool exists_file(const wchar_t *path) {
    const DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool read_marker(const wchar_t *path, wchar_t *out, size_t cap) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    char buf[4096];
    DWORD n = 0;
    const BOOL ok = ReadFile(file, buf, sizeof buf - 1, &n, NULL);
    CloseHandle(file);
    if (!ok || n == 0) return false;
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, static_cast<int>(cap)) > 0;
}

static bool write_marker(const wchar_t *path, const wchar_t *exe) {
    wchar_t dir[MAX_PATH * 2];
    wcsncpy(dir, path, MAX_PATH * 2);
    dir[MAX_PATH * 2 - 1] = 0;
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash) return false;
    *slash = 0;
    CreateDirectoryW(dir, NULL);
    char utf[4096];
    const int n = WideCharToMultiByte(CP_UTF8, 0, exe, -1, utf, sizeof utf, NULL, NULL);
    if (n <= 1) return false;
    wchar_t tmp[MAX_PATH * 2];
    _snwprintf(tmp, MAX_PATH * 2, L"%ls.tmp", path);
    tmp[MAX_PATH * 2 - 1] = 0;
    HANDLE file = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    const BOOL ok = WriteFile(file, utf, static_cast<DWORD>(n - 1), &wrote, NULL);
    CloseHandle(file);
    if (!ok || wrote != static_cast<DWORD>(n - 1)) {
        DeleteFileW(tmp);
        return false;
    }
    return MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
}

static bool save_link(const wchar_t *lnk, const wchar_t *exe, const wchar_t *work) {
    IShellLinkW *link = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&link));
    if (FAILED(hr) || !link) return false;
    link->SetPath(exe);
    link->SetWorkingDirectory(work);
    link->SetIconLocation(exe, 0);
    link->SetDescription(L"枝理");
    IPersistFile *store = NULL;
    bool saved = false;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&store))) && store) {
        saved = SUCCEEDED(store->Save(lnk, TRUE));
        store->Release();
    }
    link->Release();
    return saved;
}

// Create the desktop shortcut once per entry path. No admin and no HKLM.
static void ensure_shortcuts(const wchar_t *exe, const wchar_t *work) {
    wchar_t root[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", root, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t marker[MAX_PATH * 2];
    wchar_t saved[MAX_PATH * 2];
    _snwprintf(marker, MAX_PATH * 2, L"%ls\\ZhiLi\\entry-path.txt", root);
    marker[MAX_PATH * 2 - 1] = 0;
    if (read_marker(marker, saved, MAX_PATH * 2) && wcscmp(saved, exe) == 0) return;

    wchar_t desk[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk) != S_OK) return;
    wchar_t lnk[MAX_PATH * 2];
    _snwprintf(lnk, MAX_PATH * 2, L"%ls\\枝理.lnk", desk);
    lnk[MAX_PATH * 2 - 1] = 0;

    const HRESULT hr = CoInitialize(NULL);
    const bool uninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return;
    const bool wrote = save_link(lnk, exe, work);
    wchar_t programs[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, programs) == S_OK) {
        wchar_t menu[MAX_PATH * 2];
        _snwprintf(menu, MAX_PATH * 2, L"%ls\\枝理.lnk", programs);
        menu[MAX_PATH * 2 - 1] = 0;
        save_link(menu, exe, work);
    }
    if (wrote) write_marker(marker, exe);
    if (uninit) CoUninitialize();
}

static int launch(const wchar_t *bundle) {
    wchar_t app[MAX_PATH * 2];
    _snwprintf(app, MAX_PATH * 2, L"%ls\\simplemap.exe", bundle);
    app[MAX_PATH * 2 - 1] = 0;
    if (!exists_file(app)) {
        fail_box(L"请把 ZhiLi.exe 和旁边的 ZhiLi 文件夹放在一起，不要只拷走程序。");
        return 2;
    }
    const wchar_t *args = skip_argv0(GetCommandLineW());
    const size_t cap = wcslen(app) + wcslen(args) + 8;
    wchar_t *cmd = static_cast<wchar_t *>(malloc(cap * sizeof(wchar_t)));
    if (!cmd) return 3;
    _snwprintf(cmd, cap, L"\"%ls\"%ls%ls", app, *args ? L" " : L"", args);
    cmd[cap - 1] = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    const BOOL ok = CreateProcessW(app, cmd, NULL, NULL, FALSE, 0, NULL, bundle, &si, &pi);
    free(cmd);
    if (!ok) {
        wchar_t message[160];
        _snwprintf(message, 160, L"无法启动枝理（错误码 %lu）。", static_cast<unsigned long>(GetLastError()));
        message[159] = 0;
        fail_box(message);
        return 4;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t self[MAX_PATH * 2];
    const DWORD len = GetModuleFileNameW(NULL, self, MAX_PATH * 2);
    if (len == 0 || len >= MAX_PATH * 2) {
        fail_box(L"无法定位程序自身路径。");
        return 1;
    }
    wchar_t dir[MAX_PATH * 2];
    wcsncpy(dir, self, MAX_PATH * 2);
    dir[MAX_PATH * 2 - 1] = 0;
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash) {
        fail_box(L"无法定位程序自身路径。");
        return 1;
    }
    *slash = 0;
    wchar_t bundle[MAX_PATH * 2];
    _snwprintf(bundle, MAX_PATH * 2, L"%ls\\ZhiLi", dir);
    bundle[MAX_PATH * 2 - 1] = 0;
    wchar_t app[MAX_PATH * 2];
    _snwprintf(app, MAX_PATH * 2, L"%ls\\simplemap.exe", bundle);
    app[MAX_PATH * 2 - 1] = 0;
    if (exists_file(app)) ensure_shortcuts(self, bundle);
    return launch(bundle);
}
