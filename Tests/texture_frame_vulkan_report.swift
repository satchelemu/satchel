import Foundation

@main struct TextureFrameVulkanReportTests {
    static func main() throws {
        var count = 0
        func check(_ condition: Bool, _ message: String) {
            precondition(condition, message)
            count += 1
        }
        var result = SatchelGuestTextureFrameDiagnostic()
        check(!SwitchTextureFrameVulkanReport(status: 0, result: result).passed,
              "A zeroed output cannot claim a completed frame")
        result.version = 1
        result.size = UInt32(MemoryLayout<SatchelGuestTextureFrameDiagnostic>.size)
        result.stage = 10; result.instructions = 273; result.dispatch_events = 92
        result.transfers_submitted = 1; result.transfer_finishes = 1
        result.snapshots_started = 2; result.uploads_completed = 2; result.snapshots_completed = 1
        result.pre_submit_cancellations = 1; result.pending_cancellations = 1
        result.cancelled_snapshots_drained = 1
        result.width = 31; result.height = 17; result.row_stride = 124
        result.values_matched = 1; result.guards_matched = 1; result.cpu_mirror_unchanged = 1
        result.state_preserved = 1; result.parent_protected = 1; result.texture_finalized = 1
        result.device_finalized = 1; result.cpu_destroyed = 1; result.cpu_free_lease = 1
        result.cleanup_matched = 1
        let completed = SwitchTextureFrameVulkanReport(status: 0, result: result)
        check(completed.passed && completed.synthetic, "Completed original fixture remains synthetic")
        for status: UInt32 in [1, 2, 3, 4] {
            check(!SwitchTextureFrameVulkanReport(status: status, result: result).passed,
                  "Stale success cannot override the returned error or busy status")
        }
        var partial = result
        partial.snapshots_completed = 0
        check(!SwitchTextureFrameVulkanReport(status: 0, result: partial).passed,
              "Upload completion is not snapshot completion")
        partial = result; partial.snapshots_completed = 2
        check(!SwitchTextureFrameVulkanReport(status: 0, result: partial).passed,
              "A discarded snapshot must not count as completed")
        partial = result; partial.cpu_free_lease = 0
        check(!SwitchTextureFrameVulkanReport(status: 0, result: partial).passed,
              "Copied pixels before CPU destruction do not prove CPU-free ownership")
        partial = result; partial.cpu_mirror_unchanged = 0
        check(!SwitchTextureFrameVulkanReport(status: 0, result: partial).passed,
              "The fixture must prove current GPU mirror bytes were used")
        partial = result; partial.resources_retained = 1
        check(!SwitchTextureFrameVulkanReport(status: 0, result: partial).passed,
              "Pending ownership cannot claim cleanup")
        for field in [\SatchelGuestTextureFrameResources.frame_bytes, \SatchelGuestTextureFrameResources.staging_bytes,
                      \SatchelGuestTextureFrameResources.index_bytes] {
            var leaked = result
            leaked.final[keyPath: field] = 64
            check(!SwitchTextureFrameVulkanReport(status: 0, result: leaked).passed,
                  "Cleanup flag cannot conceal retained bytes")
        }
        var external = result
        external.baseline.frames = 1; external.baseline.frame_bytes = 64
        external.final = external.baseline
        check(SwitchTextureFrameVulkanReport(status: 0, result: external).passed,
              "Unchanged unrelated ownership is a valid baseline")
        let decoded = try JSONDecoder().decode(SwitchTextureFrameVulkanReport.self,
            from: JSONEncoder().encode(completed))
        check(decoded.passed && decoded.synthetic && decoded.snapshotsCompleted == 1
              && decoded.cancelledSnapshotsDrained == 1, "Evidence distinguishes completed and discarded frames")
        print("PASS \(count) texture-frame report assertions")
    }
}
