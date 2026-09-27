#include <windows.h>
#include <winternl.h>
#include <dxgi1_2.h>
#include <d3dkmthk.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <thread>
#include <iostream>
#include "monitor/sensors.h"
#include "../third_party/adl/adl_sdk.h"

template<class T> T proc(HMODULE m,const char* name) { return reinterpret_cast<T>(GetProcAddress(m,name)); }
void* __stdcall allocate(int n) { return std::malloc(n); }
int main(int argc,char** argv) {
    if(argc==2&&strcmp(argv[1],"--samples")==0) {
        fan::monitor::Reader reader;
        const auto value=[](float v) { if(fan::monitor::valid(v)) std::cout<<v; else std::cout<<"null"; };
        for(int i=0;i<3;++i) {
            const auto s=reader.sample();
            std::cout<<"{\"time\":"<<s.time<<",\"cpuLoad\":"; value(s.cpuLoad);
            std::cout<<",\"gpuLoad\":"; value(s.gpuLoad); std::cout<<",\"cpuMHz\":"; value(s.cpuMHz);
            std::cout<<",\"gpuMHz\":"; value(s.gpuMHz); std::cout<<",\"cpuTemp\":"; value(s.cpuTemp);
            std::cout<<",\"gpuTemp\":"; value(s.gpuTemp); std::cout<<",\"memoryLoad\":"; value(s.memoryLoad);
            std::cout<<",\"memoryUsedGiB\":"; value(s.memoryUsedGiB); std::cout<<",\"memoryTotalGiB\":"; value(s.memoryTotalGiB);
            std::cout<<",\"fanRPM\":"; value(s.fanRPM); std::cout<<"}"<<std::endl;
            if(i<2) std::this_thread::sleep_for(std::chrono::seconds(10));
        }
        return 0;
    }
    IDXGIFactory1* factory{};
    if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        for(UINT i=0;;++i) {
            IDXGIAdapter1* a{}; if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{}; a->GetDesc1(&desc);
            std::wprintf(L"DXGI %u %s vendor=%x luid=%x:%x\n",i,desc.Description,desc.VendorId,desc.AdapterLuid.HighPart,desc.AdapterLuid.LowPart);
            D3DKMT_OPENADAPTERFROMLUID open{}; open.AdapterLuid=desc.AdapterLuid;
            const LONG opened=D3DKMTOpenAdapterFromLuid(&open);
            std::printf("  open=%lx\n",opened);
            if(opened>=0) {
                D3DKMT_ADAPTER_PERFDATA data{};
                D3DKMT_QUERYADAPTERINFO query{open.hAdapter,KMTQAITYPE_ADAPTERPERFDATA,&data,sizeof(data)};
                const LONG status=D3DKMTQueryAdapterInfo(&query);
                std::printf("  perf=%lx temp=%lu fan=%lu memoryHz=%llu\n",status,data.Temperature,data.FanRPM,data.MemoryFrequency);
                D3DKMT_NODE_PERFDATA node{};
                query.Type=KMTQAITYPE_NODEPERFDATA; query.pPrivateDriverData=&node; query.PrivateDriverDataSize=sizeof(node);
                const LONG ns=D3DKMTQueryAdapterInfo(&query);
                std::printf("  node=%lx frequencyHz=%llu\n",ns,node.Frequency);
                D3DKMT_CLOSEADAPTER close{open.hAdapter}; D3DKMTCloseAdapter(&close);
            }
            a->Release();
        }
        factory->Release();
    }
    HMODULE dll=LoadLibraryExW(L"atiadlxx.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!dll) { std::printf("ADL DLL absent\n"); return 0; }
    auto create=proc<int(*)(ADL_MAIN_MALLOC_CALLBACK,int,void**)>(dll,"ADL2_Main_Control_Create");
    auto destroy=proc<int(*)(void*)>(dll,"ADL2_Main_Control_Destroy");
    auto count=proc<int(*)(void*,int*)>(dll,"ADL2_Adapter_NumberOfAdapters_Get");
    auto info=proc<int(*)(void*,AdapterInfo*,int)>(dll,"ADL2_Adapter_AdapterInfo_Get");
    void* context{}; if(!create || !destroy || !count || !info) return 1;
    const int created=create(allocate,1,&context); std::printf("ADL create=%d\n",created); if(created!=0) return 0;
    int n{}; count(context,&n); if(n<1 || n>64) { destroy(context); FreeLibrary(dll); return 0; }
    std::vector<AdapterInfo> adapters(n); for(auto& a:adapters) a.iSize=sizeof(a);
    std::printf("ADL count=%d info=%d\n",n,info(context,adapters.data(),n*sizeof(AdapterInfo)));
    for(const auto& a:adapters) {
        std::printf("ADL %d %s vendor=%x bus=%d present=%d\n",a.iAdapterIndex,a.strAdapterName,a.iVendorID,a.iBusNumber,a.iPresent);
        auto caps=proc<int(*)(void*,int,int*,int*,int*)>(dll,"ADL2_Overdrive_Caps");
        int supported{},enabled{},version{};
        if(caps) std::printf("  caps=%d supported=%d enabled=%d version=%d\n",caps(context,a.iAdapterIndex,&supported,&enabled,&version),supported,enabled,version);
        auto temp=proc<int(*)(void*,int,int,ADLTemperature*)>(dll,"ADL2_Overdrive5_Temperature_Get");
        ADLTemperature t{sizeof(t)}; if(temp) { const int s=temp(context,a.iAdapterIndex,0,&t); std::printf("  temp5=%d value=%d\n",s,t.iTemperature); }
        auto activity=proc<int(*)(void*,int,ADLPMActivity*)>(dll,"ADL2_Overdrive5_CurrentActivity_Get");
        ADLPMActivity act{}; act.iSize=sizeof(act); if(activity) { const int s=activity(context,a.iAdapterIndex,&act); std::printf("  activity5=%d clock=%d load=%d\n",s,act.iEngineClock,act.iActivityPercent); }
        auto fan=proc<int(*)(void*,int,int,ADLFanSpeedValue*)>(dll,"ADL2_Overdrive5_FanSpeed_Get");
        ADLFanSpeedValue f{sizeof(f),ADL_DL_FANCTRL_SPEED_TYPE_RPM}; if(fan) { const int s=fan(context,a.iAdapterIndex,0,&f); std::printf("  fan5=%d rpm=%d\n",s,f.iFanSpeed); }
        auto log=proc<int(*)(void*,int,ADLPMLogDataOutput*)>(dll,"ADL2_New_QueryPMLogData_Get");
        ADLPMLogDataOutput pm{}; pm.size=sizeof(pm); if(log) { const int s=log(context,a.iAdapterIndex,&pm); std::printf("  pmlog=%d\n",s); if(s==0) for(int j=0;j<ADL_PMLOG_MAX_SENSORS;++j) if(pm.sensors[j].supported) std::printf("    sensor[%d]=%d\n",j,pm.sensors[j].value); }
        auto odnt=proc<int(*)(void*,int,int,int*)>(dll,"ADL2_OverdriveN_Temperature_Get");
        int nt{}; if(odnt) { const int s=odnt(context,a.iAdapterIndex,1,&nt); std::printf("  tempN=%d value=%d\n",s,nt); }
    }
    destroy(context); FreeLibrary(dll);
}
