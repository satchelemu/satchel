import CryptoKit
import Foundation

/// Private Mac parity driver. Arguments and errors never print source content,
/// keys or bytes. Outputs belong outside the checkout. No reference extraction
/// tool participates in the native path.
@main struct ContentParity {
    static func digest(_ url: URL) throws -> Data {
        let h = try FileHandle(forReadingFrom: url); defer { try? h.close() }
        var hash = SHA256()
        while try autoreleasepool(invoking: { () throws -> Bool in
            guard let data = try h.read(upToCount: 1024 * 1024), !data.isEmpty else { return false }
            hash.update(data: data); return true
        }) {}
        return Data(hash.finalize())
    }
    static func run() throws {
        guard CommandLine.arguments.count == 6 else { throw SwitchExecutableError.invalid }
        if CommandLine.arguments[1] == "--select" {
            let args = CommandLine.arguments
            let selected = try SwitchContentPipeline.select(base: URL(fileURLWithPath: args[2]),
                update: URL(fileURLWithPath: args[3]),
                keys: SwitchKeys.parse(Data(contentsOf: URL(fileURLWithPath: args[4]))),
                applicationID: UInt64(args[5], radix: 16) ?? 0)
            print("PASS selection version=\(selected.patchVersion ?? 0)")
            return
        }
        let args = CommandLine.arguments.dropFirst().map { URL(fileURLWithPath: $0) }
        let keys = try SwitchKeys.parse(Data(contentsOf: args[2]))
        print("START native CNMT selection and Program authentication")
        fflush(stdout)
        try SwitchContentPipeline.withContent(base: args[0], update: args[1], keys: keys, applicationID: 0x0100f2c0115b6000) { content in
            print("PASS CNMT selection version=\(content.selection.patchVersion ?? 0)")
            for module in content.program.modules {
                let reference = try Data(contentsOf: args[3].appendingPathComponent("ExeFS/" + module.name))
                guard SHA256.hash(data: module.data) == SHA256.hash(data: reference) else { throw SwitchExecutableError.integrity }
                let decoded = try SwitchDecodedModule.decode(module)
                // Preserve only native outputs in the private build directory for
                // independent Python oracle comparison, never in the checkout.
                for (i, segment) in decoded.segments.enumerated() {
                    try segment.bytes.write(to: args[4].appendingPathComponent("\(module.name).segment\(i)"), options: .atomic)
                }
                print("PASS composed NSO hash and native decode module=\(module.name)")
            }
            let image = args[4].appendingPathComponent("RomFS.img")
            var last: UInt64 = 0
            let result = try SwitchRomFSImage.write(content.romFS, to: image, progress: { p in
                if p.completedBytes / (1024 * 1024 * 1024) != last || p.stage == .publishing {
                    last = p.completedBytes / (1024 * 1024 * 1024)
                    print("PROGRESS image bytes=\(p.completedBytes) total=\(p.totalBytes)")
                    fflush(stdout)
                }
            })
            print("PASS image published files=\(result.fileCount) entries=\(result.entryCount) bytes=\(result.imageBytes)")
            guard try digest(image) == digest(args[3].appendingPathComponent("RomFS.img")) else { throw SwitchExecutableError.integrity }
            print("PASS full DMROMFS1 SHA256 parity")
        }
    }
    static func main() {
        do { try run() }
        catch let error as SwitchExecutableError { print("FAIL native content: \(error)"); exit(1) }
        catch { print("FAIL native content: \(type(of: error))"); exit(1) }
    }
}
