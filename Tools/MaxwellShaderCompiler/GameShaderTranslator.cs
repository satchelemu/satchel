// Adapted from pinned NX2Apple dmsh_xlate (MIT). See licenses and game-source-lock.json.

using Ryujinx.Common.Memory;
using Ryujinx.Graphics.Shader;
using Ryujinx.Graphics.Shader.Translation;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

[JsonSerializable(typeof(JsonObject))]
internal partial class ShaderJsonContext : JsonSerializerContext { }

internal static class GameShaderTranslator
{


    const int TexBase = 0x20, ImgBase = 0x120, ImgSlots = 8, SepTexBase = 0x168, SepTexSlot0 = 128, SampBase = 0x568;


    const int C2HandleWords = (SampBase + 8 * 128) / 8;

    sealed class Accessor : IGpuAccessor
    {
        readonly byte[] _code; readonly JsonObject _job; int _binding = 1; public readonly List<string> Notes = new();
        readonly byte[] _c1;
        public Accessor(byte[] code, JsonObject job, Dictionary<int, SamplerType> imageTypes = null, Dictionary<int, SamplerType> textureTypes = null) { if (job["geometry_topology"] != null) _binding = 40; _code = code; _job = job; _c1 = job["c1"] is JsonNode h ? Convert.FromHexString((string)h) : Array.Empty<byte>(); _imageTypes = imageTypes; _textureTypes = textureTypes; }



        readonly Dictionary<int, SamplerType> _imageTypes; public bool ImageQueryGuessed;
        public static Dictionary<int, SamplerType> ImageTypesOf(ShaderProgram p)
        {
            var types = new Dictionary<int, SamplerType>();
            foreach (var im in p.Info.Images) if (im.CbufSlot == 2 || im.CbufSlot < 0) types[im.HandleIndex] = im.Type & (SamplerType.Mask | SamplerType.Array);
            return types;
        }
        // Texture types by slot from the shader's other instructions, for a size query on a texture word the answer table does not type (pinned dmsh_xlate TextureTypesOf).
        readonly Dictionary<int, SamplerType> _textureTypes; readonly HashSet<int> _guessedHandles = new();
        public Dictionary<int, SamplerType> TextureTypesOf(ShaderProgram p)
        {
            var types = new Dictionary<int, SamplerType>(); var mixed = new HashSet<int>();
            foreach (var t in p.Info.Textures)
            {
                if (t.ArrayLength != 1) continue;
                SamplerType ty = t.Type & (SamplerType.Mask | SamplerType.Array | SamplerType.Multisample);
                int slot = Slot(t.HandleIndex, t.CbufSlot, false);
                if (slot < 0 || (ty == SamplerType.Texture2D && _guessedHandles.Contains(t.HandleIndex))) continue;
                if (types.TryGetValue(slot, out SamplerType seen) && seen != ty) mixed.Add(slot); else types[slot] = ty;
            }
            var guessed = new HashSet<int>(_guessedHandles.Select(h => Slot(h, -1, false)));
            return types.Where(kv => !mixed.Contains(kv.Key) && guessed.Contains(kv.Key) && kv.Value != SamplerType.Texture2D).ToDictionary(kv => kv.Key, kv => kv.Value);
        }
        public int QueryConstantBuffer1Size() => _c1.Length;
        // Ampere (NVN2) queries. ShaderRecord refuses NVN2 records, so these keep the pinned answers for jobs without the fields.
        public int[] QueryAmpereComputeC0Layout() => _job["nvn2_cs"] is JsonArray l ? l.Select(v => (int)v).ToArray() : null;
        public int[] QueryAmpereStageMemory() => _job["stage_memory"] is JsonArray l ? l.Select(v => (int)v).ToArray() : null;
        public uint ConstantBuffer1Read(int offset) { if (offset >= 0 && offset + 4 <= _c1.Length) return BitConverter.ToUInt32(_c1, offset); Notes.Add($"c1 read out of bounds +{offset:X} (c1 length {_c1.Length})"); return 0; }


