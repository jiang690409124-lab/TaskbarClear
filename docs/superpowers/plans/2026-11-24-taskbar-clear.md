# TaskbarClear Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 做一个 Windows 10 极简任务栏透明工具：托盘常驻，一键全透明 / 一键恢复，单文件绿色 exe，体积与内存同 TaskbarTB 一个量级。

**Architecture:** 纯 Win32 C++，无第三方依赖。`core` 放纯逻辑（状态机/文案，可单测）；`taskbar` 封装未公开接口 `SetWindowCompositionAttribute` 对 `Shell_TrayWnd` / `Shell_SecondaryTrayWnd` 应用或清除透明；`main` 负责单实例、托盘图标与菜单、消息循环、explorer 重启后的自动恢复。Win32 层用可见性探针 + 手工清单验证，核心逻辑用 TDD。

**Tech Stack:** C++17（MSYS2 UCRT64 g++ 15.2，`-Os -s -static -municode -mwindows`）、MinGW `windres`、Win32 API（user32 / shell32 / gdi32）、Node.js（仅用于一次性生成 .ico 资源）。

**Spec:** `docs/superpowers/specs/2026-11-24-taskbar-clear-design.md`

## Global Constraints

- 只保证 Windows 10 x64（实测机 10.0.19045）；Win11 不验证。
- 不需要管理员权限；不写注册表；不注入进程；不使用第三方库。
- 构建只依赖 MSYS2 UCRT64 的 `g++` 与 `windres`（本机 `D:\C++\MSYS2\ucrt64\bin`）；本机无 make/cmake，脚本直调编译器。
- 目标：exe ≤ 200KB（204800 字节）、工作集内存 ≤ 5MB；实测值必须写进 README。
- 界面与提示文案全部中文；不使用 emoji。
- 错误必须暴露：启动失败弹 MessageBox、运行中失败弹托盘气泡；禁止静默降级。
- 仅本地 git 仓库，无远程；本机全局 gitignore 忽略 `*.md`，所有 `.md` 文件提交必须 `git add -f`（先 `git check-ignore -v <file>` 确认）。
- 截图与构建产物放 `build/`（已 gitignore），不提交。
- 需要重启 explorer 的测试必须先征得用户明确同意，未同意则记录“未实测”。

---

## File Structure

| 文件 | 职责 |
|---|---|
| `src/core.h` / `src/core.cpp` | 纯逻辑：状态枚举、切换目标、命令映射、应用结果判定、菜单勾选、托盘文案、重试常量。无 Win32 依赖。 |
| `src/taskbar.h` / `src/taskbar.cpp` | Win32 层：枚举任务栏窗口；对全部任务栏应用/清除 ACCENT_POLICY。 |
| `src/main.cpp` | 应用入口：单实例、隐藏顶层窗口、托盘图标/菜单、消息循环、TaskbarCreated 与 WM_DISPLAYCHANGE 处理、退出恢复。 |
| `res/resource.h` / `res/app.rc` / `res/app_on.ico` / `res/app_off.ico` | 托盘图标资源（开启态蓝色、关闭态灰色，32x32）。 |
| `tools/gen-icons.js` | 一次性生成两枚 .ico（写 ICO 文件格式，无外部依赖）。 |
| `tests/selftest.cpp` | 核心逻辑自测，控制台运行，退出码 0 = 通过。 |
| `tests/taskbar_probe.cpp` | 可见性探针：透明 3 秒后自动恢复，用于在 UI 存在前验证 Win32 层。 |
| `build.sh` / `build.bat` | 构建：`all` 出 exe、`test` 跑自测、`probe` 出探针。 |
| `README.md` | 白话说使用说明、构建方法、实测数据、已知限制。 |

---

### Task 1: 核心状态逻辑（纯 C++，TDD）

**Files:**
- Create: `.gitignore`
- Create: `build.sh`
- Create: `tests/selftest.cpp`
- Create: `src/core.h`
- Create: `src/core.cpp`

