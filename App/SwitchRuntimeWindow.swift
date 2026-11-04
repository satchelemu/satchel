import Foundation

// Pure, synchronous borrow. The caller keeps Process alive on its serial worker;
// this callback neither owns nor retains Process/CPU and does not inspect UIKit.
func runtimeWindowContextValidate(_ context: UnsafeMutableRawPointer?, _ cpu: OpaquePointer?,
                                  _ processID: UInt64, _ generation: UInt64) -> Int32 {
    guard let context, let cpu else { return 0 }
    var identity = SatchelSwitchContextIdentity(version: 1,
        size: UInt32(MemoryLayout<SatchelSwitchContextIdentity>.size),
        process_id: processID, context_generation: generation)
    return satchel_switch_process_validate_context(OpaquePointer(context), cpu, &identity)
}

// Synchronous only: no pointer/callback escapes. The caller supplies the actual
// VI epoch/identity/output guards and pure cancellation callback. A presentation
// policy must explicitly validate interval, dimensions, format, color space,
// origin, surface epoch and policy generation. Nil stays nil and Present refuses;
// this helper does not choose NVN interval or color defaults.
//
// Bootstrap lookup uses the ordinary dispatcher. The common trap uses the
// Window bridge, which also preserves old non-Window operations. WAIT is never
// success: park/retry this exact stopped thread and arguments before scheduling,
// or cancel/drain the retained ordering lease. No scheduling happens here.
func runtimeWindowDispatch(process: OpaquePointer?, dispatcher: OpaquePointer?,
                           cpu: OpaquePointer?, viBorrow: inout SatchelGuestVIBorrow,
                           stop: inout SatchelCPUProbeResult,
                           policy: SatchelGuestWindowPolicy? = nil,
                           policyContext: UnsafeMutableRawPointer? = nil) -> SatchelGuestDispatchReport {
    var report = SatchelGuestDispatchReport(status: UInt32(SATCHEL_GUEST_DISPATCH_INVALID.rawValue),
                                       operation_id: 0, events: 0)
    guard let process, let dispatcher, let cpu,
          stop.reason == SATCHEL_CPU_BREAKPOINT, viBorrow.cpu == cpu else { return report }
    var identity = SatchelSwitchContextIdentity()
    guard satchel_switch_process_context_identity(process, cpu, &identity) != 0 else { return report }
    var points = [UInt64](repeating: 0, count: 2)
    guard satchel_guest_dispatch_breakpoints(dispatcher, &points) != 0 else { return report }
    let status: SatchelGuestDispatchStatus
    if stop.pc == points[0] {
        status = satchel_guest_dispatch_handle(dispatcher, cpu, &stop,
            viBorrow.cancelled, viBorrow.cancel_context, &report)
    } else if stop.pc == points[1] {
        var returnPC: UInt64 = 0
        guard satchel_cpu_probe_get_x(cpu, 30, &returnPC) != 0 else { return report }
        var borrow = SatchelGuestWindowBorrow(cpu: cpu, vi: viBorrow,
            process_id: identity.process_id, context_generation: identity.context_generation,
            trap_pc: stop.pc, return_pc: returnPC,
            validate_context: runtimeWindowContextValidate,
            context: UnsafeMutableRawPointer(process), policy: policy, policy_context: policyContext)
        status = satchel_guest_dispatch_handle_windows(dispatcher, cpu, &stop, &borrow, &report)
    } else {
        report.status = UInt32(SATCHEL_GUEST_DISPATCH_NOT_OURS.rawValue)
        return report
    }
    // A C refusal may leave its report untouched. The return enum is authoritative.
    report.status = UInt32(status.rawValue)
    return report
}
