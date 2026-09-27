#include "hardware/lenovo.h"
#include "platform/platform.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
int wmain(int argc, wchar_t** argv) {
    if (argc>1 && wcscmp(argv[1],L"--guardian")==0) return fan::recoveryMain(argc,argv);
    fan::LenovoBackend backend;
    const std::wstring arg = argc > 1 ? argv[1] : L"--read";
    if (arg == L"--read") {
        const auto result = backend.read();
        std::wcout << L"read ok=" << result.ok << L" error=" << result.error << L" raw=" << result.value << L" " << result.detail << L"\n";
        return result.ok ? 0 : 1;
    }
    if (arg != L"--normal" && arg != L"--boost-test" && arg != L"--expiry-test" && arg != L"--controller-test" && arg != L"--guard-crash-test") return 2;
    fan::Handle own(CreateMutexW(nullptr, FALSE, L"Global\\FanController.Hardware.81BR"));
    if (!own.valid() || GetLastError() == ERROR_ALREADY_EXISTS) { std::cerr << "Controller already running\n"; return 3; }
    fan::Handle reference(CreateMutexW(nullptr, FALSE, L"LenovoFanControlMutex"));
    if (!reference.valid() || GetLastError() == ERROR_ALREADY_EXISTS) { std::cerr << "Reference controller already running\n"; return 3; }
    if (arg==L"--controller-test" || arg==L"--guard-crash-test") {
        fan::Controller controller(std::make_unique<fan::LenovoBackend>(true)); controller.start();
        const auto waitReady=[&](fan::Mode mode) {
            for(int i=0;i<100;++i) { auto s=controller.snapshot(); if(s.phase==fan::Phase::Fault) return false; if(s.phase==fan::Phase::Ready && s.mode==mode) return true; Sleep(50); }
            return false;
        };
        if(!waitReady(fan::Mode::Auto)) { auto s=controller.snapshot(); std::wcout<<L"startup failed "<<s.error<<L" "<<s.detail<<std::endl; return 1; }
        std::cout<<"startup_auto=1"<<std::endl;
        controller.request(fan::Mode::Boost);
        if(!waitReady(fan::Mode::Boost)) { auto s=controller.snapshot(); std::wcout<<L"boost failed "<<s.error<<L" "<<s.detail<<std::endl; return 1; }
        std::cout<<"boost_confirmed=1 raw="<<controller.snapshot().raw<<std::endl;
        if(arg==L"--guard-crash-test") { std::cout<<"terminating test parent; guardian must restore"<<std::endl; TerminateProcess(GetCurrentProcess(),77); }
        for(int i=0;i<15;++i) { Sleep(2000); auto s=controller.snapshot(); std::cout<<"seconds="<<(i+1)*2<<" phase="<<static_cast<int>(s.phase)<<" error="<<s.error<<" raw="<<s.raw<<std::endl; if(s.phase==fan::Phase::Fault) return 1; }
        controller.request(fan::Mode::Auto);
        if(!waitReady(fan::Mode::Auto)) return 1;
        controller.stop(); controller.join();
        const auto s=controller.snapshot(); std::cout<<"shutdown_auto="<<s.autoConfirmed<<" raw="<<s.raw<<std::endl;
        return s.autoConfirmed?0:1;
    }
    struct Restore { fan::Backend& backend; ~Restore() { const auto r=backend.set(false); std::cout << "final_restore=" << r.ok << " error=" << r.error << std::endl; } } restore{backend};
    auto r = backend.set(false);
    if (!r.ok) { std::wcout << r.detail << L" error=" << r.error << L"\n"; return 1; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    r = backend.read(); const auto baseline = r.value;
    std::cout << "normal ok=" << r.ok << " raw=" << r.value << " error=" << r.error << std::endl;
    if (!r.ok || arg == L"--normal") return r.ok ? 0 : 1;
    r = backend.set(true);
    std::cout << "boost_write=" << r.ok << " error=" << r.error << std::endl;
    if (!r.ok) return 1;
    const int seconds = arg == L"--expiry-test" ? 150 : 12;
    for (int elapsed = 0; elapsed <= seconds; elapsed += 2) {
        r = backend.read();
        std::cout << "seconds=" << elapsed << " ok=" << r.ok << " raw=" << r.value << " same_as_normal=" << (r.value == baseline) << " error=" << r.error << std::endl;
        if (!r.ok) return 1;
        if (elapsed < seconds) std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}