**Interfaces:**
- Produces: `core::TbState`（`Default` / `Transparent`）；`core::ToggleTarget(TbState)`；`core::CommandToTarget(int, TbState*)`；`core::StateAfterApply(TbState, TbState, bool)`；`core::TooltipText(TbState)`；`core::IsTransparentChecked(TbState)`；常量 `kCmdTransparent=1001`、`kCmdDefault=1002`、`kCmdExit=1003`、`kTaskbarFindMaxAttempts=5`、`kTaskbarFindDelayMs=200`。

- [ ] **Step 1: 写 .gitignore**

```
build/
```

- [ ] **Step 2: 写失败测试 `tests/selftest.cpp`**

```cpp
#include "../src/core.h"

#include <cstdio>
#include <cwchar>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expr, int line) {
    if (!condition) {
        std::printf("FAIL line %d: %s\n", line, expr);
        ++g_failures;
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

bool SameText(const wchar_t* a, const wchar_t* b) {
    return std::wcscmp(a, b) == 0;
}

} // namespace

int main() {
    using core::TbState;

    CHECK(core::ToggleTarget(TbState::Transparent) == TbState::Default);
    CHECK(core::ToggleTarget(TbState::Default) == TbState::Transparent);

    TbState target = TbState::Default;
    CHECK(core::CommandToTarget(core::kCmdTransparent, &target) && target == TbState::Transparent);
    CHECK(core::CommandToTarget(core::kCmdDefault, &target) && target == TbState::Default);
    CHECK(!core::CommandToTarget(core::kCmdExit, &target));
    CHECK(!core::CommandToTarget(9999, &target));
    CHECK(!core::CommandToTarget(core::kCmdDefault, nullptr));

    CHECK(core::StateAfterApply(TbState::Default, TbState::Transparent, true) == TbState::Transparent);
    CHECK(core::StateAfterApply(TbState::Default, TbState::Transparent, false) == TbState::Default);
    CHECK(core::StateAfterApply(TbState::Transparent, TbState::Default, true) == TbState::Default);
    CHECK(core::StateAfterApply(TbState::Transparent, TbState::Default, false) == TbState::Transparent);

    CHECK(SameText(core::TooltipText(TbState::Transparent), L"任务栏透明：已开启"));
    CHECK(SameText(core::TooltipText(TbState::Default), L"任务栏透明：已关闭"));

    CHECK(core::IsTransparentChecked(TbState::Transparent));
    CHECK(!core::IsTransparentChecked(TbState::Default));

    CHECK(core::kTaskbarFindMaxAttempts == 5);
    CHECK(core::kTaskbarFindDelayMs == 200);
    CHECK(core::kCmdTransparent != core::kCmdDefault);
    CHECK(core::kCmdDefault != core::kCmdExit);

    if (g_failures == 0) {
        std::printf("selftest: all checks passed\n");
        return 0;
    }
    std::printf("selftest: %d failure(s)\n", g_failures);
    return 1;
}
```

- [ ] **Step 3: 写 `build.sh`（本任务只含 test 目标）**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

GXX="${GXX:-g++}"
CXXFLAGS=(
  -std=c++17 -fno-exceptions -fno-rtti
  -finput-charset=UTF-8 -fexec-charset=UTF-8
  -ffunction-sections -fdata-sections
)

mkdir -p build

mode="${1:-test}"
case "$mode" in
  test)
    "$GXX" "${CXXFLAGS[@]}" -O2 -static \
      tests/selftest.cpp src/core.cpp -o build/selftest.exe
    ./build/selftest.exe
    ;;
  *)
    echo "usage: $0 [test]" >&2
    exit 1
    ;;
esac
```

- [ ] **Step 4: 运行测试，确认失败**

Run: `bash build.sh test`
Expected: 非 0 退出，编译报错（`src/core.h: No such file or directory` 或 `src/core.cpp: No such file or directory`）。

- [ ] **Step 5: 写 `src/core.h`**

```cpp
#pragma once

