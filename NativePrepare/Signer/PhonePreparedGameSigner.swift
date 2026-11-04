import Darwin
import Foundation

struct PhonePreparedGameSigner: PreparedGameSigner {
    static let identifier = "com.dell.satchel.prepared-game"
    let identity: SatchelSigningIdentity

    /// O_EXCL and plain FileHandle writes deliberately avoid clonefile, copyfile and mmap.
    static func freshCopy(_ source: URL, to destination: URL, token: PrepareCancellation, progress: SwitchByteProgress = { _, _ in }) throws {
        let input = try FileHandle(forReadingFrom: source)
        defer { try? input.close() }
        let fd = open(destination.path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR)
        guard fd >= 0 else { throw PrepareFailure(message: "Could not create a fresh signing file.") }
        let output = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
        defer { try? output.close() }
        let size = UInt64(try source.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0)
        var done: UInt64 = 0
        while try autoreleasepool(invoking: { () throws -> Bool in
            try token.check()
            guard let bytes = try input.read(upToCount: 1024 * 1024), !bytes.isEmpty else { return false }
            try output.write(contentsOf: bytes); done += UInt64(bytes.count); progress(done, size); return true
        }) {}
        try output.synchronize()
    }

    func sign(_ dylib: URL, token: PrepareCancellation, progress: @escaping SwitchByteProgress) throws -> PrepareSigningState {
        try token.check()
        let parent = dylib.deletingLastPathComponent()
        let scratch = parent.appendingPathComponent(".sign-" + UUID().uuidString, isDirectory: true)
        let fm = FileManager.default
        try fm.createDirectory(at: scratch, withIntermediateDirectories: false,
                               attributes: [.posixPermissions: 0o700])
        defer { try? fm.removeItem(at: scratch) }
        let mapped = scratch.appendingPathComponent("mapped.dylib")
        let inputSize = UInt64(try dylib.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0)
        // Exact total becomes known once zsign has allocated its signature tail.
        let estimated = inputSize * 4
        try Self.freshCopy(dylib, to: mapped, token: token) { done, _ in progress(done, estimated) }
        try identity.signMappedFile(mapped.path, identifier: Self.identifier, progress: { done, _ in
            progress(inputSize + done, estimated)
        })
        try token.check()
        // The signer has returned and unmapped. Only this new, plain-written inode is published.
        try publishVerifiedSuccessor(mapped, to: dylib, token: token) { done, total in progress(inputSize * 2 + done, inputSize * 2 + total) }
        return .signed
    }

    func publishVerifiedSuccessor(_ mapped: URL, to destination: URL, token: PrepareCancellation, progress: SwitchByteProgress = { _, _ in }) throws {
        let fresh = mapped.deletingLastPathComponent().appendingPathComponent("fresh-" + UUID().uuidString + ".dylib")
        defer { try? FileManager.default.removeItem(at: fresh) }
        let size = UInt64(try mapped.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0)
        try Self.freshCopy(mapped, to: fresh, token: token) { done, _ in progress(done, size * 2) }
        try PrepareSignatureVerifier.verify(fresh, identity: identity, token: token) { done, total in progress(size + done, size + total) }
        try token.check()
        guard rename(fresh.path, destination.path) == 0 else {
            throw PrepareFailure(message: "Could not publish the verified game signature.")
        }
    }
}
