using System.Text.Json;
using LibHac.Common.Keys;

namespace Satchel.SwitchContent;

internal sealed class InspectionFailure(string code, string message) : Exception(message)
{
    public string Code { get; } = code;
}

internal static class Program
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        WriteIndented = true
    };

    private static void Emit(object value) => Console.WriteLine(JsonSerializer.Serialize(value, JsonOptions));

    public static int Main(string[] args)
    {
        try
        {
            if (args.SequenceEqual(["self-test"]))
            {
                Emit(new { schema_version = 1, status = "passed", assertions = SelfTests.Run(), fixtures = "synthetic" });
                return 0;
            }
            if (args.Length < 5 || args[0] != "inspect" || args.Length % 2 != 1)
                throw Usage();
            var options = new Dictionary<string, string>(StringComparer.Ordinal);
            for (int i = 1; i < args.Length; i += 2)
            {
                if (args[i] is not ("--keys" or "--base" or "--update" or "--application-id") || !options.TryAdd(args[i], args[i + 1]))
                    throw Usage();
            }
            if (!options.TryGetValue("--keys", out var keysPath) || !options.TryGetValue("--base", out var basePath))
                throw Usage();
            var applicationId = options.TryGetValue("--application-id", out var requestedId)
                ? Metadata.NormalizeApplicationId(requestedId) : null;
            if (!File.Exists(keysPath))
                throw new InspectionFailure("keys_missing", "The supplied console key file is missing or unavailable.");
            KeySet keys;
            // Do not attach a LibHac progress logger: parsing diagnostics can contain key-file text.
            try { keys = ExternalKeyReader.ReadKeyFile(keysPath); }
            catch { throw new InspectionFailure("keys_invalid", "The console key file could not be parsed. Its contents were not logged."); }
            var basePackage = PackageReader.Read(basePath, keys);
            var update = options.TryGetValue("--update", out var updatePath) ? PackageReader.Read(updatePath, keys) : null;
            var selection = Metadata.Select(basePackage.Metadata, update?.Metadata, applicationId);
            Emit(new { schema_version = 1, status = "validated", @base = basePackage, update, selection });
            return 0;
        }
        catch (InspectionFailure ex)
        {
            Emit(new { schema_version = 1, status = "error", error = new { code = ex.Code, message = ex.Message } });
            return 2;
        }
        catch (MissingKeyException)
        {
            Emit(new { schema_version = 1, status = "error", error = new { code = "keys_incomplete", message = "The supplied console key set does not contain a required key for this content." } });
            return 3;
        }
        catch (Exception)
        {
            // Library exceptions can include key material or user paths. Return a bounded safe message.
            Emit(new { schema_version = 1, status = "error", error = new { code = "content_unreadable", message = "Content could not be read or decrypted. Check the supplied keys, file availability and package integrity. No game or key bytes were logged." } });
            return 4;
        }
    }

    private static InspectionFailure Usage() => new("usage", "Use: SwitchContent inspect --keys PATH --base XCI_OR_NSP [--update NSP] [--application-id 16_HEX_DIGITS], or SwitchContent self-test.");
}
