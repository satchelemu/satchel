import Foundation

/// Reads the already verified DMROMFS1 package without extracting its filesystem.
final class WarmupImage {
    struct File { let name: String; let offset: UInt64; let count: UInt64 }
    let files: [File]
    private let handle: FileHandle
    private let lock = NSLock()
    init(_ url: URL) throws {
        handle = try FileHandle(forReadingFrom: url)
        let size = try handle.seekToEnd(); try handle.seek(toOffset: 0)
        let h = try handle.read(upToCount: 32) ?? Data()
        guard h.count == 32, h.prefix(8) == Data("DMROMFS1".utf8) else { throw SatchelShaderDiscoveryError.bounds }
        func u64(_ d: Data, _ at: Int) -> UInt64 { d.withUnsafeBytes { UInt64(littleEndian: $0.loadUnaligned(fromByteOffset: at, as: UInt64.self)) } }
        let offset = u64(h, 8), length = u64(h, 16), count = u64(h, 24)
        guard count > 0, count <= 1_000_000, length <= 128 * 1024 * 1024,
              offset <= size, length == size - offset, count * 24 <= length else { throw SatchelShaderDiscoveryError.bounds }
        try handle.seek(toOffset: offset); let data = try handle.read(upToCount: Int(length)) ?? Data()
        guard data.count == length else { throw SatchelShaderDiscoveryError.bounds }
        var stack = [(0, "")], seen = Set<Int>(), result: [File] = []
        while let (i, parent) = stack.popLast() {
            guard i >= 0, i < count, seen.insert(i).inserted else { throw SatchelShaderDiscoveryError.bounds }
            let row = i * 24, first = u64(data, row), n = u64(data, row + 8)
            let no = Int(try ShaderRecord.word(data, row + 16)), nl = try ShaderRecord.short(data, row + 20)
            let start = Int(count) * 24 + no
            guard start <= data.count, nl <= data.count - start else { throw SatchelShaderDiscoveryError.bounds }
            let name = String(decoding: data[start..<start + nl], as: UTF8.self)
            guard !name.contains("/"), name != "..", name != ".", parent.utf8.count + name.utf8.count < 8192 else { throw SatchelShaderDiscoveryError.bounds }
            let path = name.isEmpty ? parent : parent + "/" + name
            if data[row + 22] == 1 {
                guard first >= 32, first <= offset, n <= offset - first else { throw SatchelShaderDiscoveryError.bounds }
                result.append(File(name: path, offset: first, count: n))
            } else {
                guard data[row + 22] == 0, first <= count, n <= count - first else { throw SatchelShaderDiscoveryError.bounds }
                for child in first..<first + n { stack.append((Int(child), path)) }
            }
        }
        guard seen.count == count else { throw SatchelShaderDiscoveryError.bounds }
        files = result
    }
    deinit { try? handle.close() }
    func input(_ file: File) -> SatchelShaderInput {
        SatchelShaderInput(count: file.count) { offset, count in
            self.lock.lock(); defer { self.lock.unlock() }
            try self.handle.seek(toOffset: file.offset + offset)
            return try self.handle.read(upToCount: count) ?? Data()
        }
    }
}

