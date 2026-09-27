#pragma once
#include "monitor/history.h"
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace fan::monitor {
struct Identity { std::wstring cpu=L"CPU",gpu=L"GPU"; std::wstring diagnostics; };
class Reader {
public:
    Reader();
    ~Reader();
    Sample sample();
    const Identity& identity() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class Monitor {
public:
    explicit Monitor(std::function<void()> notify);
    ~Monitor();
    void start();
    void stop();
    void join();
    bool finished() const;
    Sample latest() const;
    Identity identity() const;
private:
    void run();
    std::function<void()> notify_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    Sample sample_;
    Identity identity_;
    bool stopping_{},finished_{};
};
}
