#include "app/dashboard.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>
using namespace Gdiplus;
namespace fan::ui {
namespace {
const Color Background(255,8,14,23),Panel(255,15,24,37),Border(255,31,45,64);
const Color Foreground(255,235,243,255),Muted(255,125,146,173),Blue(255,92,179,255),Orange(255,242,176,112),Memory(255,116,149,255);
void text(Dashboard& a,Graphics& g,const std::wstring& value,float x,float y,float w,float h,float size,Color color,int style=FontStyleRegular,StringAlignment align=StringAlignmentNear) {
    SolidBrush brush(color); StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisCharacter); format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetAlignment(align);
    g.DrawString(value.c_str(),-1,a.typography.get(size,style),RectF(x,y,w,h),&format,&brush);
}
void round(Graphics& g,float x,float y,float w,float h,float r,Color fill,Color stroke=Border) {
    GraphicsPath path; const float d=r*2;
    path.AddArc(x,y,d,d,180,90); path.AddArc(x+w-d,y,d,d,270,90);
    path.AddArc(x+w-d,y+h-d,d,d,0,90); path.AddArc(x,y+h-d,d,d,90,90); path.CloseFigure();
    SolidBrush brush(fill); Pen pen(stroke); g.FillPath(&brush,&path); g.DrawPath(&pen,&path);
}
std::wstring number(float value,int decimals=0) {
    if(!monitor::valid(value)) return L"—";
    wchar_t buffer[64]{}; swprintf_s(buffer,L"%.*f",decimals,static_cast<double>(value)); return buffer;
}
void line(Graphics& g,float x,float y,float x2,float y2,Color color=Border) { Pen p(color); g.DrawLine(&p,x,y,x2,y2); }
std::wstring mode(const Dashboard& a) {
    if(a.closing) return L"正在退出";
    if(a.state.phase==Phase::Fault) return L"连接异常";
    if(a.state.phase==Phase::Starting) return L"正在连接";
    if(a.state.phase==Phase::Applying) return L"正在切换";
    return a.state.mode==Mode::Boost?L"强冷":(a.state.mode==Mode::Quiet?L"安静":L"自动");
}
void gear(Graphics& g,float cx,float cy,Color color) {
    Pen p(color,1.7f); g.DrawEllipse(&p,cx-6,cy-6,12.0f,12.0f); g.DrawEllipse(&p,cx-2,cy-2,4.0f,4.0f);
    for(int i=0;i<8;++i) { const float t=i*3.14159265f/4; g.DrawLine(&p,cx+6*std::cos(t),cy+6*std::sin(t),cx+9*std::cos(t),cy+9*std::sin(t)); }
}
void chip(Graphics& g,float cx,float cy,Color color) {
    Pen p(color,1.6f); g.DrawRectangle(&p,cx-6,cy-6,12.0f,12.0f); g.DrawRectangle(&p,cx-2.5f,cy-2.5f,5.0f,5.0f);
    for(int i=-1;i<=1;++i) { const float v=static_cast<float>(i*4); g.DrawLine(&p,cx+v,cy-9,cx+v,cy-6); g.DrawLine(&p,cx+v,cy+6,cx+v,cy+9); g.DrawLine(&p,cx-9,cy+v,cx-6,cy+v); g.DrawLine(&p,cx+6,cy+v,cx+9,cy+v); }
}
using Field=float monitor::Sample::*;
void trace(Dashboard& a,Graphics& g,RectF r,Field field,float maximum,Color color,std::int64_t end) {
    const auto start=end-a.hours*3600;
    std::vector<PointF> points; points.reserve(800);
    const auto flush=[&] {
        if(points.size()>1) {
            GraphicsPath path; path.AddLines(points.data(),static_cast<INT>(points.size()));
            path.AddLine(points.back(),PointF(points.back().X,r.GetBottom())); path.AddLine(PointF(points.back().X,r.GetBottom()),PointF(points.front().X,r.GetBottom())); path.CloseFigure();
            LinearGradientBrush fill(PointF(r.X,r.Y),PointF(r.X,r.GetBottom()),Color(35,color.GetR(),color.GetG(),color.GetB()),Color(0,color.GetR(),color.GetG(),color.GetB()));
            g.FillPath(&fill,&path); Pen pen(color,1.55f); g.DrawLines(&pen,points.data(),static_cast<INT>(points.size()));
        } else if(points.size()==1) { SolidBrush b(color); g.FillEllipse(&b,points[0].X-2,points[0].Y-2,4.0f,4.0f); }
        points.clear();
    };
    // Bucket at most a pixel of samples, retaining min/max extrema and real gaps.
    int bucket=-1; float lo{},hi{},last{}; std::int64_t previous=0;
    const auto emitBucket=[&] { if(bucket>=0) { const float x=r.X+static_cast<float>(bucket); if(lo!=hi) { points.emplace_back(x,lo); points.emplace_back(x,hi); } points.emplace_back(x,last); } };
    const auto state=g.Save(); g.SetClip(r);
    for(std::size_t i=0;i<a.history->size();++i) {
        const auto& s=a.history->at(i); if(s.time<start||s.time>end) continue;
        const float v=s.*field;
        if(!monitor::valid(v)||(previous&&s.time-previous>25)) { emitBucket(); bucket=-1; flush(); }
        previous=s.time;
        if(!monitor::valid(v)) continue;
        const int pixel=std::clamp(static_cast<int>(static_cast<double>(s.time-start)*r.Width/(a.hours*3600)),0,static_cast<int>(r.Width));
        const float y=r.GetBottom()-std::clamp(v/maximum,0.0f,1.0f)*r.Height;
        if(pixel!=bucket) { emitBucket(); bucket=pixel; lo=hi=last=y; }
        else { lo=std::min(lo,y); hi=std::max(hi,y); last=y; }
    }
    emitBucket(); flush(); g.Restore(state);
}
void chart(Dashboard& a,Graphics& g,const wchar_t* title,float y,Field load,Field temp,Color color,std::int64_t end) {
    text(a,g,title,326,y,100,25,13,Foreground,FontStyleBold);
    const float value=a.metrics.*load;
    text(a,g,number(value)+L" %",860,y-3,210,30,21,color,FontStyleBold,StringAlignmentFar);
    if(temp) text(a,g,number(a.metrics.*temp)+L" °C",691,y,135,24,15,Orange,FontStyleRegular,StringAlignmentFar);
    RectF r(327,y+31,710,62);
    for(int i=0;i<=2;++i) line(g,r.X,r.Y+i*r.Height/2,r.GetRight(),r.Y+i*r.Height/2,Color(255,24,37,54));
    for(int i=0;i<=6;++i) line(g,r.X+i*r.Width/6,r.Y,r.X+i*r.Width/6,r.GetBottom(),Color(255,23,35,51));
    text(a,g,L"100",1043,y+29,36,18,9,Muted); text(a,g,L"0",1043,y+78,30,18,9,Muted);
    trace(a,g,r,load,100,color,end);
    if(temp) trace(a,g,r,temp,100,Orange,end);
}
}
Font* Typography::get(float size,int style) {
    const auto n=static_cast<std::size_t>(std::clamp(size,1.0f,56.0f));
    auto& f=fonts[n][style==FontStyleBold?1:0];
    if(!f) f=std::make_unique<Font>(L"Microsoft YaHei UI",size,style,UnitPixel);
    return f.get();
}
void layout(Dashboard& a) {
    RECT r{}; GetClientRect(a.window,&r); a.scale=std::min(r.right/1120.0f,r.bottom/740.0f);
    constexpr std::array<RECT,12> rects{{{48,131,239,163},{48,211,239,243},{1006,24,1042,60},{1058,24,1094,60},
        {890,302,946,328},{952,302,1008,328},{1014,302,1070,328},
        {838,88,1076,122},{838,128,1076,162},{838,178,1076,212},{838,218,1076,252},{48,171,239,203}}};
    for(std::size_t i=0;i<a.buttons.size();++i) { const auto& p=rects[i]; MoveWindow(a.buttons[i],static_cast<int>(p.left*a.scale),static_cast<int>(p.top*a.scale),static_cast<int>((p.right-p.left)*a.scale),static_cast<int>((p.bottom-p.top)*a.scale),TRUE); }
    InvalidateRect(a.window,nullptr,FALSE);
}
void preferences(Dashboard& a,bool visible) {
    a.preferences=visible;
    for(std::size_t i=7;i<11;++i) ShowWindow(a.buttons[i],visible?SW_SHOW:SW_HIDE);
    if(visible) SetFocus(a.buttons[7]); else SetFocus(a.buttons[3]);
    InvalidateRect(a.window,nullptr,FALSE);
}
void paint(Dashboard& a,HDC target,const RECT& rect) {
    if(rect.right<=0||rect.bottom<=0) return;
    HDC memory=CreateCompatibleDC(target); HBITMAP bitmap=CreateCompatibleBitmap(target,rect.right,rect.bottom);
    if(!memory||!bitmap) { if(memory) DeleteDC(memory); if(bitmap) DeleteObject(bitmap); return; }
    HGDIOBJ old=SelectObject(memory,bitmap);
    {
        Graphics g(memory); g.Clear(Background); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit); g.ScaleTransform(a.scale,a.scale);
        text(a,g,L"FanController",25,17,430,42,27,Foreground,FontStyleBold);
        text(a,g,a.mock?L"PREVIEW":L"IDEAPAD 720S  /  RYZEN 7",27,56,590,22,10,Muted);
        round(g,24,88,1072,178,18,Panel);
        text(a,g,L"风扇控制",48,107,192,25,13,Muted);
        line(g,265,113,265,239);
        const Color accent=a.state.mode==Mode::Boost?Orange:Blue;
        Pen ring(Color(255,32,54,78),6); g.DrawEllipse(&ring,311.0f,111.0f,129.0f,129.0f);
        Pen arc(accent,3); g.DrawArc(&arc,311.0f,111.0f,129.0f,129.0f,210.0f,120.0f);
        SolidBrush blade(Color(135,accent.GetR(),accent.GetG(),accent.GetB()));
        for(int i=0;i<5;++i) { const auto saved=g.Save(); g.TranslateTransform(375.5f,175.5f); g.RotateTransform(i*72.0f); g.FillEllipse(&blade,-9.0f,-43.0f,21.0f,34.0f); g.Restore(saved); }
        SolidBrush hub(accent); g.FillEllipse(&hub,367.5f,167.5f,16.0f,16.0f);
        text(a,g,L"系统风扇",470,133,230,24,13,Muted);
        text(a,g,mode(a),468,173,243,49,32,accent,FontStyleBold);
        line(g,708,113,708,239);
        text(a,g,L"Temperature",746,124,312,23,12,Muted,FontStyleRegular,StringAlignmentCenter);
        text(a,g,number(a.metrics.cpuTemp),760,161,280,57,42,Foreground,FontStyleBold,StringAlignmentCenter);
        text(a,g,L"°C",957,184,43,27,14,Muted);
        round(g,24,286,258,418,18,Panel);
        text(a,g,L"实时频率",48,307,200,24,13,Muted);
        text(a,g,L"CPU",48,349,80,22,12,Blue,FontStyleBold);
        text(a,g,number(a.metrics.cpuMHz),46,374,189,56,37,Foreground); text(a,g,L"MHz",199,405,60,22,11,Muted);
        line(g,48,449,258,449);
        text(a,g,L"GPU",48,466,80,22,12,Blue,FontStyleBold);
        text(a,g,number(a.metrics.gpuMHz),46,491,189,56,37,Foreground); text(a,g,L"MHz",199,522,60,22,11,Muted);
        line(g,48,566,258,566);
        text(a,g,L"内存",48,586,150,23,12,Muted);
        text(a,g,number(a.metrics.memoryUsedGiB,1),46,617,123,52,35,Foreground);
        text(a,g,L"/ "+number(a.metrics.memoryTotalGiB,1)+L" GiB",148,637,114,25,14,Muted);
        round(g,302,286,794,418,18,Panel);
        text(a,g,L"监控",326,305,100,25,14,Foreground,FontStyleBold);
        SolidBrush loadDot(Blue),tempDot(Orange); g.FillEllipse(&loadDot,417.0f,313.0f,5.0f,5.0f); g.FillEllipse(&tempDot,485.0f,313.0f,5.0f,5.0f);
        text(a,g,L"负载",429,305,50,24,11,Muted); text(a,g,L"温度",497,305,50,24,11,Muted);
        const auto end=a.metrics.time?a.metrics.time:monitor::nowSeconds();
        chart(a,g,L"CPU",348,&monitor::Sample::cpuLoad,&monitor::Sample::cpuTemp,Blue,end);
        chart(a,g,L"GPU",459,&monitor::Sample::gpuLoad,&monitor::Sample::gpuTemp,Blue,end);
        chart(a,g,L"内存",570,&monitor::Sample::memoryLoad,nullptr,Memory,end);
        for(int i=0;i<3;++i) {
            const std::time_t stamp=static_cast<std::time_t>(end-a.hours*3600+i*a.hours*1800);
            std::tm local{}; localtime_s(&local,&stamp); wchar_t clock[16]{}; wcsftime(clock,16,L"%H:%M",&local);
            text(a,g,clock,327+i*323.0f,677,64,17,9,Muted,i==2?FontStyleBold:FontStyleRegular);
        }
        std::wstring footer;
        if(!a.transient.empty()) footer=a.transient;
        else if(a.state.phase==Phase::Fault) footer=L"控制器错误 · "+std::to_wstring(a.state.error);
        text(a,g,footer,28,717,780,19,11,Orange);
        std::time_t stamp=static_cast<std::time_t>(a.metrics.time); std::tm local{}; localtime_s(&local,&stamp); wchar_t clock[16]{}; wcsftime(clock,16,L"%H:%M:%S",&local);
        text(a,g,L"10 s  ·  "+std::wstring(a.metrics.time?clock:L"—"),836,717,255,19,10,Muted,FontStyleRegular,StringAlignmentFar);
        if(a.preferences) { round(g,824,76,266,190,14,Color(255,21,34,52),Color(255,51,74,104)); line(g,839,170,1075,170); }
    }
    BitBlt(target,0,0,rect.right,rect.bottom,memory,0,0,SRCCOPY); SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory);
}
void drawButton(Dashboard& a,const DRAWITEMSTRUCT& item) {
    Graphics g(item.hDC); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit); g.ScaleTransform(a.scale,a.scale);
    const float w=item.rcItem.right/a.scale,h=item.rcItem.bottom/a.scale;
    const int id=static_cast<int>(item.CtlID);
    const bool disabled=(item.itemState&ODS_DISABLED)!=0,pressed=(item.itemState&ODS_SELECTED)!=0;
    const bool active=(id==Auto&&a.state.autoConfirmed)||(id==Boost&&a.state.mode==Mode::Boost&&a.state.phase==Phase::Ready)
        ||(id==Quiet&&a.state.mode==Mode::Quiet&&a.state.phase==Phase::Ready)
        ||(id==Hour1&&a.hours==1)||(id==Hour6&&a.hours==6)||(id==Hour24&&a.hours==24)||(id==Settings&&a.preferences);
    const Color fill=active?Color(255,30,64,100):(pressed?Color(255,27,46,69):Panel);
    const Color ink=disabled?Color(255,66,83,106):(active?Blue:Foreground);
    g.Clear((id==Settings||id==Optimize)?Background:Panel);
    round(g,1,1,w-2,h-2,8,fill,active?Color(255,63,115,170):Border);
    if(id==Settings) gear(g,w/2,h/2,ink);
    else if(id==Optimize) chip(g,w/2,h/2,ink);
    else if(id==Auto||id==Boost||id==Quiet) text(a,g,id==Auto?L"自动":(id==Quiet?L"安静":L"强冷"),15,6,w-30,h-6,13,ink,FontStyleBold);
    else if(id==Hour1||id==Hour6||id==Hour24) text(a,g,id==Hour1?L"1 h":(id==Hour6?L"6 h":L"24 h"),0,4,w,h,10,ink,FontStyleRegular,StringAlignmentCenter);
    else {
        if(id==Startup||id==CloseTray) {
            const bool check=id==Startup?a.startup:a.toTray;
            round(g,10,10,13,13,3,check?Blue:Panel,check?Blue:Muted);
            if(check) { Pen p(Panel,1.7f); g.DrawLine(&p,13.0f,16.0f,16.0f,19.0f); g.DrawLine(&p,16.0f,19.0f,21.0f,13.0f); }
            text(a,g,id==Startup?L"登录自启动":L"关闭至托盘",34,8,w-44,h-8,12,ink);
        } else text(a,g,id==Diagnostics?L"诊断日志":L"退出",13,8,w-26,h-8,12,ink);
    }
    if(item.itemState&ODS_FOCUS) { Pen p(Blue); p.SetDashStyle(DashStyleDot); g.DrawRectangle(&p,4.0f,4.0f,w-9,h-9); }
}
void seedPreview(Dashboard& a) {
    // Used only by the explicit --demo/--snapshot diagnostic modes.
    const auto end=monitor::nowSeconds();
    for(int i=0;i<static_cast<int>(monitor::History::capacity);++i) {
        monitor::Sample s; s.time=end-monitor::horizon+i*monitor::interval;
        const float t=static_cast<float>(i);
        s.cpuLoad=29+10*std::sin(t*.053f)+5*std::sin(t*.31f);
        s.gpuLoad=12+7*std::sin(t*.033f)+3*std::sin(t*.19f);
        s.cpuMHz=2600+300*std::sin(t*.11f); s.gpuMHz=650+100*std::sin(t*.073f);
        s.cpuTemp=56+5*std::sin(t*.021f); s.gpuTemp=49+4*std::sin(t*.017f);
        s.memoryLoad=62+4*std::sin(t*.004f); s.memoryTotalGiB=6.8f; s.memoryUsedGiB=s.memoryTotalGiB*s.memoryLoad/100;
        s.fanRPM=2800; a.history->append(s); a.metrics=s;
    }
}
}
