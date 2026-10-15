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
#include "monitor/sensors.h"
#include "app/dashboard.h"
#include "platform/memory_optimizer.h"
using namespace Gdiplus;
using namespace fan::ui;
namespace {
constexpr UINT Changed=WM_APP+1, Tray=WM_APP+2, MetricsChanged=WM_APP+3;
constexpr wchar_t ClassName[]=L"FanController.Main.81BR";
constexpr wchar_t DemoClassName[]=L"FanController.Demo.81BR";
struct App : Dashboard {
    std::unique_ptr<fan::Controller> controller;
    std::unique_ptr<fan::monitor::Monitor> monitor;
    fan::Handle mutex,reference;
    HICON icon{};
    bool background{},trayAdded{},testFailed{},monitorLogged{},livePreview{},previewMenu{},previewEmpty{};
    UINT taskbarCreated{};
    std::uint64_t loggedRevision{};
    std::wstring snapshotPath,stressPath;
    std::filesystem::path historyPath;
    ULONGLONG lastSave{};
    bool memoryBusy{};
    std::unique_ptr<fan::memory::Task> memoryTask=std::make_unique<fan::memory::Task>();
    explicit App(bool demo,bool hidden,HANDLE own,HANDLE legacy):mutex(own),reference(legacy),background(hidden) { mock=demo; }
};
void startMemory(App& app) {
    if(app.memoryBusy || app.mock) { if(app.mock) app.transient=L"演示模式不会执行内存整理"; return; }
    if(!app.memoryTask->start()) return;
    app.memoryBusy=true; EnableWindow(app.buttons[2],FALSE); app.transient=L"正在优化内存，可能短暂卡顿…"; SetTimer(app.window,3,100,nullptr); InvalidateRect(app.window,nullptr,FALSE);
}
bool savePreview(App& app, const std::wstring& path) {
    RECT rect{}; GetClientRect(app.window,&rect);
    HDC screen=GetDC(app.window), memory=CreateCompatibleDC(screen);
    HBITMAP buffer=CreateCompatibleBitmap(screen,rect.right,rect.bottom);
    HGDIOBJ old=SelectObject(memory,buffer);
    paint(app,memory,rect);
    for (HWND button : app.buttons) {
        if(!(GetWindowLongPtrW(button,GWL_STYLE)&WS_VISIBLE)) continue;
        RECT position{}; GetWindowRect(button,&position); MapWindowPoints(nullptr,app.window,reinterpret_cast<POINT*>(&position),2);
        const int saved=SaveDC(memory); SetViewportOrgEx(memory,position.left,position.top,nullptr);
        IntersectClipRect(memory,0,0,position.right-position.left,position.bottom-position.top);
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
    for(int i=0;i<200;++i) cycle();
    const auto middle=usage();
    for(int i=0;i<200;++i) cycle();
    const auto after=usage();
    std::ofstream output(std::filesystem::path(app.stressPath),std::ios::binary);
    const auto emit=[&](const Usage& u) { output<<"{\"handles\":"<<u.handles<<",\"gdi\":"<<u.gdi<<",\"user\":"<<u.user<<",\"privateBytes\":"<<u.bytes<<",\"liveHeapBytes\":"<<u.liveHeap<<",\"liveAllocations\":"<<u.allocations<<"}"; };
    output<<"{\"cycles\":400,\"before\":"; emit(before); output<<",\"after200\":"; emit(middle); output<<",\"after400\":"; emit(after); output<<"}\n";
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
    app.closing=true; if(app.memoryTask) app.memoryTask->cancel(); app.controller->stop();
    if(app.monitor) app.monitor->stop();
    if(!app.mock&&!app.historyPath.empty()) app.history->save(app.historyPath);
    for(auto button:app.buttons) EnableWindow(button,FALSE);
    SetTimer(app.window,1,100,nullptr); InvalidateRect(app.window,nullptr,FALSE);
}
void sync(App& app) {
    app.state=app.controller->snapshot();
    SetWindowTextW(app.buttons[0],app.state.autoConfirmed?L"原厂自动：已选择":L"原厂自动");
    SetWindowTextW(app.buttons[1],app.state.mode==fan::Mode::Boost && app.state.phase==fan::Phase::Ready?L"强冷高速：已选择":L"强冷高速");
    SetWindowTextW(app.buttons[11],app.state.mode==fan::Mode::Quiet && app.state.phase==fan::Phase::Ready?L"安静：已选择":L"安静");
    EnableWindow(app.buttons[0],!app.closing && app.state.phase!=fan::Phase::Starting);
    EnableWindow(app.buttons[1],!app.closing && app.state.phase==fan::Phase::Ready);
    EnableWindow(app.buttons[11],!app.closing && app.controller->quietAvailable());
    if(GetFocus()==app.window && IsWindowEnabled(app.buttons[0])) SetFocus(app.buttons[0]);
    if(app.state.revision!=app.loggedRevision) {
        app.loggedRevision=app.state.revision;
        if(!app.mock) fan::log(app.state.detail+L"; error="+std::to_wstring(app.state.error)+L"; raw="+std::to_wstring(app.state.raw));
    }
    const auto title=std::wstring(L"FanController")+(app.mock?L" · Preview":L"");
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
        constexpr std::array<const wchar_t*,12> titles{L"自动",L"强冷",L"内存优化",L"首选项",L"最近1小时",L"最近6小时",L"最近24小时",L"登录自启动",L"关闭至托盘",L"诊断日志",L"退出",L"安静"};
        for(size_t i=0;i<buttonIds.size();++i) app->buttons[i]=CreateWindowExW(0,L"BUTTON",titles[i],WS_CHILD|(i<7||i==11?WS_VISIBLE:0)|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(buttonIds[i])),GetModuleHandleW(nullptr),nullptr);
        EnableWindow(app->buttons[0],FALSE); EnableWindow(app->buttons[1],FALSE); EnableWindow(app->buttons[2],!app->mock);
        EnableWindow(app->buttons[11],FALSE);
        app->toTray=fan::closeToTray();
        if(app->mock) EnableWindow(app->buttons[7],FALSE);
        else { const auto r=fan::startupStatus(app->startup); if(!r.ok) app->transient=r.detail; }
        SetWindowTextW(app->buttons[7],app->startup?L"登录自启动：已勾选":L"登录自启动：未勾选");
        SetWindowTextW(app->buttons[8],app->toTray?L"关闭至托盘：已勾选":L"关闭至托盘：未勾选");
        std::unique_ptr<fan::Backend> backend;
        if(app->mock) backend=std::make_unique<fan::MockBackend>(); else backend=std::make_unique<fan::LenovoBackend>(true);
        app->controller=std::make_unique<fan::Controller>(std::move(backend),[window]{PostMessageW(window,Changed,0,0);});
        trayIcon(*app,true); app->controller->start();
        if(app->mock&&!app->livePreview) { if(!app->previewEmpty) seedPreview(*app); }
        else {
            if(!app->mock) {
                app->historyPath=std::filesystem::path(fan::dataDirectory())/L"history-v2.bin";
                app->history->load(app->historyPath,fan::monitor::nowSeconds());
            }
            app->lastSave=GetTickCount64();
            app->monitor=std::make_unique<fan::monitor::Monitor>([window]{PostMessageW(window,MetricsChanged,0,0);});
            app->monitor->start();
        }
        BOOL dark=TRUE; DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
        if(!app->snapshotPath.empty() || !app->stressPath.empty()) SetTimer(window,2,300,nullptr);
        layout(*app); return 0;
    }
    case Changed: sync(*app); return 0;
    case MetricsChanged:
        if(app->monitor&&!app->closing) {
            app->metrics=app->monitor->latest(); app->identity=app->monitor->identity();
            app->controller->updateTemperature(app->metrics.cpuTemp);
            EnableWindow(app->buttons[11],app->controller->quietAvailable());
            app->history->append(app->metrics);
            if(!app->monitorLogged) { fan::log(app->identity.diagnostics); app->monitorLogged=true; }
            if(!app->mock&&GetTickCount64()-app->lastSave>=300000) { app->history->save(app->historyPath); app->lastSave=GetTickCount64(); }
            if(IsWindowVisible(window)&&!IsIconic(window)) InvalidateRect(window,nullptr,FALSE);
        } return 0;
    case WM_SETFOCUS: if(app->buttons[0] && IsWindowEnabled(app->buttons[0])) SetFocus(app->buttons[0]); return 0;
    case WM_SIZE: if(wp!=SIZE_MINIMIZED) layout(*app); return 0;
    case WM_DPICHANGED: { const RECT* r=reinterpret_cast<const RECT*>(lp); SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); return 0; }
    case WM_GETMINMAXINFO: { auto info=reinterpret_cast<MINMAXINFO*>(lp); const UINT dpi=GetDpiForWindow(window); info->ptMinTrackSize={MulDiv(840,dpi,96),MulDiv(590,dpi,96)}; return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc=BeginPaint(window,&ps); RECT r{}; GetClientRect(window,&r); paint(*app,dc,r); EndPaint(window,&ps); return 0; }
    case WM_PRINTCLIENT: { RECT r{}; GetClientRect(window,&r); paint(*app,reinterpret_cast<HDC>(wp),r); return 0; }
    case WM_DRAWITEM: drawButton(*app,*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
    case WM_LBUTTONDOWN: if(app->preferences) preferences(*app,false); return 0;
    case WM_KEYDOWN: if(wp==VK_ESCAPE&&app->preferences) { preferences(*app,false); return 0; } break;
    case WM_COMMAND: {
        const int id=LOWORD(wp);
        if(id==Show) { show(*app); return 0; }
        if(app->closing) return 0;
        app->transient.clear();
        if(id==IDCANCEL) { if(app->preferences) preferences(*app,false); return 0; }
        if(id==Settings) { preferences(*app,!app->preferences); return 0; }
        if(id==Optimize) { startMemory(*app); return 0; }
        if(id==Hour1||id==Hour6||id==Hour24) app->hours=id==Hour1?1:(id==Hour6?6:24);
        if(id==Auto || id==Boost || id==Quiet) app->controller->request(id==Auto?fan::Mode::Auto:(id==Quiet?fan::Mode::Quiet:fan::Mode::Boost));
        if(id==Exit) beginExit(*app);
        if(id==Startup && !app->mock) {
            const auto result=fan::setStartup(!app->startup);
            const auto status=fan::startupStatus(app->startup);
            app->transient=!result.ok?result.detail:(!status.ok?status.detail:L"");
            SetWindowTextW(app->buttons[7],app->startup?L"登录自启动：已勾选":L"登录自启动：未勾选");
            fan::log(result.ok?L"自启动偏好已更新":result.detail);
        }
        if(id==CloseTray) { app->toTray=!app->toTray; if(!app->mock) fan::setCloseToTray(app->toTray); SetWindowTextW(app->buttons[8],app->toTray?L"关闭至托盘：已勾选":L"关闭至托盘：未勾选"); }
        if(id==Diagnostics) { ShellExecuteW(window,L"open",fan::dataDirectory().c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
        InvalidateRect(window,nullptr,FALSE); for(auto b:app->buttons) InvalidateRect(b,nullptr,FALSE); return 0;
    }
    case Tray:
        if(lp==WM_LBUTTONUP || lp==WM_LBUTTONDBLCLK) show(*app);
        else if(lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) {
            HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,Show,L"打开控制台"); AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,Auto,L"原厂自动");
            AppendMenuW(menu,MF_STRING | (app->controller->quietAvailable()?0:MF_GRAYED),Quiet,L"安静");
            AppendMenuW(menu,MF_STRING | (app->state.phase==fan::Phase::Ready?0:MF_GRAYED),Boost,L"强冷 / 高速");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING | (app->memoryBusy||app->closing||app->mock?MF_GRAYED:0),Optimize,app->memoryBusy?L"内存优化（进行中…）":L"内存优化");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,Exit,L"恢复自动并退出");
            POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
            const int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr); DestroyMenu(menu);
            if(choice) PostMessageW(window,WM_COMMAND,choice,0); PostMessageW(window,WM_NULL,0,0);
        } return 0;
    case WM_CLOSE:
        if(app->toTray && !app->closing && app->trayAdded) {
            ShowWindow(window,SW_HIDE);
        } else beginExit(*app); return 0;
    case WM_TIMER:
        if(wp==3) {
            if(auto result=app->memoryTask->take()) { KillTimer(window,3); app->memoryBusy=false; EnableWindow(app->buttons[2],!app->closing&&!app->mock); app->transient=fan::memory::describe(*result); fan::log(fan::memory::diagnostics(*result)); if(app->monitor) { app->metrics=app->monitor->latest(); app->history->append(app->metrics); } InvalidateRect(window,nullptr,FALSE); }
            return 0;
        }
        if(wp==2 && app->state.phase==fan::Phase::Ready && (!app->livePreview||app->history->size()>=3)) {
            KillTimer(window,2);
            if(app->previewMenu) preferences(*app,true);
            if(!app->snapshotPath.empty() && !savePreview(*app,app->snapshotPath)) app->testFailed=true;
            if(!app->stressPath.empty()) stress(*app);
            beginExit(*app);
        }
        if(wp==1 && app->controller->snapshot().phase==fan::Phase::Stopped && (!app->monitor||app->monitor->finished())) {
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
    bool mock=false,background=false,livePreview=false,previewMenu=false,previewEmpty=false;
    int previewHours=1,previewWidth=1120;
    std::wstring snapshotPath,stressPath;
    int argc{}; LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==4 && wcscmp(args[1],L"--memory-optimize-worker")==0) { const int result=fan::memory::workerMain(args[2],args[3]); LocalFree(args); return result; }
    if(argc>1 && wcscmp(args[1],L"--guardian")==0) { const int result=fan::recoveryMain(argc,args); LocalFree(args); return result; }
    if(argc==2&&wcscmp(args[1],L"--exit")==0) { HWND existing=FindWindowW(ClassName,nullptr); if(existing) PostMessageW(existing,WM_COMMAND,Exit,0); LocalFree(args); return 0; }
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
        else if(wcscmp(args[i],L"--live-snapshot")==0 && i+1<argc) { snapshotPath=args[++i]; mock=true; background=true; livePreview=true; }
        else if(wcscmp(args[i],L"--preview-menu")==0) previewMenu=true;
        else if(wcscmp(args[i],L"--preview-empty")==0) previewEmpty=true;
        else if(wcscmp(args[i],L"--preview-24h")==0) previewHours=24;
        else if(wcscmp(args[i],L"--preview-compact")==0) previewWidth=840;
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
        App app(mock,background,own,reference);
        app.snapshotPath=snapshotPath; app.stressPath=stressPath;
        app.livePreview=livePreview; app.previewMenu=previewMenu; app.previewEmpty=previewEmpty; app.hours=previewHours;
        WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=windowProc; cls.hInstance=instance; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101)); cls.hIconSm=cls.hIcon; cls.lpszClassName=windowClass;
        RegisterClassExW(&cls);
        const UINT dpi=GetDpiForSystem(); RECT r{0,0,MulDiv(previewWidth,dpi,96),MulDiv(previewWidth*740/1120,dpi,96)};
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
    GdiplusShutdown(token); if(SUCCEEDED(com)) CoUninitialize(); return result;
}
