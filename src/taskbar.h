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

// transparent=true 全透明；false 恢复系统默认。
// windows_found==0 表示没有找到任何任务栏窗口。
ApplyResult ApplyAll(bool transparent);

} // namespace taskbar