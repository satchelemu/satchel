import Foundation

@main struct CapturedVulkanReportTests {
    static func main() throws {
        var count = 0
        func check(_ condition: Bool, _ message: String) {
            precondition(condition, message)
            count += 1
        }
        var result = SatchelGuestCapturedProgramDiagnostic()
        check(!SwitchCapturedVulkanReport(status: 0, result: result).passed,
              "A zeroed output cannot claim a completed diagnostic")
        result.version = 1
        result.size = UInt32(MemoryLayout<SatchelGuestCapturedProgramDiagnostic>.size)
        result.stage = 10
        result.source_admissions = 2; result.pairs_compiled = 2; result.pipelines_created = 2
        result.draws_submitted = 4; result.fences_completed = 4
        result.pre_submit_cancellations = 4; result.pending_cancellations = 4
        result.source_rebound = 1; result.values_matched = 1; result.guards_matched = 1
        result.state_preserved = 1; result.program_finalized = 1; result.pool_finalized = 1
        result.device_finalized = 1; result.cpu_destroyed = 1; result.cleanup_matched = 1
        result.source_unused_host = 6; result.source_unused_state = 8
        let completed = SwitchCapturedVulkanReport(status: 0, result: result)
        check(completed.passed && completed.synthetic, "Completed fixture is explicitly synthetic")
        for status in [UInt32(SATCHEL_GUEST_CAPTURED_DIAG_FAILED), UInt32(SATCHEL_GUEST_CAPTURED_DIAG_CANCELLED),
                       UInt32(SATCHEL_GUEST_CAPTURED_DIAG_BUSY), UInt32(SATCHEL_GUEST_CAPTURED_DIAG_INVALID)] {
            check(!SwitchCapturedVulkanReport(status: status, result: result).passed,
                  "An untouched previous successful output cannot override the returned failure")
        }
        var pending = result
        pending.resources_retained = 1; pending.final_jobs = 1
        check(!SwitchCapturedVulkanReport(status: 0, result: pending).passed,
              "Completed draws with retained GPU ownership are not success")
        var partial = result
        partial.fences_completed = 3
        check(!SwitchCapturedVulkanReport(status: 0, result: partial).passed,
              "Submitted work is not completed work")
        var inconsistent = result
        inconsistent.final_buffer_bytes = 64
        check(!SwitchCapturedVulkanReport(status: 0, result: inconsistent).passed,
              "Cleanup flag cannot conceal mismatched resource accounting")
        let decoded = try JSONDecoder().decode(SwitchCapturedVulkanReport.self,
            from: JSONEncoder().encode(completed))
        check(decoded.passed && decoded.synthetic && decoded.drawsSubmitted == 4,
              "Saved diagnostic preserves evidence and synthetic identity")
        print("PASS \(count) captured Vulkan report assertions")
    }
}
