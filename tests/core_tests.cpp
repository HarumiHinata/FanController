#include "core/controller.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
struct Data { std::atomic<bool> boost{},fail{},mismatch{}; std::atomic<int> writes{},reads{},active{},overlaps{}; };
struct Fake : fan::Backend {
    Data& data;
    explicit Fake(Data& d):data(d){}
    fan::Result set(bool boost) override {
        if(data.active.fetch_add(1)!=0) ++data.overlaps;
        ++data.writes;
        if(!data.fail) data.boost=boost;
        --data.active;
        return data.fail ? fan::Result::fail(5,L"injected failure") : fan::Result{};
    }
    fan::Result read() override {
        ++data.reads;
        if(data.fail) return fan::Result::fail(5,L"injected read failure");
        return {true,0,(!data.mismatch && data.boost)?7u:3u,{}};
    }
};
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
template<class Predicate> void until(Predicate predicate,const char* message) {
    const auto end=std::chrono::steady_clock::now()+5s;
    while(!predicate()) { if(std::chrono::steady_clock::now()>end) throw std::runtime_error(message); std::this_thread::yield(); }
}
int main() {
    try {
        Data data;
        fan::Controller controller(std::make_unique<Fake>(data),{},fan::Timing{0ms,0ms,20ms});
        controller.start();
        until([&]{return controller.snapshot().phase==fan::Phase::Ready;},"startup");
        require(controller.snapshot().autoConfirmed && !data.boost,"must start automatic");
        const int before=data.writes;
        std::this_thread::sleep_for(30ms);
        require(data.writes==before,"automatic must not poll/write");
        for(int i=0;i<10000;++i) {
            auto revision=controller.snapshot().revision;
            require(controller.request(fan::Mode::Boost),"boost accepted");
            until([&]{auto s=controller.snapshot();return s.revision>revision && s.phase==fan::Phase::Ready && s.mode==fan::Mode::Boost;},"boost reply");
            revision=controller.snapshot().revision;
            controller.request(fan::Mode::Auto);
            until([&]{auto s=controller.snapshot();return s.revision>revision && s.phase==fan::Phase::Ready && s.autoConfirmed;},"auto reply");
        }
        controller.request(fan::Mode::Boost);
        until([&]{return controller.snapshot().mode==fan::Mode::Boost && controller.snapshot().phase==fan::Phase::Ready;},"boost before suspend");
        controller.suspend();
        until([&]{return controller.snapshot().autoConfirmed;},"suspend restore");
        require(!controller.request(fan::Mode::Boost),"suspended boost rejected");
        controller.resume();
        until([&]{return controller.snapshot().phase==fan::Phase::Ready;},"resume");
        data.fail=true; controller.request(fan::Mode::Auto);
        until([&]{return controller.snapshot().phase==fan::Phase::Fault;},"failure visible");
        require(!controller.snapshot().autoConfirmed,"failed restore must be unknown");
        require(!controller.request(fan::Mode::Boost),"fault boost rejected");
        const int failedWrites=data.writes; std::this_thread::sleep_for(30ms);
        require(data.writes==failedWrites,"failure must not retry forever");
        data.fail=false; controller.request(fan::Mode::Auto);
        until([&]{return controller.snapshot().phase==fan::Phase::Ready;},"explicit retry");
        data.mismatch=true; controller.request(fan::Mode::Boost);
        until([&]{return controller.snapshot().phase==fan::Phase::Fault;},"mismatch must fail bounded");
        require(controller.snapshot().autoConfirmed,"mismatch recovered");
        data.mismatch=false; controller.request(fan::Mode::Auto);
        until([&]{return controller.snapshot().phase==fan::Phase::Ready;},"recover mismatch");
        controller.request(fan::Mode::Boost); controller.request(fan::Mode::Auto); controller.stop(); controller.join();
        require(!data.boost && controller.snapshot().autoConfirmed,"stop must restore");
        require(data.overlaps==0,"all IO serialized");
        require(!controller.request(fan::Mode::Boost),"cannot restart stopped controller");
        std::cout<<"PASS: 10000 boost/auto cycles, idle, suspend/resume, IO failure, readback mismatch, recovery, cancellation and shutdown; writes="<<data.writes<<"\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}