        public void Log(string message) => Notes.Add("translator: " + message);
        public SetBindingPair CreateConstantBufferBinding(int index) => new(0, _binding++);
        public SetBindingPair CreateImageBinding(int count, bool isBuffer) { int b = _binding; _binding += count; return new(0, b); }
        public SetBindingPair CreateStorageBufferBinding(int index) => new(0, _binding++);
        public SetBindingPair CreateTextureBinding(int count, bool isBuffer) { int b = _binding; _binding += count; return new(0, b); }
        public ReadOnlySpan<ulong> GetCode(ulong address, int minimumSize)
        {
            // An address past the code is no code, as in the pinned accessor (an Ampere jump table ends there).
            if (address >= (ulong)_code.Length) return default;
            if (address > (ulong)_code.Length || minimumSize < 0 || (ulong)minimumSize > (ulong)_code.Length - address)
                throw new InvalidDataException("Code read outside supplied shader.");
            return MemoryMarshal.Cast<byte, ulong>(_code.AsSpan((int)address));
        }


        public int QuerySamplerArrayLengthFromPool() => 1 << 12;


        public int QueryTextureArrayLengthFromBuffer(int slot) => slot == 2 ? C2HandleWords : 32;
        public int QueryTextureArrayLengthFromPool() => 1 << 20;

        string Get(string table, int slot) => _job[table] is JsonObject t && t[slot.ToString()] is JsonNode n ? (n is JsonObject o ? (string)o["type"] : (string)n) : null;


        static int Slot(int handle, int cbufSlot, bool image)
        {
            if (cbufSlot >= 0 && (cbufSlot & 0xffff) != 2) return -1;
            (int w0, int w1, TextureHandleType ty) = TextureHandle.UnpackOffsets(handle);
            foreach (int w in ty == TextureHandleType.CombinedSampler || ty == TextureHandleType.SeparateConstantSamplerHandle ? new[] { w0 } : new[] { w0, w1 })
            {
                int b = w * 4;
                if (b % 8 != 0) continue;
                if (image && b >= ImgBase && b < ImgBase + 8 * ImgSlots) return (b - ImgBase) / 8;
                if (!image && b >= TexBase && b < SepTexBase) return (b - TexBase) / 8;
                if (!image && b >= SepTexBase && b < SampBase) return SepTexSlot0 + (b - SepTexBase) / 8;
            }
            return -1;
        }
        public SamplerType QuerySamplerType(int handle, int cbufSlot = -1)
        {
            int slot = Slot(handle, cbufSlot, false), islot = Slot(handle, cbufSlot, true);
            string t = (slot >= 0 ? Get("tex", slot) : null) ?? (islot >= 0 ? Get("img", islot) : null);
            if (t == null && islot >= 0 && cbufSlot < 0)
            {
                if (_imageTypes != null && _imageTypes.TryGetValue(handle, out SamplerType it)) { Notes.Add($"texture handle {handle:X} is image slot {islot}: type {it} from the shader's image instructions"); return it; }
                ImageQueryGuessed = true;
            }
            if (t == null && slot >= 0 && islot < 0)
            {
                if (_textureTypes != null && _textureTypes.TryGetValue(slot, out SamplerType tt)) { Notes.Add($"texture handle {handle:X} is texture slot {slot}: type {tt} from the shader's other instructions"); return tt; }
                _guessedHandles.Add(handle);
            }
            if (t == null) { Notes.Add($"texture handle {handle:X} (constant buffer {cbufSlot}, slot {slot}) has no type in the answer table, using 2D"); return SamplerType.Texture2D; }
            return t switch
            {
                "1d" => SamplerType.Texture1D, "1darray" => SamplerType.Texture1D | SamplerType.Array,
                "buffer" => SamplerType.TextureBuffer, "2d" => SamplerType.Texture2D, "2darray" => SamplerType.Texture2D | SamplerType.Array,
                "2dms" => SamplerType.Texture2D | SamplerType.Multisample, "2dmsarray" => SamplerType.Texture2D | SamplerType.Multisample | SamplerType.Array,
                "3d" => SamplerType.Texture3D, "cube" => SamplerType.TextureCube, "cubearray" => SamplerType.TextureCube | SamplerType.Array,
                _ => SamplerType.Texture2D,
            };
        }