/// MIT nxnative shader_split.py Yaz0 layout, with bounded output and cancellation.
final class WarmupArchiveDecoder: SatchelShaderArchiveDecoder {
    let zstd: SatchelZstdArchiveDecoder
    let cancelled: () -> Bool
    let scratch: URL
    let reserve: UInt64
    init(scratch: URL, reserve: UInt64, cancelled: @escaping () -> Bool) throws {
        self.cancelled = cancelled; self.scratch = scratch; self.reserve = reserve
        zstd = try SatchelZstdArchiveDecoder(scratchDirectory: scratch, reserveBytes: reserve, cancelled: cancelled)
    }
    func withExpanded(_ input: SatchelShaderInput, body: (SatchelShaderInput) throws -> Void) throws {
        guard try input.read(offset: 0, count: 4) == Data("Yaz0".utf8) else { return try zstd.withExpanded(input, body: body) }
        let header = try input.read(offset: 0, count: 16)
        let length = header[4..<8].reduce(0) { ($0 << 8) | Int($1) }
        guard UInt64(length) <= 4 * 1024 * 1024 * 1024 else { throw SatchelShaderDiscoveryError.limit }
        let url = scratch.appendingPathComponent(UUID().uuidString + ".yaz-spill")
        let fm = FileManager.default
        guard fm.createFile(atPath: url.path, contents: nil) else { throw SatchelShaderDiscoveryError.bounds }
        defer { try? fm.removeItem(at: url) }
        let handle = try FileHandle(forUpdating: url); defer { try? handle.close() }
        var window = [UInt8](repeating: 0, count: 4096), output = Data(), written = 0
        var position: UInt64 = 16, cache = Data(), index = 0
        func byte() throws -> UInt8 {
            if index == cache.count {
                guard position < input.count else { throw SatchelShaderDiscoveryError.bounds }
                cache = try input.read(offset: position, count: Int(min(65536, input.count - position)))
                position += UInt64(cache.count); index = 0
                if cancelled() { throw SatchelShaderError.cancelled }
            }
            defer { index += 1 }; return cache[index]
        }
        func append(_ value: UInt8) throws {
            window[written % 4096] = value; written += 1; output.append(value)
            if output.count == 65536 {
                if cancelled() { throw SatchelShaderError.cancelled }
                let free = (try fm.attributesOfFileSystem(forPath: scratch.path)[.systemFreeSize] as? NSNumber)?.uint64Value ?? 0
                guard free >= reserve + 1024 * 1024 else { throw SatchelShaderCacheError.insufficientSpace }
                try handle.write(contentsOf: output); output.removeAll(keepingCapacity: true)
            }
        }
        while written < length {
            var flags = try byte()
            for _ in 0..<8 where written < length {
                if flags & 0x80 != 0 { try append(byte()) }
                else {
                    let a = try byte(), b = try byte(), distance = (Int(a & 15) << 8 | Int(b)) + 1
                    let n = a >> 4 == 0 ? Int(try byte()) + 0x12 : Int(a >> 4) + 2
                    guard distance <= written, n <= length - written else { throw SatchelShaderDiscoveryError.bounds }
                    for _ in 0..<n { try append(window[(written - distance) % 4096]) }
                }
                flags <<= 1
            }
        }
        try handle.write(contentsOf: output)
        try body(SatchelShaderInput(count: UInt64(length)) { offset, count in
            try handle.seek(toOffset: offset); return try handle.read(upToCount: count) ?? Data()
        })
    }
}

struct WarmupInventory: Codable {
    // Bump when discovery or record normalization changes.
    static let currentVersion = 2
    var version = 2
    struct Record: Codable { let key: String; let stage: SatchelShaderStage; let digest: String }
    let manifest: String
    var records: [Record]
    var pairs: [[String]]
    var scannedFiles: Int
    var incomplete = true

