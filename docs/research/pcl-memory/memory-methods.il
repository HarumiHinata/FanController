METHOD Void MemoryOptimize(Boolean) token=0x0600103F
LOCAL 0: System.Int64
LOCAL 1: System.String
LOCAL 2: System.Exception
LOCAL 3: System.Exception
EH Clause try=62+11 handler=73+46
EH Finally try=62+57 handler=119+7
EH Clause try=184+24 handler=208+108
EH Finally try=184+132 handler=316+7
IL_0000: ldsfld Boolean configRegistry
IL_0005: brfalse IL_001D
IL_000A: ldarg.0 
IL_000B: brfalse IL_01CD
IL_0010: ldstr "内存优化尚未结束，请稍等！"
IL_0015: ldc.i4.0 
IL_0016: ldc.i4.1 
IL_0017: call Void Hint(System.String, HintType, Boolean)
IL_001C: ret 
IL_001D: ldc.i4.1 
IL_001E: stsfld Boolean configRegistry
IL_0023: call Boolean IsAdmin()
IL_0028: brfalse IL_00A8
IL_002D: call Microsoft.VisualBasic.Devices.Computer AwakeRule()
IL_0032: callvirt Microsoft.VisualBasic.Devices.ComputerInfo get_Info()
IL_0037: callvirt UInt64 get_AvailablePhysicalMemory()
IL_003C: conv.ovf.i8.un 
IL_003D: stloc.0 
IL_003E: ldarg.0 
IL_003F: call Void MemoryOptimizeInternal(Boolean)
IL_0044: leave IL_007E
IL_0049: dup 
IL_004A: call Void SetProjectError(System.Exception)
IL_004F: stloc.2 
IL_0050: ldloc.2 
IL_0051: ldstr "内存优化失败"
IL_0056: ldarg.0 
IL_0057: brtrue IL_0062
IL_005C: ldc.i4.2 
IL_005D: br IL_0063
IL_0062: ldc.i4.3 
IL_0063: ldstr "出现错误"
IL_0068: call Void Log(System.Exception, System.String, LogLevel, System.String)
IL_006D: call Void ClearProjectError()
IL_0072: leave IL_01CD
IL_0077: ldc.i4.0 
IL_0078: stsfld Boolean configRegistry
IL_007D: endfinally 
IL_007E: call Microsoft.VisualBasic.Devices.Computer AwakeRule()
IL_0083: callvirt Microsoft.VisualBasic.Devices.ComputerInfo get_Info()
IL_0088: callvirt UInt64 get_AvailablePhysicalMemory()
IL_008D: newobj Void .ctor(UInt64)
IL_0092: ldloc.0 
IL_0093: newobj Void .ctor(Int64)
IL_0098: call System.Decimal Subtract(System.Decimal, System.Decimal)
IL_009D: call Int64 ToInt64(System.Decimal)
IL_00A2: stloc.0 
IL_00A3: br IL_014B
IL_00A8: ldstr "[Test] 没有管理员权限，将以命令行方式进行内存优化"
IL_00AD: ldc.i4.0 
IL_00AE: ldstr "出现错误"
IL_00B3: call Void Log(System.String, LogLevel, System.String)
IL_00B8: ldstr "--memory"
IL_00BD: call Int32 RunAsAdmin(System.String)
IL_00C2: conv.i8 
IL_00C3: ldc.i4 1024
IL_00C8: conv.i8 
IL_00C9: mul.ovf 
IL_00CA: stloc.0 
IL_00CB: leave IL_0143
IL_00D0: dup 
IL_00D1: call Void SetProjectError(System.Exception)
IL_00D6: stloc.3 
IL_00D7: ldloc.3 
IL_00D8: ldstr "命令行形式内存优化失败"
IL_00DD: ldc.i4.2 
IL_00DE: ldstr "出现错误"
IL_00E3: call Void Log(System.Exception, System.String, LogLevel, System.String)
IL_00E8: ldarg.0 
IL_00E9: brfalse IL_0132
IL_00EE: ldc.i4.5 
IL_00EF: newarr System.String
IL_00F4: dup 
IL_00F5: ldc.i4.0 
IL_00F6: ldstr "获取管理员权限失败，请尝试右键 PCL，选择 "
IL_00FB: stelem.ref 
IL_00FC: dup 
IL_00FD: ldc.i4.1 
IL_00FE: ldsfld Char _TagSerializer
IL_0103: call System.String ToString(Char)
IL_0108: stelem.ref 
IL_0109: dup 
IL_010A: ldc.i4.2 
IL_010B: ldstr "以管理员身份运行"
IL_0110: stelem.ref 
IL_0111: dup 
IL_0112: ldc.i4.3 
IL_0113: ldsfld Char structSerializer
IL_0118: call System.String ToString(Char)
IL_011D: stelem.ref 
IL_011E: dup 
IL_011F: ldc.i4.4 
IL_0120: ldstr "！"
IL_0125: stelem.ref 
IL_0126: call System.String Concat(System.String[])
IL_012B: ldc.i4.2 
IL_012C: ldc.i4.1 
IL_012D: call Void Hint(System.String, HintType, Boolean)
IL_0132: call Void ClearProjectError()
IL_0137: leave IL_01CD
IL_013C: ldc.i4.0 
IL_013D: stsfld Boolean configRegistry
IL_0142: endfinally 
IL_0143: ldloc.0 
IL_0144: ldc.i4.0 
IL_0145: conv.i8 
IL_0146: blt IL_01CD
IL_014B: call Microsoft.VisualBasic.Devices.Computer AwakeRule()
IL_0150: callvirt Microsoft.VisualBasic.Devices.ComputerInfo get_Info()
IL_0155: callvirt UInt64 get_AvailablePhysicalMemory()
IL_015A: conv.ovf.i8.un 
IL_015B: call System.String GetString(Int64)
IL_0160: stloc.1 
IL_0161: ldstr "[Test] 内存优化完成，可用内存改变量：{0}，大致剩余内存：{1}"
IL_0166: ldloc.0 
IL_0167: call System.String GetString(Int64)
IL_016C: ldloc.1 
IL_016D: call System.String Format(System.String, System.Object, System.Object)
IL_0172: ldc.i4.0 
IL_0173: ldstr "出现错误"
IL_0178: call Void Log(System.String, LogLevel, System.String)
IL_017D: ldloc.0 
IL_017E: ldc.i4.0 
IL_017F: conv.i8 
IL_0180: ble IL_01B5
IL_0185: ldarg.0 
IL_0186: brfalse IL_01CD
IL_018B: ldstr "内存优化完成，可用内存增加了 {0}，目前剩余内存 {1}！"
IL_0190: ldloc.0 
IL_0191: conv.r8 
IL_0192: ldc.r8 0.8
IL_019B: mul 
IL_019C: call Double Round(Double)
IL_01A1: conv.ovf.i8 
IL_01A2: call System.String GetString(Int64)
IL_01A7: ldloc.1 
IL_01A8: call System.String Format(System.String, System.Object, System.Object)
IL_01AD: ldc.i4.1 
IL_01AE: ldc.i4.1 
IL_01AF: call Void Hint(System.String, HintType, Boolean)
IL_01B4: ret 
IL_01B5: ldarg.0 
IL_01B6: brfalse IL_01CD
IL_01BB: ldstr "内存优化完成，已经优化到了最佳状态，目前剩余内存 {0}！"
IL_01C0: ldloc.1 
IL_01C1: call System.String Format(System.String, System.Object)
IL_01C6: ldc.i4.0 
IL_01C7: ldc.i4.1 
IL_01C8: call Void Hint(System.String, HintType, Boolean)
IL_01CD: ret 

