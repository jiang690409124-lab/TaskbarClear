# TaskbarClear 设计文档

- 日期：2026-11-24
- 状态：待用户复核
- 目标产物：Windows 任务栏透明小工具（对标 TaskbarTB v1.1 一类工具的极简版）
- 已确认口径：极简开关 / C++ 原生 / 仅 Windows 10 x64（本机 10.0.19045）

## 1. 背景与目标

Windows 10 的任务栏默认是不透明色块，会遮挡壁纸底部。本工具常驻托盘，一键把主/副屏任务栏变全透明（壁纸直接透出，图标与文字照常清晰），再点一下恢复系统默认。

目标：单文件绿色 exe，体积与内存同原版一个量级（exe ≤ 200KB、工作集 ≤ 5MB，实测记录），免安装、免运行库、不要管理员权限、不改注册表与系统文件。

## 2. 范围

### 做

- 双击运行即把任务栏变全透明
- 托盘图标常驻；左键单击在「透明 / 恢复系统默认」之间切换
- 右键菜单：任务栏透明度、退出（精简为两项）
- 透明度可调 0%-100%（右键「任务栏透明度」小窗滑块，拖动实时生效；0% = 系统默认外观，100% = 完全透明且清晰无模糊）
- 覆盖主任务栏与所有副屏任务栏
- explorer 重启后自动重新应用当前状态
- 退出时恢复系统默认外观

### 不做

- 开机自启、记住上次状态、动态规则（最大化/开始菜单时切换）、自定义颜色、快捷键、安装包/卸载器、多语言、自动更新

## 3. 环境与约束

- 本机：Windows 10 22H2（10.0.19045）x64
- 编译器：MSYS2 UCRT64 g++（本机已有：`D:\C++\MSYS2\ucrt64\bin\g++.exe`）；本机无 make/cmake，构建用脚本直调 g++
- 无第三方依赖；不要管理员权限；不修改系统文件与注册表
- 仅保证 Win10 x64；Win11 不验证（若可用属额外收益）
- 目标体积：exe ≤ 200KB；内存按任务管理器「内存」列（私有工作集）≤ 5MB；总工作集含共享 DLL 页，只记录实测值不做硬性目标

## 4. 实现原理

使用 Windows 未公开接口 `SetWindowCompositionAttribute`（user32.dll，Win10 19045 可用），对任务栏顶层窗口应用「强调策略」（ACCENT_POLICY）：

- 结构：`WINDOWCOMPOSITIONATTRIBDATA { DWORD Attrib; PVOID pvData; SIZE_T cbData; }`，Attrib = `WCA_ACCENT_POLICY`（19）
- `ACCENT_POLICY { int AccentState; int AccentFlags; DWORD GradientColor; int AnimationId; }`
- 透明（100%）：AccentState = 2（ACCENT_ENABLE_TRANSPARENTGRADIENT），AccentFlags = 2，GradientColor = 0x00202020（ARGB，alpha = 0）
- 半透明：AccentState = 2、AccentFlags = 2，GradientColor = (alpha << 24) | 0x00202020；alpha = 255 × (100 − 透明度%) / 100（四舍五入），alpha = 255 时为深灰实心
  - 实测备注（Win10 19045，条纹背景对比度）：state 2 + flags 2 + alpha 0 = 清晰全透明（对比度 25.8）；state 2 + flags 0 + alpha 0 会退回磨砂模糊（对比度 5.4）；BLURBEHIND / ACRYLIC 都会模糊（对比度 18.1）。因此必须使用 AccentFlags = 2。
- 恢复：AccentState = 0（ACCENT_DISABLED），其余置 0，然后向所有顶层窗口广播 `WM_DWMCOLORIZATIONCOLORCHANGED`，让 Explorer 重绘自身默认外观
  - 实测备注（Win10 19045）：只做 DISABLED 会让任务栏变纯黑（亮度 8.7 vs 真默认 54.3）；加广播后与重启 explorer 得到的未干预默认外观差 1.07

目标窗口：

- 主任务栏：类名 `Shell_TrayWnd`
- 副屏任务栏：类名 `Shell_SecondaryTrayWnd`

关键约束：

