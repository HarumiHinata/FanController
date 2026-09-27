#pragma once
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <array>
#include <memory>
#include <string>
#include "core/controller.h"
#include "monitor/history.h"
#include "monitor/sensors.h"

namespace fan::ui {
enum Command { Auto=100,Boost,Quiet,Startup=103,CloseTray,Diagnostics,Exit,Show,Settings,Optimize,Hour1,Hour6,Hour24 };
constexpr std::array<int,12> buttonIds{Auto,Boost,Optimize,Settings,Hour1,Hour6,Hour24,Startup,CloseTray,Diagnostics,Exit,Quiet};
struct Typography {
    std::array<std::array<std::unique_ptr<Gdiplus::Font>,2>,57> fonts;
    Gdiplus::Font* get(float size,int style);
};
struct Dashboard {
    HWND window{};
    std::array<HWND,buttonIds.size()> buttons{};
    Snapshot state;
    monitor::Sample metrics;
    monitor::Identity identity;
    std::unique_ptr<monitor::History> history=std::make_unique<monitor::History>();
    Typography typography;
    bool mock{},startup{},toTray{true},closing{},preferences{};
    float scale{1};
    int hours{1};
    std::wstring transient;
};
void layout(Dashboard& app);
void paint(Dashboard& app,HDC target,const RECT& rect);
void drawButton(Dashboard& app,const DRAWITEMSTRUCT& item);
void preferences(Dashboard& app,bool visible);
void seedPreview(Dashboard& app);
}