        public readonly SortedSet<int> Fetch1D = new();
        public void RecordTexelFetch1D(int handle) { int slot = Slot(handle, -1, false); if (slot >= 0) Fetch1D.Add(slot); }
        public TextureFormat QueryTextureFormat(int handle, int cbufSlot = -1)
        {
            int slot = Slot(handle, cbufSlot, true);
            string f = _job["img"] is JsonObject t && slot >= 0 && t[slot.ToString()] is JsonObject o ? (string)o["fmt"] : null;
            if (f == null) { Notes.Add($"image handle {handle:X} slot {slot} has no format in the answer table, using R8G8B8A8Unorm"); return TextureFormat.R8G8B8A8Unorm; }
            return Enum.TryParse(f, true, out TextureFormat tf) ? tf : TextureFormat.R8G8B8A8Unorm;
        }
        int Local(int i) => _job["local"] is JsonArray a && a.Count > i ? (int)a[i] : 1;
        public int QueryComputeLocalSizeX() => Local(0);
        public int QueryComputeLocalSizeY() => Local(1);
        public int QueryComputeLocalSizeZ() => Local(2);
        public int QueryComputeSharedMemorySize() => _job["shared"] is JsonNode n ? (int)n : 0;
        static AttributeType Attr(string s) => s switch { "sint" => AttributeType.Sint, "uint" => AttributeType.Uint, _ => AttributeType.Float };
        public bool QueryGeometryInputFixed() => _job["geometry_topology"] != null;


        public bool QueryHostSupportsGeometryShaderPassthrough() => _job["geometry_topology"] == null;
        public GpuGraphicsState QueryGraphicsState()
        {
            Array32<AttributeType> attr = default; Array8<AttributeType> fout = default;
            if (_job["attr"] is JsonObject a) foreach (var kv in a) attr[int.Parse(kv.Key)] = Attr((string)kv.Value);
            if (_job["fout"] is JsonObject f) foreach (var kv in f) fout[int.Parse(kv.Key)] = Attr((string)kv.Value);
            bool earlyZ = _job["early_fragment_tests"] is JsonNode ez && (bool)ez;
            return new GpuGraphicsState(earlyZ, _job["geometry_topology"] is JsonNode gt ? (InputTopology)(int)gt : InputTopology.Triangles, TessCw(), TessDomain(), TessSpace(),
                false, false, false, false , false, 1f, AlphaTestOp.Always, 0f, attr, false, fout, false,
                false , true , false);
        }
        // Tessellation mode of a domain shader: the 3D engine's TessMode state, which NVN sets from the program and
        // the record does not carry in a decoded field yet. Request facts answer it; absent, the pinned defaults
        // (triangles, equal spacing, counter-clockwise) stay, so no other stage's translation changes.
        TessPatchType TessDomain() => _job["tess_domain"] is JsonNode d ? (string)d switch
        {
            "isolines" => TessPatchType.Isolines, "triangles" => TessPatchType.Triangles, "quads" => TessPatchType.Quads,
            _ => throw new InvalidOperationException("unknown tessellation domain")
        } : TessPatchType.Triangles;
        TessSpacing TessSpace() => _job["tess_spacing"] is JsonNode s ? (string)s switch
        {
            "equal" => TessSpacing.EqualSpacing, "fractional_even" => TessSpacing.FractionalEventSpacing, "fractional_odd" => TessSpacing.FractionalOddSpacing,
            _ => throw new InvalidOperationException("unknown tessellation spacing")
        } : TessSpacing.EqualSpacing;
        bool TessCw() => _job["tess_cw"] is JsonNode cw && (bool)cw;
        public bool QueryHostSupportsTextureShadowLod() => true;
        public bool QueryHostSupportsDepthClipControl() => false;

