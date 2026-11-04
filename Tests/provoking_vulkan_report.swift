import Foundation

@main struct ProvokingVulkanReportTests {
    static func main() throws {
        var count = 0
        func check(_ condition: Bool, _ message: String) {
            precondition(condition, message)
            count += 1
        }
        var result = SatchelGuestProvokingDiagnostic()
        check(!SwitchProvokingVulkanReport(status: 0, result: result).passed,
              "A zeroed output cannot claim completed work")
        result.version = 1
        result.size = UInt32(MemoryLayout<SatchelGuestProvokingDiagnostic>.size)
        result.stage = 10; result.pairs_compiled = 1; result.pipelines_created = 2
        result.draws_submitted = 2; result.fences_completed = 2
        result.pre_submit_cancellations = 2; result.pending_cancellations = 2
        result.values_matched = 1; result.coverage_matched = 1; result.guards_matched = 1
        result.vertices_unchanged = 1; result.rotated_last_supported = 1
        result.device_finalized = 1; result.cleanup_matched = 1; result.live_index_bytes = 16_380
        let completed = SwitchProvokingVulkanReport(status: 0, result: result)
        check(completed.passed && completed.synthetic, "Completed original fixture is synthetic")
        for status: UInt32 in [1, 2, 3, 4] {
            check(!SwitchProvokingVulkanReport(status: status, result: result).passed,
                  "Stale successful output cannot override the returned failure")
        }
        var pending = result
        pending.resources_retained = 1; pending.final.index_buffers = 1
        check(!SwitchProvokingVulkanReport(status: 0, result: pending).passed,
              "Retained private index ownership is not success")
        var partial = result
        partial.fences_completed = 1
        check(!SwitchProvokingVulkanReport(status: 0, result: partial).passed,
              "Submission is not fence completion")
        var inconsistent = result
        inconsistent.final.index_bytes = 64
        check(!SwitchProvokingVulkanReport(status: 0, result: inconsistent).passed,
              "Cleanup flag cannot conceal a private allocation")
        var native = result
        native.native_last_enabled = 1
        check(!SwitchProvokingVulkanReport(status: 0, result: native).passed,
              "This fixture must exercise the rotated path")
        var external = result
        external.baseline.buffers = 1; external.baseline.buffer_bytes = 256
        external.final = external.baseline
        check(SwitchProvokingVulkanReport(status: 0, result: external).passed,
              "An unchanged external resource baseline is valid")
        let decoded = try JSONDecoder().decode(SwitchProvokingVulkanReport.self,
            from: JSONEncoder().encode(completed))
        check(decoded.passed && decoded.synthetic && decoded.drawsSubmitted == 2,
              "Saved evidence retains synthetic identity and completed draws")
        print("PASS \(count) provoking Vulkan report assertions")
    }
}
