import CryptoKit
import Foundation

/// Numeric metadata from a CNMT-linked, verified Control archive. This neither
/// configures parental controls nor states whether unmounted DLC exists.
struct SwitchControlMetadata: Equatable {
    let applicationID: UInt64
    let applicationVersion: UInt32
    let programContentID: String
    let controlContentID: String
    let parentalControlFlags: UInt32
    let ratingAges: [Int8]
    let addOnContentBaseID: UInt64
    let metaHashBytes: UInt64
    let controlHashBytes: UInt64
    let metaVerifiedBlocks: Int
    let controlVerifiedBlocks: Int
    // Full encrypted Meta/Control hashes are verified. Program linkage uses
    // CNMT ID/size/type + NCA title; its full archive digest is NOT checked here.
    // Program ExeFS verification remains the responsibility of load(). Neither
    // publisher signatures nor the separate trailing CNMT digest are recomputed.
    // CNMT#Digest documents that production retains the development-file digest;
    // its bytes are covered by verified Meta PFS/archive hashes, not a retail
    // self-hash claim. https://switchbrew.org/wiki/CNMT#Digest
}

/// Independent bounded parser. Layout facts: installed LibHac0.19.0
/// cd4004af194a51712812c680f0a028ba9152c0f8; Atmosphere6b0c077b
/// ncm_content_meta.hpp / ncm_content_info.hpp (modern40-bit size+attributes);
/// libnx b156b477 nacp.h. No reference implementation code copied.
struct SwitchApplicationMeta {
    struct Content {
        let id: String
        let hash: Data
        let size: UInt64
        let type: UInt8
        let idOffset: UInt8
    }
    let applicationID: UInt64
    let version: UInt32
    let contents: [Content]

    static func parse(_ input: Data, metaTitleID: UInt64) throws -> Self {
        guard input.count >= 0x50, input.count <= 1024 * 1024 else { throw SwitchExecutableError.invalid }
        let bytes = Data(input) // Normalize Data slices before integer indexing.
        guard bytes.sc64(0) != 0, bytes.sc64(0) == metaTitleID else { throw SwitchExecutableError.invalid }
        // Only base Application, Nx platform, ordinary full install. Patches,
        // compacted metadata and platform variants require separate composition.
        guard bytes[0xC] == 0x80, bytes[0xD] == 0, bytes.sc16(0xE) == 0x10,
              bytes[0x14] == 0, bytes[0x16] == 0, bytes[0x17] <= 1,
              bytes[0x1C..<0x20].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.unsupported }
        let count = Int(bytes.sc16(0x10)), metaCount = Int(bytes.sc16(0x12))
        guard count > 0, count <= 4096, metaCount <= 4096 else { throw SwitchExecutableError.limit }
        let tableEnd = 0x30 + count * 0x38 + metaCount * 0x10
        guard tableEnd + 32 == bytes.count else { throw SwitchExecutableError.invalid }
        var ids = Set<String>(), types = Set<UInt16>(), contents: [Content] = []
        for index in 0..<count {
            let p = 0x30 + index * 0x38
            let hash = bytes.subdata(in: p..<(p + 32)), idData = bytes.subdata(in: (p + 32)..<(p + 48))
            let id = idData.scHex, type = bytes[p + 0x36], idOffset = bytes[p + 0x37]
            let size = UInt64(bytes.sc32(p + 0x30)) | UInt64(bytes[p + 0x34]) << 32
            guard type >= 1, type <= 6, size >= 0xC00, idData.contains(where: { $0 != 0 }),
                  ids.insert(id).inserted, types.insert(UInt16(type) << 8 | UInt16(idOffset)).inserted else { throw SwitchExecutableError.invalid }
            guard hash.prefix(16) == idData else { throw SwitchExecutableError.integrity }
            // Nonzero attributes need their own transport/composition contract.
            guard bytes[p + 0x35] == 0 else { throw SwitchExecutableError.unsupported }
            contents.append(Content(id: id, hash: hash, size: size, type: type, idOffset: idOffset))
        }
        return Self(applicationID: metaTitleID, version: bytes.sc32(8), contents: contents)
    }

    func selected(programID: String) throws -> (program: Content, control: Content) {
        let programs = contents.filter { $0.type == 1 && $0.id == programID }
        guard programs.count == 1 else { throw SwitchExecutableError.invalid }
        // Do not infer which Control belongs to a nonzero Program id_offset.
        // The currently admitted profile has exactly one Program and Control,
        // both offset0. Other valid layouts remain explicit unsupported cases.
        let allPrograms = contents.filter { $0.type == 1 }, controls = contents.filter { $0.type == 3 }
        guard allPrograms.count == 1, controls.count == 1,
              programs[0].idOffset == 0, controls[0].idOffset == 0 else { throw SwitchExecutableError.unsupported }
        return (programs[0], controls[0])
    }
}

struct SwitchControlValues {
    let parentalFlags: UInt32
    let ratings: [Int8]
    let addOnBaseID: UInt64
    static func parse(_ input: Data) throws -> Self {
        // Fixed Nx ApplicationControlProperty; do not decode title/author text.
        guard input.count == 0x4000 else { throw SwitchExecutableError.invalid }
        let nacp = Data(input)
        return Self(parentalFlags: nacp.sc32(0x3030),
                    ratings: nacp[0x3040..<0x3060].map { Int8(bitPattern: $0) },
                    addOnBaseID: nacp.sc64(0x3070))
    }
}

private extension Data {
    var scHex: String { map { String(format: "%02x", $0) }.joined() }
    func sc16(_ p: Int) -> UInt16 { UInt16(self[p]) | UInt16(self[p + 1]) << 8 }
    func sc32(_ p: Int) -> UInt32 { (0..<4).reduce(0) { $0 | UInt32(self[p + $1]) << (8 * $1) } }
    func sc64(_ p: Int) -> UInt64 { (0..<8).reduce(0) { $0 | UInt64(self[p + $1]) << (8 * $1) } }
}
