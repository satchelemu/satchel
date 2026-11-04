import Foundation

// Service/snapshot telemetry only. A completed snapshot is not a displayed frame.
// The containing runtime report should add this as an optional field so older
// saved reports with no Window activity continue to decode.
struct SwitchWindowReport: Codable {
    let ownerCreated: Bool
    let attempts: UInt64
    let returnedCalls: UInt64
    let waitReturns: UInt64
    let liveWindows: UInt32
    let pendingCalls: UInt32
    let retainedSnapshots: UInt32
    let leasedSnapshots: UInt32
    let acquires: UInt64
    let presents: UInt64
    let snapshotsCompleted: UInt64
    var copiedFrames: UInt64 = 0
    var pendingLeaseReleases: UInt32 = 0
    let lastOperation: String?
    let lastStatus: UInt32?
}

