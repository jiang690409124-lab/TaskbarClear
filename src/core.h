#pragma once

namespace core {

enum class TbState { Default, Transparent };

constexpr int kCmdOpacity = 1001;
constexpr int kCmdExit    = 1002;

constexpr int kTaskbarFindMaxAttempts = 5;
constexpr int kTaskbarFindDelayMs     = 200;

TbState ToggleTarget(TbState current);
TbState StateAfterApply(TbState current, TbState target, bool apply_ok);
const wchar_t* TooltipText(TbState state);

// 透明度百分比（1-100）映射为 tint alpha（252..0）；
// 0% 不走这里，由调用方直接恢复系统默认外观。
int TintAlphaFromTransparency(int transparency_percent);

} // namespace core