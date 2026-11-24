#include "taskbar.h"

#include <cwchar>

namespace taskbar {
namespace {

constexpr int kAccentDisabled = 0;
constexpr int kAccentEnableTransparentGradient = 2;
constexpr int kWcaAccentPolicy = 19;
constexpr DWORD kTintRgb = 0x00202020; // 深灰（0x00BBGGRR）

struct AccentPolicy {
    int accent_state;
    int accent_flags;
    DWORD gradient_color;
    int animation_id;
};

struct WindowCompositionAttributeData {
    int attrib;
    PVOID data;
    SIZE_T size;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);

SetWindowCompositionAttributeFn ResolveSetWca() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return nullptr;
    return reinterpret_cast<SetWindowCompositionAttributeFn>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
}

bool IsTaskbarClass(const wchar_t* cls) {
    return std::wcscmp(cls, L"Shell_TrayWnd") == 0 ||
           std::wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0;
}

BOOL CALLBACK EnumTaskbars(HWND hwnd, LPARAM param) {
    wchar_t cls[64]{};
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0 && IsTaskbarClass(cls)) {
        auto* windows = reinterpret_cast<TaskbarWindows*>(param);
        if (windows->count < static_cast<int>(ARRAYSIZE(windows->items))) {
            windows->items[windows->count++] = hwnd;
        }
    }
    return TRUE;
}

ApplyResult ApplyPolicy(int accent_state, int accent_flags, DWORD gradient_color) {
    ApplyResult result{0, 0};
    const auto set_wca = ResolveSetWca();
    if (!set_wca) return result;

    const TaskbarWindows windows = FindAll();
    result.windows_found = windows.count;
    for (int i = 0; i < windows.count; ++i) {
        AccentPolicy policy{};
        policy.accent_state = accent_state;
        policy.accent_flags = accent_flags;
        policy.gradient_color = gradient_color;
        WindowCompositionAttributeData data{};
        data.attrib = kWcaAccentPolicy;
        data.data = &policy;
        data.size = sizeof(policy);
        if (!set_wca(windows.items[i], &data)) {
            ++result.windows_failed;
        }
    }
    return result;
}

} // namespace

TaskbarWindows FindAll() {
    TaskbarWindows windows{};
    EnumWindows(EnumTaskbars, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

ApplyResult ApplyTransparency(int tint_alpha) {
    if (tint_alpha < 0) tint_alpha = 0;
    if (tint_alpha > 255) tint_alpha = 255;
    // Win10 19045 实测：AccentState=2 + AccentFlags=2 时 alpha=0 是清晰全透明；
    // AccentFlags=0 且 alpha=0 会退回磨砂模糊，不可用。
    // 半透明档位用深灰 tint，alpha 越大越不透明。
    const DWORD color = (static_cast<DWORD>(tint_alpha) << 24) | kTintRgb;
    return ApplyPolicy(kAccentEnableTransparentGradient, 2, color);
}

ApplyResult RestoreDefault() {
    ApplyResult result = ApplyPolicy(kAccentDisabled, 0, 0);
    if (result.windows_found > 0) {
        // 仅关闭强调策略任务栏会变纯黑；广播颜色变更让 Explorer 重绘自己的默认外观
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_DWMCOLORIZATIONCOLORCHANGED, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored);
    }
    return result;
}

} // namespace taskbar