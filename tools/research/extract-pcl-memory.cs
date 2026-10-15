using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Reflection.Emit;
using System.Collections.Generic;
using System.Text;
using System.Security.Cryptography;
// Research-only extractor for the exact user-supplied PCL 2.10.3.0 binary.
// Executes its method-body initialization routine in this process; never calls
// MemoryOptimize, MemoryOptimizeInternal, RunAsAdmin, or the application entry point.
// The IL decoder covers the operand forms used by the four selected methods only.
class Probe {
  static void Main(string[] args) {
    if(args.Length!=2) throw new ArgumentException("Usage: extract.exe <PCL.exe> <output.il>");
    string hash;
    using(var sha=SHA256.Create()) using(var input=File.OpenRead(args[0]))
      hash=BitConverter.ToString(sha.ComputeHash(input)).Replace("-", "");
    if(hash!="1121B40D16D9E63E717EEEF3D8956F86EC4F95121AEBE8394B003FDEB7BDB597")
      throw new InvalidOperationException("Input hash mismatch: only the investigated PCL binary is supported.");
    if(Path.GetFullPath(args[0]).Equals(Path.GetFullPath(args[1]),StringComparison.OrdinalIgnoreCase))
      throw new ArgumentException("Output must differ from input.");
    var asm=Assembly.LoadFile(Path.GetFullPath(args[0]));
    var init=asm.GetType("dg3ypDAonQcOidMs0w.WP6RZJql8gZrNhVA9v");
    if(init==null) foreach(var t in asm.GetTypes()) if(t.Name=="WP6RZJql8gZrNhVA9v") init=t;
    Console.WriteLine("Initializer: "+init);
    init.GetMethod("prXoP4RuYp",BindingFlags.NonPublic|BindingFlags.Public|BindingFlags.Static).Invoke(null,null);
    var output=new StringBuilder();
    var codes=new Dictionary<ushort,OpCode>();
    foreach(var f in typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static)) {var op=(OpCode)f.GetValue(null); codes[(ushort)op.Value]=op;}
    foreach(var spec in new[]{"PCL.PageOtherTest:MemoryOptimize","PCL.PageOtherTest:MemoryOptimizeInternal","PCL.PageOtherTest:BtnMemory_Click","PCL.ModBase:RunAsAdmin"}) {
      var parts=spec.Split(':'); var name=parts[1];
      var m=asm.GetType(parts[0]).GetMethod(name,BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static);
      var body=m.GetMethodBody(); var il=body.GetILAsByteArray();
      output.AppendLine("METHOD "+m+" token=0x"+m.MetadataToken.ToString("X8"));
      foreach(var local in body.LocalVariables) output.AppendLine("LOCAL "+local.LocalIndex+": "+local.LocalType);
      foreach(var eh in body.ExceptionHandlingClauses) output.AppendLine("EH "+eh.Flags+" try="+eh.TryOffset+"+"+eh.TryLength+" handler="+eh.HandlerOffset+"+"+eh.HandlerLength);
      for(int p=0;p<il.Length;) {
        int offset=p; ushort v=il[p++]; if(v==0xfe) v=(ushort)(0xfe00|il[p++]); var op=codes[v]; string operand=""; int n=0;
        switch(op.OperandType) {
          case OperandType.InlineNone: break;
          case OperandType.ShortInlineI: operand=((sbyte)il[p]).ToString(); n=1; break;
          case OperandType.ShortInlineVar: operand=il[p].ToString(); n=1; break;
          case OperandType.InlineVar: operand=BitConverter.ToUInt16(il,p).ToString(); n=2; break;
          case OperandType.InlineI: operand=BitConverter.ToInt32(il,p).ToString(); n=4; break;
          case OperandType.InlineI8: operand=BitConverter.ToInt64(il,p).ToString(); n=8; break;
          case OperandType.ShortInlineR: operand=BitConverter.ToSingle(il,p).ToString(); n=4; break;
          case OperandType.InlineR: operand=BitConverter.ToDouble(il,p).ToString(); n=8; break;
          case OperandType.ShortInlineBrTarget: operand="IL_"+(p+1+(sbyte)il[p]).ToString("X4"); n=1; break;
          case OperandType.InlineBrTarget: operand="IL_"+(p+4+BitConverter.ToInt32(il,p)).ToString("X4"); n=4; break;
          default:
            int token=BitConverter.ToInt32(il,p); n=4;
            try { operand=op.OperandType==OperandType.InlineString ? "\""+m.Module.ResolveString(token).Replace("\r","\\r").Replace("\n","\\n")+"\"" : m.Module.ResolveMember(token).ToString(); }
            catch(Exception ex) {operand="TOKEN_"+token.ToString("X8")+" ("+ex.GetType().Name+")";} break;
        }
        p+=n; output.AppendLine("IL_"+offset.ToString("X4")+": "+op.Name+" "+operand);
      }
      output.AppendLine(); Console.WriteLine("Extracted "+name+", "+il.Length+" IL bytes");
    }
    File.WriteAllText(args[1],output.ToString(),new UTF8Encoding(false));
  }
}
