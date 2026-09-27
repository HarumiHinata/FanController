#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <gdiplus.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <array>
#include <algorithm>
#include <string>
#include "core/controller.h"
#include "hardware/lenovo.h"
#include "platform/platform.h"
using namespace Gdiplus;
namespace {
constexpr UINT Changed = WM_APP + 1, Tray = WM_APP + 2;
constexpr int Auto = 100, Boost = 101, Low = 102, Startup = 103, CloseTray = 104, Diagnostics = 105, Exit = 106, Show = 107;
constexpr wchar_t ClassName[] = L"FanController.Main.81BR";
constexpr wchar_t DemoClassName[] = L"FanController.Demo.81BR";
const Color Background(255,15,19,25), Panel(255,24,31,40), Border(255,43,54,68);
const Color Foreground(255,237,242,249), Muted(255,148,164,182), Green(255,99,216,180), Orange(255,255,137,91);
struct Typography {
    std::array<std::array<std::unique_ptr<Font>,2>,49> fonts;
    Font* get(float size,int style) {
        auto& entry=fonts[static_cast<size_t>(size)][style==FontStyleBold?1:0];
        if(!entry) entry=std::make_unique<Font>(L"Microsoft YaHei UI",size,style,UnitPixel);
        return entry.get();
    }
};
Typography* typography{}; // UI thread only; lifetime ends before GDI+ shutdown.
struct App {
    HWND window{};
    std::array<HWND,7> buttons{};
    std::unique_ptr<fan::Controller> controller;
    fan::Snapshot state;
    fan::Handle mutex, reference;
    HICON icon{};
    bool mock{}, background{}, startup{}, toTray{true}, closing{}, notified{}, trayAdded{};
    UINT taskbarCreated{};
    float scale{1};
    std::wstring transient;
    std::uint64_t loggedRevision{};
    std::wstring snapshotPath, stressPath;
    bool testFailed{};
    explicit App(bool demo, bool hidden, HANDLE own, HANDLE legacy) : mutex(own), reference(legacy), mock(demo), background(hidden) {}
};
void text(Graphics& g, const std::wstring& value, float x, float y, float w, float h, float size, Color color, int style = FontStyleRegular) {
    Font* font=typography->get(size,style); SolidBrush brush(color);
    StringFormat format; format.SetTrimming(StringTrimmingEllipsisCharacter); format.SetFormatFlags(StringFormatFlagsLineLimit);
    g.DrawString(value.c_str(), -1, font, RectF(x,y,w,h), &format, &brush);
}
void box(Graphics& g, float x, float y, float w, float h, Color fill, Color stroke = Border) {
    SolidBrush brush(fill); Pen pen(stroke);
    g.FillRectangle(&brush,x,y,w,h); g.DrawRectangle(&pen,x,y,w,h);
}
void fanGlyph(Graphics& g, float cx, float cy, bool boost) {
    const Color accent = boost ? Orange : Green;
    Pen ring(Color(255,46,62,73), 2); g.DrawEllipse(&ring,cx-72,cy-72,144.0f,144.0f);
    Pen mark(accent, 4); g.DrawArc(&mark,cx-72,cy-72,144.0f,144.0f,215.0f,105.0f);
    SolidBrush blade(Color(90,accent.GetR(),accent.GetG(),accent.GetB()));
    for (int i=0;i<5;++i) { const auto save=g.Save(); g.TranslateTransform(cx,cy); g.RotateTransform(static_cast<float>(i*72)); g.FillEllipse(&blade,-12,-54,27,45); g.Restore(save); }
    SolidBrush center(accent); g.FillEllipse(&center,cx-12,cy-12,24.0f,24.0f);
}
void layout(App& app) {
    RECT r{}; GetClientRect(app.window,&r);
    app.scale = std::min(static_cast<float>(r.right)/960.0f, static_cast<float>(r.bottom)/650.0f);
    const auto place=[&](int index,int x,int y,int w,int h) { MoveWindow(app.buttons[index],static_cast<int>(x*app.scale),static_cast<int>(y*app.scale),static_cast<int>(w*app.scale),static_cast<int>(h*app.scale),TRUE); };
    place(0,32,352,286,96); place(1,337,352,286,96); place(2,642,352,286,96);
    place(3,48,511,258,36); place(4,326,511,264,36); place(5,706,511,204,36); place(6,742,589,186,35);
    InvalidateRect(app.window,nullptr,FALSE);
}
void paint(App& app, HDC target, const RECT& rect) {
    HDC memory=CreateCompatibleDC(target); HBITMAP bitmap=CreateCompatibleBitmap(target,rect.right,rect.bottom);
    if (!memory || !bitmap) { if(bitmap) DeleteObject(bitmap); if(memory) DeleteDC(memory); return; }
    HGDIOBJ previous=SelectObject(memory,bitmap);
    {
        Graphics g(memory); g.Clear(Background); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.ScaleTransform(app.scale,app.scale);
        const bool boost=app.state.mode==fan::Mode::Boost && app.state.phase==fan::Phase::Ready;
        const bool error=app.state.phase==fan::Phase::Fault || (app.state.phase==fan::Phase::Stopped && !app.state.autoConfirmed);
        text(g,L"FAN / CONTROL",32,22,500,34,25,Foreground,FontStyleBold);
        text(g,L"本机散热控制台",33,63,360,23,12,Muted);
        box(g,690,30,238,34,Panel);
        text(g,app.mock?L"演示模式 · 不连接硬件":L"IDEAPAD 720S · 81BR",707,38,211,23,12,app.mock?Orange:Green);
        box(g,32,110,596,196,Panel);
        fanGlyph(g,137,208,boost);
        text(g,L"当前控制模式",241,139,350,25,12,Muted);
        std::wstring mode=error?L"需要检查":(app.closing?L"正在恢复":(boost?L"强冷模式":L"原厂自动"));
        if (app.state.phase==fan::Phase::Starting) mode=L"正在连接";
        if (app.state.phase==fan::Phase::Applying) mode=L"正在切换";
        text(g,mode,238,174,367,56,36,error?Orange:Foreground,FontStyleBold);
        text(g,app.mock?L"使用模拟驱动，不改变实际风扇":(boost?L"主动维持高风速，增强散热":L"由电脑原厂策略按需调节风速"),241,247,370,36,13,Muted);
        box(g,648,110,280,196,Panel);
        text(g,L"硬件状态",669,132,237,25,14,Foreground,FontStyleBold);
        text(g,L"温度",669,177,70,24,12,Muted); text(g,L"未提供",804,174,100,27,15,Muted);
        text(g,L"风扇转速",669,213,100,24,12,Muted); text(g,L"未提供",804,210,100,27,15,Muted);
        text(g,L"此接口提供档位，不提供传感器读数",669,264,241,30,11,Muted);
        text(g,L"控制模式",32,324,400,24,14,Foreground,FontStyleBold);
        text(g,L"每次启动默认恢复原厂自动",679,326,249,23,11,Muted);
        box(g,32,478,896,89,Panel);
        text(g,L"偏好设置",48,490,260,22,12,Muted);
        const auto status=app.transient.empty()?app.state.detail:app.transient;
        text(g,status,32,590,689,42,12,error?Orange:Muted);
        text(g,L"0.1.0  /  NATIVE EDITION",32,636,500,14,9,Muted);
    }
    BitBlt(target,0,0,rect.right,rect.bottom,memory,0,0,SRCCOPY);
    SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory);
}
void drawButton(App& app, const DRAWITEMSTRUCT& item) {
    Graphics g(item.hDC); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    const float w=static_cast<float>(item.rcItem.right)/app.scale, h=static_cast<float>(item.rcItem.bottom)/app.scale;
    g.ScaleTransform(app.scale,app.scale);
    const int id=static_cast<int>(item.CtlID);
    const bool disabled=(item.itemState&ODS_DISABLED)!=0;
    const bool selected=(id==Auto && app.state.autoConfirmed) || (id==Boost && app.state.mode==fan::Mode::Boost && app.state.phase==fan::Phase::Ready);
    Color accent=id==Boost?Orange:Green;
    if (id==Low) accent=Muted;
    if (id==Auto || id==Boost || id==Low) {
        box(g,0,0,w-1,h-1,selected?Color(255,28,48,48):Panel, selected?accent:Border);
        if (selected) { SolidBrush bar(accent); g.FillRectangle(&bar,0.0f,0.0f,3.0f,h); }
        const wchar_t* title=id==Auto?L"原厂自动":(id==Boost?L"强冷 / 高速":L"低速 / 暂不可用");
        const wchar_t* desc=id==Auto?L"正常散热 · 推荐默认":(id==Boost?L"维持高风速 · 按需开启":L"尚无可靠温度保护数据");
        text(g,title,19,17,w-38,31,18,disabled?Muted:(selected?accent:Foreground),FontStyleBold);
        text(g,desc,19,58,w-38,24,11,Muted);
    } else {
        box(g,0,0,w-1,h-1,(item.itemState&ODS_SELECTED)?Color(255,42,57,68):Panel);
        std::wstring label;
        if(id==Startup) label=app.startup?L"✓  登录后自动启动":L"○  登录后自动启动";
        if(id==CloseTray) label=app.toTray?L"✓  关闭窗口时留在托盘":L"○  关闭窗口时直接退出";
        if(id==Diagnostics) label=L"打开诊断日志";
        if(id==Exit) label=app.closing?L"正在恢复…":L"恢复自动并退出";
        text(g,label,12,8,w-20,h-8,12,disabled?Muted:Foreground);
    }
    if(item.itemState&ODS_FOCUS) { Pen focus(Foreground); focus.SetDashStyle(DashStyleDot); g.DrawRectangle(&focus,4.0f,4.0f,w-9,h-9); }
}
bool savePreview(App& app, const std::wstring& path) {
    RECT rect{}; GetClientRect(app.window,&rect);
    HDC screen=GetDC(app.window), memory=CreateCompatibleDC(screen);
    HBITMAP buffer=CreateCompatibleBitmap(screen,rect.right,rect.bottom);
    HGDIOBJ old=SelectObject(memory,buffer);
    paint(app,memory,rect);
    for (HWND button : app.buttons) {
        RECT position{}; GetWindowRect(button,&position); MapWindowPoints(nullptr,app.window,reinterpret_cast<POINT*>(&position),2);
        const int saved=SaveDC(memory); SetViewportOrgEx(memory,position.left,position.top,nullptr);
        DRAWITEMSTRUCT item{}; item.CtlID=GetDlgCtrlID(button); item.hwndItem=button; item.hDC=memory;
        item.rcItem={0,0,position.right-position.left,position.bottom-position.top};
        if(!IsWindowEnabled(button)) item.itemState=ODS_DISABLED;
        drawButton(app,item); RestoreDC(memory,saved);
    }
    bool saved=false;
    {
        UINT bytes{}, count{}; GetImageEncodersSize(&count,&bytes);
        std::vector<BYTE> storage(bytes); auto encoders=reinterpret_cast<ImageCodecInfo*>(storage.data());
        if(GetImageEncoders(count,bytes,encoders)==Ok) {
            Bitmap image(buffer,nullptr);
            for(UINT i=0;i<count;++i) if(wcscmp(encoders[i].MimeType,L"image/png")==0) { saved=image.Save(path.c_str(),&encoders[i].Clsid)==Ok; break; }
        }
    }
    SelectObject(memory,old); DeleteObject(buffer); DeleteDC(memory); ReleaseDC(app.window,screen); return saved;
}
struct Usage { DWORD handles{},gdi{},user{}; SIZE_T bytes{},liveHeap{},allocations{}; };
Usage usage() {
    Usage u; GetProcessHandleCount(GetCurrentProcess(),&u.handles);
    u.gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS); u.user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)); u.bytes=memory.PrivateUsage;
    const DWORD capacity=GetProcessHeaps(0,nullptr); std::vector<HANDLE> heaps(capacity);
    const DWORD count=GetProcessHeaps(capacity,heaps.data());
    if(count<=capacity) for(DWORD i=0;i<count;++i) {
        if(!HeapLock(heaps[i])) continue;
        PROCESS_HEAP_ENTRY entry{};
        while(HeapWalk(heaps[i],&entry)) if(entry.wFlags&PROCESS_HEAP_ENTRY_BUSY) { u.liveHeap+=entry.cbData; ++u.allocations; }
        HeapUnlock(heaps[i]);
    }
    return u;
}
void stress(App& app) {
    SetWindowPos(app.window,nullptr,-20000,-20000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    const auto cycle=[&] {
        ShowWindow(app.window,SW_SHOWNOACTIVATE); app.toTray=!app.toTray;
        RedrawWindow(app.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        ShowWindow(app.window,SW_HIDE);
        // Real use pumps messages between interactions. A blocked stress loop would
        // accumulate window-manager notifications instead of measuring steady state.
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    };
    for(int i=0;i<50;++i) cycle();
    const auto before=usage();
    for(int i=0;i<1000;++i) cycle();
    const auto middle=usage();
    for(int i=0;i<1000;++i) cycle();
    const auto after=usage();
    std::ofstream output(std::filesystem::path(app.stressPath),std::ios::binary);
    const auto emit=[&](const Usage& u) { output<<"{\"handles\":"<<u.handles<<",\"gdi\":"<<u.gdi<<",\"user\":"<<u.user<<",\"privateBytes\":"<<u.bytes<<",\"liveHeapBytes\":"<<u.liveHeap<<",\"liveAllocations\":"<<u.allocations<<"}"; };
    output<<"{\"cycles\":2000,\"before\":"; emit(before); output<<",\"after1000\":"; emit(middle); output<<",\"after2000\":"; emit(after); output<<"}\n";
}
void trayIcon(App& app, bool add=false) {
    NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=app.window; data.uID=1; data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
    data.uCallbackMessage=Tray; data.hIcon=app.icon;
    const std::wstring tooltip=L"FanController · "+(app.mock?std::wstring(L"演示模式"):app.state.detail);
    wcsncpy_s(data.szTip,tooltip.c_str(),_TRUNCATE);
    if (Shell_NotifyIconW(add?NIM_ADD:NIM_MODIFY,&data)) app.trayAdded=true;
}
void show(App& app) { ShowWindow(app.window,SW_RESTORE); SetForegroundWindow(app.window); }
void beginExit(App& app) {
    if(app.closing) return;
    app.closing=true; app.controller->stop();
    for(auto button:app.buttons) EnableWindow(button,FALSE);
    SetTimer(app.window,1,100,nullptr); InvalidateRect(app.window,nullptr,FALSE);
}
void sync(App& app) {
    app.state=app.controller->snapshot();
    SetWindowTextW(app.buttons[0],app.state.autoConfirmed?L"原厂自动：已选择":L"原厂自动");
    SetWindowTextW(app.buttons[1],app.state.mode==fan::Mode::Boost && app.state.phase==fan::Phase::Ready?L"强冷高速：已选择":L"强冷高速");
    EnableWindow(app.buttons[0],!app.closing && app.state.phase!=fan::Phase::Starting);
    EnableWindow(app.buttons[1],!app.closing && app.state.phase==fan::Phase::Ready);
    if(GetFocus()==app.window && IsWindowEnabled(app.buttons[0])) SetFocus(app.buttons[0]);
    if(app.state.revision!=app.loggedRevision) {
        app.loggedRevision=app.state.revision;
        if(!app.mock) fan::log(app.state.detail+L"; error="+std::to_wstring(app.state.error)+L"; raw="+std::to_wstring(app.state.raw));
    }
    const auto title=std::wstring(L"FanController — ")+(app.mock?L"演示模式":app.state.detail);
    SetWindowTextW(app.window,title.c_str());
    trayIcon(app);
    for(auto button:app.buttons) InvalidateRect(button,nullptr,FALSE);
    if(IsWindowVisible(app.window)) InvalidateRect(app.window,nullptr,FALSE);
}
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) { app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); app->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if(!app) return DefWindowProcW(window,message,wp,lp);
    if(message==app->taskbarCreated && app->taskbarCreated) { trayIcon(*app,true); return 0; }
    switch(message) {
    case WM_CREATE: {
        app->icon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
        app->taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
        constexpr std::array<int,7> ids{Auto,Boost,Low,Startup,CloseTray,Diagnostics,Exit};
        constexpr std::array<const wchar_t*,7> titles{L"原厂自动",L"强冷高速",L"低速不可用：尚无温度保护",L"登录后自动启动",L"关闭窗口时留在托盘",L"打开诊断日志",L"恢复自动并退出"};
        for(size_t i=0;i<ids.size();++i) app->buttons[i]=CreateWindowExW(0,L"BUTTON",titles[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),GetModuleHandleW(nullptr),nullptr);
        EnableWindow(app->buttons[0],FALSE); EnableWindow(app->buttons[1],FALSE); EnableWindow(app->buttons[2],FALSE);
        app->toTray=fan::closeToTray();
        if(app->mock) EnableWindow(app->buttons[3],FALSE);
        else { const auto r=fan::startupStatus(app->startup); if(!r.ok) app->transient=r.detail; }
        SetWindowTextW(app->buttons[3],app->startup?L"登录后自动启动：已开启":L"登录后自动启动：已关闭");
        SetWindowTextW(app->buttons[4],app->toTray?L"关闭窗口时留在托盘：已开启":L"关闭窗口时直接退出");
        std::unique_ptr<fan::Backend> backend;
        if(app->mock) backend=std::make_unique<fan::MockBackend>(); else backend=std::make_unique<fan::LenovoBackend>(true);
        app->controller=std::make_unique<fan::Controller>(std::move(backend),[window]{PostMessageW(window,Changed,0,0);});
        trayIcon(*app,true); app->controller->start();
        BOOL dark=TRUE; DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
        if(!app->snapshotPath.empty() || !app->stressPath.empty()) SetTimer(window,2,300,nullptr);
        layout(*app); return 0;
    }
    case Changed: sync(*app); return 0;
    case WM_SETFOCUS: if(app->buttons[0] && IsWindowEnabled(app->buttons[0])) SetFocus(app->buttons[0]); return 0;
    case WM_SIZE: if(wp!=SIZE_MINIMIZED) layout(*app); return 0;
    case WM_DPICHANGED: { const RECT* r=reinterpret_cast<const RECT*>(lp); SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); return 0; }
    case WM_GETMINMAXINFO: { auto info=reinterpret_cast<MINMAXINFO*>(lp); const UINT dpi=GetDpiForWindow(window); info->ptMinTrackSize={MulDiv(720,dpi,96),MulDiv(520,dpi,96)}; return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc=BeginPaint(window,&ps); RECT r{}; GetClientRect(window,&r); paint(*app,dc,r); EndPaint(window,&ps); return 0; }
    case WM_PRINTCLIENT: { RECT r{}; GetClientRect(window,&r); paint(*app,reinterpret_cast<HDC>(wp),r); return 0; }
    case WM_DRAWITEM: drawButton(*app,*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
    case WM_COMMAND: {
        const int id=LOWORD(wp);
        if(id==Show) { show(*app); return 0; }
        if(app->closing) return 0;
        app->transient.clear();
        if(id==Auto || id==Boost) app->controller->request(id==Auto?fan::Mode::Auto:fan::Mode::Boost);
        if(id==Exit) beginExit(*app);
        if(id==Startup && !app->mock) {
            const auto result=fan::setStartup(!app->startup);
            const auto status=fan::startupStatus(app->startup);
            app->transient=!result.ok?result.detail:(!status.ok?status.detail:(app->startup?L"已开启登录自启动 · 每次先恢复原厂自动":L"已关闭登录自启动"));
            SetWindowTextW(app->buttons[3],app->startup?L"登录后自动启动：已开启":L"登录后自动启动：已关闭");
            fan::log(app->transient);
        }
        if(id==CloseTray) { app->toTray=!app->toTray; if(!app->mock) fan::setCloseToTray(app->toTray); SetWindowTextW(app->buttons[4],app->toTray?L"关闭窗口时留在托盘：已开启":L"关闭窗口时直接退出"); }
        if(id==Diagnostics) { ShellExecuteW(window,L"open",fan::dataDirectory().c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
        InvalidateRect(window,nullptr,FALSE); for(auto b:app->buttons) InvalidateRect(b,nullptr,FALSE); return 0;
    }
    case Tray:
        if(lp==WM_LBUTTONUP || lp==WM_LBUTTONDBLCLK) show(*app);
        else if(lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) {
            HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,Show,L"打开控制台"); AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,Auto,L"原厂自动"); AppendMenuW(menu,MF_STRING | (app->state.phase==fan::Phase::Ready?0:MF_GRAYED),Boost,L"强冷 / 高速");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,Exit,L"恢复自动并退出");
            POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
            const int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr); DestroyMenu(menu);
            if(choice) PostMessageW(window,WM_COMMAND,choice,0); PostMessageW(window,WM_NULL,0,0);
        } return 0;
    case WM_CLOSE:
        if(app->toTray && !app->closing && app->trayAdded) {
            ShowWindow(window,SW_HIDE);
            if(!app->notified) { NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=window; data.uID=1; data.uFlags=NIF_INFO; wcscpy_s(data.szInfoTitle,L"FanController 仍在托盘运行"); wcscpy_s(data.szInfo,L"右键托盘图标可恢复原厂自动并退出。"); data.dwInfoFlags=NIIF_INFO; Shell_NotifyIconW(NIM_MODIFY,&data); app->notified=true; }
        } else beginExit(*app); return 0;
    case WM_TIMER:
        if(wp==2 && app->state.phase==fan::Phase::Ready) {
            KillTimer(window,2);
            if(!app->snapshotPath.empty() && !savePreview(*app,app->snapshotPath)) app->testFailed=true;
            if(!app->stressPath.empty()) stress(*app);
            beginExit(*app);
        }
        if(wp==1 && app->controller->snapshot().phase==fan::Phase::Stopped) {
            KillTimer(window,1); app->controller->join(); sync(*app);
            if(!app->state.autoConfirmed && !app->mock) MessageBoxW(window,L"退出前无法确认已恢复正常模式。请检查原厂散热状态，必要时重启电脑。",L"恢复未确认",MB_OK|MB_ICONWARNING);
            DestroyWindow(window);
        } return 0;
    case WM_POWERBROADCAST:
        if(wp==PBT_APMSUSPEND) app->controller->suspend();
        if(wp==PBT_APMRESUMEAUTOMATIC) app->controller->resume();
        return TRUE;
    case WM_QUERYENDSESSION: app->controller->suspend(); return TRUE;
    case WM_ENDSESSION:
        if(wp) { app->controller->stop(); } else app->controller->resume(); return 0;
    case WM_DESTROY: { NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=window; data.uID=1; Shell_NotifyIconW(NIM_DELETE,&data); PostQuitMessage(app->testFailed?6:0); return 0; }
    }
    return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    bool mock=false,background=false;
    std::wstring snapshotPath,stressPath;
    int argc{}; LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1 && wcscmp(args[1],L"--guardian")==0) { const int result=fan::recoveryMain(argc,args); LocalFree(args); return result; }
    if(argc==2 && (wcscmp(args[1],L"--startup-on")==0 || wcscmp(args[1],L"--startup-off")==0)) {
        const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        const auto result=fan::setStartup(wcscmp(args[1],L"--startup-on")==0);
        fan::log(result.ok?L"自启动命令成功":result.detail); LocalFree(args);
        if(SUCCEEDED(initialized)) CoUninitialize(); return result.ok?0:1;
    }
    for(int i=1;i<argc;++i) {
        if(wcscmp(args[i],L"--demo")==0) mock=true;
        else if(wcscmp(args[i],L"--background")==0) background=true;
        else if(wcscmp(args[i],L"--snapshot")==0 && i+1<argc) { snapshotPath=args[++i]; mock=true; background=true; }
        else if(wcscmp(args[i],L"--ui-stress")==0 && i+1<argc) { stressPath=args[++i]; mock=true; background=true; }
        else { LocalFree(args); return 2; }
    }
    LocalFree(args);
    const wchar_t* windowClass=mock?DemoClassName:ClassName;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    GdiplusStartupInput input; ULONG_PTR token{}; if(GdiplusStartup(&token,&input,nullptr)!=Ok) return 3;
    HANDLE own=CreateMutexW(nullptr,FALSE,mock?L"Local\\FanController.Demo":L"Global\\FanController.Hardware.81BR");
    if(!own || GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(windowClass,nullptr); if(existing) { ShowWindow(existing,SW_RESTORE); SetForegroundWindow(existing); }
        else MessageBoxW(nullptr,L"另一个控制实例正在运行，或无法取得设备控制权。",L"FanController",MB_OK|MB_ICONINFORMATION);
        if(own) CloseHandle(own); GdiplusShutdown(token); if(SUCCEEDED(com)) CoUninitialize(); return 0;
    }
    HANDLE reference=nullptr;
    if(!mock) {
        reference=CreateMutexW(nullptr,FALSE,L"LenovoFanControlMutex");
        if(!reference || GetLastError()==ERROR_ALREADY_EXISTS) {
            MessageBoxW(nullptr,L"参考风扇控制软件正在运行。请先通过它的退出菜单结束，再打开本软件。",L"控制冲突",MB_OK|MB_ICONWARNING);
            if(reference) CloseHandle(reference); CloseHandle(own); GdiplusShutdown(token); if(SUCCEEDED(com)) CoUninitialize(); return 4;
        }
    }
    int result=0;
    {
        Typography fonts; typography=&fonts;
        App app(mock,background,own,reference);
        app.snapshotPath=snapshotPath; app.stressPath=stressPath;
        WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=windowProc; cls.hInstance=instance; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101)); cls.hIconSm=cls.hIcon; cls.lpszClassName=windowClass;
        RegisterClassExW(&cls);
        const UINT dpi=GetDpiForSystem(); RECT r{0,0,MulDiv(960,dpi,96),MulDiv(650,dpi,96)};
        AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,0,dpi);
        RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
        const int width=std::min(r.right-r.left,area.right-area.left-32),height=std::min(r.bottom-r.top,area.bottom-area.top-32);
        HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,windowClass,L"FanController",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,nullptr,nullptr,instance,&app);
        if(window) {
            if(!background) ShowWindow(window,SW_SHOW);
            MSG message{};
            int received{};
            while((received=GetMessageW(&message,nullptr,0,0))>0) { if(!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); } }
            result=received<0?7:static_cast<int>(message.wParam);
            if(app.controller) { app.controller->stop(); app.controller->join(); }
        } else result=5;
    }
    typography=nullptr;
    GdiplusShutdown(token); if(SUCCEEDED(com)) CoUninitialize(); return result;
}
