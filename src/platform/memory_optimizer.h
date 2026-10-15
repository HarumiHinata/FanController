#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace fan::memory {
enum class Status : std::uint32_t { Complete, Failed, Cancelled, Unconfirmed, Demo };
// Fixed-width fields only: also used by the versioned local IPC protocol.
struct Report {
    Status status{Status::Failed};
    std::uint32_t error{}, completed{}, failedCommand{}, ntstatus{}, samples{}, restoreError{};
    std::uint64_t before{}, after{}, elapsedMs{};
    std::array<std::uint32_t,3> stageStatus{}, stageMs{};
};
std::wstring describe(const Report& report);
std::wstring diagnostics(const Report& report);
// Called before any window, monitor, or hardware initialization.
int workerMain(const wchar_t* requestId, const wchar_t* parentId);
class Task {
public:
    Task();
    ~Task();
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    bool start(bool demo=false);
    std::optional<Report> take();
    void cancel();
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
