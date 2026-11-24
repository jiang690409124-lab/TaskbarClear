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

    CHECK(core::TintAlphaFromTransparency(100) == 0);
    CHECK(core::TintAlphaFromTransparency(75) == 64);
    CHECK(core::TintAlphaFromTransparency(50) == 128);
    CHECK(core::TintAlphaFromTransparency(25) == 191);
    CHECK(core::TintAlphaFromTransparency(0) == 255);
    CHECK(core::TintAlphaFromTransparency(-10) == 255);
    CHECK(core::TintAlphaFromTransparency(110) == 0);
    CHECK(!core::CommandToTarget(core::kCmdOpacity, &target));

    if (g_failures == 0) {
        std::printf("selftest: all checks passed\n");
        return 0;
    }
    std::printf("selftest: %d failure(s)\n", g_failures);
    return 1;
}