import Foundation

/// Packaged CNMT, independent native implementation of LibHac 0.19.0 layouts.
/// Retail's trailing development digest is not a self-hash. Its containing Meta
/// NCA and PFS hash chain are verified by the package reader.
struct SwitchContentMeta {
    struct Content {
        let id: String
        let hash: Data
        let size: UInt64
        let type: UInt8
        let idOffset: UInt8
    }
    let titleID: UInt64
    let version: UInt32
    let type: UInt8
    let applicationID: UInt64
    let patchID: UInt64
    let contents: [Content]
    var requiredApplicationVersion: UInt32 = 0
    var dataPatchID: UInt64 = 0

    static func parse(_ input: Data, metaTitleID: UInt64) throws -> Self {
        let b = Data(input)
        guard b.count >= 0x50, b.count <= 16 * 1024 * 1024, b.cp64(0) == metaTitleID,
              metaTitleID != 0 else { throw SwitchExecutableError.invalid }
        // CNMT attribute bit 3 is ProperProgramExists (system 20+). It does
        // not alter table layout. Compacted and unknown variants stay rejected.
        let type = b[12], extended = Int(b.cp16(14))
        guard [0x80, 0x81, 0x82].contains(type), b[13] == 0, b[0x16] == 0,
              b[0x14] & ~UInt8(8) == 0, b[0x17] <= 1,
              b[0x1c..<0x20].allSatisfy({ $0 == 0 }),
              (type == 0x82 ? [16, 24].contains(extended) : extended == (type == 0x80 ? 16 : 24)) else { throw SwitchExecutableError.unsupported }
        guard b.count >= 32 + extended + 32 else { throw SwitchExecutableError.invalid }
        // AddOnContent extended headers are 0x10 before system 15 and 0x18 afterward.
        // Layout: https://switchbrew.org/wiki/CNMT#AddOnContentMetaExtendedHeader
        if type == 0x82 {
            guard b.cp64(32) != 0, b[0x2c] <= 1, b[0x2d..<0x30].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.unsupported }
        }
        let count = Int(b.cp16(16)), metaCount = Int(b.cp16(18)), start = 32 + extended
        guard count > 0, count <= 4096, metaCount <= 4096 else { throw SwitchExecutableError.limit }
        let end = start + count * 56 + metaCount * 16
        let extendedSize = type == 0x81 ? Int(b.cp32(0x2c)) : 0
        guard end <= b.count - 32, extendedSize == b.count - 32 - end else { throw SwitchExecutableError.invalid }
        var contents: [Content] = [], ids = Set<String>(), types = Set<UInt16>()
        for i in 0..<count {
            let p = start + i * 56, id = b.subdata(in: p + 32..<p + 48)
            let hash = b.subdata(in: p..<p + 32), kind = b[p + 54], offset = b[p + 55]
            let size = UInt64(b.cp32(p + 48)) | UInt64(b[p + 52]) << 32
            guard id.contains(where: { $0 != 0 }), ids.insert(id.cpHex).inserted,
                  (1...6).contains(kind), size >= 0xc00 else { throw SwitchExecutableError.invalid }
            // Multiple delta fragments may share an id offset. They are transport
            // artifacts, never a source of final Program bytes.
            if kind != 6 {
                guard types.insert(UInt16(kind) << 8 | UInt16(offset)).inserted else { throw SwitchExecutableError.invalid }
            }
            guard b[p + 53] == 0 else { throw SwitchExecutableError.unsupported }
            guard hash.prefix(16) == id else { throw SwitchExecutableError.integrity }
            contents.append(Content(id: id.cpHex, hash: hash, size: size, type: kind, idOffset: offset))
        }
        return Self(titleID: metaTitleID, version: b.cp32(8), type: type,
                    applicationID: type == 0x80 ? metaTitleID : b.cp64(32),
                    patchID: type == 0x80 ? b.cp64(32) : (type == 0x81 ? metaTitleID : 0), contents: contents,
                    requiredApplicationVersion: type == 0x82 ? b.cp32(0x28) : 0,
                    dataPatchID: type == 0x82 && extended == 24 ? b.cp64(0x30) : 0)
    }

    static func select(base: [Self], update: [Self]?, applicationID: UInt64) throws -> (Self, Self?) {
        let matches = base.filter { $0.type == 0x80 && $0.titleID == applicationID }
        guard matches.count == 1 else { throw matches.isEmpty ? SwitchExecutableError.noProgram : .ambiguous }
        let selected = matches[0]
        guard selected.contents.contains(where: { $0.type == 1 }) else { throw SwitchExecutableError.noProgram }
        guard let update else { return (selected, nil) }
        // An update NSP can carry DLC beside its patch. Those records are prepared separately.
        let patches = update.filter { $0.type != 0x82 }
        guard patches.count == 1, let patch = patches.first, patch.type == 0x81,
              patch.applicationID == selected.titleID, patch.titleID == selected.patchID,
              patch.version > selected.version else { throw SwitchExecutableError.unrelatedUpdate }
        guard patch.contents.contains(where: { $0.type == 1 }) else { throw SwitchExecutableError.noProgram }
        return (selected, patch)
    }
}

extension Data {
    var cpHex: String { map { String(format: "%02x", $0) }.joined() }
    func cp16(_ p: Int) -> UInt16 { UInt16(self[p]) | UInt16(self[p + 1]) << 8 }
    func cp32(_ p: Int) -> UInt32 { (0..<4).reduce(0) { $0 | UInt32(self[p + $1]) << ($1 * 8) } }
    func cp64(_ p: Int) -> UInt64 { (0..<8).reduce(0) { $0 | UInt64(self[p + $1]) << ($1 * 8) } }
    mutating func cpPut(_ value: UInt64, at offset: Int, count: Int) {
        for i in 0..<count { self[offset + i] = UInt8(truncatingIfNeeded: value >> (i * 8)) }
    }
}
