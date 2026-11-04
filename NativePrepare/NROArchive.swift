import Foundation

/// Bounded port of pinned nxnative dm/gfx/tools/smash_arc.py (MIT).
/// The index prioritizes current NROs. It never exempts an unresolved NRR hash:
/// historical registered copies still go through the complete fallback scan.
enum PrepareNROArchive {
    struct Member { let offset: UInt64; let stored: UInt64; let size: UInt64 }
    static func members(_ input: SwitchContentStorage, cancelled: @escaping () -> Bool) throws -> [Member]? {
        guard input.size >= 0x38 else { return nil }
        let header = try input.bytes(0, 0x38)
        guard header.cp64(0) == 0xabcdef9876543210 else { return nil }
        let dataBase = header.cp64(0x10), indexOffset = header.cp64(0x20)
        let indexHeader = try input.bytes(indexOffset, 16)
        let size = UInt64(indexHeader.cp32(4)), stored = UInt64(indexHeader.cp32(8))
        guard indexHeader.cp32(0) == 16, size >= 0x58, size <= 64 * 1024 * 1024,
              indexOffset <= input.size - 16, stored > 0, stored <= input.size - indexOffset - 16 else { throw SwitchExecutableError.invalid }
        let bytes = try SwitchZstdReader.decode(SwitchContentStorage(size: stored) { at, count in
            try input.bytes(indexOffset + 16 + at, count)
        }, bytes: size, cancelled: cancelled)
        let declared = UInt64(bytes.cp32(0))
        guard declared <= size, declared >= 0x58 else { throw SwitchExecutableError.invalid }
        func count(_ at: Int) throws -> UInt64 {
            let value = UInt64(bytes.cp32(at)); guard value <= 1_000_000 else { throw SwitchExecutableError.limit }; return value
        }
        let paths = try count(4), indices = try count(8), directories = try count(12)
        let directoryOffsets = try count(16) + count(36) + count(0x40)
        let childHashes = try count(20), infos = try count(24) + count(40) + count(0x44)
        let subinfos = try count(28) + count(40) + count(0x50), data = try count(32) + count(40) + count(0x44)
        var offset: UInt64 = 0x58
        func table(_ count: UInt64, _ stride: UInt64) throws -> Int {
            guard count <= 4_000_000, offset <= declared, count <= (declared - offset) / stride else { throw SwitchExecutableError.invalid }
            let start = offset; offset += count * stride; return Int(start)
        }
        _ = try table(UInt64(bytes[0x38]), 12)
        let stream = try table(1, 16)
        _ = try table(count(stream), 12); _ = try table(count(stream+4), 20)
        _ = try table(count(stream+8), 4); _ = try table(count(stream+12), 16)
        let hashes = try table(1, 8)
        _ = try table(count(hashes+4), 8); _ = try table(count(hashes), 8)
        let p = try table(paths, 32), idx = try table(indices, 8)
        _ = try table(directories, 8); _ = try table(directories, 0x34)
        let dir = try table(directoryOffsets, 0x1c); _ = try table(childHashes, 8)
        let info = try table(infos, 16), sub = try table(subinfos, 12), file = try table(data, 16)
        var result: [Member] = [], seen = Set<UInt64>()
        for i in 0..<Int(paths) {
            if cancelled() { throw CancellationError() }
            let entry = p + i * 32
            let ext = UInt64(bytes.cp32(entry+8)) | UInt64(bytes[entry+12]) << 32
            guard ext == 0x37d0c5885 else { continue }
            let index = UInt64(bytes.cp32(entry+4) >> 8)
            guard index < indices else { throw SwitchExecutableError.invalid }
            let fileInfo = UInt64(bytes.cp32(idx + Int(index)*8+4))
            guard fileInfo < infos else { throw SwitchExecutableError.invalid }
            let first = UInt64(bytes.cp32(info + Int(fileInfo)*16+8))
            let flags = bytes.cp32(info + Int(fileInfo)*16+12)
            let variants = flags & 0x8000 != 0 ? UInt64(bytes[0x38]) : 1
            guard variants > 0, first <= subinfos, variants <= subinfos - first else { throw SwitchExecutableError.invalid }
            for variant in first..<first+variants {
                let row = sub + Int(variant)*12
                let d = UInt64(bytes.cp32(row)), f = UInt64(bytes.cp32(row+4))
                guard d < directoryOffsets, f < data else { throw SwitchExecutableError.invalid }
                let region = bytes.cp64(dir + Int(d)*0x1c), record = file + Int(f)*16
                let within = UInt64(bytes.cp32(record))*4, stored = UInt64(bytes.cp32(record+4)), size = UInt64(bytes.cp32(record+8))
                guard dataBase <= input.size, region <= input.size - dataBase, within <= input.size - dataBase - region else { throw SwitchExecutableError.invalid }
                let at = dataBase + region + within
                guard stored > 0, stored <= input.size - at, size >= 0x80, size <= 512 * 1024 * 1024 else { throw SwitchExecutableError.invalid }
                if seen.insert(at).inserted { result.append(Member(offset: at, stored: stored, size: size)) }
            }
        }
        return result.sorted { $0.offset < $1.offset }
    }
}
