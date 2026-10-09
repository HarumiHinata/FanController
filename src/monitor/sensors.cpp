#include "monitor/sensors.h"
#include <windows.h>
#include <winternl.h>
#include <dxgi1_2.h>
#include <d3dkmthk.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <intrin.h>
#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <sstream>
#include "../../third_party/adl/adl_sdk.h"

namespace fan::monitor {
namespace {
template<class T> T function(HMODULE dll,const char* name) { return reinterpret_cast<T>(GetProcAddress(dll,name)); }
void* __stdcall allocate(int bytes) { return std::malloc(bytes); }
std::uint64_t ticks(FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
std::wstring widen(const char* text) { const int n=MultiByteToWideChar(CP_ACP,0,text,-1,nullptr,0); if(n<=1) return {}; std::wstring s(static_cast<size_t>(n),0); MultiByteToWideChar(CP_ACP,0,text,-1,s.data(),n); s.pop_back(); return s; }
float counter(PDH_HCOUNTER c) {
    PDH_FMT_COUNTERVALUE v{};
    if(!c || PdhGetFormattedCounterValue(c,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,nullptr,&v)!=ERROR_SUCCESS
        ||(v.CStatus!=PDH_CSTATUS_VALID_DATA&&v.CStatus!=PDH_CSTATUS_NEW_DATA)) return missing;
    return static_cast<float>(v.doubleValue);
}
}
struct Reader::Impl {
    Identity identity;
    PDH_HQUERY query{};
    PDH_HCOUNTER frequency{},engines{};
    std::vector<unsigned char> pdhBuffer;
    std::vector<Engine> engineValues;
    D3DKMT_HANDLE adapter{};
    std::wstring luid;
    HMODULE adl{};
    void* context{};
    int adlIndex{-1};
    int (*destroy)(void*){};
    int (*pmLog)(void*,int,ADLPMLogDataOutput*){};
    int (*activity)(void*,int,ADLPMActivity*){};
    int (*temperature)(void*,int,int,ADLTemperature*){};
    int (*fanSpeed)(void*,int,int,ADLFanSpeedValue*){};
    bool fanSupported{};
    std::uint64_t oldIdle{},oldKernel{},oldUser{};
    ULONGLONG lastSample{};
    bool haveCpu{},collected{};
    Impl() {
        int cpuid[4]{}; __cpuid(cpuid,static_cast<int>(0x80000000));
        if(static_cast<unsigned int>(cpuid[0])>=0x80000004) {
            char name[49]{}; for(unsigned int i=0;i<3;++i) { __cpuid(cpuid,static_cast<int>(0x80000002+i)); memcpy(name+i*16,cpuid,16); }
            identity.cpu=widen(name);
        }
        std::wostringstream log;
        if(PdhOpenQueryW(nullptr,0,&query)==ERROR_SUCCESS) {
            const auto fs=PdhAddEnglishCounterW(query,L"\\Processor Information(_Total)\\Processor Frequency",0,&frequency);
            const auto gs=PdhAddEnglishCounterW(query,L"\\GPU Engine(*)\\Utilization Percentage",0,&engines);
            log<<L"PDH frequency="<<fs<<L" GPU="<<gs<<L"; ";
        }
        IDXGIFactory1* factory{};
        if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            for(UINT i=0;;++i) {
                IDXGIAdapter1* candidate{}; const auto result=factory->EnumAdapters1(i,&candidate);
                if(FAILED(result)) break;
                DXGI_ADAPTER_DESC1 d{}; const auto status=candidate->GetDesc1(&d); candidate->Release();
                if(FAILED(status)||(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)||d.VendorId!=0x1002) continue;
                identity.gpu=d.Description;
                wchar_t id[90]{}; swprintf_s(id,L"luid_0x%08x_0x%08x_phys_0_",static_cast<unsigned int>(d.AdapterLuid.HighPart),d.AdapterLuid.LowPart); luid=id;
                D3DKMT_OPENADAPTERFROMLUID open{}; open.AdapterLuid=d.AdapterLuid;
                if(D3DKMTOpenAdapterFromLuid(&open)>=0) adapter=open.hAdapter;
                break;
            }
            factory->Release();
        }
        // Only load the installed AMD driver library; never a DLL beside the executable.
        adl=LoadLibraryExW(L"atiadlxx.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(adl) {
            const auto create=function<int(*)(ADL_MAIN_MALLOC_CALLBACK,int,void**)>(adl,"ADL2_Main_Control_Create");
            destroy=function<int(*)(void*)>(adl,"ADL2_Main_Control_Destroy");
            const auto count=function<int(*)(void*,int*)>(adl,"ADL2_Adapter_NumberOfAdapters_Get");
            const auto info=function<int(*)(void*,AdapterInfo*,int)>(adl,"ADL2_Adapter_AdapterInfo_Get");
            if(create&&destroy&&count&&info && create(allocate,1,&context)==ADL_OK) {
                int n{};
                if(count(context,&n)==ADL_OK&&n>0&&n<=64) {
                    std::vector<AdapterInfo> adapters(static_cast<size_t>(n)); for(auto& a:adapters) a.iSize=sizeof(a);
                    if(info(context,adapters.data(),static_cast<int>(adapters.size()*sizeof(AdapterInfo)))==ADL_OK)
                        for(const auto& a:adapters) if(a.iPresent&&(a.iVendorID==1002||a.iVendorID==0x1002)) { adlIndex=a.iAdapterIndex; break; }
                }
                pmLog=function<int(*)(void*,int,ADLPMLogDataOutput*)>(adl,"ADL2_New_QueryPMLogData_Get");
                activity=function<int(*)(void*,int,ADLPMActivity*)>(adl,"ADL2_Overdrive5_CurrentActivity_Get");
                temperature=function<int(*)(void*,int,int,ADLTemperature*)>(adl,"ADL2_Overdrive5_Temperature_Get");
                fanSpeed=function<int(*)(void*,int,int,ADLFanSpeedValue*)>(adl,"ADL2_Overdrive5_FanSpeed_Get");
                const auto fanInfo=function<int(*)(void*,int,int,ADLFanSpeedInfo*)>(adl,"ADL2_Overdrive5_FanSpeedInfo_Get");
                ADLFanSpeedInfo fi{}; fi.iSize=sizeof(fi);
                fanSupported=adlIndex>=0&&fanInfo&&fanInfo(context,adlIndex,0,&fi)==ADL_OK&&(fi.iFlags&ADL_DL_FANCTRL_SUPPORTS_RPM_READ);
                ADLPMLogDataOutput pm{}; pm.size=sizeof(pm);
                const int code=(adlIndex>=0&&pmLog)?pmLog(context,adlIndex,&pm):ADL_ERR_NOT_SUPPORTED;
                log<<L"ADL index="<<adlIndex<<L" PMLog="<<code<<L" CPU-temperature-supported="<<pm.sensors[ADL_PMLOG_TEMPERATURE_CPU].supported
                   <<L" fan-supported="<<pm.sensors[ADL_PMLOG_FAN_RPM].supported<<L"; ";
            }
        }
        identity.diagnostics=log.str();
    }
    ~Impl() {
        if(context&&destroy) destroy(context);
        if(adl) FreeLibrary(adl);
        if(adapter) { D3DKMT_CLOSEADAPTER close{adapter}; D3DKMTCloseAdapter(&close); }
        if(query) PdhCloseQuery(query);
    }
    Sample sample() {
        Sample s; s.time=nowSeconds();
        const ULONGLONG uptime=GetTickCount64();
        const bool continuous=lastSample!=0&&uptime-lastSample<=25000;
        FILETIME idle{},kernel{},user{};
        if(GetSystemTimes(&idle,&kernel,&user)) {
            const auto i=ticks(idle),k=ticks(kernel),u=ticks(user);
            if(haveCpu&&continuous) s.cpuLoad=cpuUsage(i,k,u,oldIdle,oldKernel,oldUser);
            oldIdle=i; oldKernel=k; oldUser=u; haveCpu=true;
        } else haveCpu=false;
        if(query && PdhCollectQueryData(query)==ERROR_SUCCESS) {
            s.cpuMHz=checked(counter(frequency),1,10000);
            if(collected&&continuous&&engines) {
                DWORD bytes=0,count=0;
                if(PdhGetFormattedCounterArrayW(engines,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&count,nullptr)==PDH_MORE_DATA && bytes<=4*1024*1024) {
                    pdhBuffer.resize(bytes);
                    auto* items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(pdhBuffer.data());
                    if(PdhGetFormattedCounterArrayW(engines,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&count,items)==ERROR_SUCCESS) {
                        engineValues.clear();
                        for(DWORD i=0;i<count;++i) if(items[i].szName && (items[i].FmtValue.CStatus==PDH_CSTATUS_VALID_DATA||items[i].FmtValue.CStatus==PDH_CSTATUS_NEW_DATA))
                            engineValues.push_back({items[i].szName,items[i].FmtValue.doubleValue});
                        s.gpuLoad=gpuUsage(engineValues,luid);
                    }
                }
            }
            collected=true;
        } else collected=false;
        MEMORYSTATUSEX m{sizeof(m)};
        if(GlobalMemoryStatusEx(&m)&&m.ullTotalPhys&&m.ullAvailPhys<=m.ullTotalPhys) {
            s.memoryTotalGiB=static_cast<float>(static_cast<double>(m.ullTotalPhys)/1073741824.0);
            s.memoryUsedGiB=static_cast<float>(static_cast<double>(m.ullTotalPhys-m.ullAvailPhys)/1073741824.0);
            s.memoryLoad=100.0f*s.memoryUsedGiB/s.memoryTotalGiB;
        }
        if(context&&adlIndex>=0) {
            ADLPMLogDataOutput data{}; data.size=sizeof(data);
            if(pmLog&&pmLog(context,adlIndex,&data)==ADL_OK) {
                const auto sensor=[&](int id,double lo,double hi) { return data.sensors[id].supported?checked(data.sensors[id].value,lo,hi):missing; };
                s.gpuMHz=sensor(ADL_PMLOG_CLK_GFXCLK,0,10000);
                s.gpuTemp=sensor(ADL_PMLOG_TEMPERATURE_EDGE,1,150);
                s.cpuTemp=sensor(ADL_PMLOG_TEMPERATURE_CPU,1,150);
                s.fanRPM=sensor(ADL_PMLOG_FAN_RPM,0,30000);
                if(!valid(s.gpuLoad)) s.gpuLoad=sensor(ADL_PMLOG_INFO_ACTIVITY_GFX,0,100);
            }
            if(!valid(s.gpuMHz)&&activity) { ADLPMActivity a{}; a.iSize=sizeof(a); if(activity(context,adlIndex,&a)==ADL_OK) s.gpuMHz=checked(a.iEngineClock/100.0,0,10000); }
            if(!valid(s.gpuTemp)&&temperature) { ADLTemperature t{sizeof(t)}; if(temperature(context,adlIndex,0,&t)==ADL_OK) s.gpuTemp=checked(t.iTemperature/1000.0,1,150); }
            if(!valid(s.fanRPM)&&fanSpeed&&fanSupported) { ADLFanSpeedValue f{sizeof(f),ADL_DL_FANCTRL_SPEED_TYPE_RPM}; if(fanSpeed(context,adlIndex,0,&f)==ADL_OK) s.fanRPM=checked(f.iFanSpeed,0,30000); }
        }
        if(adapter) {
            D3DKMT_ADAPTER_PERFDATA perf{};
            D3DKMT_QUERYADAPTERINFO q{adapter,KMTQAITYPE_ADAPTERPERFDATA,&perf,sizeof(perf)};
            if(D3DKMTQueryAdapterInfo(&q)>=0) {
                const auto t=checked(perf.Temperature/10.0,1,150); if(valid(t)) s.gpuTemp=t;
                // A zero here also means unsupported on this APU; don't claim a stopped fan.
                if(!valid(s.fanRPM)&&perf.FanRPM>0) s.fanRPM=checked(perf.FanRPM,1,30000);
            }
        }
        // On this Ryzen 2700U, the separately exposed CPU sensor is unsupported.
        // The user has explicitly chosen the APU adapter reading as a provisional CPU temperature.
        if(!valid(s.cpuTemp) && valid(s.gpuTemp)) s.cpuTemp=s.gpuTemp;
        lastSample=uptime;
        return s;
    }
};
Reader::Reader():impl_(std::make_unique<Impl>()) {}
Reader::~Reader()=default;
Sample Reader::sample() { return impl_->sample(); }
const Identity& Reader::identity() const { return impl_->identity; }
Monitor::Monitor(std::function<void()> notify):notify_(std::move(notify)) {}
Monitor::~Monitor() { stop(); join(); }
void Monitor::start() { worker_=std::thread(&Monitor::run,this); }
void Monitor::stop() { { std::lock_guard lock(mutex_); stopping_=true; } cv_.notify_all(); }
void Monitor::join() { if(worker_.joinable()) worker_.join(); }
bool Monitor::finished() const { std::lock_guard lock(mutex_); return finished_; }
Sample Monitor::latest() const { std::lock_guard lock(mutex_); return sample_; }
Identity Monitor::identity() const { std::lock_guard lock(mutex_); return identity_; }
void Monitor::run() {
    try {
        Reader reader;
        { std::lock_guard lock(mutex_); identity_=reader.identity(); }
        while(true) {
            { std::lock_guard lock(mutex_); if(stopping_) break; }
            const auto start=std::chrono::steady_clock::now();
            const auto sample=reader.sample();
            { std::lock_guard lock(mutex_); sample_=sample; }
            if(notify_) notify_();
            std::unique_lock lock(mutex_);
            if(cv_.wait_until(lock,start+std::chrono::seconds(interval),[this]{return stopping_;})) break;
        }
    } catch(...) {
        { std::lock_guard lock(mutex_); identity_.diagnostics=L"监控采集失败"; sample_=Sample{}; sample_.time=nowSeconds(); }
        if(notify_) notify_();
    }
    { std::lock_guard lock(mutex_); finished_=true; }
}
}
