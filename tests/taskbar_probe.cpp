#include "../src/taskbar.h"

#include <cstdio>

int main() {
    const taskbar::ApplyResult applied = taskbar::ApplyAll(true);
    std::printf("apply: found=%d failed=%d\n", applied.windows_found, applied.windows_failed);
    if (applied.windows_found == 0 || applied.windows_failed > 0) {
        std::printf("PROBE FAIL\n");
        return 1;
    }
    std::printf("taskbar should be fully transparent for 3 seconds...\n");
    Sleep(3000);

    const taskbar::ApplyResult restored = taskbar::ApplyAll(false);
    std::printf("restore: found=%d failed=%d\n", restored.windows_found, restored.windows_failed);
    if (restored.windows_found == 0 || restored.windows_failed > 0) {
        std::printf("PROBE FAIL\n");
        return 1;
    }
    std::printf("PROBE PASS\n");
    return 0;
}