namespace core {

enum class TbState { Default, Transparent };

constexpr int kCmdTransparent = 1001;
constexpr int kCmdDefault     = 1002;
constexpr int kCmdExit        = 1003;

constexpr int kTaskbarFindMaxAttempts = 5;
constexpr int kTaskbarFindDelayMs     = 200;

TbState ToggleTarget(TbState current);
bool CommandToTarget(int cmd, TbState* out);
TbState StateAfterApply(TbState current, TbState target, bool apply_ok);
const wchar_t* TooltipText(TbState state);
bool IsTransparentChecked(TbState state);

} // namespace core
```

- [ ] **Step 6: 写 `src/core.cpp`**

```cpp
#include "core.h"

namespace core {

TbState ToggleTarget(TbState current) {
    return current == TbState::Transparent ? TbState::Default : TbState::Transparent;
}

bool CommandToTarget(int cmd, TbState* out) {
    if (!out) return false;
    switch (cmd) {
    case kCmdTransparent:
        *out = TbState::Transparent;
        return true;
    case kCmdDefault:
        *out = TbState::Default;
        return true;
    default:
        return false;
    }
}

TbState StateAfterApply(TbState current, TbState target, bool apply_ok) {
    return apply_ok ? target : current;
}

const wchar_t* TooltipText(TbState state) {
    return state == TbState::Transparent ? L"任务栏透明：已开启" : L"任务栏透明：已关闭";
}

bool IsTransparentChecked(TbState state) {
    return state == TbState::Transparent;
}

} // namespace core
```

- [ ] **Step 7: 运行测试，确认通过**

Run: `bash build.sh test`
Expected: 输出 `selftest: all checks passed`，退出码 0。

- [ ] **Step 8: 提交**

```bash
git check-ignore -v build/ || true
git add .gitignore build.sh tests/selftest.cpp src/core.h src/core.cpp
git commit -m "feat: 核心状态逻辑与自测（TDD）"
```

---

### Task 2: 任务栏透明度层 + 可见性探针

**Files:**
- Create: `tests/taskbar_probe.cpp`
- Create: `src/taskbar.h`
- Create: `src/taskbar.cpp`
- Modify: `build.sh`（新增 probe 目标）

**Interfaces:**
- Consumes: 无（不依赖 Task 1）。
- Produces: `taskbar::ApplyResult { int windows_found; int windows_failed; }`；`taskbar::TaskbarWindows { HWND items[64]; int count; }`；`taskbar::TaskbarWindows FindAll()`；`taskbar::ApplyResult ApplyAll(bool transparent)`。语义：`ApplyAll(true)` 全透明，`ApplyAll(false)` 恢复系统默认；`windows_found == 0` 表示没找到任务栏。

- [ ] **Step 1: 写探针 `tests/taskbar_probe.cpp`**

```cpp
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
```

- [ ] **Step 2: 更新 `build.sh`（新增 probe 目标）**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

GXX="${GXX:-g++}"
CXXFLAGS=(
  -std=c++17 -fno-exceptions -fno-rtti
  -finput-charset=UTF-8 -fexec-charset=UTF-8
  -ffunction-sections -fdata-sections
)

mkdir -p build

mode="${1:-test}"
case "$mode" in
  test)
    "$GXX" "${CXXFLAGS[@]}" -O2 -static \
      tests/selftest.cpp src/core.cpp -o build/selftest.exe
    ./build/selftest.exe
    ;;
  probe)
    "$GXX" "${CXXFLAGS[@]}" -O2 -static \
      tests/taskbar_probe.cpp src/taskbar.cpp -luser32 -o build/taskbar_probe.exe
    echo "built: build/taskbar_probe.exe (run it manually to test; taskbar becomes transparent for 3s)"
    ;;
  *)
    echo "usage: $0 [test|probe]" >&2
    exit 1
    ;;
esac
```

- [ ] **Step 3: 运行构建，确认失败**

Run: `bash build.sh probe`
Expected: 非 0 退出，编译报错 `../src/taskbar.h: No such file or directory`。

- [ ] **Step 4: 写 `src/taskbar.h`**

```cpp
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
```

