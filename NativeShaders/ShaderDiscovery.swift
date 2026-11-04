import Foundation

public enum SatchelShaderDiscoveryError: Error { case bounds, malformedArchive, limit, decoderRequired }

// The caller supplies authenticated reads, for example SwitchRomFS.read(file:offset:count:).
// No filesystem URL or title-specific content layout is part of this interface.
public struct SatchelShaderInput {
    public let count: UInt64
    private let reader: (UInt64, Int) throws -> Data
    public init(count: UInt64, read: @escaping (UInt64, Int) throws -> Data) { self.count = count; reader = read }
    public func read(offset: UInt64, count size: Int) throws -> Data {
        guard size >= 0, size <= 1024 * 1024, offset <= count, UInt64(size) <= count - offset else {
            throw SatchelShaderDiscoveryError.bounds
        }
        let data = try reader(offset, size)
        guard data.count == size else { throw SatchelShaderDiscoveryError.bounds }
        return data.subdata(in: data.startIndex..<data.endIndex)
    }
    public func slice(offset: UInt64, count size: UInt64) throws -> SatchelShaderInput {
        guard offset <= count, size <= count - offset else { throw SatchelShaderDiscoveryError.bounds }
        return SatchelShaderInput(count: size) { try self.read(offset: offset + $0, count: $1) }
    }
    func bytes(_ offset: UInt64, _ size: Int) throws -> Data {
        guard size >= 0, size <= 17 * 1024 * 1024, offset <= count, UInt64(size) <= count - offset else {
            throw SatchelShaderDiscoveryError.bounds
        }
        var result = Data(); result.reserveCapacity(size)
        while result.count < size {
            result.append(try read(offset: offset + UInt64(result.count), count: min(1024 * 1024, size - result.count)))
        }
        return result
    }
    func u32(_ offset: UInt64) throws -> UInt32 { try ShaderRecord.word(read(offset: offset, count: 4), 0) }
    func u64(_ offset: UInt64) throws -> UInt64 {
        try read(offset: offset, count: 8).withUnsafeBytes { UInt64(littleEndian: $0.loadUnaligned(as: UInt64.self)) }
    }
}

public struct SatchelDiscoveredShader {
    public let key: String
    public let request: SatchelShaderRequest
    public let origin: String
    // An archive program variation, never a guessed vertex/pixel cross product.
    public let program: String?
}

public struct SatchelShaderDiscoverySummary {
    public var records = 0
    public var recognizedArchives = 0
    public var unknownFiles = 0
    public var runtimeCaptureRequired: Bool { unknownFiles > 0 || records == 0 }
}

// A decoder lends a bounded random-access view while body executes, then releases
// its spill file. Dictionary resolution belongs to the decoder's content view.
public protocol SatchelShaderArchiveDecoder {
    func withExpanded(_ input: SatchelShaderInput, body: (SatchelShaderInput) throws -> Void) throws
}

public enum SatchelShaderDiscovery {
    private static let stages: [SatchelShaderStage] = [.vertex, .hull, .domain, .geometry, .fragment, .compute]

    public static func discover(_ input: SatchelShaderInput, origin: String,
                                decoder: SatchelShaderArchiveDecoder? = nil,
                                cancelled: () -> Bool = { false },
                                emit: (SatchelDiscoveredShader) throws -> Void) throws -> SatchelShaderDiscoverySummary {
        var summary = SatchelShaderDiscoverySummary()
        try scan(input, origin: origin, depth: 0, decoder: decoder, cancelled: cancelled, summary: &summary, emit: emit)
        return summary
    }

    private static func emitRecord(control: Data, code: Data, expectedStage: SatchelShaderStage?,
                                   origin: String, program: String?, summary: inout SatchelShaderDiscoverySummary,
                                   emit: (SatchelDiscoveredShader) throws -> Void) throws {
        let stage = try SatchelShaderTranslator.stage(control: control, code: code)
        guard expectedStage == nil || expectedStage == stage else { throw SatchelShaderDiscoveryError.malformedArchive }
        let request = try SatchelShaderRequest(control: control, code: code, stage: stage)
        let key = try SatchelShaderTranslator.normalized(request).key
        guard summary.records < 100_000 else { throw SatchelShaderDiscoveryError.limit }
        summary.records += 1
        try emit(SatchelDiscoveredShader(key: key, request: request, origin: origin, program: program))
    }

