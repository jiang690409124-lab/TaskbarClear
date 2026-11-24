#pragma once

namespace core {

enum class TbState { Default, Transparent };

constexpr int kCmdTransparent = 1001;
constexpr int kCmdDefault     = 1002;
constexpr int kCmdExit        = 1003;
constexpr int kCmdOpacity     = 1004;

constexpr int kTaskbarFindMaxAttempts = 5;
constexpr int kTaskbarFindDelayMs     = 200;

TbState ToggleTarget(TbState current);
bool CommandToTarget(int cmd, TbState* out);
TbState StateAfterApply(TbState current, TbState target, bool apply_ok);
const wchar_t* TooltipText(TbState state);
bool IsTransparentChecked(TbState state);

// 透明度百分比（0=完全不透明，100=完全透明）映射为 tint alpha（255..0）
int TintAlphaFromTransparency(int transparency_percent);

} // namespace core