- [ ] **Step 5: 写 `src/taskbar.cpp`**

```cpp
#include "taskbar.h"

#include <cwchar>

namespace taskbar {
namespace {

constexpr int kAccentDisabled = 0;
constexpr int kAccentEnableTransparentGradient = 2;
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
            // ACCENT_ENABLE_TRANSPARENTGRADIENT + 全透明色（ARGB alpha=0）
            policy.accent_state = kAccentEnableTransparentGradient;
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
```

- [ ] **Step 6: 构建探针并实测**

Run: `bash build.sh probe`
Expected: 输出 `built: build/taskbar_probe.exe`。

Run: `./build/taskbar_probe.exe`
Expected: 输出 `apply: found=1 failed=0`（有副屏时为 2），随后任务栏**完全透明 3 秒**（壁纸直接透出），然后自动恢复默认；最后输出 `PROBE PASS`。

NOTE（预案）：若任务栏没有变透明，把 `src/taskbar.cpp` 中 `kAccentEnableTransparentGradient = 2` 改为 `= 3`（ACCENT_ENABLE_BLURBEHIND，同样配 `gradient_color = 0x00000000`）后重跑探针；确认效果后将此变更同步更新到 spec 第 4 节并一起提交。

- [ ] **Step 7: 提交**

```bash
git add tests/taskbar_probe.cpp src/taskbar.h src/taskbar.cpp build.sh
git commit -m "feat: 任务栏透明 Win32 层与可见性探针"
```

---

### Task 3: 托盘应用端到端（图标 + 资源 + 主程序）

**Files:**
- Create: `tools/gen-icons.js`
- Create: `res/resource.h`
- Create: `res/app.rc`
- Create（由脚本生成）: `res/app_on.ico`、`res/app_off.ico`
- Create: `src/main.cpp`
- Create: `build.bat`
- Modify: `build.sh`（新增 all 目标并设为默认）

**Interfaces:**
- Consumes: `core::*`（Task 1）、`taskbar::ApplyAll` / `ApplyResult`（Task 2）。
- Produces: `build/TaskbarClear.exe`；资源 ID `IDI_APP_ON=101`、`IDI_APP_OFF=102`。

- [ ] **Step 1: 写图标生成脚本 `tools/gen-icons.js`**

