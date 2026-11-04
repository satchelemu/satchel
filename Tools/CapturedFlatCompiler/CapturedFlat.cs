using System;
using System.Diagnostics;
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Threading;
using Ryujinx.Common.Memory;
using Ryujinx.Graphics.Shader;
using Ryujinx.Graphics.Shader.Translation;
using SatchelProfile;
public static unsafe partial class CapturedFlat {
 [StructLayout(LayoutKind.Sequential)] public struct Source {public byte* Sph;public byte* Code;public uint SphSize,CodeSize;}
 [StructLayout(LayoutKind.Sequential)] public struct State {public uint Known,Single,Viewport,Depth,YNegate,Origin,AlphaAlways,AlphaCoverage,Feedback;public uint ExtraKnown,Topology,EarlyZ,AlphaDither,ProgramPointSize,PointSize,AlphaRef,DrawParams,DualBlend,HalveId,FloatVertexMask,FloatOutputMask,Provoking;}
 [StructLayout(LayoutKind.Sequential)] public struct Host {public ulong Known,Values;public uint Alignment,PhysicalBgra,AdmittedFormat,CurrentFormat;}
 [StructLayout(LayoutKind.Sequential)] public struct Input {public uint Version,Size;public Source Vertex,Fragment;public State State;public Host Host;public uint MaxWork,TimeoutNs;public delegate* unmanaged<void*,int> Cancel;public void* Context;public fixed uint Reserved[4];}
 [StructLayout(LayoutKind.Sequential)] public struct Module {public uint Version,Size,Words;public fixed uint Code[4096];}
 [StructLayout(LayoutKind.Sequential)] public struct Facts {public uint Operations,Attributes,Varying,DeclaredUniforms,ActiveUniforms,IdBound;}
 [StructLayout(LayoutKind.Sequential)] public struct Pair {public uint Version,Size,DynamicCode,WorkUsed;public Module Vertex,Fragment;public Facts VertexFacts,FragmentFacts;}
 sealed class Deadline : Exception {}
 static int Gate;
 static bool Range(void* p,nuint size)=>p!=null && (nuint)p<=nuint.MaxValue-size;
 static bool Overlap(void* a,nuint an,void* b,nuint bn)=>(nuint)a<(nuint)b+bn && (nuint)b<(nuint)a+an;
 static byte[] Copy(Source s,Pair* output) {
  if(s.SphSize!=80||s.CodeSize==0||s.CodeSize>1024||(s.CodeSize&7)!=0||!Range(s.Sph,80)||!Range(s.Code,s.CodeSize)||Overlap(output,(nuint)sizeof(Pair),s.Sph,80)||Overlap(output,(nuint)sizeof(Pair),s.Code,s.CodeSize))throw new ArgumentException();
  var b=new byte[80+s.CodeSize];new ReadOnlySpan<byte>(s.Sph,80).CopyTo(b);new ReadOnlySpan<byte>(s.Code,(int)s.CodeSize).CopyTo(b.AsSpan(80));return b;
 }
 static void CheckState(Input input,ulong sourceUnused) {
  var s=input.State;var h=input.Host;
  if(s.Known!=255||s.Single!=1||s.Viewport!=1||s.Depth!=1||s.YNegate!=0||s.Origin!=1||s.AlphaAlways!=1||s.AlphaCoverage!=0||s.Feedback!=0||s.ExtraKnown!=4095||s.Topology!=3||s.EarlyZ!=0||s.AlphaDither!=0||s.ProgramPointSize!=0||s.PointSize!=0x3f800000||s.AlphaRef!=0||s.DrawParams!=0||s.DualBlend!=0||s.HalveId!=0||s.FloatVertexMask==0||(s.FloatVertexMask&~255u)!=0||s.FloatOutputMask!=1||s.Provoking!=0)throw new NotSupportedException();
  if((sourceUnused!=0&&sourceUnused!=6)||(h.Known&sourceUnused)!=0||(h.Known|sourceUnused)!=16777215UL||(h.Values&~h.Known)!=0||h.Alignment<16||h.Alignment>65536||(h.Alignment&(h.Alignment-1))!=0||h.PhysicalBgra!=1||h.AdmittedFormat!=1||h.CurrentFormat!=1||!Has(h,3))throw new NotSupportedException();
 }
 static bool Has(Host h,int bit)=>((h.Values>>bit)&1)!=0;
 sealed class Accessor(byte[] bytes,State state,Host host):IGpuAccessor {
  public void Log(string message)=>throw new NotSupportedException();
  // The owned compilation mode is native VS/FS, never offline fixed geometry capture.
  public bool QueryGeometryInputFixed() =>false;
  public uint ConstantBuffer1Read(int offset) =>throw new NotSupportedException();
  public ReadOnlySpan<ulong> GetCode(ulong address, int minimumSize) {WorkBudget.Charge(BoundKind.CodeFetch);if((address&7)!=0||minimumSize<0||address>(ulong)bytes.Length||(ulong)minimumSize>(ulong)bytes.Length-address)throw new NotSupportedException();return MemoryMarshal.Cast<byte,ulong>(bytes.AsSpan((int)address));}
  public SetBindingPair CreateConstantBufferBinding(int index) =>throw new NotSupportedException();
  public SetBindingPair CreateImageBinding(int count, bool isBuffer) =>throw new NotSupportedException();
  public SetBindingPair CreateStorageBufferBinding(int index) =>throw new NotSupportedException();
  public SetBindingPair CreateTextureBinding(int count, bool isBuffer) =>throw new NotSupportedException();
  public int CreateExtraSet() =>throw new NotSupportedException();
  public int QueryComputeLocalSizeX() =>throw new NotSupportedException();
  public int QueryComputeLocalSizeY() =>throw new NotSupportedException();
  public int QueryComputeLocalSizeZ() =>throw new NotSupportedException();
  public int QueryComputeLocalMemorySize() =>throw new NotSupportedException();
  public int QueryComputeSharedMemorySize() =>throw new NotSupportedException();
  public uint QueryConstantBufferUse() =>0;
  public GpuGraphicsState QueryGraphicsState() {WorkBudget.Check();return new(false,InputTopology.Triangles,false,TessPatchType.Triangles,TessSpacing.EqualSpacing,false,false,false,false,false,BitConverter.UInt32BitsToSingle(state.PointSize),AlphaTestOp.Always,0f,default(Array32<AttributeType>),false,default(Array8<AttributeType>),false,false,true,false);}
  public bool QueryHasConstantBufferDrawParameters() =>false;
  public bool QueryHasUnalignedStorageBuffer() =>throw new NotSupportedException();
  public int QueryHostGatherBiasPrecision() =>throw new NotSupportedException();
  public bool QueryHostReducedPrecision() =>Has(host,0);
  public bool QueryHostHasFrontFacingBug() =>Has(host,1);
  public bool QueryHostHasVectorIndexingBug() =>Has(host,2);
  public int QueryHostStorageBufferOffsetAlignment() =>checked((int)host.Alignment);
  public int QueryHostSubgroupSize() =>throw new NotSupportedException();
  public bool QueryHostSupportsBgraFormat() =>Has(host,3);
  public bool QueryHostSupportsFragmentShaderInterlock() =>Has(host,4);
  public bool QueryHostSupportsFragmentShaderOrderingIntel() =>Has(host,5);
  public bool QueryHostSupportsGeometryShader() =>Has(host,6);
  public bool QueryHostSupportsGeometryShaderPassthrough() =>Has(host,7);
  public bool QueryHostSupportsImageLoadFormatted() =>Has(host,8);
  public bool QueryHostSupportsLayerVertexTessellation() =>Has(host,9);
  public bool QueryHostSupportsNonConstantTextureOffset() =>Has(host,10);
  public bool QueryHostSupportsScaledVertexFormats() =>Has(host,11);
  public bool QueryHostSupportsSeparateSampler() =>Has(host,12);
  public bool QueryHostSupportsShaderBallot() =>Has(host,13);
  public bool QueryHostSupportsShaderBarrierDivergence() =>Has(host,14);
  public bool QueryHostSupportsShaderFloat64() =>Has(host,15);
  public bool QueryHostSupportsShaderNonUniformIndexing() =>Has(host,16);
  public bool QueryHostSupportsSnormBufferTextureFormat() =>Has(host,17);
  public bool QueryHostSupportsTextureGatherOffsets() =>Has(host,18);
  public bool QueryHostSupportsTextureShadowLod() =>Has(host,19);
  public bool QueryHostSupportsTransformFeedback() =>Has(host,20);
  public bool QueryHostSupportsViewportIndexVertexTessellation() =>Has(host,21);
  public bool QueryHostSupportsViewportMask() =>Has(host,22);
  public bool QueryHostSupportsDepthClipControl() =>Has(host,23);
  public int QuerySamplerArrayLengthFromPool() =>0;
  public SamplerType QuerySamplerType(int handle, int cbufSlot = -1) =>throw new NotSupportedException();
  public void RecordTexelFetch1D(int handle) =>throw new NotSupportedException();
  public int QueryTextureArrayLengthFromBuffer(int slot) =>throw new NotSupportedException();
  public int QueryTextureArrayLengthFromPool() =>0;
  public bool QueryTextureCoordNormalized(int handle, int cbufSlot = -1) =>throw new NotSupportedException();
  public TextureFormat QueryTextureFormat(int handle, int cbufSlot = -1) =>throw new NotSupportedException();
  public bool QueryTransformFeedbackEnabled() =>false;
  public ReadOnlySpan<byte> QueryTransformFeedbackVaryingLocations(int bufferIndex) =>throw new NotSupportedException();
  public int QueryTransformFeedbackStride(int bufferIndex) =>throw new NotSupportedException();
  public void RegisterTexture(int handle, int cbufSlot) =>throw new NotSupportedException();
 }
 static Module Encode(byte[] bytes,out SpirvFacts facts) {
  facts=SpirvProof.Prove(bytes);if(facts.Words>4096||facts.Bound>256)throw new BudgetExceeded(BoundKind.SpirvWords);
  Module m=default;m.Version=1;m.Size=(uint)sizeof(Module);m.Words=(uint)facts.Words;
  for(int i=0;i<facts.Words;i++){WorkBudget.Check();m.Code[i]=BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(i*4,4));}return m;
 }
 static Facts Describe(SourceFacts source,SpirvFacts output)=>new(){Operations=(uint)source.Operations,Attributes=source.AttributeMask,Varying=source.VaryingMask,DeclaredUniforms=(uint)output.DeclaredUniforms,ActiveUniforms=(uint)output.ActiveUniforms,IdBound=(uint)output.Bound};
 [UnmanagedCallersOnly(EntryPoint="satchel_captured_flat_compile")]
 public static int Compile(Input* input,Pair* output,uint size) {try{return Managed(input,output,size);}catch{return 7;}}
 [MethodImpl(MethodImplOptions.NoInlining)]
 static int Managed(Input* input,Pair* output,uint size,ulong sourceUnused=0) {
  bool locked=false,budget=false;
  try {
   if(!Range(input,(nuint)sizeof(Input))||!Range(output,(nuint)sizeof(Pair))||size!=sizeof(Pair)||((nuint)output&3)!=0||Overlap(input,(nuint)sizeof(Input),output,(nuint)sizeof(Pair)))return 1;
   if(Interlocked.CompareExchange(ref Gate,1,0)!=0)return 6;locked=true;
   Input v=*input;if(v.Version!=1||v.Size!=sizeof(Input)||v.MaxWork==0||v.MaxWork>65536||v.TimeoutNs==0||v.TimeoutNs>1000000000)return 1;
   for(int i=0;i<4;i++)if(v.Reserved[i]!=0)return 1;
   CheckState(v,sourceUnused);var vs=Copy(v.Vertex,output);var fs=Copy(v.Fragment,output);
   var started=Stopwatch.GetTimestamp();
   WorkBudget.Begin(32768);budget=true;
   var cancel=v.Cancel;var context=v.Context;uint timeout=v.TimeoutNs;
   WorkBudget.Poll=()=>{if(cancel!=null&&cancel(context)!=0)throw new BudgetCancelled();if(Stopwatch.GetElapsedTime(started).TotalNanoseconds>=timeout)throw new Deadline();};
   WorkBudget.TotalLimit=(int)v.MaxWork;WorkBudget.Check();
   var snapshot=new AdmittedPair(vs,fs);
   // Only StrictSource-accepted user-varying IPA/direct attribute programs can
   // reach here. No FrontFacing reads or constant-buffer dynamic indexing exist.
   // These two normalized query answers are NOT observed device bug facts.
   v.Host.Known|=sourceUnused;
   if((snapshot.VertexFacts.AttributeMask&~v.State.FloatVertexMask)!=0)throw new NotSupportedException();
   var options=new TranslationOptions(TargetLanguage.Spirv,TargetApi.Vulkan,TranslationFlags.None);
   var fc=Translator.CreateContext(0,new Accessor(snapshot.Fragment,v.State,v.Host),options);
   var vc=Translator.CreateContext(0,new Accessor(snapshot.Vertex,v.State,v.Host),options);vc.SetNextStage(fc);vc.SetLastInVertexPipeline();
   byte[] vb=vc.Translate().BinaryCode,fb=fc.Translate().BinaryCode;
   Pair result=default;result.Version=1;result.Size=(uint)sizeof(Pair);result.DynamicCode=(RuntimeFeature.IsDynamicCodeSupported?1u:0)|(RuntimeFeature.IsDynamicCodeCompiled?2u:0);
   if(result.DynamicCode!=0)throw new NotSupportedException();
   result.Vertex=Encode(vb,out var vf);result.Fragment=Encode(fb,out var ff);result.VertexFacts=Describe(snapshot.VertexFacts,vf);result.FragmentFacts=Describe(snapshot.FragmentFacts,ff);result.WorkUsed=(uint)WorkBudget.Total;
   WorkBudget.Check();*output=result;return 0;
  }catch(ArgumentException){return 1;}catch(NotSupportedException){return 2;}catch(BudgetExceeded){return 3;}catch(BudgetCancelled){return 4;}catch(Deadline){return 5;}catch{return 7;}
  finally{if(budget)WorkBudget.End();if(locked)Volatile.Write(ref Gate,0);}
 }
}
