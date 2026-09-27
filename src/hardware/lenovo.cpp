#include "hardware/lenovo.h"
#include "platform/platform.h"
#include <array>
namespace fan {
std::wstring systemError(DWORD code) {
    wchar_t text[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, text, 512, nullptr);
    std::wstring result(text);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result.empty() ? L"Windows 错误 " + std::to_wstring(code) : result;
}
static std::wstring biosValue(const wchar_t* name) {
    wchar_t value[256]{}; DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return {};
    return value;
}
bool supportedMachine(std::wstring& reason) {
    if (biosValue(L"SystemManufacturer") != L"LENOVO" || biosValue(L"SystemProductName") != L"81BR") {
        reason = L"此版本仅适配 Lenovo ideapad 720S-13ARR（81BR）"; return false;
    }
    if (biosValue(L"BIOSVersion") != L"6KCN40WW") {
        reason = L"BIOS 版本与已验证的 6KCN40WW 不同，需重新验证适配"; return false;
    }
    return true;
}
LenovoBackend::LenovoBackend(bool protectedBoost) : protectedBoost_(protectedBoost) { supported_ = supportedMachine(reason_); }
LenovoBackend::~LenovoBackend() { if (guardian_) CloseHandle(guardian_); }
Result LenovoBackend::call(bool write, bool boost) {
    if (!supported_) return Result::fail(ERROR_NOT_SUPPORTED, reason_);
    Handle device(CreateFileW(L"\\\\.\\EnergyDrv", write ? GENERIC_WRITE : GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!device.valid()) { const DWORD code = GetLastError(); return Result::fail(code, L"无法访问联想控制器：" + systemError(code)); }
    DWORD returned{}, output{};
    std::array<DWORD, 3> command{6, 1, boost ? 1u : 0u};
    DWORD query = 14;
    const BOOL ok = DeviceIoControl(device.get(), write ? 0x831020C0u : 0x831020C4u,
        write ? static_cast<void*>(command.data()) : static_cast<void*>(&query), write ? sizeof(command) : sizeof(query),
        write ? nullptr : &output, write ? 0 : sizeof(output), &returned, nullptr);
    if (!ok) { const DWORD code = GetLastError(); return Result::fail(code, L"联想驱动调用失败：" + systemError(code)); }
    if (!write && returned != sizeof(output)) return Result::fail(ERROR_INVALID_DATA, L"联想驱动返回长度无效");
    return {true, 0, output, {}};
}
Result LenovoBackend::set(bool boost) {
    if (boost && protectedBoost_) { const auto guard = ensureRecoveryGuard(guardian_); if (!guard.ok) return guard; }
    return call(true, boost);
}
Result LenovoBackend::read() { return call(false, false); }
}
