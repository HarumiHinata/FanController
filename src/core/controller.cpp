#include "core/controller.h"
#include <exception>

namespace fan {
Controller::Controller(std::unique_ptr<Backend> backend, std::function<void()> notify, Timing timing)
    : backend_(std::move(backend)), notify_(std::move(notify)), timing_(timing) {}
Controller::~Controller() { stop(); join(); }
void Controller::start() { if (!worker_.joinable()) worker_ = std::thread(&Controller::run, this); }
void Controller::stop() { { std::lock_guard lock(mutex_); stopping_ = true; ++generation_; } cv_.notify_all(); }
void Controller::join() { if (worker_.joinable()) worker_.join(); }
Snapshot Controller::snapshot() const { std::lock_guard lock(mutex_); return snapshot_; }
bool Controller::request(Mode mode) {
    { std::lock_guard lock(mutex_);
      if (stopping_ || suspended_) return false;
      if (mode == Mode::Boost && (snapshot_.phase != Phase::Ready || restorePending_)) return false;
      if (mode == Mode::Auto) restorePending_ = true;
      requested_ = mode; ++generation_;
    }
    cv_.notify_all(); return true;
}
void Controller::suspend() {
    { std::lock_guard lock(mutex_); suspended_ = true; restorePending_ = true; requested_ = Mode::Auto; ++generation_; }
    cv_.notify_all();
}
void Controller::resume() {
    { std::lock_guard lock(mutex_); suspended_ = false; restorePending_ = true; requested_ = Mode::Auto; recalibrate_ = true; ++generation_; }
    cv_.notify_all();
}
bool Controller::interruptedWait(std::chrono::milliseconds duration, std::uint64_t generation) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, duration, [&] { return stopping_ || generation_ != generation; });
}
void Controller::publish(Phase phase, Mode mode, const std::wstring& detail, std::uint32_t error, bool confirmed, std::uint32_t raw) {
    { std::lock_guard lock(mutex_);
      snapshot_.phase = phase; snapshot_.mode = mode; snapshot_.detail = detail;
      snapshot_.error = error; snapshot_.autoConfirmed = confirmed; snapshot_.raw = raw; ++snapshot_.revision;
    }
    if (notify_) notify_();
}
Result Controller::applyAuto(bool calibrate) {
    auto result = backend_->set(false);
    if (!result.ok) return result;
    // A restore must finish even when a newer command is queued. No boost can overtake it.
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::this_thread::sleep_for(timing_.settle);
        result = backend_->read();
        if (!result.ok) return result;
        if (calibrate || !baselineValid_) {
            const auto candidate=result.value;
            std::this_thread::sleep_for(timing_.settle);
            result=backend_->read();
            if (!result.ok) return result;
            if (candidate != result.value) continue;
            baseline_=result.value; baselineValid_=true;
        }
        if (result.value == baseline_) return result;
    }
    return Result::fail(13, L"正常模式回读不匹配，恢复尚未确认");
}
Result Controller::applyBoost(std::uint64_t generation) {
    { std::lock_guard lock(mutex_); if (stopping_ || generation_ != generation) return Result::fail(995, L"操作已取消"); }
    auto result = backend_->set(true);
    if (!result.ok) return result;
    boostStarted_ = std::chrono::steady_clock::now();
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (interruptedWait(timing_.retry, generation)) return Result::fail(995, L"操作已取消");
        result = backend_->read();
        if (!result.ok) return result;
        if (baselineValid_ && backend_->isBoostValue(result.value, baseline_)) return result;
    }
    return Result::fail(1460, L"强冷模式未得到驱动回读确认");
}
std::uint64_t Controller::fault(const Result& reason) {
    const auto restored = applyAuto(false);
    // A queued boost must never override an error recovery; Auto can explicitly retry.
    auto text = reason.detail + (restored.ok ? L"；已恢复正常模式" : L"；恢复未确认，请退出其他控制工具后重试");
    std::uint64_t handled;
    {
        std::lock_guard lock(mutex_);
        requested_=Mode::Auto; restorePending_=true; handled=generation_;
        snapshot_.phase=Phase::Fault; snapshot_.mode=Mode::Auto; snapshot_.detail=std::move(text);
        snapshot_.error=reason.error; snapshot_.autoConfirmed=restored.ok; snapshot_.raw=restored.value; ++snapshot_.revision;
    }
    if(notify_) notify_();
    return handled;
}
void Controller::run() {
    try {
        Mode active = Mode::Auto;
        std::uint64_t handled = ~std::uint64_t{0};
        auto deadline = std::chrono::steady_clock::time_point::max();
        for (;;) {
            Mode desired; std::uint64_t generation; bool calibrate;
            {
                std::unique_lock lock(mutex_);
                cv_.wait_until(lock, deadline, [&] { return stopping_ || generation_ != handled; });
                if (stopping_) break;
                desired = suspended_ ? Mode::Auto : requested_;
                generation = generation_; handled = generation;
                calibrate = recalibrate_; recalibrate_ = false;
            }
            publish(Phase::Applying, desired, desired == Mode::Auto ? L"正在恢复原厂自动控制…" : L"正在应用强冷模式…");
            Result result;
            if (desired == Mode::Auto || calibrate) result = applyAuto(calibrate);
            else if (active == Mode::Boost) result = applyAuto(false); // reset the dust-removal interval
            if (result.ok && desired == Mode::Boost) result = applyBoost(generation);
            if (!result.ok) {
                if (result.error == 995) { active = Mode::Boost; deadline = std::chrono::steady_clock::time_point::max(); continue; }
                handled = fault(result); active = Mode::Auto;
                deadline = std::chrono::steady_clock::time_point::max(); continue;
            }
            active = desired;
            if (active == Mode::Auto) { std::lock_guard lock(mutex_); if (generation_ == generation) restorePending_ = false; }
            publish(Phase::Ready, active, active == Mode::Auto ? L"正常模式回读已确认 · 由原厂控制散热" : L"强冷模式回读已确认 · 持续维持中", 0, active == Mode::Auto, result.value);
            deadline = active == Mode::Boost ? boostStarted_ + timing_.boostRefresh : std::chrono::steady_clock::time_point::max();
        }
        publish(Phase::Stopping, Mode::Auto, L"正在恢复原厂控制并退出…");
        const auto restored = applyAuto(false);
        publish(Phase::Stopped, Mode::Auto, restored.ok ? L"已恢复正常模式" : L"退出前恢复未确认", restored.error, restored.ok, restored.value);
    } catch (...) {
        bool restored = false;
        try { restored = applyAuto(false).ok; } catch (...) {}
        publish(Phase::Stopped, Mode::Auto, L"控制线程异常，恢复结果请查看诊断", 1, restored);
    }
}
}
