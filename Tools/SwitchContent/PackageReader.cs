// Archive/NCA opening adapted from NX2Apple nx_extract, commit
// 8227f80f9b381c680611303319656c4e0c806a50. See THIRD_PARTY_NOTICES.md.
using System.Buffers.Binary;
using System.Text;
using System.Text.RegularExpressions;
using LibHac.Common;
using LibHac.Common.Keys;
using LibHac.Fs;
using LibHac.Fs.Fsa;
using LibHac.FsSystem;
using LibHac.Tools.Fs;
using LibHac.Tools.FsSystem;
using LibHac.Tools.FsSystem.NcaUtils;

namespace Satchel.SwitchContent;

internal sealed record PackageReport(string Format, IReadOnlyList<ContentMetadata> Metadata,
    bool MetadataIntegrityChecked = true, bool FullContentHashesChecked = false,
    bool SignaturesVerified = false);

internal static partial class PackageReader
{
    private const int MaximumEntries = 8192;
    private const int MaximumMetadataBytes = 16 * 1024 * 1024;
    [GeneratedRegex("^([0-9a-fA-F]{32})(?:\\.cnmt)?\\.nca$", RegexOptions.CultureInvariant)]
    private static partial Regex ContentName();

    public static PackageReport Read(string path, KeySet keys)
    {
        if (!File.Exists(path)) throw new InspectionFailure("package_missing", "A selected package is missing or unavailable.");
        string format;
        long prefix = 0;
        long length;
        using (var file = File.OpenRead(path))
        {
            length = file.Length;
            if (HasMagic(file, 0, "PFS0"u8)) { ValidatePfs0(file); format = "nsp"; }
            else if (HasMagic(file, 0x100, "HEAD"u8)) format = "xci";
            else if (HasMagic(file, 0x1100, "HEAD"u8)) { format = "xci"; prefix = 0x1000; }
            else throw new InspectionFailure("container_invalid", "The selected file is not a supported PFS0/NSP or XCI container.");
        }
        using var storage = new LocalStorage(path, FileAccess.Read);
        using var region = new SubStorage(storage, prefix, length - prefix);
        IFileSystem filesystem;
        if (format == "xci")
        {
            var xci = new Xci(keys, region);
            if (!xci.HasPartition(XciPartitionType.Secure))
                throw new InspectionFailure("container_invalid", "The XCI has no secure content partition.");
            filesystem = xci.OpenPartition(XciPartitionType.Secure);
        }
        else
        {
            var pfs = new PartitionFileSystem();
            pfs.Initialize(region).ThrowIfFailure();
            filesystem = pfs;
        }
        using (filesystem)
        {
            var entries = filesystem.EnumerateEntries("*", SearchOptions.Default).Take(MaximumEntries + 1).ToArray();
            if (entries.Length > MaximumEntries) throw new InspectionFailure("container_limit", "The package has more entries than the inspection limit.");
            var contentSizes = new Dictionary<string, long>(StringComparer.Ordinal);
            var ncas = new List<(string Id, string Name, DirectoryEntryEx Entry)>();
            foreach (var entry in entries)
            {
                if (!entry.Name.EndsWith(".nca", StringComparison.OrdinalIgnoreCase)) continue;
                var match = ContentName().Match(entry.Name);
                if (!match.Success) throw new InspectionFailure("content_provenance", "An NCA filename is not a content ID. Renamed content is not accepted.");
                string id = match.Groups[1].Value.ToLowerInvariant();
                if (!contentSizes.TryAdd(id, entry.Size)) throw new InspectionFailure("content_provenance", "The package contains duplicate NCA content IDs.");
                ncas.Add((id, entry.Name, entry));
            }
            var metadata = new List<ContentMetadata>();
            foreach (var (_, name, entry) in ncas)
            {
                using var handle = new UniqueRef<IFile>();
                filesystem.OpenFile(ref handle.Ref, entry.FullPath.ToU8Span(), OpenMode.Read).ThrowIfFailure();
                using var ncaStorage = handle.Release().AsStorage();
                var nca = new Nca(keys, ncaStorage);
                if (nca.Header.ContentType != NcaContentType.Meta) continue;
                using var metaFs = nca.OpenFileSystem(NcaSectionType.Data, IntegrityCheckLevel.ErrorOnInvalid);
                var cnmts = metaFs.EnumerateEntries("*.cnmt", SearchOptions.Default).Take(17).ToArray();
                if (cnmts.Length is 0 or > 16) throw new InspectionFailure("metadata_invalid", "A metadata NCA has an invalid number of CNMT records.");
                foreach (var cnmtEntry in cnmts)
                {
                    using var cnmtFile = new UniqueRef<IFile>();
                    metaFs.OpenFile(ref cnmtFile.Ref, cnmtEntry.FullPath.ToU8Span(), OpenMode.Read).ThrowIfFailure();
                    cnmtFile.Get.GetSize(out var size).ThrowIfFailure();
                    if (size < 0x20 || size > MaximumMetadataBytes) throw new InspectionFailure("metadata_invalid", "CNMT size is outside the bounded inspection limit.");
                    using var stream = cnmtFile.Release().AsStream();
                    byte[] bytes = new byte[(int)size];
                    stream.ReadExactly(bytes);
                    var record = Metadata.Parse(bytes, name);
                    if (record.Id != nca.Header.TitleId.ToString("x16"))
                        throw new InspectionFailure("metadata_invalid", "The CNMT title ID disagrees with its metadata NCA header.");
                    metadata.Add(Metadata.ValidateReferences(record, contentSizes));
                }
            }
            if (metadata.Count == 0) throw new InspectionFailure("metadata_missing", "The package contains no readable content metadata.");
            if (metadata.Select(m => (m.Id, m.Version, m.Type)).Distinct().Count() != metadata.Count)
                throw new InspectionFailure("metadata_duplicate", "The package contains duplicate metadata identities.");
            return new(format, metadata);
        }
    }

