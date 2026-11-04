import Foundation

/// Retained by the worker inside withRomFS, through process destruction.
/// The C callback receives host staging memory and commits only verified bytes.
final class SwitchStorageProvider {
    let romFS: SwitchRomFS
    init(_ romFS: SwitchRomFS) { self.romFS = romFS }

    var descriptor: SatchelSwitchStorage {
        SatchelSwitchStorage(context: Unmanaged.passUnretained(self).toOpaque(),
            size: romFS.imageBytes, read: switchStorageRead)
    }
}

private func switchStorageRead(_ context: UnsafeMutableRawPointer?, _ offset: UInt64,
                               _ output: UnsafeMutableRawPointer?, _ count: Int) -> SatchelSwitchStorageStatus {
    guard let context, count >= 0, count <= Int(SATCHEL_SWITCH_STORAGE_READ_LIMIT),
          count == 0 || output != nil else { return SATCHEL_SWITCH_STORAGE_INVALID }
    let provider = Unmanaged<SwitchStorageProvider>.fromOpaque(context).takeUnretainedValue()
    guard offset <= provider.romFS.imageBytes,
          UInt64(count) <= provider.romFS.imageBytes - offset else { return SATCHEL_SWITCH_STORAGE_INVALID }
    do {
        let bytes = try provider.romFS.readStorage(offset, count: UInt64(count))
        guard bytes.count == count else { return SATCHEL_SWITCH_STORAGE_IO }
        if count > 0, let output {
            bytes.withUnsafeBytes { input in output.copyMemory(from: input.baseAddress!, byteCount: count) }
        }
        return SATCHEL_SWITCH_STORAGE_OK
    } catch is CancellationError {
        return SATCHEL_SWITCH_STORAGE_CANCELLED
    } catch SwitchExecutableError.integrity {
        return SATCHEL_SWITCH_STORAGE_INTEGRITY
    } catch SwitchExecutableError.invalid {
        return SATCHEL_SWITCH_STORAGE_INVALID
    } catch SwitchExecutableError.limit {
        return SATCHEL_SWITCH_STORAGE_INVALID
    } catch {
        return SATCHEL_SWITCH_STORAGE_IO
    }
}