        public bool QueryHostReducedPrecision() => _job["reduced"] is JsonNode n && (bool)n;


        public bool QueryHostSupportsShaderFloat64() => !(_job["softfloat64"] is JsonNode n && (bool)n);
    }


    static bool UsesFloat64(byte[] spirv)
    {
        for (int i = 20; i + 12 <= spirv.Length; )
        {
            uint word = BitConverter.ToUInt32(spirv, i);
            if ((word & 0xffff) == 22 && (word >> 16) == 3 && BitConverter.ToUInt32(spirv, i + 8) == 64) return true;
            i += Math.Max(1, (int)(word >> 16)) * 4;
        }
        return false;
    }

    internal static (byte[] Spirv, byte[] Resources, byte[] Reader) Translate(byte[] code, JsonObject job)
    {
        string key = (string)job["key"];
        byte[] readerSpirv = Array.Empty<byte>();
                var acc = new Accessor(code, job);

                var flags = TranslationFlags.MetalImageAtomics | (job["debug"] is JsonNode dbg && !(bool)dbg ? 0 : TranslationFlags.DebugMode) | ((string)job["stage"] == "cs" ? TranslationFlags.Compute : 0) | ((string)job["arch"] == "sm86" ? TranslationFlags.Ampere : 0);
                var ctx = Translator.CreateContext(0, acc, new TranslationOptions(TargetLanguage.Spirv, TargetApi.Vulkan, flags));
                bool geometry = job["geometry_topology"] != null;
                ShaderProgram p = ctx.Translate(geometry);
                Dictionary<int, SamplerType> imageTypes = null, textureTypes = null;
                if (acc.ImageQueryGuessed && (imageTypes = Accessor.ImageTypesOf(p)).Count == 0) imageTypes = null;
                if ((textureTypes = acc.TextureTypesOf(p)).Count == 0) textureTypes = null;
                if (imageTypes != null || textureTypes != null)
                {
                    acc = new Accessor(code, job, imageTypes, textureTypes);
                    ctx = Translator.CreateContext(0, acc, new TranslationOptions(TargetLanguage.Spirv, TargetApi.Vulkan, flags));
                    p = ctx.Translate(geometry);
                }
                if (UsesFloat64(p.BinaryCode))
                {
                    job["softfloat64"] = true; acc = new Accessor(code, job, imageTypes, textureTypes);
                    ctx = Translator.CreateContext(0, acc, new TranslationOptions(TargetLanguage.Spirv, TargetApi.Vulkan, flags));
                    p = ctx.Translate(geometry); acc.Notes.Add("FP64 lowered to FP32 (DoubleToFloat)");
                }
                if (geometry)
                {

                    var reader = Translator.CreateContext(0, new Accessor(code, job, imageTypes, textureTypes), new TranslationOptions(TargetLanguage.Spirv, TargetApi.Vulkan, flags));
                    readerSpirv = reader.GenerateVertexPassthroughForCompute().BinaryCode;
                }
                byte[] spirv = VertexCounterLowering.Apply(code, (string)job["stage"], p.BinaryCode, acc.Notes);
                var res = new JsonObject
                {
                    ["key"] = key, ["stage"] = (string)job["stage"], ["notes"] = new JsonArray(acc.Notes.ConvertAll(n => (JsonNode)n).ToArray()),
                    ["cbufs"] = new JsonArray(), ["sbufs"] = new JsonArray(), ["textures"] = new JsonArray(), ["images"] = new JsonArray(),
                    ["geometry_stride"] = geometry ? ctx.GetResourceReservations().OutputSizePerInvocation : 0,
                    ["uses_frag_coord"] = p.Info.UsesFragCoord, ["frag_out_map"] = p.Info.FragmentOutputMap,
                };
                foreach (var b in p.Info.CBuffers) res["cbufs"].AsArray().Add((JsonNode)new JsonObject { ["binding"] = b.Binding, ["slot"] = b.Slot });
                foreach (var b in p.Info.SBuffers) res["sbufs"].AsArray().Add((JsonNode)new JsonObject { ["binding"] = b.Binding, ["slot"] = b.Slot, ["cb_slot"] = b.SbCbSlot, ["cb_offset"] = b.SbCbOffset, ["flags"] = (int)b.Flags });
                foreach (var (list, name) in new[] { (p.Info.Textures, "textures"), (p.Info.Images, "images") })
                    foreach (var t in list) res[name].AsArray().Add((JsonNode)new JsonObject { ["binding"] = t.Binding, ["type"] = (int)t.Type, ["format"] = t.Format.ToString(), ["cbuf_slot"] = t.CbufSlot, ["handle"] = t.HandleIndex, ["array"] = t.ArrayLength, ["separate"] = t.Separate, ["flags"] = (int)t.Flags });
                if (acc.Fetch1D.Count > 0) { var f1 = new JsonArray(); foreach (int h in acc.Fetch1D) f1.Add((JsonNode)h); res["fetch1d"] = f1; }
                return (spirv, System.Text.Encoding.UTF8.GetBytes(res.ToJsonString(new JsonSerializerOptions { WriteIndented = true, TypeInfoResolver = ShaderJsonContext.Default })), readerSpirv);
    }
}

