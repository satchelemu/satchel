using System;
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Threading;
using Ryujinx.Graphics.Shader;
using Ryujinx.Graphics.Shader.Translation;

// Independent original fixture. No imported shaders may reach this entry point.
public static unsafe class FixtureCompiler {
    [StructLayout(LayoutKind.Sequential)]
    public struct Input {
        public uint Version, StructSize;
        public byte* Code;
        public byte* ConstantBuffer;
        public uint CodeSize, ConstantBufferSize;
        public uint X, Y, Z, LocalMemory, SharedMemory, CbSlot, UniformBinding, StorageBinding;
        public uint StorageSize, StorageAlignment;
        public ulong StorageAddress;
        public fixed uint Reserved[4];
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct Buffer {
        public uint Kind, Set, Binding, Slot, AddressCbSlot, AddressCbOffset, Flags, RequiredBytes;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct Reflection {
        public uint Version, StructSize, SpirvSize, BufferCount, ActiveCount, X, Y, Z, Capability;
        public fixed uint Reserved[3];
        public Buffer Support, Uniform, Storage;
    }
    internal sealed class Unsupported : Exception { }
    private static readonly object Gate = new();
    // LDC.64 R0,c[3][0]; LDC.32 R2,c[3][12]; LDC.32 R3,c[3][16];
    // IADD R4,R2,R3; STG.E.32 [R0],R4; EXIT. Scheduling words occupy 0 and 32.
    // These words were composed from the pinned decoder's documented bitfields,
    // independently from the C test encoder. They contain no game code.
    internal static readonly ulong[] Code = {
        0, 0xef9500300007ff00, 0xef94003000c7ff02, 0xef9400300107ff03,
        0, 0x5c10000000370204, 0xeedc200000070004, 0xe30000000007000f
    };
    internal sealed class Snapshot {
        public readonly byte[] CodeBytes, CbBytes;
        public readonly Input State;
        public Snapshot(Input state) {
            if (state.Version != 1 || state.StructSize != sizeof(Input) ||
                state.Code == null || state.ConstantBuffer == null || state.CodeSize != 64 ||
                state.ConstantBufferSize != 32 || state.X != 1 || state.Y != 1 || state.Z != 1 ||
                state.LocalMemory != 0 || state.SharedMemory != 0 || state.CbSlot != 3 ||
                state.UniformBinding is < 1 or > 7 || state.StorageBinding is < 1 or > 7 ||
                state.UniformBinding == state.StorageBinding || state.StorageSize is < 16 or > 4096 ||
                (state.StorageSize & 3) != 0 || state.StorageAlignment is < 1 or > 65536 ||
                (state.StorageAlignment & (state.StorageAlignment - 1)) != 0 ||
                state.StorageAddress == 0 || state.StorageAddress >= (1UL << 40) ||
                state.StorageSize > (1UL << 40) - state.StorageAddress ||
                (state.StorageAddress & (state.StorageAlignment - 1)) != 0)
                throw new ArgumentException();
            for (int i=0; i<4; i++) if (state.Reserved[i] != 0) throw new ArgumentException();
            CodeBytes = new ReadOnlySpan<byte>(state.Code, 64).ToArray();
            CbBytes = new ReadOnlySpan<byte>(state.ConstantBuffer, 32).ToArray();
            for (int i=0; i<8; i++)
                if (BinaryPrimitives.ReadUInt64LittleEndian(CodeBytes.AsSpan(i*8, 8)) != Code[i])
                    throw new Unsupported();
            if (BinaryPrimitives.ReadUInt64LittleEndian(CbBytes) != state.StorageAddress ||
                BinaryPrimitives.ReadUInt32LittleEndian(CbBytes.AsSpan(8)) != state.StorageSize)
                throw new ArgumentException();
            for (int i=20; i<32; i++) if (CbBytes[i] != 0) throw new ArgumentException();
            // Never retain caller pointers in managed state.
            state.Code = null; state.ConstantBuffer = null; State = state;
        }
    }
    private sealed class Accessor(Snapshot snapshot) : IGpuAccessor {
        public ReadOnlySpan<ulong> GetCode(ulong address, int minimumSize) {
            if ((address & 7) != 0 || address > 64 || minimumSize < 0 ||
                (ulong)minimumSize > 64-address) throw new Unsupported();
            return MemoryMarshal.Cast<byte, ulong>(snapshot.CodeBytes.AsSpan((int)address));
        }
        public void Log(string message) => throw new Unsupported(); // Never emit translator payloads.
        public uint ConstantBuffer1Read(int offset) => throw new Unsupported();
        public SetBindingPair CreateConstantBufferBinding(int index) => index == 3
            ? new(0, (int)snapshot.State.UniformBinding) : throw new Unsupported();
        public SetBindingPair CreateStorageBufferBinding(int index) => index == 0
            ? new(0, (int)snapshot.State.StorageBinding) : throw new Unsupported();
        public SetBindingPair CreateImageBinding(int count, bool isBuffer) => throw new Unsupported();
        public SetBindingPair CreateTextureBinding(int count, bool isBuffer) => throw new Unsupported();
        public int QuerySamplerArrayLengthFromPool() => 0;
        public int QueryTextureArrayLengthFromBuffer(int slot) => throw new Unsupported();
        public int QueryTextureArrayLengthFromPool() => 0;
        public int QueryComputeLocalSizeX() => 1;
        public int QueryComputeLocalSizeY() => 1;
        public int QueryComputeLocalSizeZ() => 1;
        public int QueryComputeLocalMemorySize() => 0;
        public int QueryComputeSharedMemorySize() => 0;
        public uint QueryConstantBufferUse() => 1u << 3;
        public int QueryHostStorageBufferOffsetAlignment() => (int)snapshot.State.StorageAlignment;
        public bool QueryHasUnalignedStorageBuffer() => false;
        public int QueryHostSubgroupSize() => throw new Unsupported();
        public bool QueryHostSupportsBgraFormat() => false;
        public bool QueryHostSupportsFragmentShaderInterlock() => false;
        public bool QueryHostSupportsFragmentShaderOrderingIntel() => false;
        public bool QueryHostSupportsGeometryShader() => false;
        public bool QueryHostSupportsGeometryShaderPassthrough() => false;
        public bool QueryHostSupportsImageLoadFormatted() => false;
        public bool QueryHostSupportsLayerVertexTessellation() => false;
        public bool QueryHostSupportsNonConstantTextureOffset() => false;
        public bool QueryHostSupportsScaledVertexFormats() => false;
        public bool QueryHostSupportsSeparateSampler() => false;
        public bool QueryHostSupportsShaderBallot() => false;
        public bool QueryHostSupportsShaderBarrierDivergence() => false;
        public bool QueryHostSupportsShaderFloat64() => false;
        public bool QueryHostSupportsShaderNonUniformIndexing() => false;
        public bool QueryHostSupportsSnormBufferTextureFormat() => false;
        public bool QueryHostSupportsTextureGatherOffsets() => false;
        public bool QueryHostSupportsTextureShadowLod() => false;
        public bool QueryHostSupportsTransformFeedback() => false;
        public bool QueryHostSupportsViewportIndexVertexTessellation() => false;
        public bool QueryHostSupportsViewportMask() => false;
        public bool QueryHostSupportsDepthClipControl() => false;
    }
    internal static ShaderProgram Translate(Snapshot snapshot) {
        var context = Translator.CreateContext(0, new Accessor(snapshot),
            new TranslationOptions(TargetLanguage.Spirv, TargetApi.Vulkan, TranslationFlags.Compute));
        var program = context.Translate();
        var info = program.Info;
        if (info.Stage != ShaderStage.Compute || info.CBuffers.Count != 1 || info.SBuffers.Count != 1 ||
            info.Textures.Count != 0 || info.Images.Count != 0 || info.UsesFragCoord || info.UsesInstanceId ||
            info.UsesDrawParameters || info.UsesRtLayer || info.ClipDistancesWritten != 0)
            throw new Unsupported();
        var cb = info.CBuffers[0]; var sb = info.SBuffers[0];
        if (cb.Set != 0 || cb.Binding != snapshot.State.UniformBinding || cb.Slot != 3 ||
            sb.Set != 0 || sb.Binding != snapshot.State.StorageBinding || sb.Slot != 0 ||
            sb.SbCbSlot != 3 || sb.SbCbOffset != 0 || sb.Flags != BufferUsageFlags.Write)
            throw new Unsupported();
        return program;
    }
    [UnmanagedCallersOnly(EntryPoint="satchel_shader_fixture_compile")]
    public static int Compile(Input* input, byte* output, uint capacity, Reflection* reflection) {
        // NativeAOT 10.0.14 initializes its stack cookie on first reverse P/Invoke.
        // Keep this transition frame free of local structs/buffers: a protected
        // frame created before runtime entry otherwise observes a changed cookie.
        // NoInlining makes the large managed frame start AFTER runtime entry.
        try { return CompileManaged(input, output, capacity, reflection); }
        catch { return 3; }
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int CompileManaged(Input* input, byte* output, uint capacity, Reflection* reflection) {
        if (input == null || output == null || reflection == null || capacity > 65536 ||
            (nuint)output > nuint.MaxValue-capacity || (nuint)reflection > nuint.MaxValue-(nuint)sizeof(Reflection)) return 1;
        if ((nuint)output < (nuint)reflection+(nuint)sizeof(Reflection) &&
            (nuint)reflection < (nuint)output+capacity) return 1;
        bool acquired = false;
        try {
            if (!Monitor.TryEnter(Gate)) return 4;
            acquired = true;
            var snapshot = new Snapshot(*input);
            var program = Translate(snapshot);
            var normalized = FixtureSpirv.Normalize(program.BinaryCode, snapshot.State, out Reflection result);
            if (normalized.Length > capacity) return 2;
            normalized.CopyTo(new Span<byte>(output, (int)capacity));
            *reflection = result;
            return 0;
        } catch (ArgumentException) { return 1; }
          catch { return 3; }
        finally { if (acquired) Monitor.Exit(Gate); }
    }
#if SATCHEL_SHADER_FIXTURE_TESTING
    [UnmanagedCallersOnly(EntryPoint="satchel_shader_fixture_test_raw")]
    public static int Raw(Input* input, byte* output, uint capacity) {
        if (input == null || output == null || capacity > 65536) return -1;
        bool acquired = false;
        try {
            if (!Monitor.TryEnter(Gate)) return -4;
            acquired = true;
            byte[] result = Translate(new Snapshot(*input)).BinaryCode;
            if (result.Length > capacity) return -2;
            result.CopyTo(new Span<byte>(output, (int)capacity)); return result.Length;
        } catch { return -3; } finally { if (acquired) Monitor.Exit(Gate); }
    }
    [UnmanagedCallersOnly(EntryPoint="satchel_shader_fixture_test_normalize")]
    public static int TestNormalize(Input* input, byte* source, uint size, byte* output, uint capacity, Reflection* reflection) {
        if(input==null || source==null || output==null || reflection==null || size>16384 || capacity>65536)return 1;
        try {
            var snapshot=new Snapshot(*input);
            byte[] normalized=FixtureSpirv.Normalize(new ReadOnlySpan<byte>(source,(int)size).ToArray(),snapshot.State,out Reflection result);
            if(normalized.Length>capacity)return 2;
            normalized.CopyTo(new Span<byte>(output,(int)capacity));*reflection=result;return 0;
        } catch { return 3; }
    }
    [UnmanagedCallersOnly(EntryPoint="satchel_shader_fixture_test_log")]
    public static int TestLog() {
        try { ((IGpuAccessor)new Accessor(null)).Log("Original test diagnostic; not game data.");return 1; }
        catch(Unsupported){return 0;}
    }
#endif
}
