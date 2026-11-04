import Foundation

struct SwitchTextureFrameResources: Codable, Equatable {
    let devices: UInt32
    let buffers: UInt32
    let jobs: UInt32
    let targets: UInt32
    let pipelines: UInt32
    let computePipelines: UInt32
    let indexBuffers: UInt32
    let frameOwners: UInt32
    let frames: UInt32
    let frameSnapshotting: UInt32
    let frameReady: UInt32
    let frameLeased: UInt32
    let frameRetiring: UInt32
    let snapshotOwners: UInt32
    let snapshotGpuOwners: UInt32
    let reservedContexts: UInt32
    let activeContexts: UInt32
    let retiringContexts: UInt32
    let pendingDevices: UInt32
    let bufferBytes: UInt64
    let targetBytes: UInt64
    let indexBytes: UInt64
    let frameBytes: UInt64
    let stagingBytes: UInt64

    init(_ result: SatchelGuestTextureFrameResources) {
        devices = result.devices
        buffers = result.buffers
        jobs = result.jobs
        targets = result.targets
        pipelines = result.pipelines
        computePipelines = result.compute_pipelines
        indexBuffers = result.index_buffers
        frameOwners = result.frame_owners
        frames = result.frames
        frameSnapshotting = result.frame_snapshotting
        frameReady = result.frame_ready
        frameLeased = result.frame_leased
        frameRetiring = result.frame_retiring
        snapshotOwners = result.snapshot_owners
        snapshotGpuOwners = result.snapshot_gpu_owners
        reservedContexts = result.reserved_contexts
        activeContexts = result.active_contexts
        retiringContexts = result.retiring_contexts
        pendingDevices = result.pending_devices
        bufferBytes = result.buffer_bytes
        targetBytes = result.target_bytes
        indexBytes = result.index_bytes
        frameBytes = result.frame_bytes
        stagingBytes = result.staging_bytes
    }
}

// Original offscreen texture fixture; no game window or display selection.
struct SwitchTextureFrameVulkanReport: Codable {
    let synthetic: Bool
    let passed: Bool
    let status: UInt32
    let reportedStatus: UInt32
    let stage: UInt32
    let dispatchStatus: UInt32
    let frameStatus: UInt32
    let cpuStopReason: UInt32
    let retirementStatus: UInt32
    let transfersSubmitted: UInt32
    let transferFinishes: UInt32
    let snapshotsStarted: UInt32
    let uploadsCompleted: UInt32
    let snapshotsCompleted: UInt32
    let preSubmitCancellations: UInt32
    let pendingCancellations: UInt32
    let cancelledSnapshotsDrained: UInt32
    let width: UInt32
    let height: UInt32
    let rowStride: UInt32
    let instructions: UInt64
    let dispatchEvents: UInt64
    let valuesMatched: Bool
    let guardsMatched: Bool
    let cpuMirrorUnchanged: Bool
    let statePreserved: Bool
    let parentProtected: Bool
    let textureFinalized: Bool
    let deviceFinalized: Bool
    let cpuDestroyed: Bool
    let cpuFreeLease: Bool
    let cleanupMatched: Bool
    let resourcesRetained: Bool
    let priorReaped: Bool
    let baseline: SwitchTextureFrameResources
    let final: SwitchTextureFrameResources

    init(status: UInt32, result: SatchelGuestTextureFrameDiagnostic) {
        synthetic = true
        self.status = status
        reportedStatus = result.status
        stage = result.stage
        dispatchStatus = result.dispatch_status
        frameStatus = result.frame_status
        cpuStopReason = result.cpu_stop_reason
        retirementStatus = result.retirement_status
        transfersSubmitted = result.transfers_submitted
        transferFinishes = result.transfer_finishes
        snapshotsStarted = result.snapshots_started
        uploadsCompleted = result.uploads_completed
        snapshotsCompleted = result.snapshots_completed
        preSubmitCancellations = result.pre_submit_cancellations
        pendingCancellations = result.pending_cancellations
        cancelledSnapshotsDrained = result.cancelled_snapshots_drained
        width = result.width
        height = result.height
        rowStride = result.row_stride
        instructions = result.instructions
        dispatchEvents = result.dispatch_events
        valuesMatched = result.values_matched == 1
        guardsMatched = result.guards_matched == 1
        cpuMirrorUnchanged = result.cpu_mirror_unchanged == 1
        statePreserved = result.state_preserved == 1
        parentProtected = result.parent_protected == 1
        textureFinalized = result.texture_finalized == 1
        deviceFinalized = result.device_finalized == 1
        cpuDestroyed = result.cpu_destroyed == 1
        cpuFreeLease = result.cpu_free_lease == 1
        cleanupMatched = result.cleanup_matched == 1
        resourcesRetained = result.resources_retained != 0
        priorReaped = result.prior_reaped != 0
        baseline = SwitchTextureFrameResources(result.baseline)
        final = SwitchTextureFrameResources(result.final)
        passed = status == SATCHEL_GUEST_TFD_OK && result.status == SATCHEL_GUEST_TFD_OK
            && result.version == 1 && result.size == MemoryLayout<SatchelGuestTextureFrameDiagnostic>.size
            && stage == 10 && instructions == 273 && dispatchEvents == 92
            && transfersSubmitted == 1 && transferFinishes == 1
            && snapshotsStarted == 2 && uploadsCompleted == 2 && snapshotsCompleted == 1
            && preSubmitCancellations == 1 && pendingCancellations == 1 && cancelledSnapshotsDrained == 1
            && width == 31 && height == 17 && rowStride == 124
            && valuesMatched && guardsMatched && cpuMirrorUnchanged && statePreserved
            && parentProtected && textureFinalized && deviceFinalized && cpuDestroyed && cpuFreeLease
            && cleanupMatched && !resourcesRetained && baseline == final
    }
}