    private static func scan(_ input: SatchelShaderInput, origin: String, depth: Int,
                             decoder: SatchelShaderArchiveDecoder?, cancelled: () -> Bool,
                             summary: inout SatchelShaderDiscoverySummary,
                             emit: (SatchelDiscoveredShader) throws -> Void) throws {
        if cancelled() { throw SatchelShaderError.cancelled }
        guard depth <= 16, origin.utf8.count <= 8192,
              summary.recognizedArchives + summary.unknownFiles < 100_000 else { throw SatchelShaderDiscoveryError.limit }
        guard input.count >= 4 else { summary.unknownFiles += 1; return }
        let magic = try input.read(offset: 0, count: 4)
        if magic == Data([0x28, 0xb5, 0x2f, 0xfd]) || magic == Data("Yaz0".utf8) {
            guard let decoder else { throw SatchelShaderDiscoveryError.decoderRequired }
            try decoder.withExpanded(input) { expanded in
                try scan(expanded, origin: origin, depth: depth + 1, decoder: decoder, cancelled: cancelled, summary: &summary, emit: emit)
            }
            return
        }
        if magic == Data("SARC".utf8) {
            summary.recognizedArchives += 1
            try sarcMembers(input, cancelled: cancelled) { name, member in
                try scan(member, origin: origin + "/" + name, depth: depth + 1, decoder: decoder,
                         cancelled: cancelled, summary: &summary, emit: emit)
            }
            return
        }
        if magic == Data("BAHS".utf8) {
            summary.recognizedArchives += 1
            try bahs(input, origin: origin, cancelled: cancelled, summary: &summary, emit: emit)
            return
        }
        var offset: UInt64 = 0, found = false
        while offset < input.count {
            if cancelled() { throw SatchelShaderError.cancelled }
            let chunk = try input.read(offset: offset, count: Int(min(65536, input.count - offset)))
            var position = chunk.startIndex
            while let range = chunk.range(of: Data("BNSH".utf8), in: position..<chunk.endIndex) {
                let base = offset + UInt64(range.lowerBound)
                if base <= input.count, input.count - base >= 0x88,
                   try input.read(offset: base + 0x60, count: 4) == Data("grsc".utf8) {
                    summary.recognizedArchives += 1; found = true
                    try bnsh(input.slice(offset: base, count: input.count - base), origin: origin + "#BNSH@\(base)",
                             cancelled: cancelled, summary: &summary, emit: emit)
                }
                position = range.upperBound
            }
            if chunk.count <= 3 { break }
            offset += UInt64(chunk.count - 3)
        }
        if !found { summary.unknownFiles += 1 }
    }

    public static func sarcMembers(_ input: SatchelShaderInput, cancelled: () -> Bool = { false },
                                   visit: (String, SatchelShaderInput) throws -> Void) throws {
        guard try input.read(offset: 0, count: 4) == Data("SARC".utf8) else {
            throw SatchelShaderDiscoveryError.malformedArchive
        }
            let header = try input.read(offset: 0, count: 0x14)
            let headerSize = try ShaderRecord.short(header, 4)
            guard try ShaderRecord.short(header, 6) == 0xfeff, headerSize >= 0x14 else {
                throw SatchelShaderDiscoveryError.malformedArchive
            }
            let dataOffset = UInt64(try ShaderRecord.word(header, 12))
            let table = try input.read(offset: UInt64(headerSize), count: 12)
            guard table.prefix(4) == Data("SFAT".utf8) else { throw SatchelShaderDiscoveryError.malformedArchive }
            let count = try ShaderRecord.short(table, 6)
            let names = UInt64(headerSize + 12 + 16 * count + 8)
            guard names <= dataOffset, dataOffset <= input.count else { throw SatchelShaderDiscoveryError.malformedArchive }
            for i in 0..<count {
                if cancelled() { throw SatchelShaderError.cancelled }
                let row = try input.read(offset: UInt64(headerSize + 12 + 16 * i), count: 16)
                let attr = try ShaderRecord.word(row, 4), start = UInt64(try ShaderRecord.word(row, 8)), end = UInt64(try ShaderRecord.word(row, 12))
                guard start <= end, end <= input.count - dataOffset else { throw SatchelShaderDiscoveryError.malformedArchive }
                var name = "#\(i)"
                if attr >> 24 != 0 {
                    let nameOffset = names + UInt64(attr & 0xffffff) * 4
                    guard nameOffset < dataOffset else { throw SatchelShaderDiscoveryError.malformedArchive }
                    let bytes = try input.read(offset: nameOffset, count: Int(min(4096, dataOffset - nameOffset)))
                    guard let zero = bytes.firstIndex(of: 0), let decoded = String(data: bytes.prefix(upTo: zero), encoding: .utf8) else {
                        throw SatchelShaderDiscoveryError.malformedArchive
                    }
                    name = decoded
                }
                try visit(name, input.slice(offset: dataOffset + start, count: end - start))
            }
    }

