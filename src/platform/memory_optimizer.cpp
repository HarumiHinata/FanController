#include "platform/memory_optimizer_internal.h"
#include <shellapi.h>
#include <sddl.h>
#include <objbase.h>
#include <atomic>
#include <cwchar>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <thread>
#include <vector>

namespace fan::memory {
namespace {
class Handle {
public:
    HANDLE value;
    explicit Handle(HANDLE h=nullptr):value(h) {}
    ~Handle() { if(valid()) CloseHandle(value); }
    bool valid() const { return value && value!=INVALID_HANDLE_VALUE; }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
struct Local { HLOCAL value{}; ~Local() { if(value) LocalFree(value); } };
constexpr std::uint32_t Magic=0x464D4F50,Version=1;
struct Packet { std::uint32_t magic{Magic},version{Version}; GUID request{}; Report report{}; };
static_assert(sizeof(Packet)<256);
Report failure(DWORD error,Status status=Status::Failed) { Report r; r.error=error; r.status=status; return r; }
std::wstring image(HANDLE process) {
    std::wstring path(32768,L'\0'); DWORD size=static_cast<DWORD>(path.size());
    if(!QueryFullProcessImageNameW(process,0,path.data(),&size)) return {};
    path.resize(size); return path;
}
std::wstring pipeName(const std::wstring& id) { return L"\\\\.\\pipe\\FanController.Memory."+id; }
bool requestGuid(const wchar_t* text,GUID& guid) {
    if(!text||wcslen(text)!=38||text[0]!=L'{'||text[37]!=L'}') return false;
    return SUCCEEDED(CLSIDFromString(text,&guid));
}
bool parsePid(const wchar_t* text,DWORD& pid) {
    if(!text||!*text) return false;
    std::uint64_t value{};
    for(const wchar_t* p=text;*p;++p) { if(*p<L'0'||*p>L'9') return false; value=value*10+(*p-L'0'); if(value>MAXDWORD) return false; }
    pid=static_cast<DWORD>(value); return pid!=0;
}
// Every overlapped operation is cancelled and drained before its stack storage dies.
DWORD await(HANDLE file,OVERLAPPED& operation,HANDLE cancel,HANDLE process,ULONGLONG deadline,bool processMayExit=false) {
    const ULONGLONG now=GetTickCount64();
    HANDLE handles[]{operation.hEvent,cancel,process};
    const DWORD count=process&&!processMayExit?3:2;
    const DWORD wait=WaitForMultipleObjects(count,handles,FALSE,now>=deadline?0:static_cast<DWORD>(deadline-now));
    if(wait==WAIT_OBJECT_0) return ERROR_SUCCESS;
    const DWORD error=wait==WAIT_OBJECT_0+1?ERROR_CANCELLED:wait==WAIT_OBJECT_0+2?ERROR_PROCESS_ABORTED:wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
    CancelIoEx(file,&operation); DWORD ignored{}; GetOverlappedResult(file,&operation,&ignored,TRUE); return error;
}
DWORD transfer(HANDLE pipe,void* data,DWORD bytes,bool write,HANDLE cancel,HANDLE process,ULONGLONG deadline,bool processMayExit=false) {
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr)); if(!event.valid()) return GetLastError();
    OVERLAPPED operation{}; operation.hEvent=event.value; DWORD transferred{};
    const BOOL done=write?WriteFile(pipe,data,bytes,&transferred,&operation):ReadFile(pipe,data,bytes,&transferred,&operation);
    if(!done) {
        const DWORD error=GetLastError(); if(error!=ERROR_IO_PENDING) return error;
        const DWORD waited=await(pipe,operation,cancel,process,deadline,processMayExit); if(waited) return waited;
        if(!GetOverlappedResult(pipe,&operation,&transferred,FALSE)) return GetLastError();
    }
    return transferred==bytes?ERROR_SUCCESS:ERROR_INVALID_DATA;
}
class NativeBackend final : public detail::Backend {
    using NtSet=LONG(NTAPI*)(ULONG,PVOID,ULONG);
    using Convert=ULONG(NTAPI*)(LONG);
    NtSet nt_{}; Convert convert_{};
    HANDLE token_{};
    TOKEN_PRIVILEGES previous_{};
    DWORD previousSize_{sizeof(previous_)};
    bool adjusted_{};
    HANDLE parent_,pipe_;
public:
    NativeBackend(HANDLE parent,HANDLE pipe):parent_(parent),pipe_(pipe) {}
    ~NativeBackend() override { finish(); }
    DWORD prepare() override {
        const auto module=GetModuleHandleW(L"ntdll.dll");
        nt_=module?reinterpret_cast<NtSet>(GetProcAddress(module,"NtSetSystemInformation")):nullptr;
        convert_=module?reinterpret_cast<Convert>(GetProcAddress(module,"RtlNtStatusToDosError")):nullptr;
        if(!nt_||!convert_) return ERROR_NOT_SUPPORTED;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES,&token_)) return GetLastError();
        TOKEN_ELEVATION elevation{}; DWORD size{};
        if(!GetTokenInformation(token_,TokenElevation,&elevation,sizeof(elevation),&size)) return GetLastError();
        if(!elevation.TokenIsElevated) return ERROR_ELEVATION_REQUIRED;
        TOKEN_PRIVILEGES privilege{}; privilege.PrivilegeCount=1; privilege.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
        if(!LookupPrivilegeValueW(nullptr,L"SeProfileSingleProcessPrivilege",&privilege.Privileges[0].Luid)) return GetLastError();
        SetLastError(ERROR_SUCCESS);
        const BOOL ok=AdjustTokenPrivileges(token_,FALSE,&privilege,sizeof(previous_),&previous_,&previousSize_);
        const DWORD error=GetLastError();
        adjusted_=ok!=FALSE && error==ERROR_SUCCESS;
        return !ok?(error?error:ERROR_ACCESS_DENIED):error;
    }
    bool available(std::uint64_t& bytes) override {
        MEMORYSTATUSEX m{sizeof(m)};
        if(!GlobalMemoryStatusEx(&m)) return false;
        bytes=m.ullAvailPhys; return true;
    }
    std::uint32_t apply(std::uint32_t command) override { return static_cast<std::uint32_t>(nt_(80,&command,sizeof(command))); }
    DWORD translate(std::uint32_t status) override { return convert_?convert_(static_cast<LONG>(status)):ERROR_GEN_FAILURE; }
    DWORD finish() override {
        DWORD error{};
        if(token_) {
            if(adjusted_) {
                SetLastError(ERROR_SUCCESS);
                if(!AdjustTokenPrivileges(token_,FALSE,&previous_,previousSize_,nullptr,nullptr)) error=GetLastError();
                else error=GetLastError();
            }
            CloseHandle(token_); token_=nullptr; adjusted_=false;
        }
        return error;
    }
    bool connected() override {
        return WaitForSingleObject(parent_,0)==WAIT_TIMEOUT && PeekNamedPipe(pipe_,nullptr,0,nullptr,nullptr,nullptr)!=FALSE;
    }
};
}
namespace detail {
Report run(Backend& backend) {
    Report report; const auto start=GetTickCount64();
    if(backend.available(report.before)) report.samples|=1;
    report.error=backend.prepare();
    if(!report.error) {
        report.status=Status::Complete;
        for(std::uint32_t command=2;command<=4;++command) {
            if(!backend.connected()) { report.status=Status::Cancelled; report.error=ERROR_CANCELLED; break; }
            const auto step=GetTickCount64();
            report.ntstatus=backend.apply(command);
            report.stageStatus[command-2]=report.ntstatus;
            report.stageMs[command-2]=static_cast<std::uint32_t>(GetTickCount64()-step);
            if(report.ntstatus) { report.status=Status::Failed; report.failedCommand=command; report.error=backend.translate(report.ntstatus); break; }
            ++report.completed;
        }
    }
    report.restoreError=backend.finish();
    if(report.restoreError && report.status==Status::Complete) { report.status=Status::Failed; report.error=report.restoreError; }
    if(backend.available(report.after)) report.samples|=2;
    report.elapsedMs=GetTickCount64()-start;
    return report;
}
bool valid(const Report& r) {
    if(r.status!=Status::Complete && r.status!=Status::Failed && r.status!=Status::Cancelled) return false;
    if(r.completed>3||r.samples>3||r.before>INT64_MAX||r.after>INT64_MAX) return false;
    if(r.status==Status::Complete && (r.completed!=3||r.ntstatus||r.error||r.restoreError)) return false;
    if(r.failedCommand && (r.failedCommand!=r.completed+2||r.failedCommand>4||!r.ntstatus)) return false;
    for(std::uint32_t i=0;i<r.completed;++i) if(r.stageStatus[i]) return false;
    return true;
}
Report request(const std::wstring& executable,HANDLE cancel,bool elevate,DWORD timeoutMs) {
    if(WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT) return failure(ERROR_CANCELLED,Status::Cancelled);
    GUID guid{}; if(FAILED(CoCreateGuid(&guid))) return failure(ERROR_GEN_FAILURE);
    wchar_t id[40]{}; StringFromGUID2(guid,id,40); const auto name=pipeName(id);
    Handle token; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value)) return failure(GetLastError());
    DWORD bytes{}; GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes);
    std::vector<BYTE> buffer(bytes);
    if(!GetTokenInformation(token.value,TokenUser,buffer.data(),bytes,&bytes)) return failure(GetLastError());
    Local sid; if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,reinterpret_cast<LPWSTR*>(&sid.value))) return failure(GetLastError());
    const auto sddl=L"D:P(A;;GA;;;SY)(A;;GA;;;"+std::wstring(static_cast<wchar_t*>(sid.value))+L")";
    Local descriptor;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value),nullptr)) return failure(GetLastError());
    SECURITY_ATTRIBUTES security{sizeof(security),descriptor.value,FALSE};
    Handle pipe(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,sizeof(Packet),sizeof(Packet),0,&security));
    if(!pipe.valid()) return failure(GetLastError());
    Handle connected(CreateEventW(nullptr,TRUE,FALSE,nullptr)); if(!connected.valid()) return failure(GetLastError());
    OVERLAPPED connection{}; connection.hEvent=connected.value;
    const BOOL immediate=ConnectNamedPipe(pipe.value,&connection); const DWORD connectError=immediate?ERROR_SUCCESS:GetLastError();
    if(!immediate && connectError!=ERROR_IO_PENDING && connectError!=ERROR_PIPE_CONNECTED) return failure(connectError);
    Handle process;
    const auto args=L"--memory-optimize-worker "+std::wstring(id)+L" "+std::to_wstring(GetCurrentProcessId());
    DWORD launchError{};
    if(elevate) {
        const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        SHELLEXECUTEINFOW info{sizeof(info)};
        info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
        info.lpVerb=L"runas"; info.lpFile=executable.c_str(); info.lpParameters=args.c_str(); info.nShow=SW_HIDE;
        if(!ShellExecuteExW(&info)) launchError=GetLastError(); else process.value=info.hProcess;
        if(SUCCEEDED(com)) CoUninitialize();
    } else {
        // Test transport with a separate executable whose backend is entirely simulated.
        std::wstring command=L"\""+executable+L"\" "+args;
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION info{};
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)) launchError=GetLastError();
        else { process.value=info.hProcess; CloseHandle(info.hThread); }
    }
    if(launchError||!process.valid()) {
        CancelIoEx(pipe.value,&connection); DWORD ignored{}; GetOverlappedResult(pipe.value,&connection,&ignored,TRUE);
        return failure(launchError?launchError:ERROR_INVALID_HANDLE,launchError==ERROR_CANCELLED?Status::Cancelled:Status::Failed);
    }
    const auto deadline=GetTickCount64()+timeoutMs;
    DWORD error{};
    if(!immediate && connectError==ERROR_IO_PENDING) {
        error=await(pipe.value,connection,cancel,process.value,deadline);
        DWORD ignored{}; if(!error&&!GetOverlappedResult(pipe.value,&connection,&ignored,FALSE)) error=GetLastError();
    }
    if(error) return failure(error,Status::Unconfirmed);
    ULONG client{};
    if(!GetNamedPipeClientProcessId(pipe.value,&client)||client!=GetProcessId(process.value)) return failure(ERROR_ACCESS_DENIED);
    Packet packet{}; packet.request=guid;
    error=transfer(pipe.value,&packet,sizeof(packet),true,cancel,process.value,deadline);
    if(error) return failure(error,Status::Unconfirmed);
    error=transfer(pipe.value,&packet,sizeof(packet),false,cancel,process.value,deadline,true);
    if(error) return failure(error,Status::Unconfirmed);
    if(packet.magic!=Magic||packet.version!=Version||!IsEqualGUID(packet.request,guid)||!valid(packet.report)) return failure(ERROR_INVALID_DATA,Status::Unconfirmed);
    return packet.report;
}
int serve(const wchar_t* requestId,const wchar_t* parentId,Backend* substitute) {
    GUID guid{}; DWORD pid{};
    if(!requestGuid(requestId,guid)||!parsePid(parentId,pid)) return 2;
    Handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
    const auto own=image(GetCurrentProcess()),source=parent.valid()?image(parent.value):L"";
    if(source.empty()||own.empty()||_wcsicmp(own.c_str(),source.c_str())!=0) return 3;
    const auto name=pipeName(requestId);
    Handle pipe(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
    if(!pipe.valid()) return 4;
    ULONG server{}; if(!GetNamedPipeServerProcessId(pipe.value,&server)||server!=pid) return 5;
    DWORD mode=PIPE_READMODE_MESSAGE; if(!SetNamedPipeHandleState(pipe.value,&mode,nullptr,nullptr)) return 6;
    Handle cancel(CreateEventW(nullptr,TRUE,FALSE,nullptr)); if(!cancel.valid()) return 7;
    Packet packet{}; const auto deadline=GetTickCount64()+120000;
    if(transfer(pipe.value,&packet,sizeof(packet),false,cancel.value,parent.value,deadline)||packet.magic!=Magic||packet.version!=Version||!IsEqualGUID(packet.request,guid)) return 8;
    NativeBackend native(parent.value,pipe.value);
    packet.report=run(substitute?*substitute:native);
    // Completion is acknowledged before returning: no FlushFileBuffers deadlock.
    if(transfer(pipe.value,&packet,sizeof(packet),true,cancel.value,parent.value,GetTickCount64()+5000)) return 9;
    return packet.report.status==Status::Complete?0:1;
}
}
int workerMain(const wchar_t* requestId,const wchar_t* parentId) {
    try { return detail::serve(requestId,parentId); } catch(...) { return 10; }
}
std::wstring describe(const Report& r) {
    if(r.status==Status::Demo) return L"演示：内存优化反馈预览，未执行清理";
    if(r.status==Status::Unconfirmed) return L"未确认内存整理完成（超时或结果通道中断）";
    if(r.status==Status::Cancelled) return L"已取消内存优化";
    if(r.status==Status::Complete) {
        if(r.samples!=3) return L"内存整理完成；可用内存读数暂不可用";
        const auto delta=static_cast<std::int64_t>(r.after)-static_cast<std::int64_t>(r.before);
        return L"内存整理完成，可用内存变化 "+std::wstring(delta>=0?L"+":L"")+std::to_wstring(delta/1048576)+L" MiB";
    }
    const auto prefix=r.completed?L"内存整理部分完成（"+std::to_wstring(r.completed)+L"/3），":L"内存整理失败，";
    if(r.error==ERROR_ELEVATION_REQUIRED||r.error==ERROR_PRIVILEGE_NOT_HELD||r.error==ERROR_NOT_ALL_ASSIGNED) return prefix+L"未取得所需管理员权限";
    return prefix+L"错误 "+std::to_wstring(r.error)+L"；详见诊断日志";
}
std::wstring diagnostics(const Report& r) {
    std::wostringstream out;
    out<<describe(r)<<L"; completed="<<r.completed<<L"; command="<<r.failedCommand<<L"; NTSTATUS=0x"<<std::hex<<std::setw(8)<<std::setfill(L'0')<<r.ntstatus
       <<std::dec<<L"; error="<<r.error<<L"; restoreError="<<r.restoreError<<L"; before="<<r.before<<L"; after="<<r.after<<L"; samples="<<r.samples<<L"; elapsedMs="<<r.elapsedMs;
    for(std::size_t i=0;i<3;++i) out<<L"; step"<<(i+2)<<L"=0x"<<std::hex<<r.stageStatus[i]<<std::dec<<L"/"<<r.stageMs[i]<<L"ms";
    return out.str();
}
struct Task::State {
    Handle cancel{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::mutex mutex;
    bool busy{};
    std::optional<Report> result;
};
Task::Task():state_(std::make_shared<State>()) {}
Task::~Task() { cancel(); }
void Task::cancel() { SetEvent(state_->cancel.value); }
bool Task::start(bool demo) {
    std::lock_guard lock(state_->mutex);
    if(state_->busy||state_->result||!state_->cancel.valid()) return false;
    ResetEvent(state_->cancel.value); state_->busy=true;
    try {
        // Shared state owns every resource; no HWND, App pointer, or UI callback is captured.
        std::thread([state=state_,demo] {
            Report result;
            try { result=demo?Report{Status::Demo}:detail::request(image(GetCurrentProcess()),state->cancel.value); }
            catch(...) { result=failure(ERROR_UNHANDLED_EXCEPTION); }
            std::lock_guard guard(state->mutex); state->result=result; state->busy=false;
        }).detach();
    } catch(...) { state_->busy=false; return false; }
    return true;
}
std::optional<Report> Task::take() {
    std::lock_guard lock(state_->mutex); auto result=state_->result; state_->result.reset(); return result;
}
}
