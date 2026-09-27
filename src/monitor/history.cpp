#include "monitor/history.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <map>

namespace fan::monitor {
bool valid(float v) { return std::isfinite(v); }
float checked(double v,double low,double high) { return std::isfinite(v)&&v>=low&&v<=high?static_cast<float>(v):missing; }
std::int64_t nowSeconds() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
float cpuUsage(std::uint64_t i,std::uint64_t k,std::uint64_t u,std::uint64_t oi,std::uint64_t ok,std::uint64_t ou) {
    if(i<oi || k<ok || u<ou) return missing;
    const auto total=(k-ok)+(u-ou),idle=i-oi;
    if(!total || idle>total) return missing;
    return static_cast<float>(100.0*static_cast<double>(total-idle)/static_cast<double>(total));
}
float gpuUsage(std::span<const Engine> input,const std::wstring& luid) {
    if(luid.empty()) return missing;
    auto folded=luid; std::transform(folded.begin(),folded.end(),folded.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    std::map<std::wstring,double> totals;
    for(const auto& engine:input) {
        auto name=engine.name; std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        const auto pos=name.find(folded);
        if(pos==std::wstring::npos || !std::isfinite(engine.load) || engine.load<0) continue;
        totals[name.substr(pos)]+=engine.load;
    }
    if(totals.empty()) return missing;
    double result=0; for(const auto& [key,value]:totals) { (void)key; result=std::max(result,value); }
    return static_cast<float>(std::clamp(result,0.0,100.0));
}
const Sample& History::at(std::size_t i) const { return samples_[(first_+i)%capacity]; }
void History::append(const Sample& sample) {
    if(sample.time<=0) return;
    if(count_ && sample.time<at(count_-1).time) { first_=count_=0; }
    if(count_ && sample.time==at(count_-1).time) return;
    while(count_ && sample.time-at(0).time>horizon) { first_=(first_+1)%capacity; --count_; }
    if(count_==capacity) { first_=(first_+1)%capacity; --count_; }
    samples_[(first_+count_)%capacity]=sample; ++count_;
}
namespace {
struct Header { char magic[8]; std::uint32_t version,count,checksum,recordSize; };
static_assert(sizeof(Sample)==48);
std::uint32_t hash(const void* data,std::size_t size,std::uint32_t h=2166136261u) {
    const auto* p=static_cast<const unsigned char*>(data); for(std::size_t i=0;i<size;++i) h=(h^p[i])*16777619u; return h;
}
bool sane(const Sample& s) {
    const auto range=[](float v,float lo,float hi) { return std::isnan(v)||(std::isfinite(v)&&v>=lo&&v<=hi); };
    return s.time>0 && range(s.cpuLoad,0,100)&&range(s.gpuLoad,0,100)&&range(s.memoryLoad,0,100)
        && range(s.cpuMHz,0,10000)&&range(s.gpuMHz,0,10000)&&range(s.cpuTemp,-20,150)&&range(s.gpuTemp,-20,150)
        && range(s.memoryUsedGiB,0,1048576)&&range(s.memoryTotalGiB,0,1048576)&&range(s.fanRPM,0,30000);
}
}
bool History::save(const std::filesystem::path& path) const {
    if(path.empty()) return false;
    Header h{{'F','C','H','I','S','T','2',0},1,static_cast<std::uint32_t>(count_),2166136261u,sizeof(Sample)};
    for(std::size_t i=0;i<count_;++i) h.checksum=hash(&at(i),sizeof(Sample),h.checksum);
    auto temporary=path; temporary+=L".tmp";
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc); if(!output) return false;
    output.write(reinterpret_cast<const char*>(&h),sizeof(h));
    for(std::size_t i=0;i<count_;++i) output.write(reinterpret_cast<const char*>(&at(i)),sizeof(Sample));
    output.flush(); if(!output) return false; output.close();
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}
bool History::load(const std::filesystem::path& path,std::int64_t now) {
    std::ifstream input(path,std::ios::binary|std::ios::ate); if(!input) return false;
    const auto length=input.tellg(); if(length<static_cast<std::streamoff>(sizeof(Header)) || length>static_cast<std::streamoff>(sizeof(Header)+capacity*sizeof(Sample))) return false;
    input.seekg(0); Header h{}; input.read(reinterpret_cast<char*>(&h),sizeof(h));
    if(std::string(h.magic,8)!=std::string("FCHIST2\0",8)||h.version!=1||h.recordSize!=sizeof(Sample)||h.count>capacity
        ||length!=static_cast<std::streamoff>(sizeof(Header)+h.count*sizeof(Sample))) return false;
    std::vector<Sample> records(h.count); input.read(reinterpret_cast<char*>(records.data()),static_cast<std::streamsize>(records.size()*sizeof(Sample)));
    if(!input || hash(records.data(),records.size()*sizeof(Sample))!=h.checksum) return false;
    std::int64_t previous=0;
    for(const auto& s:records) { if(!sane(s)||s.time<=previous||s.time>now+60) return false; previous=s.time; }
    first_=count_=0;
    for(const auto& s:records) if(s.time>=now-horizon && s.time<=now) append(s);
    return true;
}
}
