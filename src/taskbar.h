#pragma once

#include <windows.h>

namespace taskbar {

struct ApplyResult {
    int windows_found;
    int windows_failed;
};

struct TaskbarWindows {
    HWND items[64];
    int count;
};

TaskbarWindows FindAll();

// tint_alpha：0..255。0 = 完全透明（清晰无模糊）；255 = 完全不透明（深灰实心）。
ApplyResult ApplyTransparency(int tint_alpha);

// 移除强调策略，恢复系统默认外观。
ApplyResult RestoreDefault();

} // namespace taskbar