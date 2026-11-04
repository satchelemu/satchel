using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
public static unsafe partial class CapturedFlat {
 [StructLayout(LayoutKind.Sequential)] public struct InputV2 {public uint Version,Size;public Input Input;public ulong DeviceKnown,PolicyKnown,RequestedSourceUnused;}
 [StructLayout(LayoutKind.Sequential)] public struct PairV2 {public uint Version,Size;public ulong ProvenSourceUnused;public Pair Pair;}
 [UnmanagedCallersOnly(EntryPoint="satchel_captured_flat_compile_v2")]
 public static int CompileV2(InputV2* input,PairV2* output,uint size){try{return ManagedV2(input,output,size);}catch{return 7;}}
 [MethodImpl(MethodImplOptions.NoInlining)]
 static int ManagedV2(InputV2* input,PairV2* output,uint size){
  if(!Range(input,(nuint)sizeof(InputV2))||!Range(output,(nuint)sizeof(PairV2))||size!=sizeof(PairV2)||((nuint)input&7)!=0||((nuint)output&7)!=0||Overlap(input,(nuint)sizeof(InputV2),output,(nuint)sizeof(PairV2)))return 1;
  InputV2 v=*input;if(v.Version!=2||v.Size!=sizeof(InputV2))return 1;
  const ulong bugs=6,device=8,all=16777215;
  // Fixed current backend profile: actual BGRA/target fact, implemented-policy
  // facts and exactly two unknown driver-bug facts. No arbitrary unused mask.
  if(v.DeviceKnown!=device||v.PolicyKnown!=(all&~(device|bugs))||v.RequestedSourceUnused!=bugs||v.Input.Host.Known!=(v.DeviceKnown|v.PolicyKnown))return 2;
  for(int stage=0;stage<2;stage++){
   Source s=stage==0?v.Input.Vertex:v.Input.Fragment;
   if(s.SphSize!=80||s.CodeSize==0||s.CodeSize>1024||(s.CodeSize&7)!=0||!Range(s.Sph,80)||!Range(s.Code,s.CodeSize)||Overlap(output,(nuint)sizeof(PairV2),s.Sph,80)||Overlap(output,(nuint)sizeof(PairV2),s.Code,s.CodeSize))return 1;
  }
  PairV2 result=default;Input actual=v.Input;
  // Managed owns the SAME gate/budget/copies as v1 and proves source admission
  // before normalizing only the requested irrelevant queries. Reentry is BUSY.
  int status=Managed(&actual,&result.Pair,(uint)sizeof(Pair),bugs);if(status!=0)return status;
  result.Version=2;result.Size=(uint)sizeof(PairV2);result.ProvenSourceUnused=bugs;*output=result;return 0;
 }
}
