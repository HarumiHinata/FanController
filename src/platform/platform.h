#pragma once
#include <windows.h>
#include <string>
#include "core/controller.h"
namespace fan {
std::wstring executablePath();
std::wstring dataDirectory();
void log(const std::wstring& message);
Result startupStatus(bool& enabled);
Result setStartup(bool enabled);
bool closeToTray();
void setCloseToTray(bool enabled);
Result ensureRecoveryGuard(HANDLE& process);
int recoveryMain(int argc, wchar_t** argv);
}
