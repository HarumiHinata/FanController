#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace fan {
struct Result {
    bool ok{true};
    std::uint32_t error{};
    std::uint32_t value{};
    std::wstring detail;
    static Result fail(std::uint32_t code, std::wstring text) { return {false, code, 0, std::move(text)}; }
};
class Backend {
public:
    virtual ~Backend() = default;
    virtual Result set(bool boost) = 0;
    virtual Result read() = 0;
    virtual bool isBoostValue(std::uint32_t value, std::uint32_t baseline) const { return value != baseline; }
};
enum class Mode { Auto, Boost };
enum class Phase { Starting, Applying, Ready, Fault, Stopping, Stopped };
struct Snapshot {
    Mode mode{Mode::Auto};
    Phase phase{Phase::Starting};
    bool autoConfirmed{};
    std::uint32_t error{};
    std::uint32_t raw{};
    std::uint64_t revision{};
    std::wstring detail{L"正在连接原厂控制器…"};
};
struct Timing {
    std::chrono::milliseconds settle{150};
    std::chrono::milliseconds retry{150};
    std::chrono::milliseconds boostRefresh{8500};
};
class Controller {
public:
    Controller(std::unique_ptr<Backend> backend, std::function<void()> notify = {}, Timing timing = {});
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    void start();
    bool request(Mode mode);
    void suspend();
    void resume();
    void stop();
    void join();
    Snapshot snapshot() const;
private:
    bool interruptedWait(std::chrono::milliseconds duration, std::uint64_t generation);
    void publish(Phase phase, Mode mode, const std::wstring& detail, std::uint32_t error = 0, bool confirmed = false, std::uint32_t raw = 0);
    Result applyAuto(bool calibrate);
    Result applyBoost(std::uint64_t generation);
    std::uint64_t fault(const Result& reason);
    void run();
    std::unique_ptr<Backend> backend_;
    std::function<void()> notify_;
    Timing timing_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    Snapshot snapshot_;
    Mode requested_{Mode::Auto};
    std::uint64_t generation_{};
    bool stopping_{};
    bool suspended_{};
    bool recalibrate_{true};
    bool restorePending_{true};
    bool baselineValid_{};
    std::uint32_t baseline_{};
    std::chrono::steady_clock::time_point boostStarted_{};
};
}