    static func discover(package: URL, directory: URL, manifest: String, reserve: UInt64,
                         cancelled: @escaping () -> Bool, progress: (Int, Int) -> Void) throws -> Self {
        let fm = FileManager.default
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
        let image = try WarmupImage(package.appendingPathComponent("romfs.img"))
        let decoder = try WarmupArchiveDecoder(scratch: directory.appendingPathComponent("scratch"), reserve: reserve, cancelled: cancelled)
        if let dictionary = image.files.first(where: { $0.name.hasSuffix("/ZsDic.pack.zs") }) { try decoder.zstd.loadDictionaryArchive(image.input(dictionary)) }
        var hints = Set<String>()
        let map = package.appendingPathComponent("dm/shader_map.tsv")
        if let size = try? map.resourceValues(forKeys: [.fileSizeKey]).fileSize, size < 64 * 1024 * 1024,
           let text = try? String(contentsOf: map, encoding: .utf8) {
            for row in text.split(separator: "\n") where !row.hasPrefix("#") {
                let c = row.split(separator: "\t", omittingEmptySubsequences: false)
                if c.count > 4 {
                    var path = String(c[4]).components(separatedBy: "#")[0]
                    while path.contains("/") && path.count > 1 {
                        hints.insert(path); path = (path as NSString).deletingLastPathComponent
                    }
                }
            }
        }
        let candidates = image.files.filter { file in
            let name = file.name.lowercased()
            var container = name
            for suffix in [".zs", ".szs"] where container.hasSuffix(suffix) { container.removeLast(suffix.count) }
            // Library and UI SARC containers can hold shaders without naming the
            // outer file after them. Their compressed forms use the same reader.
            let archive = [".sarc", ".blarc", ".sharcb"].contains { container.hasSuffix($0) }
            return archive || hints.contains(file.name) || name.contains("shader") || name.contains(".bfsha") || name.hasSuffix(".bnsh") ||
                ((name.hasPrefix("/effect/") || name.hasPrefix("/effectdata/")) && (name.hasSuffix(".zs") || name.hasSuffix(".szs")))
        }.sorted { $0.name < $1.name }
        var records: [String: Record] = [:], pairs = Set<String>()
        for (index, file) in candidates.enumerated() {
            if cancelled() { throw SatchelShaderError.cancelled }
            try autoreleasepool {
                var groups: [String: [SatchelShaderStage: Set<String>]] = [:]
                _ = try SatchelShaderDiscovery.discover(image.input(file), origin: file.name, decoder: decoder, cancelled: cancelled) { found in
                    if let old = records[found.key] {
                        if old.digest != SatchelShaderTranslator.sha256(found.request.record) {
                            // Archives may pad control headers differently beyond their declared
                            // key length. Require equal normalized code and translator inputs.
                            let prior = try SatchelShaderTranslator.normalized(SatchelShaderRequest(
                                record: Data(contentsOf: directory.appendingPathComponent(old.key + ".record")), stage: old.stage, expectedKey: old.key))
                            let current = try SatchelShaderTranslator.normalized(found.request)
                            guard old.stage == found.request.stage, prior.code == current.code,
                                  try JSONSerialization.data(withJSONObject: prior.job, options: .sortedKeys) == JSONSerialization.data(withJSONObject: current.job, options: .sortedKeys)
                            else { throw SatchelShaderError.invalidRequest }
                        }
                    } else {
                        guard records.count < 100_000 else { throw SatchelShaderDiscoveryError.limit }
                        let bytes = found.request.record
                        let free = (try fm.attributesOfFileSystem(forPath: directory.path)[.systemFreeSize] as? NSNumber)?.uint64Value ?? 0
                        guard free > reserve + UInt64(bytes.count) * 2 else { throw SatchelShaderCacheError.insufficientSpace }
                        try bytes.write(to: directory.appendingPathComponent(found.key + ".record"), options: .atomic)
                        records[found.key] = Record(key: found.key, stage: found.request.stage, digest: SatchelShaderTranslator.sha256(bytes))
                    }
                    if let program = found.program { groups[program, default: [:]][found.request.stage, default: []].insert(found.key) }
                }
                for group in groups.values {
                    if let v = group[.vertex], v.count == 1, let f = group[.fragment], f.count == 1 { pairs.insert(v.first! + "\t" + f.first!) }
                }
            }
            progress(index + 1, candidates.count)
        }
        let result = Self(manifest: manifest, records: records.values.sorted { $0.key < $1.key },
                          pairs: pairs.sorted().map { $0.components(separatedBy: "\t") }, scannedFiles: candidates.count)
        try JSONEncoder().encode(result).write(to: directory.appendingPathComponent("inventory.json"), options: .atomic)
        return result
    }
    func request(_ row: Record, directory: URL, wanted: Set<Int> = []) throws -> SatchelShaderRequest {
        guard NativePreparationRecipe.hex(row.key, count: 16) else { throw SatchelShaderError.invalidRequest }
        let url = directory.appendingPathComponent(row.key + ".record")
        let attrs = try url.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey])
        guard attrs.isRegularFile == true, attrs.isSymbolicLink != true, (attrs.fileSize ?? Int.max) <= 17 * 1024 * 1024 + 0x38 else { throw SatchelShaderError.invalidRequest }
        let data = try Data(contentsOf: url)
        guard SatchelShaderTranslator.sha256(data) == row.digest else { throw SatchelShaderError.invalidRequest }
        return SatchelShaderRequest(record: data, stage: row.stage, wantedPixelLocations: wanted, expectedKey: row.key)
    }
}