    private static func bnsh(_ input: SatchelShaderInput, origin: String, cancelled: () -> Bool,
                             summary: inout SatchelShaderDiscoverySummary, emit: (SatchelDiscoveredShader) throws -> Void) throws {
        let count = Int(try input.u32(0x7c)), variations = try input.u64(0x80)
        guard count <= 100_000, variations <= input.count, UInt64(count) * 0x40 <= input.count - variations else {
            throw SatchelShaderDiscoveryError.malformedArchive
        }
        for v in 0..<count {
            if cancelled() { throw SatchelShaderError.cancelled }
            let program = try input.u64(variations + UInt64(v * 0x40) + 0x10)
            if program == 0 { continue }
            guard program <= input.count, input.count - program >= 56 else { throw SatchelShaderDiscoveryError.bounds }
            for (i, stage) in stages.enumerated() {
                let descriptor = try input.u64(program + 8 + UInt64(i * 8))
                if descriptor == 0 { continue }
                guard descriptor <= input.count, input.count - descriptor >= 32 else { throw SatchelShaderDiscoveryError.bounds }
                let control = try input.u64(descriptor + 8), code = try input.u64(descriptor + 16)
                let codeSize = Int(try input.u32(descriptor + 24)), controlSize = Int(try input.u32(descriptor + 28))
                guard controlSize <= 1024 * 1024 else { throw SatchelShaderDiscoveryError.limit }
                try emitRecord(control: input.bytes(control, controlSize), code: input.bytes(code, codeSize), expectedStage: stage,
                    origin: origin + "#var\(v)/\(stage.rawValue)", program: origin + "#var\(v)", summary: &summary, emit: emit)
            }
        }
    }

