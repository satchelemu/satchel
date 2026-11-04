using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using SatchelProfile;
public static unsafe partial class CapturedFlat {
 [StructLayout(LayoutKind.Sequential)] public struct AdmissionInput {public uint Version,Size;public Source Vertex,Fragment;public uint MaxWork,TimeoutNs;public delegate* unmanaged<void*,int> Cancel;public void* Context;}
 [StructLayout(LayoutKind.Sequential)] public struct AdmissionStage {public uint Operations,Attributes,Varying;}
 [StructLayout(LayoutKind.Sequential)] public struct Admission {public uint Version,Size,Profile,NvnStageMask;public ulong SourceUnusedHost,SourceUnusedState;public AdmissionStage Vertex,Fragment;}
 [UnmanagedCallersOnly(EntryPoint="satchel_captured_flat_admit")]
 public static int Admit(AdmissionInput* input,Admission* output,uint size){try{return ManagedAdmission(input,output,size);}catch{return 7;}}
 static byte[] AdmissionCopy(Source s,Admission* output){
  if(s.SphSize!=80||s.CodeSize==0||s.CodeSize>1024||(s.CodeSize&7)!=0||!Range(s.Sph,80)||!Range(s.Code,s.CodeSize)||Overlap(output,(nuint)sizeof(Admission),s.Sph,80)||Overlap(output,(nuint)sizeof(Admission),s.Code,s.CodeSize))throw new ArgumentException();
  var b=new byte[80+s.CodeSize];new ReadOnlySpan<byte>(s.Sph,80).CopyTo(b);new ReadOnlySpan<byte>(s.Code,(int)s.CodeSize).CopyTo(b.AsSpan(80));return b;
 }
 [MethodImpl(MethodImplOptions.NoInlining)]
 static int ManagedAdmission(AdmissionInput* input,Admission* output,uint size){bool locked=false,budget=false;
  try{
   if(!Range(input,(nuint)sizeof(AdmissionInput))||!Range(output,(nuint)sizeof(Admission))||size!=sizeof(Admission)||((nuint)input&7)!=0||((nuint)output&7)!=0||Overlap(input,(nuint)sizeof(AdmissionInput),output,(nuint)sizeof(Admission)))return 1;
   if(Interlocked.CompareExchange(ref Gate,1,0)!=0)return 6;locked=true;
   var v=*input;if(v.Version!=1||v.Size!=sizeof(AdmissionInput)||v.MaxWork==0||v.MaxWork>65536||v.TimeoutNs==0||v.TimeoutNs>1000000000)return 1;
   var vs=AdmissionCopy(v.Vertex,output);var fs=AdmissionCopy(v.Fragment,output);var started=Stopwatch.GetTimestamp();WorkBudget.Begin(32768);budget=true;var cancel=v.Cancel;var context=v.Context;uint timeout=v.TimeoutNs;
   WorkBudget.Poll=()=>{if(cancel!=null&&cancel(context)!=0)throw new BudgetCancelled();if(Stopwatch.GetElapsedTime(started).TotalNanoseconds>=timeout)throw new Deadline();};WorkBudget.TotalLimit=(int)v.MaxWork;WorkBudget.Check();
   var source=new AdmittedPair(vs,fs);
   // The current typed backend requires at least one actual float vertex input.
   // Reject unserviceable requirements here rather than claim SetShaders success.
   if(source.VertexFacts.AttributeMask==0)throw new NotSupportedException();
   if(RuntimeFeature.IsDynamicCodeSupported||RuntimeFeature.IsDynamicCodeCompiled)throw new NotSupportedException();
   Admission result=new(){Version=1,Size=(uint)sizeof(Admission),Profile=1,NvnStageMask=3,SourceUnusedHost=6,SourceUnusedState=8,
    Vertex=new(){Operations=(uint)source.VertexFacts.Operations,Attributes=source.VertexFacts.AttributeMask,Varying=source.VertexFacts.VaryingMask},
    Fragment=new(){Operations=(uint)source.FragmentFacts.Operations,Attributes=source.FragmentFacts.AttributeMask,Varying=source.FragmentFacts.VaryingMask}};
   WorkBudget.Check();*output=result;return 0;
  }catch(ArgumentException){return 1;}catch(NotSupportedException){return 2;}catch(BudgetExceeded){return 3;}catch(BudgetCancelled){return 4;}catch(Deadline){return 5;}catch{return 7;}
  finally{if(budget)WorkBudget.End();if(locked)Volatile.Write(ref Gate,0);}
 }
}