METHOD Void MemoryOptimizeInternal(Boolean) token=0x06001040
LOCAL 0: System.Security.Principal.WindowsIdentity
LOCAL 1: PCL.PageOtherTest+PrivilegeToken
LOCAL 2: System.String
LOCAL 3: System.String
LOCAL 4: System.IntPtr
LOCAL 5: System.Int32
LOCAL 6: System.Int32
LOCAL 7: System.Runtime.InteropServices.GCHandle
LOCAL 8: System.Int32
EH Finally try=45+142 handler=187+13
IL_0000: call Boolean IsAdmin()
IL_0005: brtrue IL_0015
IL_000A: ldstr "内存优化功能需要管理员权限！\r\n如果需要自动以管理员身份启动 PCL，可以右键 PCL，打开 属性 → 兼容性 → 以管理员身份运行此程序。"
IL_000F: newobj Void .ctor(System.String)
IL_0014: throw 
IL_0015: ldstr "[Test] 获取内存优化权限"
IL_001A: ldc.i4.0 
IL_001B: ldstr "出现错误"
IL_0020: call Void Log(System.String, LogLevel, System.String)
IL_0025: ldc.i4.s 40
IL_0027: call System.Security.Principal.WindowsIdentity GetCurrent(System.Security.Principal.TokenAccessLevels)
IL_002C: stloc.0 
IL_002D: ldloca.s 1
IL_002F: initobj PCL.PageOtherTest+PrivilegeToken
IL_0035: ldloca.s 1
IL_0037: ldc.i4.1 
IL_0038: stfld Int32 _InitializerRules
IL_003D: ldloca.s 1
IL_003F: ldc.i4.0 
IL_0040: conv.i8 
IL_0041: stfld Int64 _ProcessRules
IL_0046: ldloca.s 1
IL_0048: ldc.i4.2 
IL_0049: stfld Int32 m_RefRules
IL_004E: ldnull 
IL_004F: stloc.2 
IL_0050: ldloca.s 2
IL_0052: ldstr "SeProfileSingleProcessPrivilege"
IL_0057: stloc.3 
IL_0058: ldloca.s 3
IL_005A: ldloca.s 1
IL_005C: ldflda Int64 _ProcessRules
IL_0061: call Boolean LookupPrivilegeValue(System.String ByRef, System.String ByRef, Int64 ByRef)
IL_0066: brfalse IL_009C
IL_006B: ldloc.0 
IL_006C: callvirt IntPtr get_Token()
IL_0071: ldc.i4.0 
IL_0072: ldloca.s 1
IL_0074: ldloc.1 
IL_0075: call Int32 SizeOf[PrivilegeToken](PrivilegeToken)
IL_007A: ldsfld IntPtr Zero
IL_007F: stloc.s 4
IL_0081: ldloca.s 4
IL_0083: ldc.i4.0 
IL_0084: stloc.s 5
IL_0086: ldloca.s 5
IL_0088: call Boolean AdjustTokenPrivileges(IntPtr, Boolean, PrivilegeToken ByRef, Int32, IntPtr ByRef, Int32 ByRef)
IL_008D: brfalse IL_009C
IL_0092: call Int32 GetLastWin32Error()
IL_0097: brfalse IL_00B6
IL_009C: ldstr "获取内存优化权限失败（错误代码：{0}）"
IL_00A1: call Int32 GetLastWin32Error()
IL_00A6: box System.Int32
IL_00AB: call System.String Format(System.String, System.Object)
IL_00B0: newobj Void .ctor(System.String)
IL_00B5: throw 
IL_00B6: leave IL_00C8
IL_00BB: ldloc.0 
IL_00BC: brfalse IL_00C7
IL_00C1: ldloc.0 
IL_00C2: callvirt Void Dispose()
IL_00C7: endfinally 
IL_00C8: ldarg.0 
IL_00C9: brfalse IL_00DA
IL_00CE: ldstr "正在进行内存优化……"
IL_00D3: ldc.i4.0 
IL_00D4: ldc.i4.1 
IL_00D5: call Void Hint(System.String, HintType, Boolean)
IL_00DA: ldc.i4.2 
IL_00DB: stloc.s 6
IL_00DD: ldstr "[Test] 内存优化操作 {0} 开始"
IL_00E2: ldloc.s 6
IL_00E4: box System.Int32
IL_00E9: call System.String Format(System.String, System.Object)
IL_00EE: ldc.i4.0 
IL_00EF: ldstr "出现错误"
IL_00F4: call Void Log(System.String, LogLevel, System.String)
IL_00F9: ldloc.s 6
IL_00FB: box System.Int32
IL_0100: ldc.i4.3 
IL_0101: call System.Runtime.InteropServices.GCHandle Alloc(System.Object, System.Runtime.InteropServices.GCHandleType)
IL_0106: stloc.s 7
IL_0108: ldc.i4.s 80
IL_010A: ldloca.s 7
IL_010C: call IntPtr AddrOfPinnedObject()
IL_0111: ldloc.s 6
IL_0113: call Int32 SizeOf[Int32](Int32)
IL_0118: call UInt32 NtSetSystemInformation(Int32, IntPtr, Int32)
IL_011D: conv.ovf.i4.un 
IL_011E: stloc.s 8
IL_0120: ldloca.s 7
IL_0122: call Void Free()
IL_0127: ldloc.s 8
IL_0129: brfalse IL_014C
IL_012E: ldstr "内存优化操作 {0} 失败（错误代码：{1}）"
IL_0133: ldloc.s 6
IL_0135: box System.Int32
IL_013A: ldloc.s 8
IL_013C: box System.Int32
IL_0141: call System.String Format(System.String, System.Object, System.Object)
IL_0146: newobj Void .ctor(System.String)
IL_014B: throw 
IL_014C: ldloc.s 6
IL_014E: ldc.i4.1 
IL_014F: add.ovf 
IL_0150: stloc.s 6
IL_0152: ldloc.s 6
IL_0154: ldc.i4.4 
IL_0155: ble IL_00DD
IL_015A: ret 

METHOD Void BtnMemory_Click() token=0x0600103E
IL_0000: ldsfld System.Action $I16-0
IL_0005: brfalse IL_0014
IL_000A: ldsfld System.Action $I16-0
IL_000F: br IL_002A
IL_0014: ldsfld _Closure$__ $I
IL_0019: ldftn Void _Lambda$__16-0()
IL_001F: newobj Void .ctor(System.Object, IntPtr)
IL_0024: dup 
IL_0025: stsfld System.Action $I16-0
IL_002A: call Void RunInThread(System.Action)
IL_002F: ret 

METHOD Int32 RunAsAdmin(System.String) token=0x06001928
IL_0000: ldsfld System.String _PublisherSerializer
IL_0005: newobj Void .ctor(System.String)
IL_000A: dup 
IL_000B: ldstr "runas"
IL_0010: callvirt Void set_Verb(System.String)
IL_0015: dup 
IL_0016: ldarg.0 
IL_0017: callvirt Void set_Arguments(System.String)
IL_001C: call System.Diagnostics.Process Start(System.Diagnostics.ProcessStartInfo)
IL_0021: dup 
IL_0022: callvirt Void WaitForExit()
IL_0027: callvirt Int32 get_ExitCode()
IL_002C: ret 

