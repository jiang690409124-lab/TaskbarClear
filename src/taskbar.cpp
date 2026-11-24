#include "taskbar.h"

#include <cwchar>

namespace taskbar {
namespace {

constexpr int kAccentDisabled = 0;
constexpr int kAccentEnableBlurBehind = 3;
constexpr int kWcaAccentPolicy = 19;

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

} // namespace

TaskbarWindows FindAll() {
    TaskbarWindows windows{};
    EnumWindows(EnumTaskbars, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

ApplyResult ApplyAll(bool transparent) {
    ApplyResult result{0, 0};
    const auto set_wca = ResolveSetWca();
    if (!set_wca) return result;

    const TaskbarWindows windows = FindAll();
    result.windows_found = windows.count;
    for (int i = 0; i < windows.count; ++i) {
        AccentPolicy policy{};
        if (transparent) {
            // Win10 19045 实测：ACCENT_ENABLE_TRANSPARENTGRADIENT(2) 只会把任务栏变浅灰，
            // 不透背景；ACCENT_ENABLE_BLURBEHIND(3) + 全透明色（ARGB alpha=0）才是真透明。
            policy.accent_state = kAccentEnableBlurBehind;
            policy.gradient_color = 0x00000000;
        } else {
            policy.accent_state = kAccentDisabled;
        }
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

} // namespace taskbar