    private static func bahs(_ input: SatchelShaderInput, origin: String, cancelled: () -> Bool,
                             summary: inout SatchelShaderDiscoverySummary, emit: (SatchelDiscoveredShader) throws -> Void) throws {
        let regionSize = UInt64(try input.u32(0x18)), region = UInt64(try input.u32(0x1c))
        guard region <= input.count, regionSize <= input.count - region, region <= 64 * 1024 * 1024 else {
            throw SatchelShaderDiscoveryError.bounds
        }
        var offset: UInt64 = 0
        var descriptors: [(address: UInt64, control: UInt64, size: Int, code: UInt64, codeSize: Int)] = []
        while offset + 16 < region {
            if cancelled() { throw SatchelShaderError.cancelled }
            let chunk = try input.read(offset: offset, count: Int(min(65536, region - offset)))
            for local in stride(from: 0, to: max(0, chunk.count - 16), by: 4) {
                let codeOffset = UInt64(try ShaderRecord.word(chunk, local)), zero = try ShaderRecord.word(chunk, local + 4)
                let size = Int(try ShaderRecord.word(chunk, local + 8)), relative = Int64(Int32(bitPattern: try ShaderRecord.word(chunk, local + 12)))
                if zero != 0 || !(size == 0x88c || (0x760...0x880).contains(size)) || codeOffset % 256 != 0 { continue }
                let address = offset + UInt64(local), controlOffset = Int64(address) + relative
                if controlOffset < 0 || UInt64(controlOffset) > region || UInt64(size) > region - UInt64(controlOffset) { continue }
                let ctl = try input.bytes(UInt64(controlOffset), size)
                if try ShaderRecord.word(ctl, 0) != 0x98761234 { continue }
                let codeSize = Int(try ShaderRecord.word(ctl, 0x704))
                if codeOffset > regionSize || UInt64(codeSize) > regionSize - codeOffset { continue }
                // BAHS discovery scans possible descriptors, as nxnative bahs_shaders does.
                // Only validated shader records become entries, not coincidental table bytes.
                guard codeSize >= 32, codeSize <= 16 * 1024 * 1024 else { continue }
                let code = try input.bytes(region + codeOffset, codeSize)
                let magic = try ShaderRecord.word(code, 0)
                guard magic == 0x12345678 || magic == 0x12345679,
                      let stage = try? SatchelShaderTranslator.stage(control: ctl, code: code),
                      let request = try? SatchelShaderRequest(control: ctl, code: code, stage: stage),
                      (try? SatchelShaderTranslator.normalized(request)) != nil else { continue }
                guard descriptors.count < 100_000 else { throw SatchelShaderDiscoveryError.limit }
                descriptors.append((address, UInt64(controlOffset), size, region + codeOffset, codeSize))
            }
            if chunk.count <= 16 { break }
            offset += UInt64((chunk.count - 16) / 4 * 4)
        }
        let programs = (try? bahsPrograms(input, entries: Set(descriptors.map(\.address)), origin: origin)) ?? [:]
        for descriptor in descriptors {
            if cancelled() { throw SatchelShaderError.cancelled }
            try emitRecord(control: input.bytes(descriptor.control, descriptor.size),
                code: input.bytes(descriptor.code, descriptor.codeSize), expectedStage: nil,
                origin: origin + "#BAHS@\(descriptor.address)", program: programs[descriptor.address], summary: &summary, emit: emit)
        }
    }

    private static func bahsPrograms(_ input: SatchelShaderInput, entries: Set<UInt64>, origin: String) throws -> [UInt64: String] {
        guard let first = entries.min(), first >= 0x38 else { return [:] }
        let array = first - 0x18, size = UInt64(try input.u32(array)), count = Int(try input.u32(array + 4))
        guard size <= input.count - array, count >= entries.count, count <= 100_000 else { return [:] }
        let end = array + size
        var offset = array + 8, records: [(UInt64, UInt32)] = []
        for _ in 0..<count {
            guard offset <= end, end - offset >= 0x20 else { return [:] }
            let length = UInt64(try input.u32(offset)), kind = try input.u32(offset + 4)
            guard length >= 0x20, length <= end - offset else { return [:] }
            records.append((offset + 0x10, kind)); offset += length
        }
        guard offset == end, entries.isSubset(of: Set(records.map { $0.0 })), input.count - offset >= 8 else { return [:] }
        let programSize = UInt64(try input.u32(offset)), programCount = Int(try input.u32(offset + 4))
        guard programSize <= input.count - offset, programCount > 0, programCount <= 100_000 else { return [:] }
        let programEnd = offset + programSize
        var cursor = offset + 8, bases: [Int] = []
        for _ in 0..<programCount {
            guard cursor <= programEnd, programEnd - cursor >= 16 else { return [:] }
            let length = UInt64(try input.u32(cursor)), base = Int(try input.u32(cursor + 12))
            guard length >= 16, length <= programEnd - cursor, base <= count, base >= (bases.last ?? 0) else { return [:] }
            bases.append(base); cursor += length
        }
        guard cursor == programEnd else { return [:] }
        bases.append(count)
        var result: [UInt64: String] = [:]
        for p in 0..<programCount {
            var seen = [0, 0]
            for i in bases[p]..<bases[p + 1] {
                let (address, kind) = records[i]
                if kind > 1 { continue }
                if entries.contains(address) { result[address] = origin + "#BAHS#prog\(p)#var\(seen[Int(kind)])" }
                seen[Int(kind)] += 1
            }
        }
        return result
    }
}