- 不注入 explorer、不 hook、不改注册表，只做窗口作用域调用
- explorer 重启后任务栏窗口重建，通过 `RegisterWindowMessage(L"TaskbarCreated")` 监听并重新应用
- 系统广播只发给顶层窗口，接收消息的隐藏窗口必须是普通顶层窗口（不能用 HWND_MESSAGE 消息专用窗口）

被否决的方案：

- 改注册表 `EnableTransparency`：只能半透明，且改的是全系统开关，会连带影响开始菜单等
- 注入 explorer 进程改渲染：复杂、易被杀软误报、没有必要

## 5. 行为规格

### 5.1 启动

1. 单实例：`CreateMutexW(L"Local\\TaskbarClearMutex")`，已存在则静默退出（不开第二个）
2. 注册窗口类 `TaskbarClear`，创建隐藏顶层窗口（接收系统广播）
3. 添加托盘图标（NIM_ADD），提示文字：`任务栏透明：已开启`
4. 立即应用透明；找不到任务栏窗口时重试 5 次 × 200ms；仍失败 → MessageBox 说明原因并退出（非 0 退出码，不假装成功）

### 5.2 交互

- 左键单击托盘图标：在「系统默认外观 / 上次非零透明度（默认 100%）」之间切换
- 右键单击：弹出菜单，只有两项
  - `任务栏透明度`（打开 420×200 小窗，滑块 0-100%，拖动实时生效；0% = 系统默认外观）
  - `退出`
- 每次状态变化后更新托盘图标（开启态/关闭态两套图标）与提示文字
- 状态语义：先尝试应用到任务栏，成功后才翻转状态并刷新 UI；失败则气泡提示、状态不变

### 5.3 系统事件

- `TaskbarCreated`（explorer 重启）：当前为透明则重新查找并应用；当前为恢复态则不动
- `WM_DISPLAYCHANGE`（显示器变化）：重新枚举所有任务栏并按当前状态应用
- 定时补应用：透明状态下每 500ms 重新应用一次（0% 时不做任何动作），用于对抗开始菜单/搜索等系统界面造成的强调策略重置
- 开始菜单/搜索打开期间：Windows 会强制接管任务栏外观，本工具无法覆盖（实测 100ms 补应用仍被覆盖）；关闭后 ≤0.5s 自动恢复设定透明度
- 运行中应用/恢复失败：托盘气泡提示（NIF_INFO），不静默、不无限自动重试

### 5.4 退出

1. 应用「恢复系统默认」；失败则气泡提示，但仍在 1 秒内退出（不留僵尸进程）
2. 删除托盘图标
3. 销毁窗口并退出；确保进程结束、图标消失、外观恢复

## 6. 架构与文件

```
docs/superpowers/specs/2026-11-24-taskbar-clear-design.md   本文件
src/core.h / core.cpp        纯逻辑：状态机、重试策略、菜单/提示文案（无 Win32 依赖，可单测）
src/taskbar.h / taskbar.cpp  Win32：枚举任务栏窗口、应用/清除 accent
src/main.cpp                 WinMain、单实例、托盘、菜单、消息循环、系统事件
res/app.rc + app_on.ico + app_off.ico   托盘图标资源（开启=蓝色，关闭=灰色，32x32 提交入库）
tests/selftest.cpp           控制台自测（退出码 0 = 全部通过）
build.sh / build.bat         构建脚本
README.md                    使用说明（白话）
```

单元职责：

- core：回答「下一个状态是什么、重试几次、菜单怎么勾、提示写什么」
- taskbar：回答「任务栏窗口有哪些、apply/clear 是否成功」
- main：只做编排与 Win32 消息胶水

## 7. 错误处理与日志

- 启动失败（找不到任务栏 / 系统接口不可用）：MessageBoxW 明确说明原因，非 0 退出码
- 运行中失败（apply/clear、Shell_NotifyIcon）：托盘气泡提示；不静默、不自动降级
- 诊断：关键失败路径调用 OutputDebugStringW（不落文件日志）
- 退出时释放图标、菜单、互斥量、窗口资源

## 8. 构建

```bash
# 编译资源
windres res/app.rc -O coff -o build/app.res

# 主程序（Git Bash / MSYS2 均可）
g++ -std=c++17 -Os -s -municode -mwindows -fno-exceptions -fno-rtti -static \
    src/main.cpp src/taskbar.cpp src/core.cpp build/app.res \
    -luser32 -lshell32 -lgdi32 -o build/TaskbarClear.exe

# 自测
g++ -std=c++17 -O2 -static -fno-exceptions -fno-rtti \
    tests/selftest.cpp src/core.cpp -o build/selftest.exe
```