    private static bool HasMagic(Stream stream, long offset, ReadOnlySpan<byte> magic)
    {
        if (offset > stream.Length - magic.Length) return false;
        Span<byte> bytes = stackalloc byte[4];
        stream.Position = offset;
        stream.ReadExactly(bytes);
        return bytes.SequenceEqual(magic);
    }

    internal static void ValidatePfs0(Stream stream)
    {
        if (stream.Length < 16) throw InvalidPfs();
        stream.Position = 0;
        Span<byte> header = stackalloc byte[16];
        stream.ReadExactly(header);
        if (!header[..4].SequenceEqual("PFS0"u8)) throw InvalidPfs();
        uint count = BinaryPrimitives.ReadUInt32LittleEndian(header[4..]);
        uint stringsSize = BinaryPrimitives.ReadUInt32LittleEndian(header[8..]);
        if (count > MaximumEntries || stringsSize > MaximumMetadataBytes) throw InvalidPfs();
        long dataStart = 16L + count * 24L + stringsSize;
        if (dataStart > stream.Length) throw InvalidPfs();
        byte[] table = new byte[checked((int)(count * 24))];
        byte[] strings = new byte[(int)stringsSize];
        stream.ReadExactly(table);
        stream.ReadExactly(strings);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var ranges = new List<(ulong Start, ulong End)>();
        for (int i = 0; i < count; i++)
        {
            var entry = table.AsSpan(i * 24, 24);
            ulong offset = BinaryPrimitives.ReadUInt64LittleEndian(entry);
            ulong size = BinaryPrimitives.ReadUInt64LittleEndian(entry[8..]);
            uint nameOffset = BinaryPrimitives.ReadUInt32LittleEndian(entry[16..]);
            ulong dataLength = (ulong)(stream.Length - dataStart);
            if (offset > dataLength || size > dataLength - offset || nameOffset >= strings.Length) throw InvalidPfs();
            int end = Array.IndexOf(strings, (byte)0, (int)nameOffset);
            if (end <= nameOffset) throw InvalidPfs();
            string name;
            try { name = new UTF8Encoding(false, true).GetString(strings, (int)nameOffset, end - (int)nameOffset); }
            catch (DecoderFallbackException) { throw InvalidPfs(); }
            if (name.Contains('/') || name.Contains('\\') || name is "." or ".." || !names.Add(name)) throw InvalidPfs();
            if (size != 0) ranges.Add((offset, offset + size));
        }
        ranges.Sort((a, b) => a.Start.CompareTo(b.Start));
        for (int i = 1; i < ranges.Count; i++) if (ranges[i].Start < ranges[i - 1].End) throw InvalidPfs();
    }

    private static InspectionFailure InvalidPfs() => new("container_invalid", "The PFS0 directory is truncated, malformed, overlapping or exceeds inspection limits.");
}
