# FanController 0.3.0

针对 Lenovo ideapad 720S-13ARR（81BR）、BIOS 6KCN40WW 的轻量 Windows 原生风扇控制器。

每次启动恢复原厂正常模式，支持强冷、安静、托盘、自启动设置及诊断日志。安静沿用参考软件每 5 秒发送正常命令的策略；只在温度低于 80°C 且读数新鲜时可选，温度达到 80°C、读数失效或超过 25 秒未更新即恢复原厂自动。驱动没有第三个已验证的安静写入值。风扇 RPM 不在本版本范围内。

面板每 10 秒更新 CPU／GPU 负载与频率、温度及内存使用情况。独立 CPU 温度接口在本机不可用；经用户选择，暂以同芯片核显适配器温度显示 CPU 温度，并用于安静保护。此数值不能证明 CPU 核心或最高热点温度。保存最近 24 小时数据，可查看 1／6／24 小时曲线。右上角齿轮包含偏好勾选项、诊断和退出；旁边的内存优化图标暂不执行操作。

运行 `FanController.exe`。关闭窗口默认留在托盘，完全退出请选择齿轮或托盘菜单中的“退出”。`--background` 用于登录后托盘启动；`--demo` 是不连接风扇硬件的界面预览。手动档位不会保存到下次启动。

开发环境：Visual Studio 2022 C++ 工具、Windows SDK、CMake 3.24+。

```powershell
cmake --preset windows
cmake --build --preset release
ctest --preset release
```

日志位于 `%LOCALAPPDATA%\FanController\controller.log`，最多保留三份约 2 MiB 日志；历史位于同目录 `history-v2.bin`。偏好设置位于当前用户注册表 `HKCU\Software\FanController`。自启动由当前用户任务计划程序任务实现，不保存密码。不开启自启动开关时，不注册启动任务。

使用协议知识参考 Lenovo-Fan-Control（MIT），AMD ADL 头文件用于驱动监控。许可位于 `third_party/notices`。验证结果见 `docs/verification-report.md`。
