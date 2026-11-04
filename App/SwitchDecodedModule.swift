import Foundation

/// Owned data for the native rewriter. Segment offsets remain guest-relative;
/// no host executable allocation, relocation, rewriting or signing happens here.
struct SwitchDecodedModule {
    struct Segment {
        let offset: UInt64
        let bytes: Data
    }
    let name: String
    let segments: [Segment]
    let bssSize: UInt32
    let buildID: Data

    static func decode(_ module: SwitchExecutableModule) throws -> Self {
        var info = SatchelNSOInfo(), message = [CChar](repeating: 0, count: 256)
        let valid = module.data.withUnsafeBytes { bytes in
            satchel_nso_inspect(bytes.bindMemory(to: UInt8.self).baseAddress, bytes.count, &info, &message, message.count)
        }
        guard valid else { throw SwitchExecutableError.unsupported }
        var image = Data(count: Int(info.image_size))
        let decoded = image.withUnsafeMutableBytes { output in module.data.withUnsafeBytes { input in
            satchel_nso_decode(input.bindMemory(to: UInt8.self).baseAddress, input.count,
                          output.bindMemory(to: UInt8.self).baseAddress, output.count, &message, message.count)
        } }
        guard decoded else { throw SwitchExecutableError.integrity }
        var segments: [Segment] = []
        for i in 0..<3 {
            let segment = satchel_nso_segment(&info, UInt32(i)), start = Int(segment.memory_offset)
            segments.append(Segment(offset: UInt64(start), bytes: image.subdata(in: start..<start + Int(segment.size))))
        }
        let build = withUnsafeBytes(of: info.build_id) { Data($0) }
        return Self(name: module.name, segments: segments, bssSize: info.bss_size, buildID: build)
    }
}
