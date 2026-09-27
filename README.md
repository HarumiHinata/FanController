# FanController 0.1.0

针对 Lenovo ideapad 720S-13ARR（81BR）、BIOS 6KCN40WW 的轻量 Windows 原生风扇控制器。

每次启动恢复原厂正常模式，支持强冷、托盘、自启动设置及诊断日志。低速因缺少可靠温度保护暂不可用；接口未提供温度、RPM 和连续百分比调速。当前为验证版本，长期稳定性验收结果见 docs/verification-report.md。

运行 `FanController.exe`。关闭窗口默认留在托盘，完全退出请选择“恢复自动并退出”。启动参数仅支持 `--background`（托盘启动）和 `--demo`（模拟界面，不连接硬件）。手动档位不会保存到下次启动。

开发环境：Visual Studio 2022 C++ 工具、Windows SDK、CMake 3.24+。

```powershell
cmake --preset windows
cmake --build --preset release
ctest --preset release
```

日志位于 `%LOCALAPPDATA%\FanController\controller.log`，最多保留三份约 2 MiB 日志。偏好设置位于当前用户注册表 `HKCU\Software\FanController`。自启动由当前用户任务计划程序任务实现，不保存密码。不开启自启动开关时，不注册启动任务。

使用协议知识参考 Lenovo-Fan-Control（MIT），许可位于 third_party/notices/Lenovo-Fan-Control.txt。
