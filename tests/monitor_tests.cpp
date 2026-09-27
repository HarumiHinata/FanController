#include "monitor/history.h"
#include <windows.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace fan::monitor;
void require(bool test,const char* text) { if(!test) throw std::runtime_error(text); }
int main() {
    try {
        require(std::fabs(cpuUsage(110,250,150,100,200,100)-90)<.01f,"CPU kernel includes idle");
        require(!valid(cpuUsage(100,100,100,100,100,100)),"No elapsed CPU time is not idle");
        require(!valid(cpuUsage(10,10,10,100,100,100)),"CPU counter reset");
        const std::vector<Engine> engines{{L"pid_1_luid_A_phys_0_eng_0",40},{L"pid_2_luid_A_phys_0_eng_0",35},{L"pid_1_luid_A_phys_0_eng_1",50},{L"pid_3_luid_B_phys_0_eng_0",99}};
        require(gpuUsage(engines,L"luid_A_phys_0_")==75,"GPU aggregation and adapter isolation");
        require(gpuUsage(engines,L"luid_a_phys_0_")==75,"LUID hexadecimal case insensitive");
        require(!valid(gpuUsage(engines,L"luid_C_phys_0_")),"Missing adapter is not 0 percent");
        require(!valid(checked(65536,0,100)),"Broken activity result must not be shown");
        auto history=std::make_unique<History>();
        const auto start=nowSeconds()-2*horizon;
        for(int i=0;i<=17280;++i) { Sample s; s.time=start+i*10; s.memoryLoad=static_cast<float>(i%100); history->append(s); }
        require(history->size()==History::capacity,"History bounded at 24h");
        require(history->at(history->size()-1).time-history->at(0).time==horizon,"History window");
        const auto last=history->at(history->size()-1);
        history->append(last); require(history->size()==History::capacity,"Duplicate timestamp ignored");
        wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH,temp);
        const auto file=std::filesystem::path(temp)/(L"fan-history-test-"+std::to_wstring(GetCurrentProcessId())+L".bin");
        require(history->save(file),"Atomic save");
        require(history->save(file),"Atomic replace");
        auto restored=std::make_unique<History>();
        require(restored->load(file,last.time),"Roundtrip load");
        require(restored->size()==history->size(),"Roundtrip count");
        require(restored->at(500).memoryLoad==history->at(500).memoryLoad,"Roundtrip metric");
        require(!valid(restored->at(0).cpuTemp),"Missing sensor preserved");
        require(restored->load(file,last.time+1000),"Reload with elapsed time");
        require(restored->at(0).time>=last.time+1000-horizon,"Expired samples removed");
        require(!restored->load(file,last.time-horizon),"Future history rejected");
        { std::fstream damage(file,std::ios::binary|std::ios::in|std::ios::out); damage.seekp(50); damage.put('\xff'); }
        require(!restored->load(file,last.time),"Checksum rejects corrupted data");
        std::filesystem::remove(file);
        Sample next; next.time=last.time+60; history->append(next);
        require(history->at(history->size()-1).time-history->at(history->size()-2).time==60,"Gap remains a gap");
        next.time=last.time-60; history->append(next); require(history->size()==1,"Clock rollback starts new series");
        std::cout<<"Monitor tests passed: counter semantics, GPU engines, capacity, gaps, atomic persistence, corruption, clock rollback.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
