# 本机与参考软件研究记录

日期：2026-09-27。使用本机只读系统查询、EXE 静态字符串与文件校验、上游公开源码研究。外部源码和说明仅作为研究材料，项目需求以用户请求及三项确认答复为准。

本轮没有启动参考 EXE，没有打开 EnergyDrv 设备，也没有发送风扇控制命令。实际调速可用性的证据来自用户确认，后续实现仍需本机集成验证。

## 1. 本机快照

| 字段 | 结果 |
| --- | --- |
| 制造商／机型 | LENOVO / 81BR |
| 产品名称 | Lenovo ideapad 720S-13ARR |
| CPU | AMD Ryzen 7 2700U with Radeon Vega Mobile Gfx |
| GPU | AMD Radeon RX Vega 10 Graphics |
| 系统报告物理内存 | 7,303,938,048 字节，约 6.80 GiB；此为系统报告值，不推断内存条标称容量 |
| 操作系统 | Microsoft Windows 10 家庭版，10.0.19045，64 位 |
| BIOS | 6KCN40WW，2018-08-17 |
| 驱动设备 | Lenovo ACPI-Compliant Virtual Power Controller，状态 OK |
| 驱动服务 | ACPIVPC，Running |
| 驱动文件 | C:\Windows\system32\drivers\AcpiVpc.sys |
| 驱动版本 | 15.11.29.70 |
| 驱动签名 | IsSigned = true；Microsoft Windows Hardware Compatibility Publisher |
| 检查时运行的同名风扇工具 | 未发现 LenovoFanControl / FanController / FanControl / NitroSense 匹配进程；不据此排除其他控制软件 |

开发工具预检：已发现 Git，以及具备 C++ 工具组件的 Visual Studio 2022 Community。当前普通 PowerShell PATH 未找到 cl、CMake、Ninja；开发阶段先检查 VS 开发者环境和随附工具，再决定是否补充依赖。

## 2. 用户提供的 EXE

| 字段 | 结果 |
| --- | --- |
| 路径 | C:\Users\lenovo\Desktop\Development Tools\LenovoFanControl-x64.exe |
| 文件大小 | 151,017 字节 |
| 格式 | PE32+ / AMD64 |
| SHA-256 | `3C85875B285F34FF01A51ECF24D45EB394E1B9A45EA55CDE2545670FD3C5718E` |
| Authenticode | 未签名；与原厂驱动签名状态分开记录 |
| 内嵌标识 | Lenovo Fan Control v0.4 |
| 内嵌项目地址 | https://github.com/jiarandiana0307/Lenovo-Fan-Control |

