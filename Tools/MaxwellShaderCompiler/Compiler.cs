using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Ryujinx.Graphics.Shader;
using Ryujinx.Graphics.Shader.Translation;

// Original feasibility facade: resolves the three linked GLSL
// resources without pulling in unrelated logging, message-pack, or IO packages.
namespace Ryujinx.Common {
    public static class EmbeddedResources {
        public static string ReadAllText(string name) {
            using Stream stream = typeof(EmbeddedResources).Assembly.GetManifestResourceStream(name.Replace('/', '.'))
                ?? throw new InvalidDataException("Embedded shader helper missing.");
            using var reader = new StreamReader(stream);
            return reader.ReadToEnd();
        }
    }
}

// This probe deliberately exposes only resource-free compute translation.
// Production needs explicit GPU state and descriptor snapshots, not defaults.
public static unsafe class TranslatorProbe {
    [UnmanagedCallersOnly(EntryPoint="satchel_shader_runtime_features")]
    public static int RuntimeFeatures() => (RuntimeFeature.IsDynamicCodeSupported ? 1 : 0) |
        (RuntimeFeature.IsDynamicCodeCompiled ? 2 : 0);
    sealed class Accessor(byte[] code) : IGpuAccessor {
        public ReadOnlySpan<ulong> GetCode(ulong address, int minimumSize) {
            if (address > (ulong)code.Length || minimumSize < 0 || (ulong)minimumSize > (ulong)code.Length - address)
                throw new InvalidDataException("Code read outside supplied shader.");
            return MemoryMarshal.Cast<byte, ulong>(code.AsSpan((int)address));
        }
        public SetBindingPair CreateConstantBufferBinding(int index) => throw new NotSupportedException();
        public SetBindingPair CreateImageBinding(int count, bool isBuffer) => throw new NotSupportedException();
        public SetBindingPair CreateStorageBufferBinding(int index) => throw new NotSupportedException();
        public SetBindingPair CreateTextureBinding(int count, bool isBuffer) => throw new NotSupportedException();
        public int QuerySamplerArrayLengthFromPool() => 0;
        public int QueryTextureArrayLengthFromBuffer(int slot) => 0;
        public int QueryTextureArrayLengthFromPool() => 0;
    }
    [UnmanagedCallersOnly(EntryPoint="satchel_shader_probe")]
    public static int Translate(byte* code, int size, byte* output, int capacity) {
        try {
            if (code == null || output == null || size < 32 || size > 65536 || (size & 7) != 0 || capacity < 0) return -1;
            byte[] input = new ReadOnlySpan<byte>(code, size).ToArray();
            var context = Translator.CreateContext(0, new Accessor(input), new TranslationOptions(TargetLanguage.Spirv, TargetApi.Vulkan, TranslationFlags.Compute));
            byte[] result = context.Translate().BinaryCode;
            if (result.Length > capacity) return -2;
            result.CopyTo(new Span<byte>(output, capacity));
            return result.Length;
        } catch { return -3; }
    }
}