```javascript
'use strict';

const fs = require('fs');
const path = require('path');

const OUT_DIR = path.join(__dirname, '..', 'res');
const FINAL_SIZE = 32;
const SS = 4; // 超采样倍数
const W = FINAL_SIZE * SS;

function createCanvas(w, h) {
  return new Uint8ClampedArray(w * h * 4); // RGBA，直通 alpha
}

function blend(canvas, w, x, y, r, g, b, a) {
  if (x < 0 || y < 0 || x >= w || y >= w) return;
  const i = (y * w + x) * 4;
  const sa = a / 255;
  const da = canvas[i + 3] / 255;
  const oa = sa + da * (1 - sa);
  if (oa <= 0) return;
  canvas[i]     = Math.round((r * sa + canvas[i]     * da * (1 - sa)) / oa);
  canvas[i + 1] = Math.round((g * sa + canvas[i + 1] * da * (1 - sa)) / oa);
  canvas[i + 2] = Math.round((b * sa + canvas[i + 2] * da * (1 - sa)) / oa);
  canvas[i + 3] = Math.round(oa * 255);
}

function insideRoundRect(px, py, x0, y0, x1, y1, radius) {
  const cx = Math.min(Math.max(px, x0 + radius), x1 - radius);
  const cy = Math.min(Math.max(py, y0 + radius), y1 - radius);
  const dx = px - cx;
  const dy = py - cy;
  return dx * dx + dy * dy <= radius * radius;
}

function fillRoundRect(canvas, w, x0, y0, x1, y1, radius, r, g, b, a) {
  for (let y = 0; y < w; y++) {
    for (let x = 0; x < w; x++) {
      if (insideRoundRect(x + 0.5, y + 0.5, x0, y0, x1, y1, radius)) {
        blend(canvas, w, x, y, r, g, b, a);
      }
    }
  }
}

function renderIcon(kind) {
  const canvas = createCanvas(W, W);
  const s = SS;
  const px = (v) => v * s;

  // 逻辑坐标：胶囊 (3.5,10) - (28.5,22)，圆角 6
  const x0 = px(3.5);
  const y0 = px(10);
  const x1 = px(28.5);
  const y1 = px(22);
  const r = px(6);

  if (kind === 'on') {
    fillRoundRect(canvas, W, x0, y0, x1, y1, r, 255, 255, 255, 235);
    fillRoundRect(canvas, W, x0 + s, y0 + s, x1 - s, y1 - s, r - s, 41, 140, 235, 200);
    fillRoundRect(canvas, W, x0 + px(2), y0 + px(1.5), x1 - px(2), y0 + px(3.5), px(2), 255, 255, 255, 110);
  } else {
    fillRoundRect(canvas, W, x0, y0, x1, y1, r, 60, 60, 60, 255);
    fillRoundRect(canvas, W, x0 + s, y0 + s, x1 - s, y1 - s, r - s, 120, 120, 120, 255);
  }

  const out = Buffer.alloc(FINAL_SIZE * FINAL_SIZE * 4);
  const n = SS * SS;
  for (let y = 0; y < FINAL_SIZE; y++) {
    for (let x = 0; x < FINAL_SIZE; x++) {
      let rSum = 0;
      let gSum = 0;
      let bSum = 0;
      let aSum = 0;
      for (let sy = 0; sy < SS; sy++) {
        for (let sx = 0; sx < SS; sx++) {
          const i = ((y * SS + sy) * W + (x * SS + sx)) * 4;
          const a = canvas[i + 3];
          rSum += canvas[i] * a;
          gSum += canvas[i + 1] * a;
          bSum += canvas[i + 2] * a;
          aSum += a;
        }
      }
      const o = (y * FINAL_SIZE + x) * 4;
      if (aSum === 0) {
        out[o] = 0;
        out[o + 1] = 0;
        out[o + 2] = 0;
        out[o + 3] = 0;
      } else {
        out[o] = Math.round(rSum / aSum);
        out[o + 1] = Math.round(gSum / aSum);
        out[o + 2] = Math.round(bSum / aSum);
        out[o + 3] = Math.round(aSum / n);
      }
    }
  }
  return out;
}

function buildIco(rgba, size) {
  const header = Buffer.alloc(6);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2); // 1 = icon
  header.writeUInt16LE(1, 4); // 1 张图

  const maskRowBytes = Math.ceil(size / 32) * 4;
  const xor = Buffer.alloc(size * size * 4);
  const mask = Buffer.alloc(maskRowBytes * size);

  for (let y = 0; y < size; y++) {
    const srcY = size - 1 - y; // ICO 像素自下而上
    for (let x = 0; x < size; x++) {
      const src = (srcY * size + x) * 4;
      const dst = (y * size + x) * 4;
      xor[dst] = rgba[src + 2];     // B
      xor[dst + 1] = rgba[src + 1]; // G
      xor[dst + 2] = rgba[src];     // R
      xor[dst + 3] = rgba[src + 3]; // A
      if (rgba[src + 3] < 8) {
        mask[y * maskRowBytes + (x >> 3)] |= 0x80 >> (x & 7);
      }
    }
  }

  const bih = Buffer.alloc(40);
  bih.writeUInt32LE(40, 0);
  bih.writeInt32LE(size, 4);
  bih.writeInt32LE(size * 2, 8); // XOR + AND 两张位图
  bih.writeUInt16LE(1, 12);
  bih.writeUInt16LE(32, 14);
  bih.writeUInt32LE(0, 16); // BI_RGB
  bih.writeUInt32LE(xor.length + mask.length, 20);

  const payload = Buffer.concat([bih, xor, mask]);

  const entry = Buffer.alloc(16);
  entry[0] = size >= 256 ? 0 : size;
  entry[1] = size >= 256 ? 0 : size;
  entry[2] = 0;
  entry[3] = 0;
  entry.writeUInt16LE(1, 4);
  entry.writeUInt16LE(32, 6);
  entry.writeUInt32LE(payload.length, 8);
  entry.writeUInt32LE(6 + 16, 12);

  return Buffer.concat([header, entry, payload]);
}

fs.mkdirSync(OUT_DIR, { recursive: true });
const onIco = buildIco(renderIcon('on'), FINAL_SIZE);
const offIco = buildIco(renderIcon('off'), FINAL_SIZE);
fs.writeFileSync(path.join(OUT_DIR, 'app_on.ico'), onIco);
fs.writeFileSync(path.join(OUT_DIR, 'app_off.ico'), offIco);
console.log(`app_on.ico ${onIco.length} bytes`);
console.log(`app_off.ico ${offIco.length} bytes`);
```