GitHub Releases API 返回的 v0.4 x64 资产大小及 SHA-256 与用户文件一致。因此可以确认它与该发布资产字节一致；该校验不等同于证明二进制由源码可复现构建。[发布页](https://github.com/jiarandiana0307/Lenovo-Fan-Control/releases/tag/v0.4)、[发布元数据](https://api.github.com/repos/jiarandiana0307/Lenovo-Fan-Control/releases/tags/v0.4)

研究固定到 v0.4 提交 `c808102a0eddc6fecbe86dafa164188f750beac6`。检索时 main 为 `83f1c8fb83ee0cf3e4d82bd2694cc9ec72ac0432`，已逐文件比较 `fanctrl.c`、`fanctrl.h`、`lenovo_fan_control.c`、`LICENSE`，这四份文件与 v0.4 内容相同。

## 3. 已查明的能力

该项目通过 `\\.\EnergyDrv` 调用 Lenovo 原厂驱动的除尘相关功能。正常和强冷是底层命令，低速通过周期性发送正常命令实现；强冷通过约 8.98 秒周期重置维持。用户层看到的三种策略不等同于三种可精确设定的 RPM。[固定版本控制源码](https://github.com/jiarandiana0307/Lenovo-Fan-Control/blob/c808102a0eddc6fecbe86dafa164188f750beac6/src/fanctrl.c)

上游说明明确没有精确转速控制。接口返回值用于区分正常／强冷状态，没有提供真实 RPM、温度或每个风扇的独立控制能力。本机具备匹配原厂驱动，用户也确认参考工具有效，因而这条控制路径值得作为首版基础，但仍需验证新程序的行为。[项目说明](https://github.com/jiarandiana0307/Lenovo-Fan-Control)

原程序默认启动为高速，用户要求新软件每次新启动为原厂自动；必须主动修改启动策略。上游采用 MIT 许可，使用其代码时应保留相应版权和许可文本。[主程序源码](https://github.com/jiarandiana0307/Lenovo-Fan-Control/blob/c808102a0eddc6fecbe86dafa164188f750beac6/src/lenovo_fan_control.c)、[许可](https://github.com/jiarandiana0307/Lenovo-Fan-Control/blob/c808102a0eddc6fecbe86dafa164188f750beac6/LICENSE)

## 4. 源码观察与新实现要求

以下是静态代码观察及由此提出的工程要求，并非在用户电脑上重现的运行故障。

| 观察 | 影响 | 新实现要求 |
| --- | --- | --- |
| 驱动读写没有完整检查 DeviceIoControl 成功与返回长度 | 可能把调用失败当成成功，或使用无效返回值 | 校验结果、长度、错误码与数据有效性 |
| `read_state()` 初次调用会先写正常命令 | 查询存在控制副作用 | 将显式初始化与查询分开 |
| 开启低速／高速会创建 pthread，未见 join 或 detach | 有线程资源未回收的隐患 | 固定工作线程，退出时回收，模式切换只更新状态 |
| 线程共享标志使用 volatile | 不能由此保证线程间同步 | 使用单线程所有权、事件及必要的同步原语 |
| 等待模式匹配的内层循环缺少完整取消和重试上限 | 驱动异常时可能持续重试，退出不及时 | 所有等待可取消，重试有界，错误可见 |
| 低速策略缺少温度监控 | 高负载下持续降速没有应用侧保护 | 验证温度源、阈值与失效回退后开放 |
| 多种状态切换和退出可能与工作线程交叉 | 可能在恢复之后又收到手动写入 | 所有硬件操作串行化，恢复优先 |

以上观察来自 [fanctrl.c](https://github.com/jiarandiana0307/Lenovo-Fan-Control/blob/c808102a0eddc6fecbe86dafa164188f750beac6/src/fanctrl.c) 与 [lenovo_fan_control.c](https://github.com/jiarandiana0307/Lenovo-Fan-Control/blob/c808102a0eddc6fecbe86dafa164188f750beac6/src/lenovo_fan_control.c)。项目应复用已研究的协议知识，并重做生命周期、错误处理和资源管理。

## 5. 实现阶段必须补充的证据

| 待确认事项 | 验证方式 | 对计划的影响 |
| --- | --- | --- |
| NORMAL 是否确实交回原厂自动控温 | 对照原厂基线，观察恢复后的风速及负载响应 | 决定“原厂自动”能否作为已确认状态展示 |
| 本机实际可用模式 | 有间隔地测试正常、强冷及具备保护条件后的低速 | 决定发布界面中的可用卡片 |
| 状态值与初始化 | 校验回执、长度、冷启动和唤醒行为 | 决定模式确认策略；不能仅依赖一次基线值 |
| 温度／RPM 数据来源 | 优先核查原厂或系统可用接口，验证含义、更新与占用 | 决定监测功能及低速保护能否实现 |
| 进程停止后的恢复 | 在受控条件下停止维持命令、退出和终止进程并观察 | 决定是否需要辅助恢复机制或限制某种模式 |
| 普通用户与提升权限 | 分别验证驱动访问与自启动 | 决定启动任务权限和提权路径 |
| 低资源预算 | 本机 Release 采样与长期测试 | 决定界面刷新频率、缓存和模块取舍 |

精确 RPM、连续百分比调速、独立 CPU／GPU 风扇控制没有足够证据；不通过试写未知 EC 地址推断。后续若确需这些能力，应作为单独的硬件研究扩展，与本机稳定首版分开估算。
