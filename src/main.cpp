#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "core.h"
#include "resource.h"
#include "taskbar.h"

namespace {

constexpr wchar_t kWindowClass[] = L"TaskbarClear";
constexpr wchar_t kAppTitle[] = L"TaskbarClear";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

struct AppContext {
    HWND hwnd = nullptr;
    HICON icon_on = nullptr;
    HICON icon_off = nullptr;
    UINT taskbar_created_msg = 0;
    core::TbState state = core::TbState::Default;
    bool tray_added = false;
    bool skip_next_lbuttonup = false;
};

AppContext g_app;

void DebugLog(const wchar_t* text) {
    OutputDebugStringW(text);
    OutputDebugStringW(L"\n");
}

void UpdateTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_app.hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayCallbackMessage;
    nid.hIcon = g_app.state == core::TbState::Transparent ? g_app.icon_on : g_app.icon_off;
    lstrcpynW(nid.szTip, core::TooltipText(g_app.state), ARRAYSIZE(nid.szTip));
    if (Shell_NotifyIconW(g_app.tray_added ? NIM_MODIFY : NIM_ADD, &nid)) {
        g_app.tray_added = true;
    }
}

bool EnsureTrayIcon() {
    for (int attempt = 0; attempt < core::kTaskbarFindMaxAttempts; ++attempt) {
        UpdateTrayIcon();
        if (g_app.tray_added) return true;
        Sleep(core::kTaskbarFindDelayMs);
    }
    DebugLog(L"TaskbarClear: 添加托盘图标失败");
    return false;
}

void RemoveTrayIcon() {
    if (!g_app.tray_added) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_app.hwnd;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_app.tray_added = false;
}

void ShowBalloon(const wchar_t* text, DWORD info_flags) {
    if (!g_app.tray_added) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_app.hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_INFO;
    lstrcpynW(nid.szInfoTitle, kAppTitle, ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text, ARRAYSIZE(nid.szInfo));
    nid.dwInfoFlags = info_flags;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

bool ApplyOnce(core::TbState target) {
    const taskbar::ApplyResult result =
        taskbar::ApplyAll(target == core::TbState::Transparent);
    return result.windows_found > 0 && result.windows_failed == 0;
}

bool ApplyWithRetry(core::TbState target) {
    for (int attempt = 0; attempt < core::kTaskbarFindMaxAttempts; ++attempt) {
        if (ApplyOnce(target)) return true;
        Sleep(core::kTaskbarFindDelayMs);
    }
    return false;
}

void ApplyStateChange(core::TbState target, bool use_retry) {
    const bool ok = use_retry ? ApplyWithRetry(target) : ApplyOnce(target);
    const core::TbState next = core::StateAfterApply(g_app.state, target, ok);
    if (!ok) {
        DebugLog(L"TaskbarClear: 应用或恢复任务栏外观失败");
        ShowBalloon(target == core::TbState::Transparent
                        ? L"应用透明失败：未找到任务栏或系统接口不可用。"
                        : L"恢复系统默认外观失败。",
                    NIIF_ERROR);
    }
    if (next != g_app.state) {
        g_app.state = next;
        UpdateTrayIcon();
    }
}

void ShowContextMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    UINT transparent_flags = MF_STRING;
    if (core::IsTransparentChecked(g_app.state)) {
        transparent_flags |= MF_CHECKED;
    }
    AppendMenuW(menu, transparent_flags, core::kCmdTransparent, L"任务栏透明");
    AppendMenuW(menu, MF_STRING, core::kCmdDefault, L"恢复系统默认");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, core::kCmdExit, L"退出");
    SetForegroundWindow(g_app.hwnd);
    POINT pt{};
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_app.hwnd, nullptr);
    PostMessageW(g_app.hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void QuitApp() {
    const taskbar::ApplyResult restore = taskbar::ApplyAll(false);
    if (restore.windows_found == 0 || restore.windows_failed > 0) {
        DebugLog(L"TaskbarClear: 退出前恢复系统默认外观失败");
        ShowBalloon(L"退出前恢复系统默认外观失败，可重启资源管理器恢复。", NIIF_WARNING);
        Sleep(1000);
    }
    RemoveTrayIcon();
    DestroyWindow(g_app.hwnd);
}

void HandleCommand(int cmd) {
    if (cmd == core::kCmdExit) {
        QuitApp();
        return;
    }
    core::TbState target;
    if (!core::CommandToTarget(cmd, &target)) return;
    if (target != g_app.state) {
        ApplyStateChange(target, false);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (g_app.taskbar_created_msg != 0 && msg == g_app.taskbar_created_msg) {
        g_app.tray_added = false;
        EnsureTrayIcon();
        if (g_app.state == core::TbState::Transparent) {
            ApplyStateChange(core::TbState::Transparent, true);
        }
        return 0;
    }

    switch (msg) {
    case kTrayCallbackMessage:
        switch (LOWORD(lparam)) {
        case WM_LBUTTONDBLCLK:
            g_app.skip_next_lbuttonup = true;
            break;
        case WM_LBUTTONUP:
            if (g_app.skip_next_lbuttonup) {
                g_app.skip_next_lbuttonup = false;
            } else {
                ApplyStateChange(core::ToggleTarget(g_app.state), false);
            }
            break;
        case WM_RBUTTONUP:
            ShowContextMenu();
            break;
        }
        return 0;
    case WM_COMMAND:
        HandleCommand(LOWORD(wparam));
        return 0;
    case WM_DISPLAYCHANGE:
        ApplyStateChange(g_app.state, true);
        return 0;
    case WM_CLOSE:
        QuitApp();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\TaskbarClearMutex");
    if (!mutex) {
        MessageBoxW(nullptr, L"创建单实例锁失败。", kAppTitle, MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    g_app.icon_on = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ON), IMAGE_ICON, 0, 0,
                   LR_DEFAULTSIZE | LR_SHARED));
    g_app.icon_off = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_OFF), IMAGE_ICON, 0, 0,
                   LR_DEFAULTSIZE | LR_SHARED));
    if (!g_app.icon_on || !g_app.icon_off) {
        MessageBoxW(nullptr, L"加载图标资源失败（构建不完整）。", kAppTitle, MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"注册窗口类失败。", kAppTitle, MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    g_app.hwnd = CreateWindowExW(0, kWindowClass, kAppTitle, 0, 0, 0, 0, 0,
                                 nullptr, nullptr, instance, nullptr);
    if (!g_app.hwnd) {
        MessageBoxW(nullptr, L"创建隐藏窗口失败。", kAppTitle, MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    g_app.taskbar_created_msg = RegisterWindowMessageW(L"TaskbarCreated");

    if (!EnsureTrayIcon()) {
        MessageBoxW(nullptr, L"添加托盘图标失败，程序将退出。", kAppTitle, MB_ICONERROR);
        DestroyWindow(g_app.hwnd);
        CloseHandle(mutex);
        return 1;
    }

    if (!ApplyWithRetry(core::TbState::Transparent)) {
        RemoveTrayIcon();
        MessageBoxW(nullptr,
                    L"未能找到任务栏或应用透明失败（SetWindowCompositionAttribute 不可用）。\n程序将退出。",
                    kAppTitle, MB_ICONERROR);
        DestroyWindow(g_app.hwnd);
        CloseHandle(mutex);
        return 2;
    }
    g_app.state = core::TbState::Transparent;
    UpdateTrayIcon();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CloseHandle(mutex);
    return 0;
}