- [ ] **Step 2: 生成并校验图标**

Run: `node tools/gen-icons.js`
Expected: 输出 `app_on.ico 4286 bytes` 与 `app_off.ico 4286 bytes`。

Run: `stat -c %s res/app_on.ico res/app_off.ico`
Expected: 两行均为 `4286`。

- [ ] **Step 3: 写资源文件 `res/resource.h` 与 `res/app.rc`**

`res/resource.h`:

```cpp
#pragma once

#define IDI_APP_ON  101
#define IDI_APP_OFF 102
```

`res/app.rc`:

```
#include "resource.h"

IDI_APP_ON  ICON "app_on.ico"
IDI_APP_OFF ICON "app_off.ico"
```

- [ ] **Step 4: 写主程序 `src/main.cpp`**

```cpp
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
```

- [ ] **Step 5: 更新 `build.sh`（新增 all 目标并设为默认）**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

GXX="${GXX:-g++}"
WINDRES="${WINDRES:-windres}"
CXXFLAGS=(
  -std=c++17 -fno-exceptions -fno-rtti
  -finput-charset=UTF-8 -fexec-charset=UTF-8
  -ffunction-sections -fdata-sections
)

mkdir -p build

mode="${1:-all}"
case "$mode" in
  all)
    "$WINDRES" -I res res/app.rc -O coff -o build/app.res
    "$GXX" "${CXXFLAGS[@]}" -I res -Os -s -municode -mwindows -static -Wl,--gc-sections \
      src/main.cpp src/taskbar.cpp src/core.cpp build/app.res \
      -luser32 -lshell32 -lgdi32 -o build/TaskbarClear.exe
    echo "built: build/TaskbarClear.exe"
    ;;
  test)
    "$GXX" "${CXXFLAGS[@]}" -O2 -static \
      tests/selftest.cpp src/core.cpp -o build/selftest.exe
    ./build/selftest.exe
    ;;
  probe)
    "$GXX" "${CXXFLAGS[@]}" -O2 -static \
      tests/taskbar_probe.cpp src/taskbar.cpp -luser32 -o build/taskbar_probe.exe
    echo "built: build/taskbar_probe.exe (run it manually to test; taskbar becomes transparent for 3s)"
    ;;
  *)
    echo "usage: $0 [all|test|probe]" >&2
    exit 1
    ;;
esac
```

- [ ] **Step 6: 写 `build.bat`（cmd 等价入口）**

```bat
@echo off
setlocal
cd /d "%~dp0"
if not defined MSYS2_BIN set "MSYS2_BIN=D:\C++\MSYS2\ucrt64\bin"
set "PATH=%MSYS2_BIN%;%PATH%"
if not exist build mkdir build

set "MODE=%~1"
if "%MODE%"=="" set "MODE=all"
if /i "%MODE%"=="all" goto app
if /i "%MODE%"=="test" goto test
if /i "%MODE%"=="probe" goto probe
echo usage: build.bat [all^|test^|probe]
exit /b 1

