import Foundation

// Minimal report envelope proves the optional-field contract. Root owns adding
// `window: SwitchWindowReport?` to the full application report separately.
private struct Envelope: Codable { let instructions: UInt64; let window: SwitchWindowReport? }
private func cancelled(_ context: UnsafeMutableRawPointer?) -> Int32 { 1 }
private func refusePolicy(_ context: UnsafeMutableRawPointer?, _ info: UnsafePointer<SatchelGuestVIInfo>?, _ interval: Int32, _ width: UInt32, _ height: UInt32, _ output: UnsafeMutablePointer<SatchelGuestWindowPresentation>?) -> Int32 { 0 }
@main struct Test {
    static var checks = 0
    static func check(_ value: @autoclosure () -> Bool, _ name: String) {
        checks += 1
        if !value() { fatalError("FAIL: \(name)") }
    }
    static func main() throws {
        check(MemoryLayout<SatchelSwitchContextIdentity>.size == 24, "Process context descriptor ABI24")
        var process: OpaquePointer?, dispatcher: OpaquePointer?, cpu: OpaquePointer?
        check(wf_create(&process, &dispatcher, &cpu) == 1, "actual original Process/dispatch creation")
        defer { wf_destroy(process, dispatcher) }
        guard let process, let dispatcher, let cpu else { fatalError("fixture pointers") }
        var vi = SatchelGuestVIBorrow(); vi.cpu = cpu
        var stop = SatchelCPUProbeResult()
        check(wf_stop(process, dispatcher, cpu, 0, &stop) == 1, "actual common breakpoint")
        var identity = SatchelSwitchContextIdentity()
        check(satchel_switch_process_context_identity(process, cpu, &identity) == 1, "actual live Process identity")
        let context = UnsafeMutableRawPointer(process)
        check(runtimeWindowContextValidate(context, cpu, identity.process_id, identity.context_generation) == 1, "valid borrowed current identity")
        check(runtimeWindowContextValidate(context, cpu, identity.process_id, identity.context_generation + 1) == 0, "stale generation refused")
        check(runtimeWindowContextValidate(context, cpu, identity.process_id + 1, identity.context_generation) == 0, "wrong Process refused")
        check(runtimeWindowContextValidate(nil, cpu, 1, 1) == 0, "nil Process refused")
        check(runtimeWindowContextValidate(context, nil, 1, 1) == 0, "nil CPU refused")
        wf_mode(0, 0)
        let actual = runtimeWindowDispatch(process: process, dispatcher: dispatcher, cpu: cpu, viBorrow: &vi, stop: &stop)
        check(actual.status == UInt32(SATCHEL_GUEST_DISPATCH_UNIMPLEMENTED.rawValue), "actual C no-owner Window refusal")
        check(wf_window_calls() == 1 && wf_ordinary_calls() == 1, "common trap crosses actual checked Window bridge")
        check(wf_borrow_valid() == 1 && wf_policy_seen() == 0, "fresh numeric identity/LR supplied, absent policy remains nil")
        check(wf_unchanged(cpu, &stop) == 1, "actual refusal preserves trapped PC")
        check(actual.operation_id != 0 && actual.events == 1, "actual C operation/event telemetry")
        let statuses = [SATCHEL_GUEST_DISPATCH_WINDOW_WAIT, SATCHEL_GUEST_DISPATCH_SURFACE_UNAVAILABLE,
                        SATCHEL_GUEST_DISPATCH_PRESENT_TIMING_UNAVAILABLE, SATCHEL_GUEST_DISPATCH_CANCELLED,
                        SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY, SATCHEL_GUEST_DISPATCH_INVALID]
        for status in statuses {
            wf_mode(1, UInt32(status.rawValue))
            let report = runtimeWindowDispatch(process: process, dispatcher: dispatcher, cpu: cpu, viBorrow: &vi, stop: &stop)
            check(report.status == UInt32(status.rawValue), "test-only untouched-output status stays authoritative")
            check(report.operation_id == 0 && report.events == 0, "untouched output invents no counters")
            check(wf_window_calls() == 1 && wf_ordinary_calls() == 0 && wf_borrow_valid() == 1, "status seam observes exact synchronous borrow")
            check(wf_unchanged(cpu, &stop) == 1, "WAIT/refusal helper does not schedule or advance CPU")
        }
        wf_mode(1, UInt32(SATCHEL_GUEST_DISPATCH_PRESENT_TIMING_UNAVAILABLE.rawValue))
        let supplied = runtimeWindowDispatch(process: process, dispatcher: dispatcher, cpu: cpu, viBorrow: &vi, stop: &stop,
            policy: refusePolicy, policyContext: context)
        check(supplied.status == UInt32(SATCHEL_GUEST_DISPATCH_PRESENT_TIMING_UNAVAILABLE.rawValue) && wf_policy_seen() == 1, "externally supplied policy/context forwarded, never invented")
        wf_mode(0, 0)
        var lr: UInt64 = 0
        check(satchel_cpu_probe_get_x(cpu, 30, &lr) == 1, "capture original LR")
        check(satchel_cpu_probe_set_x(cpu, 30, 0) == 1, "original non-executable LR fixture")
        let badLR = runtimeWindowDispatch(process: process, dispatcher: dispatcher, cpu: cpu, viBorrow: &vi, stop: &stop)
        check(badLR.status == UInt32(SATCHEL_GUEST_DISPATCH_BAD_RETURN.rawValue) && badLR.events == 0, "actual C checks executable LR before dispatch event")
        var afterLR: UInt64 = 9
        check(satchel_cpu_probe_get_x(cpu, 30, &afterLR) == 1 && afterLR == 0 && wf_unchanged(cpu, &stop) == 1, "invalid LR/PC remain unchanged")
        check(satchel_cpu_probe_set_x(cpu, 30, lr) == 1 && satchel_cpu_probe_set_pc(cpu, lr) == 1, "move actual CPU away from stale stop")
        let stale = runtimeWindowDispatch(process: process, dispatcher: dispatcher, cpu: cpu, viBorrow: &vi, stop: &stop)
        check(stale.status == UInt32(SATCHEL_GUEST_DISPATCH_INVALID.rawValue) && stale.events == 0, "actual C rejects stale stop despite live identity")
        check(satchel_cpu_probe_run(cpu, 0, nil, nil).pc == lr, "stale stop does not rewrite current PC")
        check(wf_stop(process, dispatcher, cpu, 1, &stop) == 1, "cancelled bootstrap fixture")
        vi.cancelled = cancelled
        let cancellation = runtimeWindowDispatch(process: process, dispatcher: dispatcher, cpu: cpu, viBorrow: &vi, stop: &stop)
        check(cancellation.status == UInt32(SATCHEL_GUEST_DISPATCH_CANCELLED.rawValue) && cancellation.events == actual.events, "actual ordinary handler receives pure cancellation without consuming an event")
        check(wf_unchanged(cpu, &stop) == 1, "cancellation preserves bootstrap PC")
        vi.cancelled = nil
        wf_mode(0, 0)
        check(wf_stop(process, dispatcher, cpu, 1, &stop) == 1, "actual bootstrap breakpoint")
        let lookup = runtimeWindowDispatch(process: process, dispatcher: dispatcher, cpu: cpu, viBorrow: &vi, stop: &stop)
        check(lookup.status == UInt32(SATCHEL_GUEST_DISPATCH_RETURNED.rawValue), "actual bootstrap name lookup succeeds")
        check(wf_ordinary_calls() == 1 && wf_window_calls() == 0, "bootstrap bypasses common-trap Window wrapper")
        check(lookup.operation_id == 0, "bootstrap is not mislabeled Window operation")
        check(wf_stop(process, dispatcher, cpu, 0, &stop) == 1, "restore original stop")
        wf_mode(0, 0)
        var unrelated = stop; unrelated.pc += 4
        let notOurs = runtimeWindowDispatch(process: process, dispatcher: dispatcher, cpu: cpu, viBorrow: &vi, stop: &unrelated)
        check(notOurs.status == UInt32(SATCHEL_GUEST_DISPATCH_NOT_OURS.rawValue), "unrelated stop refused")
        check(wf_window_calls() == 0 && wf_ordinary_calls() == 0, "unrelated stop enters no dispatcher")
        var noCPU = SatchelGuestVIBorrow()
        let invalid = runtimeWindowDispatch(process: process, dispatcher: dispatcher, cpu: cpu, viBorrow: &noCPU, stop: &stop)
        check(invalid.status == UInt32(SATCHEL_GUEST_DISPATCH_INVALID.rawValue), "mismatched VI borrow refused")
        let absent = runtimeWindowDispatch(process: nil, dispatcher: dispatcher, cpu: cpu, viBorrow: &vi, stop: &stop)
        check(absent.status == UInt32(SATCHEL_GUEST_DISPATCH_INVALID.rawValue) && absent.events == 0, "absent Process refuses with zero counters")
        check(wf_unchanged(cpu, &stop) == 1, "all preflights preserve PC")
        let old = try JSONDecoder().decode(Envelope.self, from: Data("{\"instructions\":19}".utf8))
        check(old.instructions == 19 && old.window == nil, "legacy absent optional report decodes")
        let null = try JSONDecoder().decode(Envelope.self, from: Data("{\"instructions\":20,\"window\":null}".utf8))
        check(null.window == nil && null.instructions == 20, "explicit null accepted")
        let value = SwitchWindowReport(ownerCreated: true, attempts: 4, returnedCalls: 1, waitReturns: 2,
            liveWindows: 1, pendingCalls: 1, retainedSnapshots: 2, leasedSnapshots: 1, acquires: 1,
            presents: 1, snapshotsCompleted: 1, lastOperation: "nvnQueueAcquireTexture", lastStatus: UInt32(SATCHEL_GUEST_DISPATCH_WINDOW_WAIT.rawValue))
        let bytes = try JSONEncoder().encode(Envelope(instructions: 21, window: value))
        let decoded = try JSONDecoder().decode(Envelope.self, from: bytes)
        check(decoded.window?.waitReturns == 2 && decoded.window?.snapshotsCompleted == 1, "typed report retains separate wait/snapshot counts")
        check(decoded.window?.copiedFrames == 0 && decoded.window?.pendingLeaseReleases == 0, "new copy/release counters default to zero, not display counts")
        check(decoded.window?.lastStatus == UInt32(SATCHEL_GUEST_DISPATCH_WINDOW_WAIT.rawValue), "WAIT is not success telemetry")
        let object = try JSONSerialization.jsonObject(with: bytes) as! [String: Any]
        let keys = Set((object["window"] as! [String: Any]).keys)
        check(keys == Set(["ownerCreated","attempts","returnedCalls","waitReturns","liveWindows","pendingCalls","retainedSnapshots","leasedSnapshots","acquires","presents","snapshotsCompleted","copiedFrames","pendingLeaseReleases","lastOperation","lastStatus"]), "safe telemetry exact keys, no identities/pointers/bytes")
        print("Runtime Window Swift/actual C: \(checks) assertions passed; status injection is test-only, no GPU/display/game claim.")
    }
}
