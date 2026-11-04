using System.Buffers.Binary;

namespace Satchel.SwitchContent;

internal sealed record ContentReference(string Id, ulong Size, byte Type);
internal sealed record ContentMetadata(string Id, string Type, uint Version, string? ApplicationId,
    string? PatchId, IReadOnlyList<string> ContentIds, string SourceMetaNca)
{
    [System.Text.Json.Serialization.JsonIgnore]
    public IReadOnlyList<ContentReference> References { get; init; } = [];
    public IReadOnlyList<string> OmittedDeltaFragments { get; init; } = [];
}
internal sealed record GameSelection(string ApplicationId, uint BaseVersion, string? PatchId, uint? PatchVersion);

internal static class Metadata
{
    public static ContentMetadata ValidateReferences(ContentMetadata record, IReadOnlyDictionary<string, long> contentSizes)
    {
        var omittedDeltaFragments = new List<string>();
        foreach (var reference in record.References)
        {
            if (!contentSizes.TryGetValue(reference.Id, out long actualSize))
            {
                // Distribution delta fragments are not the executable or patched RomFS NCA.
                // Dump tools may omit them while preserving their CNMT records. Never extend
                // this exception to Program, Control, Data, or any other required content.
                if (record.Type == "Patch" && reference.Type == 6)
                {
                    omittedDeltaFragments.Add(reference.Id);
                    continue;
                }
                throw new InspectionFailure("content_missing", $"CNMT {record.Id} ({record.Type}) references missing content {reference.Id}, type {reference.Type}.");
            }
            if (actualSize < 0 || (ulong)actualSize != reference.Size)
                throw new InspectionFailure("content_size", $"CNMT {record.Id} references content {reference.Id} with a different size from its metadata.");
        }
        return record with { OmittedDeltaFragments = omittedDeltaFragments };
    }

    // PackagedContentMetaHeader and PackagedContentInfo, documented by Switchbrew.
    // Parsing is independent of package filenames and bounded before every table access.
    public static ContentMetadata Parse(ReadOnlySpan<byte> bytes, string sourceMetaNca)
    {
        if (bytes.Length < 0x20) throw Invalid("CNMT header is truncated.");
        var id = BinaryPrimitives.ReadUInt64LittleEndian(bytes);
        var version = BinaryPrimitives.ReadUInt32LittleEndian(bytes[8..]);
        byte type = bytes[0xc];
        int extended = BinaryPrimitives.ReadUInt16LittleEndian(bytes[0xe..]);
        int count = BinaryPrimitives.ReadUInt16LittleEndian(bytes[0x10..]);
        int metaCount = BinaryPrimitives.ReadUInt16LittleEndian(bytes[0x12..]);
        int start = 0x20 + extended;
        long tableEnd = (long)start + count * 0x38L + metaCount * 0x10L;
        if (tableEnd > bytes.Length - 0x20L) throw Invalid("CNMT tables or trailing digest are truncated.");
        if (id == 0) throw Invalid("CNMT has an empty title ID.");
        string typeName = type switch
        {
            0x80 => "Application", 0x81 => "Patch", 0x82 => "AddOnContent",
            0x83 => "Delta", 0x84 => "DataPatch", _ => $"Other_{type:x2}"
        };
        string? applicationId = null, patchId = null;
        int requiredExtended = type switch { 0x80 => 0x10, 0x81 => 0x18, 0x82 => 0x10, 0x83 => 0x10, 0x84 => 0x20, _ => 0 };
        if (extended < requiredExtended) throw Invalid("CNMT extended header is truncated.");
        if (type == 0x80) patchId = BinaryPrimitives.ReadUInt64LittleEndian(bytes[0x20..]).ToString("x16");
        if (type is 0x81 or 0x82 or 0x83) applicationId = BinaryPrimitives.ReadUInt64LittleEndian(bytes[0x20..]).ToString("x16");
        if (type == 0x84) applicationId = BinaryPrimitives.ReadUInt64LittleEndian(bytes[0x28..]).ToString("x16");
        var refs = new List<ContentReference>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        for (int i = 0; i < count; i++)
        {
            var entry = bytes.Slice(start + i * 0x38, 0x38);
            var contentId = Convert.ToHexString(entry.Slice(0x20, 16)).ToLowerInvariant();
            if (!seen.Add(contentId)) throw Invalid("CNMT contains duplicate content IDs.");
            // Current format uses a 40-bit size; byte 0x35 is attributes, not size.
            ulong size = BinaryPrimitives.ReadUInt32LittleEndian(entry[0x30..]) | ((ulong)entry[0x34] << 32);
            refs.Add(new(contentId, size, entry[0x36]));
        }
        return new(id.ToString("x16"), typeName, version, applicationId, patchId, refs.Select(r => r.Id).ToArray(), sourceMetaNca) { References = refs };
    }

    public static string NormalizeApplicationId(string applicationId)
    {
        if (applicationId.Length != 16 || applicationId.Any(c => !char.IsAsciiHexDigit(c)) || applicationId.All(c => c == '0'))
            throw new InspectionFailure("application_id_invalid", "The application ID must be exactly 16 hexadecimal digits and nonzero.");
        return applicationId.ToLowerInvariant();
    }

    public static GameSelection Select(IReadOnlyList<ContentMetadata> baseMetadata, IReadOnlyList<ContentMetadata>? updateMetadata,
        string? applicationId = null)
    {
        var applications = baseMetadata.Where(m => m.Type == "Application").ToArray();
        if (applicationId is not null)
        {
            applicationId = NormalizeApplicationId(applicationId);
            applications = applications.Where(m => m.Id == applicationId).ToArray();
            if (applications.Length == 0)
                throw new InspectionFailure("application_not_found", "The requested application ID is not an Application in the selected base package.");
        }
        if (applications.Length != 1)
            throw new InspectionFailure("base_ambiguous", "The base must contain one Application, or --application-id must identify exactly one Application record.");
        var application = applications[0];
        if (!application.References.Any(r => r.Type == 1))
            throw new InspectionFailure("program_missing", "The selected base metadata does not reference a Program NCA.");
        if (updateMetadata is null) return new(application.Id, application.Version, null, null);
        var patches = updateMetadata.Where(m => m.Type == "Patch").ToArray();
        if (patches.Length != 1 || updateMetadata.Any(m => m.Type is "Application" or "AddOnContent"))
            throw new InspectionFailure("update_not_patch", "The update must contain one Patch, not another base game or DLC package.");
        var patch = patches[0];
        if (!patch.References.Any(r => r.Type == 1))
            throw new InspectionFailure("program_missing", "The selected update metadata does not reference a Program NCA.");
        if (patch.ApplicationId != application.Id || patch.Id != application.PatchId)
            throw new InspectionFailure("update_mismatch", "The update's internal application and patch IDs do not match the selected base game.");
        if (patch.Version <= application.Version)
            throw new InspectionFailure("update_not_newer", "The selected update version is not newer than the base content version.");
        return new(application.Id, application.Version, patch.Id, patch.Version);
    }

    private static InspectionFailure Invalid(string message) => new("metadata_invalid", message);
}
