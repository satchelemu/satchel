import Foundation

/// Read-only BKTR composition. Layouts checked against LibHac 0.19.0
/// BucketTree, IndirectStorage and AesCtrCounterExtendedStorage.
/// Metadata is bounded independently of the size of the virtual section.
struct SwitchBucketTree {
    let entries: [Data]
    let end: UInt64

    init(header: Data, size: UInt64, entrySize: Int, cancelled: () -> Bool, progress: SwitchByteProgress = { _, _ in },
         read: (UInt64, UInt64) throws -> Data) throws {
        guard header.count == 16, header.prefix(4) == Data("BKTR".utf8),
              header.cp32(4) <= 1, header.cp32(12) == 0,
              [16, 20, 24].contains(entrySize) else { throw SwitchExecutableError.invalid }
        let count = Int(header.cp32(8)), nodeSize = 0x4000, perSet = (nodeSize - 16) / entrySize
        // Bound serialized routing tables to 64 MiB, including the second index level.
        guard count > 0, count <= 2_000_000 else { throw SwitchExecutableError.limit }
        let sets = (count + perSet - 1) / perSet, offsets = (nodeSize - 16) / 8
        let groups = (sets + offsets - 1) / offsets
        let children = sets <= offsets ? 0 : (sets - (offsets - (groups - 1)) + offsets - 1) / offsets
        let nodes = 1 + children, total = UInt64((sets + nodes) * nodeSize)
        guard total <= 64 * 1024 * 1024 else { throw SwitchExecutableError.limit }
        guard total <= size else { throw SwitchExecutableError.invalid }
        progress(0, total)
        let root = try read(0, UInt64(nodeSize))
        guard root.count == nodeSize, root.cp32(0) == 0,
              root.cp32(4) == (children == 0 ? sets : children),
              root.cp64(8) > 0, root.cp64(8) <= UInt64(Int64.max) else { throw SwitchExecutableError.invalid }
        end = root.cp64(8)
        var starts: [UInt64] = []
        if children == 0 {
            starts = (0..<sets).map { root.cp64(16 + $0 * 8) }
        } else {
            // LibHac BucketTree.Builder packs direct entry-set offsets after
            // the child pointers in L1; later entry sets live in L2 nodes.
            starts = (children..<offsets).map { root.cp64(16 + $0 * 8) }
            for child in 0..<children {
                if cancelled() { throw CancellationError() }
                let block = try read(UInt64((child + 1) * nodeSize), UInt64(nodeSize))
                let n = min(offsets, sets - starts.count)
                let limit = child + 1 == children ? end : root.cp64(16 + (child + 1) * 8)
                guard block.count == nodeSize, block.cp32(0) == child, block.cp32(4) == n,
                      block.cp64(8) == limit, block.cp64(16) == root.cp64(16 + child * 8) else {
                    throw SwitchExecutableError.invalid
                }
                starts += (0..<n).map { block.cp64(16 + $0 * 8) }
                progress(UInt64((child + 2) * nodeSize), total)
            }
        }
        guard starts.count == sets, starts.first == 0,
              starts.last! < end, zip(starts, starts.dropFirst()).allSatisfy({ $0 < $1 }) else {
            throw SwitchExecutableError.invalid
        }
        var result: [Data] = []; result.reserveCapacity(count)
        var previousEnd: UInt64 = 0
        for set in 0..<sets {
            if cancelled() { throw CancellationError() }
            let block = try read(UInt64((set + nodes) * nodeSize), UInt64(nodeSize))
            let n = min(perSet, count - result.count)
            guard block.count == nodeSize, block.cp32(0) == set, block.cp32(4) == n,
                  block.cp64(8) > previousEnd, block.cp64(8) == (set + 1 == sets ? end : starts[set + 1]),
                  block.cp64(16) == previousEnd, starts[set] == previousEnd else {
                throw SwitchExecutableError.invalid
            }
            var previous: UInt64?
            for i in 0..<n {
                let p = 16 + i * entrySize, start = block.cp64(p)
                guard start < block.cp64(8), previous == nil || start > previous! else { throw SwitchExecutableError.invalid }
                result.append(block.subdata(in: p..<p + entrySize)); previous = start
            }
            previousEnd = block.cp64(8)
            progress(UInt64((set + nodes + 1) * nodeSize), total)
        }
        guard previousEnd == end, result.count == count else { throw SwitchExecutableError.invalid }
        entries = result
    }

    func index(at offset: UInt64) throws -> Int {
        guard offset < end else { throw SwitchExecutableError.invalid }
        var lo = 0, hi = entries.count
        while lo + 1 < hi {
            let mid = (lo + hi) / 2
            if entries[mid].cp64(0) <= offset { lo = mid } else { hi = mid }
        }
        return lo
    }
    func limit(_ index: Int) -> UInt64 { index + 1 == entries.count ? end : entries[index + 1].cp64(0) }
}

