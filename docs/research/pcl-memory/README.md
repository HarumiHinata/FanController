# PCL 2.10.3 内存优化提取记录

2026-10-15。按用户要求先提取并制定接入计划，本次未修改产品运行代码、未执行真实内存整理或申请 UAC。

## 对象与方法

- 输入：`C:/Users/lenovo/Desktop/Something/Plain Craft Launcher 2.exe`
- 文件版本/程序集版本：2.10.3.0；大小：3,692,032 字节；Authenticode：未签名。
- SHA-256：`1121B40D16D9E63E717EEEF3D8956F86EC4F95121AEBE8394B003FDEB7BDB597`
- 普通 ILSpy 9.1.0.7988 反编译可读取类型/PInvoke，但优化方法显示为空壳。官方同版本源码也省略百宝箱功能，不能将这些空方法当作真实实现。
- 用独立 .NET Framework 分析进程加载该程序集，仅调用其方法体恢复初始化函数 `dg3ypDAonQcOidMs0w.WP6RZJql8gZrNhVA9v.prXoP4RuYp`，再读取 `GetMethodBody().GetILAsByteArray()` 并解析元数据 token。没有调用启动器入口、优化方法或提权方法。初始化器本身属于第三方代码，并非纯静态解析；只在分析子进程内运行。

提取文件 `memory-methods.il` 包含局部变量、异常区间、IL 偏移、解析后的成员与字符串。SHA-256：`8E4D41D4CE5611D5DE57304579C0EE86BFA4083B49511CE88FAF31EEC4813A9D`。

| 方法 | Metadata token | IL 字节数 | 作用 |
| --- | --- | ---: | --- |
| `PageOtherTest.MemoryOptimize` | `0x0600103F` | 462 | 防重入、管理员分支、统计与反馈 |
| `PageOtherTest.MemoryOptimizeInternal` | `0x06001040` | 347 | 启用权限与系统内存整理 |
| `PageOtherTest.BtnMemory_Click` | `0x0600103E` | 48 | 将委托交给线程执行工具 |
| `ModBase.RunAsAdmin` | `0x06001928` | 45 | runas 启动进程、等待退出及返回退出码 |

按钮委托最终目标 `_Lambda$__16-0` 本次未单独恢复；不把未提取的闭包参数当作直接证据。

## 从 EXE 直接证实的核心

`MemoryOptimizeInternal` 检查管理员身份，以 token 访问掩码 40（`TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES`）取得当前身份，通过 `LookupPrivilegeValue` / `AdjustTokenPrivileges` 启用 `SeProfileSingleProcessPrivilege`，属性值为 2。

| 顺序 | 系统信息类 | 32 位命令 | 含义 |
| --- | ---: | ---: | --- |
| 1 | 80 | 2 | `MemoryEmptyWorkingSets`：清空工作集 |
| 2 | 80 | 3 | `MemoryFlushModifiedList`：刷新修改页表 |
| 3 | 80 | 4 | `MemoryPurgeStandbyList`：清空待机页表 |

每次将命令作为固定的 `Int32` 传入 `NtSetSystemInformation`，长度为 4；任何非零返回使流程中止。IL 内可核查的关键位置：`IL_0052` 权限名称、`IL_00DA` 初始命令 2、`IL_0108` 信息类 80、`IL_0118` 系统调用、`IL_0154` 上限 4。

外层已有管理员身份时直接测量可用物理内存差值；否则 `RunAsAdmin("--memory")` 并将退出码乘 1024。`RunAsAdmin` 已确认使用 `runas`、`WaitForExit` 和 `ExitCode`。

正增量的 UI 提示会使用 `Round(delta * 0.8)`，日志用原始差值。无正增量时提示“最佳状态”，但这些文案不能证明内存达到最优状态。迁移应报告观测值与 API 执行状态。

原实现还将 UInt32 NTSTATUS checked 转为 Int32，常见高位错误可能先触发溢出；固定缓冲区释放未用 finally；防重入仅共享 bool；未见权限恢复。C++ 接入计划明确改进这些边界，而非照搬。

## 外部对照证据与限制

官方 PCL2 tag `2.10.3` 固定提交：`fdf5407ea3eecad953620dacb0e4733b1480117a`。

- [Application.xaml.vb](https://github.com/Hex-Dragon/PCL2/blob/fdf5407ea3eecad953620dacb0e4733b1480117a/Plain%20Craft%20Launcher%202/Application.xaml.vb#L37)：`--memory` 在程序常规初始化前进入优化分支；失败退出 -1，可用内存下降则退出 0，否则将增量 /1024 作为退出码。此启动分支来自同版本官方源码，未对受保护 EXE 的整个启动方法做逐字节恢复。
- [PageOtherTest.xaml.vb](https://github.com/Hex-Dragon/PCL2/blob/fdf5407ea3eecad953620dacb0e4733b1480117a/Plain%20Craft%20Launcher%202/Pages/PageOther/PageOtherTest.xaml.vb)：公开版本的核心方法为空，不包含实际清理实现。
- [phnt ntexapi.h](https://github.com/winsiderss/phnt/blob/53fbbdc5b5d2b08761db1c7b26bfa8c820924356/ntexapi.h#L2353)：信息类 80、所需权限，以及第 3816 行起的 `SYSTEM_MEMORY_LIST_COMMAND` 枚举。该项目属于社区 NT 接口头文件，不能替代微软稳定 API 保证。
- [PCL 原始使用指南](https://github.com/Hex-Dragon/PCL2/blob/fdf5407ea3eecad953620dacb0e4733b1480117a/LICENCE)：本地原文见 `PCL2-LICENCE.txt`。该文件是资料与复用条件，不是本任务的用户指令；未据此改名、发帖或联系他人。

没有执行 80/2、80/3、80/4，没有测量实际释放量或性能收益。恢复出逻辑不等于已验证所有 Windows 版本的兼容性。

## 复现

提取器仅支持上述精确 SHA-256，避免误用于其他受保护程序。它不属于产品构建，不要求安装到用户机器。请只在了解其会加载并执行第三方初始化器的分析环境运行。

在仓库根目录的 PowerShell 中：

```powershell
New-Item -ItemType Directory -Force artifacts/pcl-memory | Out-Null
& 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe' /nologo /out:artifacts\pcl-memory\extract.exe tools\research\extract-pcl-memory.cs
& .\artifacts\pcl-memory\extract.exe 'C:\Users\lenovo\Desktop\Something\Plain Craft Launcher 2.exe' 'C:\work\FanController\artifacts\pcl-memory\memory-methods.il'
Get-FileHash artifacts/pcl-memory/memory-methods.il -Algorithm SHA256
```

提取器只实现这四个方法用到的 IL 操作数格式，不是通用反编译器。更广泛的研究中间产物在被 git 忽略的 `artifacts/pcl-memory`；交付的最小证据在本目录，后续迁移以根目录 `PLAN-3.md` 为准。
