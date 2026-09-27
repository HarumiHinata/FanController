#pragma once
#include "core/controller.h"
#include <windows.h>
namespace fan {
class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
private: HANDLE value_;
};
std::wstring systemError(DWORD code);
bool supportedMachine(std::wstring& reason);
class LenovoBackend final : public Backend {
public:
    explicit LenovoBackend(bool protectedBoost = false);
    ~LenovoBackend() override;
    Result set(bool boost) override;
    Result read() override;
    // 3 is the observed strong-cooling value on the explicitly supported 81BR/BIOS.
    bool isBoostValue(std::uint32_t value, std::uint32_t) const override { return value == 3; }
private:
    Result call(bool write, bool boost);
    bool supported_{};
    std::wstring reason_;
    bool protectedBoost_{};
    HANDLE guardian_{};
};
class MockBackend final : public Backend {
public:
    Result set(bool boost) override { boost_ = boost; return {}; }
    Result read() override { return {true, 0, boost_ ? 1u : 0u, {}}; }
private: bool boost_{};
};
}
