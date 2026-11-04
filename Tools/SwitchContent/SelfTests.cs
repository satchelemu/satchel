using System.Buffers.Binary;
using System.Text;

namespace Satchel.SwitchContent;

internal static class SelfTests
{
    public static int Run()
    {
        int assertions = 0;
        void Check(bool condition) { assertions++; if (!condition) throw new Exception("Synthetic test assertion failed."); }
        void Reject(Action action, string code)
        {
            assertions++;
            try { action(); } catch (InspectionFailure ex) when (ex.Code == code) { return; }
            throw new Exception("Malformed fixture or invalid update was accepted.");
        }
        const ulong baseId = 0x0100f2c0115b6000;
        var application = Metadata.Parse(CnmtFixture(0x80, baseId, 0, baseId + 0x800), "synthetic.cnmt.nca");
        var patch = Metadata.Parse(CnmtFixture(0x81, baseId + 0x800, 720896, baseId), "synthetic-patch.cnmt.nca");
        Check(application.Id == "0100f2c0115b6000" && application.PatchId == "0100f2c0115b6800");
        Check(patch.Type == "Patch" && patch.ApplicationId == application.Id && patch.Version == 720896);
        Check(application.ContentIds.Single() == "0102030405060708090a0b0c0d0e0f10");
        var selection = Metadata.Select([application], [patch]);
        Check(selection.ApplicationId == application.Id && selection.PatchVersion == 720896);
        Check(Metadata.Select([application], null).PatchId is null);
        var deltaReference = new ContentReference("ffeeddccbbaa99887766554433221100", 32, 6);
        var withDelta = patch with { References = patch.References.Append(deltaReference).ToArray() };
        var programSizes = new Dictionary<string, long> { [patch.References[0].Id] = 0x1000 };
        Check(Metadata.ValidateReferences(withDelta, programSizes).OmittedDeltaFragments.SequenceEqual([deltaReference.Id]));
        Reject(() => Metadata.ValidateReferences(withDelta, new Dictionary<string, long>()), "content_missing");
        Reject(() => Metadata.ValidateReferences(application with { References = [deltaReference] }, new Dictionary<string, long>()), "content_missing");
        Reject(() => Metadata.ValidateReferences(patch, new Dictionary<string, long> { [patch.References[0].Id] = 1 }), "content_size");
        Reject(() => Metadata.ValidateReferences(withDelta, new Dictionary<string, long> { [patch.References[0].Id] = 0x1000, [deltaReference.Id] = 1 }), "content_size");
        var unrelated = Metadata.Parse(CnmtFixture(0x81, 0x01005f4027e94800, 65536, 0x01005f4027e94000), "other.cnmt.nca");
        Reject(() => Metadata.Select([application], [unrelated]), "update_mismatch");
        Reject(() => Metadata.Select([application], [application]), "update_not_patch");
        Reject(() => Metadata.Select([application, application], [patch]), "base_ambiguous");
        Reject(() => Metadata.Select([application], [patch with { Version = 0 }]), "update_not_newer");
        Reject(() => Metadata.Select([application], [patch with { Id = "0100000000000800" }]), "update_mismatch");
        // Synthetic binary CNMTs mirror the two-Application shape observed in the
        // Castlevania XCI. The gallery patch targets only the second Application.
        const ulong mainId = 0x0100ba4026e94000, galleryId = 0x01005f4027e94000;
        var main = Metadata.Parse(CnmtFixture(0x80, mainId, 0, mainId + 0x800), "main.cnmt.nca");
        var gallery = Metadata.Parse(CnmtFixture(0x80, galleryId, 0, galleryId + 0x800), "gallery.cnmt.nca");
        Reject(() => Metadata.Select([main, gallery], [unrelated]), "base_ambiguous");
        Check(Metadata.Select([main, gallery], null, main.Id).ApplicationId == main.Id);
        Check(Metadata.Select([main, gallery], null, gallery.Id).ApplicationId == gallery.Id);
        var gallerySelection = Metadata.Select([main, gallery], [unrelated], "01005F4027E94000");
        Check(gallerySelection.ApplicationId == gallery.Id && gallerySelection.PatchId == unrelated.Id && gallerySelection.PatchVersion == 65536);
        Reject(() => Metadata.Select([main, gallery], [unrelated], main.Id), "update_mismatch");
        Reject(() => Metadata.Select([main, gallery], null, application.Id), "application_not_found");
        Reject(() => Metadata.Select([main, gallery, unrelated], null, unrelated.Id), "application_not_found");
        Reject(() => Metadata.Select([main, main, gallery], null, main.Id), "base_ambiguous");
        Check(Metadata.Select([application], [patch], application.Id).PatchId == patch.Id);
        Reject(() => Metadata.Select([main, gallery], null, "0000000000000000"), "application_id_invalid");
        Reject(() => Metadata.NormalizeApplicationId("0100ba4026e9400"), "application_id_invalid");
        Reject(() => Metadata.NormalizeApplicationId("0100ba4026e9400g"), "application_id_invalid");
        Reject(() => Metadata.NormalizeApplicationId(" 100ba4026e94000"), "application_id_invalid");
        Reject(() => Metadata.NormalizeApplicationId("0100ba4026e9400\uff10"), "application_id_invalid");
        Reject(() => Metadata.Parse(new byte[31], "fixture"), "metadata_invalid");
        byte[] truncated = CnmtFixture(0x81, baseId + 0x800, 720896, baseId);
        Reject(() => Metadata.Parse(truncated.AsSpan(0, truncated.Length - 1), "fixture"), "metadata_invalid");
        byte[] badCount = CnmtFixture(0x80, baseId, 0, baseId + 0x800);
        BinaryPrimitives.WriteUInt16LittleEndian(badCount.AsSpan(0x10), ushort.MaxValue);
        Reject(() => Metadata.Parse(badCount, "fixture"), "metadata_invalid");
        byte[] validPfs = PfsFixture();
        PackageReader.ValidatePfs0(new MemoryStream(validPfs, false)); assertions++;
        string scratch = Path.Combine(Path.GetTempPath(), "satchel-content-test-" + Guid.NewGuid().ToString("N") + ".nsp");
        try
        {
            File.WriteAllBytes(scratch, validPfs);
            // Exercise the real LibHac PFS0 reader, not just our bounds checks.
            Reject(() => PackageReader.Read(scratch, new LibHac.Common.Keys.KeySet()), "metadata_missing");
        }
        finally { File.Delete(scratch); }
        Reject(() => PackageReader.ValidatePfs0(new MemoryStream(validPfs[..^1], false)), "container_invalid");
        byte[] overflowing = (byte[])validPfs.Clone();
        BinaryPrimitives.WriteUInt64LittleEndian(overflowing.AsSpan(16), ulong.MaxValue);
        Reject(() => PackageReader.ValidatePfs0(new MemoryStream(overflowing, false)), "container_invalid");
        byte[] unterminated = (byte[])validPfs.Clone(); unterminated[41] = 1;
        Reject(() => PackageReader.ValidatePfs0(new MemoryStream(unterminated, false)), "container_invalid");
        byte[] hugeTable = (byte[])validPfs.Clone(); BinaryPrimitives.WriteUInt32LittleEndian(hugeTable.AsSpan(4), uint.MaxValue);
        Reject(() => PackageReader.ValidatePfs0(new MemoryStream(hugeTable, false)), "container_invalid");
        return assertions;
    }

    private static byte[] CnmtFixture(byte type, ulong id, uint version, ulong relationship)
    {
        int extended = type == 0x81 ? 0x18 : 0x10;
        byte[] bytes = new byte[0x20 + extended + 0x38 + 0x20];
        BinaryPrimitives.WriteUInt64LittleEndian(bytes, id);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(8), version);
        bytes[0xc] = type;
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(0xe), (ushort)extended);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(0x10), 1);
        BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(0x20), relationship);
        for (int i = 0; i < 16; i++) bytes[0x20 + extended + 0x20 + i] = (byte)(i + 1);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(0x20 + extended + 0x30), 0x1000);
        bytes[0x20 + extended + 0x36] = 1;
        return bytes;
    }

    private static byte[] PfsFixture()
    {
        byte[] bytes = new byte[46];
        Encoding.ASCII.GetBytes("PFS0").CopyTo(bytes, 0);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4), 1);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(8), 2);
        BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(24), 4);
        bytes[40] = (byte)'a';
        Encoding.ASCII.GetBytes("data").CopyTo(bytes, 42);
        return bytes;
    }
}
