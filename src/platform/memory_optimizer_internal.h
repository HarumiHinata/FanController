#pragma once
#include "platform/memory_optimizer.h"

namespace fan::memory::detail {
// The production backend calls NT APIs; tests inject a backend that never cleans memory.
class Backend {
public:
    virtual ~Backend() = default;
    virtual DWORD prepare() = 0;
    virtual bool available(std::uint64_t& bytes) = 0;
    virtual std::uint32_t apply(std::uint32_t command) = 0;
    virtual DWORD translate(std::uint32_t status) = 0;
    virtual DWORD finish() = 0;
    virtual bool connected() { return true; }
};
Report run(Backend& backend);
Report request(const std::wstring& executable, HANDLE cancel, bool elevate=true, DWORD timeoutMs=120000);
// Injection is available to the separate test executable only; no product CLI switch enables it.
int serve(const wchar_t* requestId, const wchar_t* parentId, Backend* substitute=nullptr);
bool valid(const Report& report);
}
