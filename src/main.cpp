#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include "core.h"
#include "resource.h"
#include "taskbar.h"

namespace {

constexpr wchar_t kWindowClass[] = L"TaskbarClear";
constexpr wchar_t kOpacityClass[] = L"TaskbarClearOpacity";
constexpr wchar_t kAppTitle[] = L"TaskbarClear";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;
constexpr int kOpacityLabelId = 2001;
constexpr int kOpacitySliderId = 2002;

struct AppContext {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HICON icon_on = nullptr;
    HICON icon_off = nullptr;
    UINT taskbar_created_msg = 0;
    core::TbState state = core::TbState::Default;
    int transparency = 100;
    int last_transparency = 100;
    bool tray_added = false;
    bool skip_next_lbuttonup = false;
    HWND opacity_hwnd = nullptr;
    HWND opacity_slider = nullptr;
    HWND opacity_label = nullptr;
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

core::TbState CurrentTarget() {
    return g_app.transparency > 0 ? core::TbState::Transparent : core::TbState::Default;
}

bool ApplyOnce(core::TbState target) {
    const taskbar::ApplyResult result =
        target == core::TbState::Transparent
            ? taskbar::ApplyTransparency(core::TintAlphaFromTransparency(g_app.transparency))
            : taskbar::RestoreDefault();
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

void UpdateOpacityLabel() {
    if (g_app.opacity_label) {
        wchar_t text[64];
        wsprintfW(text, L"透明度：%d%%", g_app.transparency);
        SetWindowTextW(g_app.opacity_label, text);
    }
    if (g_app.opacity_slider) {
        SendMessageW(g_app.opacity_slider, TBM_SETPOS, TRUE, g_app.transparency);
    }
}

void ToggleTransparency() {
    const core::TbState target = core::ToggleTarget(g_app.state);
    if (target == core::TbState::Default) {
        if (g_app.transparency > 0) {
            g_app.last_transparency = g_app.transparency;
        }
        g_app.transparency = 0;
    } else {
        g_app.transparency = g_app.last_transparency > 0 ? g_app.last_transparency : 100;
    }
    ApplyStateChange(CurrentTarget(), false);
    UpdateOpacityLabel();
}

void ShowOpacityWindow() {
    if (g_app.opacity_hwnd) {
        ShowWindow(g_app.opacity_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_app.opacity_hwnd);
        return;
    }

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = 420;
    const int height = 200;
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;

    HWND hwnd = CreateWindowExW(0, kOpacityClass, L"透明度设置",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                x, y, width, height, nullptr, nullptr, g_app.instance,
                                nullptr);
    if (!hwnd) {
        DebugLog(L"TaskbarClear: 创建透明度窗口失败");
        MessageBoxW(nullptr, L"创建透明度窗口失败。", kAppTitle, MB_ICONERROR);
        return;
    }
    g_app.opacity_hwnd = hwnd;
    g_app.opacity_label = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 16, width - 40, 24,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacityLabelId)),
        g_app.instance, nullptr);
    g_app.opacity_slider = CreateWindowExW(
        0, L"msctls_trackbar32", L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS, 20, 48, width - 40, 40,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacitySliderId)),
        g_app.instance, nullptr);
    HWND hint = CreateWindowExW(
        0, L"STATIC", L"100% = 完全透明（清晰无模糊）；0% = 系统默认外观",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 100, width - 40, 44, hwnd, nullptr,
        g_app.instance, nullptr);
    if (!g_app.opacity_label || !g_app.opacity_slider || !hint) {
        DebugLog(L"TaskbarClear: 创建透明度控件失败");
        MessageBoxW(hwnd, L"创建透明度控件失败。", kAppTitle, MB_ICONERROR);
        DestroyWindow(hwnd);
        return;
    }

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(g_app.opacity_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(g_app.opacity_slider, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(g_app.opacity_slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(g_app.opacity_slider, TBM_SETTICFREQ, 10, 0);
    UpdateOpacityLabel();

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
}

LRESULT CALLBACK OpacityWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_HSCROLL:
        if (g_app.opacity_slider) {
            const int pos = static_cast<int>(
                SendMessageW(g_app.opacity_slider, TBM_GETPOS, 0, 0));
            if (pos != g_app.transparency) {
                g_app.transparency = pos;
                if (pos > 0) {
                    g_app.last_transparency = pos;
                }
                UpdateOpacityLabel();
                ApplyStateChange(CurrentTarget(), false);
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_app.opacity_hwnd = nullptr;
        g_app.opacity_slider = nullptr;
        g_app.opacity_label = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void ShowContextMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, core::kCmdOpacity, L"任务栏透明度");
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
    const taskbar::ApplyResult restore = taskbar::RestoreDefault();
    if (restore.windows_found == 0 || restore.windows_failed > 0) {
        DebugLog(L"TaskbarClear: 退出前恢复系统默认外观失败");
        ShowBalloon(L"退出前恢复系统默认外观失败，可重启资源管理器恢复。", NIIF_WARNING);
        Sleep(1000);
    }
    RemoveTrayIcon();
    if (g_app.opacity_hwnd) {
        DestroyWindow(g_app.opacity_hwnd);
    }
    DestroyWindow(g_app.hwnd);
}

void HandleCommand(int cmd) {
    if (cmd == core::kCmdExit) {
        QuitApp();
        return;
    }
    if (cmd == core::kCmdOpacity) {
        ShowOpacityWindow();
        return;
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
                ToggleTransparency();
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

    g_app.instance = instance;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

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

    WNDCLASSEXW oc{};
    oc.cbSize = sizeof(oc);
    oc.lpfnWndProc = OpacityWndProc;
    oc.hInstance = instance;
    oc.lpszClassName = kOpacityClass;
    oc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&oc)) {
        MessageBoxW(nullptr, L"注册透明度窗口类失败。", kAppTitle, MB_ICONERROR);
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