struct SwitchContentStorage {
    let size: UInt64
    let read: (UInt64, UInt64) throws -> Data

    func bytes(_ offset: UInt64, _ count: UInt64) throws -> Data {
        guard count <= 16 * 1024 * 1024, offset <= size, count <= size - offset else { throw SwitchExecutableError.invalid }
        if count == 0 { return Data() }
        let data = try read(offset, count)
        guard data.count == count else { throw SwitchExecutableError.invalid }
        return data
    }

    static func indirect(_ tree: SwitchBucketTree, base: Self, patch: Self,
                         cancelled: @escaping () -> Bool) throws -> Self {
        for (i, entry) in tree.entries.enumerated() {
            let storage = entry.cp32(16), physical = entry.cp64(8), length = tree.limit(i) - entry.cp64(0)
            guard storage <= 1 else { throw SwitchExecutableError.invalid }
            let size = storage == 0 ? base.size : patch.size
            guard physical <= size, length <= size - physical else { throw SwitchExecutableError.invalid }
        }
        return Self(size: tree.end) { offset, count in
            var out = Data(), position = offset
            while position < offset + count {
                if cancelled() { throw CancellationError() }
                let i = try tree.index(at: position), entry = tree.entries[i]
                let length = min(offset + count - position, tree.limit(i) - position)
                let physical = entry.cp64(8) + (position - entry.cp64(0))
                out.append(try (entry.cp32(16) == 0 ? base : patch).bytes(physical, length))
                position += length
            }
            return out
        }
    }
}

// NCA compression layout/read order ported from LibHac 0.19.0
// Tools.FsSystem.CompressedStorage (MIT, Copyright Alex Barney).
// Routing and compressed payload are read only through the verified IVFC view.
extension SwitchContentStorage {
    static func compressed(header: Data, verified: Self, cancelled: @escaping () -> Bool) throws -> Self {
        guard header.count == 512, header[0x198..<0x1a0].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.invalid }
        let offset = header.cp64(0x178), size = header.cp64(0x180)
        if offset == 0 && size == 0 {
            guard header[0x188..<0x198].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.invalid }
            return verified
        }
        guard offset > 0, size > 0, offset <= verified.size, size <= verified.size - offset else { throw SwitchExecutableError.invalid }
        let tree = try SwitchBucketTree(header: header.subdata(in: 0x188..<0x198), size: size,
            entrySize: 24, cancelled: cancelled) { at, count in try verified.bytes(offset + at, count) }
        for (i, entry) in tree.entries.enumerated() {
            let physical = entry.cp64(8), length = tree.limit(i) - entry.cp64(0), stored = UInt64(entry.cp32(20)), kind = entry[16]
            guard entry[18] == 0, entry[19] == 0 else { throw SwitchExecutableError.invalid }
            guard [0, 1, 3].contains(kind) else {
                throw SwitchExecutableError.unsupportedFeature("NCA compression type \(kind) is not supported.")
            }
            if kind == 1 { continue }
            let count = kind == 0 ? length : stored
            guard physical <= offset, count <= offset - physical else { throw SwitchExecutableError.invalid }
            if kind == 3 {
                guard stored > 0, stored <= 16 * 1024 * 1024, length <= 16 * 1024 * 1024 else { throw SwitchExecutableError.limit }
            }
        }
        // One decoded block, at most 16 MiB, shared across sequential small reads.
        var cachedIndex: Int?, cached = Data()
        return Self(size: tree.end) { at, count in
            var result = Data(), position = at
            while position < at + count {
                if cancelled() { throw CancellationError() }
                let i = try tree.index(at: position), entry = tree.entries[i], start = entry.cp64(0)
                let take = min(at + count - position, tree.limit(i) - position), within = position - start
                switch entry[16] {
                case 0: result.append(try verified.bytes(entry.cp64(8) + within, take))
                case 1: result.append(Data(count: Int(take)))
                case 3:
                    if cachedIndex != i {
                        let input = try verified.bytes(entry.cp64(8), UInt64(entry.cp32(20)))
                        var output = Data(count: Int(tree.limit(i) - start))
                        let outputCount = output.count
                        let valid = input.withUnsafeBytes { source in output.withUnsafeMutableBytes { destination in
                            satchel_lz4_decode(source.bindMemory(to: UInt8.self).baseAddress, input.count,
                                          destination.bindMemory(to: UInt8.self).baseAddress, outputCount)
                        } }
                        guard valid else { throw SwitchExecutableError.invalid }
                        cached = output; cachedIndex = i
                    }
                    result.append(cached.subdata(in: Int(within)..<Int(within + take)))
                default: throw SwitchExecutableError.invalid
                }
                position += take
            }
            return result
        }
    }
}