:app
windres -I res res\app.rc -O coff -o build\app.res || exit /b 1
g++ -std=c++17 -fno-exceptions -fno-rtti -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffunction-sections -fdata-sections -I res -Os -s -municode -mwindows -static -Wl,--gc-sections src\main.cpp src\taskbar.cpp src\core.cpp build\app.res -luser32 -lshell32 -lgdi32 -o build\TaskbarClear.exe || exit /b 1
echo built: build\TaskbarClear.exe
exit /b 0

:test
g++ -std=c++17 -fno-exceptions -fno-rtti -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffunction-sections -fdata-sections -O2 -static tests\selftest.cpp src\core.cpp -o build\selftest.exe || exit /b 1
build\selftest.exe
exit /b %ERRORLEVEL%

:probe
g++ -std=c++17 -fno-exceptions -fno-rtti -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffunction-sections -fdata-sections -O2 -static tests\taskbar_probe.cpp src\taskbar.cpp -luser32 -o build\taskbar_probe.exe || exit /b 1
echo built: build\taskbar_probe.exe
exit /b 0
```

- [ ] **Step 7: 构建并记录体积**

Run: `bash build.sh`
Expected: 输出 `built: build/TaskbarClear.exe`。

Run: `stat -c %s build/TaskbarClear.exe`
Expected: ≤ 204800；记录实际值（Task 4 写入 README）。若超标，先加 `-Wl,--gc-sections` 已有仍超标时，检查是否误引入 STL。

- [ ] **Step 8: 运行并做视觉验证**

Run: `./build/TaskbarClear.exe`
Expected: 托盘出现蓝色胶囊图标；任务栏立即全透明（壁纸直接透出，图标/时间清晰可点）。

截图留证（保存到 `build/`，不提交）：

```powershell
powershell -NoProfile -Command "Add-Type -AssemblyName System.Windows.Forms,System.Drawing; $b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds; $bmp=New-Object System.Drawing.Bitmap($b.Width,$b.Height); $g=[System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size); $bmp.Save('build\shot1.png',[System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()"
```

用 Read 查看 `build/shot1.png`，确认任务栏区域透出壁纸。

- [ ] **Step 9: 手工交互验收（请用户配合点击，记录结果）**

依次确认：
1. 左键单击托盘图标 → 任务栏恢复系统默认；再点 → 全透明
2. 右键托盘图标 → 菜单项：`任务栏透明`（透明态带勾选）、`恢复系统默认`、`退出`
3. 打开开始菜单、关闭；最大化任意窗口 → 透明保持
4. 退出 → 外观恢复默认、托盘图标消失
5. 再次双击 exe（先在运行状态下），用 `powershell -NoProfile -Command "(Get-Process TaskbarClear -ErrorAction SilentlyContinue | Measure-Object).Count"` 确认进程数为 1

- [ ] **Step 10: 提交**

```bash
git add tools/gen-icons.js res/resource.h res/app.rc res/app_on.ico res/app_off.ico src/main.cpp build.sh build.bat
git commit -m "feat: 托盘交互完整的 TaskbarClear 应用（图标/资源/单实例/退出恢复）"
```

---

### Task 4: 实测数据、README 与完整验收

**Files:**
- Create: `README.md`

**Interfaces:**
- Consumes: `build/TaskbarClear.exe`（Task 3）。
- Produces: 带真实实测数据的 README；最终验收结论。

- [ ] **Step 1: 启动应用并测资源占用**

Run: `./build/TaskbarClear.exe`

Run: `powershell -NoProfile -Command "Get-Process TaskbarClear | Select-Object Name,Id,WorkingSet64,PrivateMemorySize64,Handles | Format-List"`
Expected: 记录 WorkingSet64（目标 ≤ 5MB = 5242880）、PrivateMemorySize64、Handles（记为空闲句柄数 H0）。

- [ ] **Step 2: 50 次切换稳定性**

请用户左键点击托盘图标 50 次（透明↔恢复来回）。再运行 Step 1 的 PowerShell 命令。
Expected: 进程仍在、界面无卡顿；Handles 与 H0 差值 ≤ 5；WorkingSet64 仍 ≤ 5MB。

- [ ] **Step 3: exe 体积与自测回归**

Run: `stat -c %s build/TaskbarClear.exe`
Expected: ≤ 204800，记录实际值。

Run: `bash build.sh test`
Expected: `selftest: all checks passed`。

- [ ] **Step 4: 可选：explorer 重启自动恢复（必须先获得用户明确同意）**

先询问用户是否允许重启 explorer（桌面会闪一下、文件资源管理器窗口会关闭）。

若同意：

```powershell
powershell -NoProfile -Command "Stop-Process -Name explorer -Force; Start-Sleep -Seconds 2; if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }"
```

Expected: explorer 恢复后任务栏自动恢复全透明（应用仍为开启态）、托盘图标重新出现。用截图命令（同 Task 3 Step 8，保存 `build/shot_restart.png`）加用户确认留证。

若不同意：在 README 写“explorer 重启自动恢复：未实测（未获授权）”，不执行该命令。

- [ ] **Step 5: 写 `README.md`（用前几步实测值替换右列示例值）**

````markdown
# TaskbarClear

把 Windows 10 任务栏变全透明的极简小工具：托盘常驻，一键透明，一键恢复。

## 用法

- 双击 `TaskbarClear.exe`：任务栏立即变全透明
- 左键单击托盘图标：透明 / 恢复系统默认 来回切换
- 右键托盘图标：任务栏透明（当前状态带勾选）、恢复系统默认、退出
- 退出时自动恢复系统默认外观；explorer 重启后会自动恢复透明状态

## 不包含

开机自启、记住上次状态、动态规则、自定义颜色、快捷键（以后要加都是小增量）。

## 构建

需要 MSYS2 UCRT64 的 g++ 与 windres（默认路径 D:\C++\MSYS2\ucrt64\bin，可用环境变量 MSYS2_BIN 指定）。

```bash
./build.sh        # 生成 build/TaskbarClear.exe
./build.sh test   # 运行核心逻辑自测
./build.sh probe  # 生成任务栏透明度探针（运行后透明 3 秒再恢复）
```

Windows 命令行：`build.bat [all|test|probe]`

## 实测数据（2026-11-24，Win10 19045 x64）

| 项目 | 目标 | 实测 |
|---|---|---|
| exe 大小 | ≤ 200 KB | 示例：123456 字节 |
| 工作集内存 | ≤ 5 MB | 示例：2.3 MB |
| 空闲句柄数 | 稳定 | 示例：H0=xx |
| 50 次切换后句柄数 | 与 H0 差 ≤ 5 | 示例：H1=xx |
| explorer 重启自动恢复 | 通过 | 示例：通过 / 未实测（未获授权） |

## 已知限制

- 只保证 Windows 10 x64（10.0.19045 实测）；Win11 未验证
- 使用未公开接口，Windows 大版本更新后可能失效
- 全屏独占游戏、远程桌面下效果可能被系统接管
- 与 TranslucentTB 等同类工具同时运行可能互相覆盖
````

- [ ] **Step 6: 提交（.md 被全局忽略，必须 -f）**

```bash
git add -f README.md
git commit -m "docs: README 使用说明与实测数据"
```

---

## Plan Self-Review

- Spec 覆盖：5.1 启动（Task 3 单实例/隐藏顶层窗口/重试 5×200/失败弹窗）、5.2 交互与状态语义（Task 1 逻辑 + Task 3 绑定）、5.3 TaskbarCreated 与 WM_DISPLAYCHANGE（Task 3 WndProc）、5.4 退出恢复（Task 3 QuitApp）、6 文件清单（四个任务全覆盖）、7 错误处理（MessageBox/气泡/DebugLog）、8 构建（build.sh/build.bat）、9 测试与验收（Task 1 自测 + Task 2 探针 + Task 3/4 手工清单与实测）、10 限制（README）。
- 无占位符：所有代码/命令均为完整可执行内容；README 仅需填入实测数字。
- 类型一致性：`taskbar::ApplyResult{windows_found, windows_failed}`、`TaskbarWindows{items[64], count}`、`core::*` 签名在各任务间一致。