static class VertexCounterLowering
{
    const string InstanceCodeHash = "F5F4D23149565ACD852F6B7091F64CC8CB3AA019CCB33BA3F88CED62C6D1FF7B";

    internal static byte[] Apply(byte[] code, string stage, byte[] spirv, List<string> notes)
    {
        if (stage != "vs" || Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(code)) != InstanceCodeHash) return spirv;
        uint[] w = new uint[spirv.Length / 4]; Buffer.BlockCopy(spirv, 0, w, 0, spirv.Length);
        uint lane = 0, uintType = 0, vectorType = 0, trueId = 0; int ballots = 0, loads = 0;
        for (int i = 5; i < w.Length; i += (int)(w[i] >> 16))
        {
            uint op = w[i] & 65535;
            if (op == 71 && w[i + 2] == 11 && w[i + 3] == 41) lane = w[i + 1];
            if (op == 21 && w[i + 2] == 32 && w[i + 3] == 0) uintType = w[i + 1];
            if (op == 41) trueId = w[i + 2];
            if (op == 339) { vectorType = w[i + 1]; ballots++; if (w[i + 4] != trueId) throw new InvalidOperationException("visibility counter: ballot predicate changed"); }
        }
        if (lane == 0 || uintType == 0 || vectorType == 0 || ballots != 1) throw new InvalidOperationException("visibility counter: SPIR-V shape changed");
        uint zero = w[3]++, one = w[3]++, mask = w[3]++;
        var result = new List<uint>(w.Length + 16); result.AddRange(w.AsSpan(0, 5).ToArray());
        bool constants = false;
        for (int i = 5; i < w.Length; i += (int)(w[i] >> 16))
        {
            uint op = w[i] & 65535;
            if (op == 54 && !constants)
            {
                result.AddRange(new uint[] { (4u << 16) | 43, uintType, zero, 0, (4u << 16) | 43, uintType, one, 1, (7u << 16) | 44, vectorType, mask, one, zero, zero, zero });
                constants = true;
            }
            if (op == 61 && w[i + 3] == lane) { result.AddRange(new uint[] { (4u << 16) | 83, w[i + 1], w[i + 2], zero }); loads++; }
            else if (op == 339) result.AddRange(new uint[] { (4u << 16) | 83, w[i + 1], w[i + 2], mask });
            else result.AddRange(w.AsSpan(i, (int)(w[i] >> 16)).ToArray());
        }
        if (loads != 1 || !constants) throw new InvalidOperationException("visibility counter: lane id use changed");
        notes.Add("visibility counter lowered to singleton groups (one atomic add per passing vertex)");
        byte[] output = new byte[result.Count * 4]; Buffer.BlockCopy(result.ToArray(), 0, output, 0, output.Length);
        return output;
    }
}
