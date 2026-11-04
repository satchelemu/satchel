import Foundation

// Typed service telemetry only: no opaque identity addresses or guest bytes.
struct SwitchVIReport: Codable {
    let entryCount: UInt32
    let surfaceAttachedAtStart: Bool
    let ownerCreated: Bool
    let attempts: UInt64
    let returnedCalls: UInt64
    let liveDisplays: UInt32
    let liveLayers: UInt32
    let liveNativeWindows: UInt32
    let retainedWindows: UInt32
    let lastOperation: String?
    let lastStatus: UInt32?
}

private func processReservation(_ value: SatchelGuestVIReservation) -> SatchelSwitchIdentityReservation {
    SatchelSwitchIdentityReservation(version: value.version, size: value.size,
        process_id: value.process_id, generation: value.generation,
        address: value.address, length: value.length)
}

final class RuntimeVIClaimContext {
    let process: OpaquePointer
    let surface: SwitchRuntimeSurfaceState
    let epoch: UInt64
    let cancelled: () -> Bool
    init(process: OpaquePointer, surface: SwitchRuntimeSurfaceState, epoch: UInt64,
         cancelled: @escaping () -> Bool) {
        self.process = process; self.surface = surface; self.epoch = epoch; self.cancelled = cancelled
    }
}

// Borrowed only for the current call; VI retains no Process or CPU pointer.
func runtimeVIClaim(_ context: UnsafeMutableRawPointer?, _ cpu: OpaquePointer?,
                    _ descriptor: UnsafePointer<SatchelGuestVIReservation>?) -> Int32 {
    guard let context, let cpu, let descriptor else { return 0 }
    let claim = Unmanaged<RuntimeVIClaimContext>.fromOpaque(context).takeUnretainedValue()
    guard !claim.cancelled(), claim.surface.validate(epoch: claim.epoch) else { return 0 }
    var value = processReservation(descriptor.pointee)
    return satchel_switch_process_claim_identity(claim.process, cpu, &value)
}

func runtimeVIValidate(_ context: UnsafeMutableRawPointer?, _ cpu: OpaquePointer?,
                       _ descriptor: UnsafePointer<SatchelGuestVIReservation>?) -> Int32 {
    guard let context, let cpu, let descriptor else { return 0 }
    var value = processReservation(descriptor.pointee)
    return satchel_switch_process_validate_identity(OpaquePointer(context), cpu, &value)
}

func runtimeVIOutputGuard(_ context: UnsafeMutableRawPointer?, _ cpu: OpaquePointer?,
                          _ address: UInt64, _ size: Int) -> Int32 {
    guard let context, let cpu else { return 0 }
    return satchel_guest_dispatch_check_output_range(OpaquePointer(context), cpu, address, size)
}

func runtimeVIOperationName(_ operation: UInt32) -> String? {
    switch operation {
    case 1: return "OpenDefaultDisplay"
    case 2: return "CreateLayer"
    case 3: return "GetNativeWindow"
    case 4: return "DestroyLayer"
    case 5: return "CloseDisplay"
    case 6: return "CreateLayer (sized)"
    case 7: return "CreateLayer (settings)"
    default: return nil
    }
}

// Early C adapter refusals may leave the output record untouched. Always report
// the returned status, while retaining an unknown operation as zero/nil.
func runtimeVIDispatch(entries: inout SatchelGuestVIEntries, owner: OpaquePointer?,
                       borrow: inout SatchelGuestVIBorrow, stop: inout SatchelCPUProbeResult) -> SatchelGuestVIResult {
    var result = SatchelGuestVIResult()
    let status = satchel_guest_vi_dispatch(&entries, owner, &borrow, &stop, &result)
    result.status = UInt32(status.rawValue)
    return result
}
