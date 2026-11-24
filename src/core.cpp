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