`build.bat` 提供 cmd 下的等价命令；`build.sh` 额外提供 `test` 子命令（编译并运行 selftest）。

## 9. 测试与验收

### 9.1 自动测试

`tests/selftest.cpp` 覆盖：状态机全部事件、重试次数上限、菜单勾选映射、提示文案映射。运行 `./build.sh test`，退出码 0 视为通过。

### 9.2 手工验收清单（必须全过）

1. 双击运行：托盘出现图标，任务栏立即全透明，壁纸透出
2. 图标/时间/开始按钮清晰可点，开始菜单能正常打开和关闭
3. 左键点击：恢复系统默认；再点：透明
4. 右键菜单勾选状态与实际一致
5. 打开、最大化、切换窗口后透明保持
6. 多屏（如有）：副屏任务栏同样透明
7. 退出：外观恢复默认，托盘图标消失，进程消失
8. 透明↔恢复来回 50 次：无卡顿，任务管理器句柄数稳定
9. 再次双击 exe：不会出现第二个实例
10. explorer 重启后自动恢复透明（需用户同意后执行，见 10）
11. 右键「任务栏透明度」打开小窗；滑块 100% 时条纹背景应清晰透过任务栏（无模糊）
12. 滑块 50%：半透明且清晰（背景不发糊）
13. 滑块 0%：与系统默认外观一致（对比"重启 explorer 后、程序未干预"的真默认，实测像素差 1.07）

### 9.3 资源实测

exe 文件大小、私有工作集/总工作集/私有提交、句柄数记录进 README，与目标值对照；总工作集单独列出，避免与共享 DLL 页混淆。资源分两段记录：仅托盘空闲、打开设置窗口后（本机因输入法注入组件上升，空白窗口可复现，非本进程泄漏；切换 40 次句柄不增长）。

## 10. 已知限制与风险

- 使用未公开接口，Windows 大版本更新后可能失效（本期只保 Win10 19045）
- 全屏独占游戏、远程桌面下效果可能被系统接管，属系统行为
- 任务栏内搜索框、小组件等由系统自绘，透明度随任务栏整体生效，不做单独处理
- 与其它任务栏美化工具（如 TranslucentTB）同时运行可能互相覆盖，本工具不检测冲突
- 打开设置窗口会加载系统/第三方注入组件（本机搜狗输入法约 +25MB 私有工作集），属环境行为，非本程序泄漏；关闭窗口后注入 DLL 不会卸载
- 开始菜单/搜索/任务视图等系统界面打开期间，Windows 会强制改变任务栏外观，本工具无法覆盖；关闭后 ≤0.5s 自动恢复设定透明度
- 测试第 10 项需要重启 explorer：桌面会闪一下、资源管理器窗口会关闭，其他程序不受影响；执行前必须先征得用户同意，不同意则只做代码级验证

## 11. 后续可选增量（不在本期）

开机自启（HKCU Run 或启动文件夹）、状态持久化、动态规则、自定义颜色、快捷键。

## 12. 决策记录

| 决策 | 结论 | 来源 |
|---|---|---|
| 功能范围 | 极简开关（托盘 + 一键透明/恢复） | 用户选择 |
| 技术栈 | C++ 原生（MSYS2 g++） | 用户选择 |
| 系统支持 | 仅 Win10 x64 | 用户选择 |
| 实现方案 | SetWindowCompositionAttribute，无注入 | 设计推荐，用户确认 |
| 应用名 | TaskbarClear（可改） | 设计默认 |
| 启动状态 | 启动即透明 | 设计默认 |
| 退出行为 | 自动恢复系统默认外观 | 设计默认 |
| 透明度调节 | 菜单只有「任务栏透明度」+「退出」；滑块 0-100%，0% = 系统默认外观、100% = 清晰全透明 | 用户追加需求（2026-11-24） |
| 透明配方 | AccentState=2 + AccentFlags=2 + alpha（0 为清晰全透明） | 本机条纹实验结论 |
| 0% 还原 | ACCENT_DISABLED + 广播 WM_DWMCOLORIZATIONCOLORCHANGED | 本机真默认对比实验（差 1.07） |