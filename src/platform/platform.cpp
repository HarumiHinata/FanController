#include "platform/platform.h"
#include "hardware/lenovo.h"
#include <shlobj.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace fan {
std::wstring executablePath() { wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768); return path; }
std::wstring dataDirectory() {
    PWSTR base{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) return L".";
    std::wstring path = std::wstring(base) + L"\\FanController"; CoTaskMemFree(base);
    std::error_code ec; std::filesystem::create_directories(path, ec); return path;
}
void log(const std::wstring& message) {
    static std::mutex mutex; std::lock_guard guard(mutex);
    try {
        const std::filesystem::path path = std::filesystem::path(dataDirectory()) / L"controller.log";
        if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 2 * 1024 * 1024) {
            auto old = path; old += L".1"; auto older = path; older += L".2";
            std::error_code ec; std::filesystem::remove(older, ec);
            if (std::filesystem::exists(old)) std::filesystem::rename(old, older, ec);
            std::filesystem::rename(path, old, ec);
        }
        SYSTEMTIME time{}; GetLocalTime(&time);
        wchar_t stamp[64]{}; swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u ", time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
        const std::wstring line = std::wstring(stamp) + message.substr(0, 1500) + L"\r\n";
        const int count = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(count, 0); WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(), count, nullptr, nullptr);
        std::ofstream out(path, std::ios::binary | std::ios::app); out << utf8;
    } catch (...) { /* Logging must never prevent a hardware restore. */ }
}
bool closeToTray() {
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\FanController", L"CloseToTray", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value != 0;
}
void setCloseToTray(bool enabled) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\FanController", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        const DWORD value = enabled ? 1 : 0; RegSetValueExW(key, L"CloseToTray", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)); RegCloseKey(key);
    }
}
struct Bstr {
    BSTR value;
    explicit Bstr(const wchar_t* str) : value(SysAllocString(str)) {}
    ~Bstr() { SysFreeString(value); }
    operator BSTR() const { return value; }
};
static Result hrError(HRESULT hr) { return Result::fail(static_cast<DWORD>(hr), L"自启动任务操作失败：" + systemError(static_cast<DWORD>(hr))); }
static HRESULT folder(ComPtr<ITaskService>& service, ComPtr<ITaskFolder>& root) {
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
    VARIANT empty{};
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    if (SUCCEEDED(hr)) hr = service->GetFolder(Bstr(L"\\"), &root);
    return hr;
}
static std::wstring taskName() {
    wchar_t name[256]{}; DWORD size = 256; GetUserNameW(name, &size);
    return std::wstring(L"FanController-") + name;
}
static Result checkTaskOwner(ITaskFolder* root, bool& exists) {
    exists=false; ComPtr<IRegisteredTask> task;
    HRESULT hr=root->GetTask(Bstr(taskName().c_str()),&task);
    if(hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return {};
    if(FAILED(hr)) return hrError(hr);
    exists=true;
    ComPtr<ITaskDefinition> definition; ComPtr<IRegistrationInfo> info; BSTR author{};
    hr=task->get_Definition(&definition); if(SUCCEEDED(hr)) hr=definition->get_RegistrationInfo(&info);
    if(SUCCEEDED(hr)) hr=info->get_Author(&author);
    const bool owned=SUCCEEDED(hr) && author && wcscmp(author,L"FanController")==0;
    SysFreeString(author);
    return owned?Result{}:Result::fail(ERROR_ALREADY_EXISTS,L"同名自启动任务不属于本软件，未作修改");
}
Result startupStatus(bool& enabled) {
    enabled = false; ComPtr<ITaskService> service; ComPtr<ITaskFolder> root;
    HRESULT hr = folder(service, root); if (FAILED(hr)) return hrError(hr);
    ComPtr<IRegisteredTask> task; hr = root->GetTask(Bstr(taskName().c_str()), &task);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return {};
    if (FAILED(hr)) return hrError(hr);
    VARIANT_BOOL on{}; hr = task->get_Enabled(&on);
    if (FAILED(hr)) return hrError(hr);
    enabled = on != VARIANT_FALSE;
    ComPtr<ITaskDefinition> definition; ComPtr<IActionCollection> actions; ComPtr<IAction> action; ComPtr<IExecAction> exec;
    hr=task->get_Definition(&definition); if(SUCCEEDED(hr)) hr=definition->get_Actions(&actions);
    if(SUCCEEDED(hr)) hr=actions->get_Item(1,&action); if(SUCCEEDED(hr)) hr=action.As(&exec);
    if(FAILED(hr)) return hrError(hr);
    BSTR path{},args{}; hr=exec->get_Path(&path); if(SUCCEEDED(hr)) hr=exec->get_Arguments(&args);
    const bool matching=SUCCEEDED(hr) && path && args && _wcsicmp(path,executablePath().c_str())==0 && wcscmp(args,L"--background")==0;
    SysFreeString(path); SysFreeString(args);
    if(!matching) return Result::fail(ERROR_INVALID_DATA,L"自启动指向其他路径，请关闭后重新开启以修复");
    return {};
}
Result setStartup(bool enabled) {
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> root;
    HRESULT hr = folder(service, root); if (FAILED(hr)) return hrError(hr);
    bool exists{}; const auto owner=checkTaskOwner(root.Get(),exists); if(!owner.ok) return owner;
    if(!enabled && !exists) return {};
    if (!enabled) { hr = root->DeleteTask(Bstr(taskName().c_str()), 0); return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? Result{} : hrError(hr); }
    ComPtr<ITaskDefinition> definition; hr = service->NewTask(0, &definition); if (FAILED(hr)) return hrError(hr);
    ComPtr<IRegistrationInfo> info; hr = definition->get_RegistrationInfo(&info);
    if (SUCCEEDED(hr)) hr = info->put_Author(Bstr(L"FanController"));
    if (FAILED(hr)) return hrError(hr);
    ComPtr<IPrincipal> principal; hr = definition->get_Principal(&principal);
    if (SUCCEEDED(hr)) hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    if (SUCCEEDED(hr)) hr = principal->put_RunLevel(TASK_RUNLEVEL_LUA);
    if (FAILED(hr)) return hrError(hr);
    ComPtr<ITaskSettings> settings; hr = definition->get_Settings(&settings);
    if (SUCCEEDED(hr)) hr = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = settings->put_ExecutionTimeLimit(Bstr(L"PT0S"));
    if (SUCCEEDED(hr)) hr = settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    if (FAILED(hr)) return hrError(hr);
    ComPtr<ITriggerCollection> triggers; ComPtr<ITrigger> trigger;
    hr = definition->get_Triggers(&triggers); if (SUCCEEDED(hr)) hr = triggers->Create(TASK_TRIGGER_LOGON, &trigger);
    if (FAILED(hr)) return hrError(hr);
    ComPtr<ILogonTrigger> logon; hr = trigger.As(&logon); if (FAILED(hr)) return hrError(hr);
    wchar_t account[256]{}; DWORD length = 256; if (!GetUserNameW(account, &length)) return Result::fail(GetLastError(), L"无法读取当前用户");
    hr = logon->put_UserId(Bstr(account)); if (SUCCEEDED(hr)) hr = logon->put_Delay(Bstr(L"PT5S")); if (FAILED(hr)) return hrError(hr);
    ComPtr<IActionCollection> actions; ComPtr<IAction> action; ComPtr<IExecAction> exec;
    hr = definition->get_Actions(&actions); if (SUCCEEDED(hr)) hr = actions->Create(TASK_ACTION_EXEC, &action);
    if (SUCCEEDED(hr)) hr = action.As(&exec);
    if (SUCCEEDED(hr)) hr = exec->put_Path(Bstr(executablePath().c_str()));
    if (SUCCEEDED(hr)) hr = exec->put_Arguments(Bstr(L"--background"));
    if (FAILED(hr)) return hrError(hr);
    VARIANT empty{}; ComPtr<IRegisteredTask> registered;
    hr = root->RegisterTaskDefinition(Bstr(taskName().c_str()), definition.Get(), TASK_CREATE_OR_UPDATE, empty, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered);
    return SUCCEEDED(hr) ? Result{} : hrError(hr);
}
Result ensureRecoveryGuard(HANDLE& process) {
    if (process) return WaitForSingleObject(process, 0) == WAIT_TIMEOUT ? Result{} : Result::fail(ERROR_PROCESS_ABORTED, L"恢复保护进程已结束，请退出并重启软件");
    HANDLE parentRaw{};
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &parentRaw, SYNCHRONIZE, TRUE, 0)) return Result::fail(GetLastError(), L"无法创建恢复保护句柄");
    Handle parent(parentRaw);
    Handle own(OpenMutexW(SYNCHRONIZE, TRUE, L"Global\\FanController.Hardware.81BR"));
    Handle legacy(OpenMutexW(SYNCHRONIZE, TRUE, L"LenovoFanControlMutex"));
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle ready(CreateEventW(&security, TRUE, FALSE, nullptr));
    if (!own.valid() || !legacy.valid() || !ready.valid()) return Result::fail(GetLastError(), L"无法建立独占恢复保护");
    HANDLE inherited[]{parent.get(), own.get(), legacy.get(), ready.get()};
    SIZE_T bytes{}; InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) return Result::fail(GetLastError(), L"无法初始化恢复保护");
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Cleanup(){DeleteProcThreadAttributeList(p);} } cleanup{attributes};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) return Result::fail(GetLastError(), L"无法限制保护进程句柄");
    const auto path = executablePath();
    std::wstring command = L"\"" + path + L"\" --guardian";
    for (const auto handle : inherited) command += L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &child)) return Result::fail(GetLastError(), L"无法启动恢复保护进程");
    CloseHandle(child.hThread); process = child.hProcess;
    HANDLE wait[]{ready.get(), process};
    if (WaitForMultipleObjects(2, wait, FALSE, 3000) != WAIT_OBJECT_0) return Result::fail(ERROR_TIMEOUT, L"恢复保护未就绪，未开启强冷");
    log(L"恢复保护进程已就绪"); return {};
}
int recoveryMain(int argc, wchar_t** argv) {
    if (argc != 6) return 2;
    HANDLE inherited[4]{};
    for (int i=0;i<4;++i) {
        wchar_t* end{}; const auto value = _wcstoui64(argv[i+2], &end, 10);
        DWORD flags{}; inherited[i] = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
        if (!value || !end || *end || !GetHandleInformation(inherited[i], &flags)) return 2;
    }
    Handle parent(inherited[0]), own(inherited[1]), legacy(inherited[2]), ready(inherited[3]);
    SetEvent(ready.get());
    if (WaitForSingleObject(parent.get(), INFINITE) != WAIT_OBJECT_0) return 3;
    LenovoBackend backend;
    for (int i=0;i<3;++i) {
        const auto restored = backend.set(false);
        if (restored.ok) { Sleep(150); const auto read=backend.read(); log(L"保护进程已发送恢复命令；read_ok=" + std::to_wstring(read.ok) + L"; raw=" + std::to_wstring(read.value)); return 0; }
        Sleep(150);
    }
    log(L"保护进程恢复失败，请检查联想驱动状态"); return 1;
}
}
