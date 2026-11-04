using System;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;

// Versioned in-memory boundary. No paths, subprocesses or GPU work cross it.
public static unsafe class GameShaderFacade
{
    [UnmanagedCallersOnly(EntryPoint = "satchel_shader_translate_v1")]
    public static int Translate(byte* code, int codeSize, byte* job, int jobSize, byte** result, int* resultSize)
    {
        if (result == null || resultSize == null) return -1;
        *result = null;
        *resultSize = 0;
        if (code == null || job == null || codeSize < 32 || codeSize > 16 * 1024 * 1024 ||
            (codeSize & 7) != 0 || jobSize <= 0 || jobSize > 1024 * 1024) return -1;
        try
        {
            var snapshot = JsonNode.Parse(new ReadOnlySpan<byte>(job, jobSize)) as JsonObject;
            if (snapshot == null || snapshot["stage"] is not JsonValue stage ||
                !stage.TryGetValue<string>(out var name) ||
                name is not ("vs" or "ps" or "cs" or "gs" or "hs" or "ds") ||
                snapshot["key"] is not JsonValue key || !key.TryGetValue<string>(out var identity) ||
                identity.Length != 16 || !IsHex(identity)) return -1;
            // The upstream job filename is irrelevant to the native boundary.
            snapshot.Remove("code");
            var translated = GameShaderTranslator.Translate(new ReadOnlySpan<byte>(code, codeSize).ToArray(), snapshot);
            int size = checked(16 + translated.Spirv.Length + translated.Resources.Length + translated.Reader.Length);
            if (size > 64 * 1024 * 1024) return -2;
            byte* output = (byte*)NativeMemory.Alloc((nuint)size);
            if (output == null) return -2;
            var bytes = new Span<byte>(output, size);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bytes, 1);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bytes[4..], translated.Spirv.Length);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bytes[8..], translated.Resources.Length);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bytes[12..], translated.Reader.Length);
            translated.Spirv.CopyTo(bytes[16..]);
            translated.Resources.CopyTo(bytes[(16 + translated.Spirv.Length)..]);
            translated.Reader.CopyTo(bytes[(16 + translated.Spirv.Length + translated.Resources.Length)..]);
            *result = output;
            *resultSize = size;
            return 0;
        }
        catch { return -3; } // Do not export diagnostics containing shader instructions.
    }

    private static bool IsHex(string text)
    {
        foreach (char c in text) if (!Uri.IsHexDigit(c)) return false;
        return true;
    }

    [UnmanagedCallersOnly(EntryPoint = "satchel_shader_translate_free")]
    public static void Free(void* result) => NativeMemory.Free(result);
}
