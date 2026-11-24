#include "core.h"

namespace core {

TbState ToggleTarget(TbState current) {
    return current == TbState::Transparent ? TbState::Default : TbState::Transparent;
}

TbState StateAfterApply(TbState current, TbState target, bool apply_ok) {
    return apply_ok ? target : current;
}

const wchar_t* TooltipText(TbState state) {
    return state == TbState::Transparent ? L"任务栏透明：已开启" : L"任务栏透明：已关闭";
}

int TintAlphaFromTransparency(int transparency_percent) {
    if (transparency_percent < 0) transparency_percent = 0;
    if (transparency_percent > 100) transparency_percent = 100;
    return (255 * (100 - transparency_percent) + 50) / 100;
}

} // namespace core