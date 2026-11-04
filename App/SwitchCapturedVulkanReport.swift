import Foundation

// Original checked-memory shader fixtures, not an imported game's NVN calls.
struct SwitchCapturedVulkanReport: Codable {
    let synthetic: Bool
    let passed: Bool
    let status: UInt32
    let reportedStatus: UInt32
    let stage: UInt32
    let ownerStatus: UInt32
    let poolStatus: UInt32
    let backendStatus: UInt32
    let sourceAdmissions: UInt32
    let pairsCompiled: UInt32
    let pipelinesCreated: UInt32
    let drawsSubmitted: UInt32
    let fencesCompleted: UInt32
    let preSubmitCancellations: UInt32
    let pendingCancellations: UInt32
    let sourceRebound: Bool
    let valuesMatched: Bool
    let guardsMatched: Bool
    let statePreserved: Bool
    let programFinalized: Bool
    let poolFinalized: Bool
    let deviceFinalized: Bool
    let cpuDestroyed: Bool
    let cleanupMatched: Bool
    let resourcesRetained: Bool
    let priorReaped: Bool
    let baselineDevices: UInt32
    let finalDevices: UInt32
    let baselineBuffers: UInt32
    let finalBuffers: UInt32
    let baselineJobs: UInt32
    let finalJobs: UInt32
    let baselineTargets: UInt32
    let finalTargets: UInt32
    let baselinePipelines: UInt32
    let finalPipelines: UInt32
    let baselineBufferBytes: UInt64
    let finalBufferBytes: UInt64
    let baselineTargetBytes: UInt64
    let finalTargetBytes: UInt64
    let sourceUnusedHost: UInt64
    let sourceUnusedState: UInt64

    init(status: UInt32, result: SatchelGuestCapturedProgramDiagnostic) {
        synthetic = true
        self.status = status
        reportedStatus = result.status
        stage = result.stage
        ownerStatus = result.owner_status
        poolStatus = result.pool_status
        backendStatus = result.backend_status
        sourceAdmissions = result.source_admissions
        pairsCompiled = result.pairs_compiled
        pipelinesCreated = result.pipelines_created
        drawsSubmitted = result.draws_submitted
        fencesCompleted = result.fences_completed
        preSubmitCancellations = result.pre_submit_cancellations
        pendingCancellations = result.pending_cancellations
        sourceRebound = result.source_rebound == 1
        valuesMatched = result.values_matched == 1
        guardsMatched = result.guards_matched == 1
        statePreserved = result.state_preserved == 1
        programFinalized = result.program_finalized == 1
        poolFinalized = result.pool_finalized == 1
        deviceFinalized = result.device_finalized == 1
        cpuDestroyed = result.cpu_destroyed == 1
        cleanupMatched = result.cleanup_matched == 1
        resourcesRetained = result.resources_retained != 0
        priorReaped = result.prior_reaped != 0
        baselineDevices = result.baseline_devices
        finalDevices = result.final_devices
        baselineBuffers = result.baseline_buffers
        finalBuffers = result.final_buffers
        baselineJobs = result.baseline_jobs
        finalJobs = result.final_jobs
        baselineTargets = result.baseline_targets
        finalTargets = result.final_targets
        baselinePipelines = result.baseline_pipelines
        finalPipelines = result.final_pipelines
        baselineBufferBytes = result.baseline_buffer_bytes
        finalBufferBytes = result.final_buffer_bytes
        baselineTargetBytes = result.baseline_target_bytes
        finalTargetBytes = result.final_target_bytes
        sourceUnusedHost = result.source_unused_host
        sourceUnusedState = result.source_unused_state
        // The C API returns a status enum: zero is success. Require the full
        // completed journey so an untouched zeroed report can never pass.
        passed = status == SATCHEL_GUEST_CAPTURED_DIAG_OK && result.status == SATCHEL_GUEST_CAPTURED_DIAG_OK
            && result.version == 1 && result.size == MemoryLayout<SatchelGuestCapturedProgramDiagnostic>.size
            && stage == 10 && sourceAdmissions == 2 && pairsCompiled == 2 && pipelinesCreated == 2
            && drawsSubmitted == 4 && fencesCompleted == 4
            && preSubmitCancellations == 4 && pendingCancellations == 4
            && sourceRebound && valuesMatched && guardsMatched && statePreserved
            && programFinalized && poolFinalized && deviceFinalized && cpuDestroyed
            && cleanupMatched && !resourcesRetained
            && baselineDevices == finalDevices && baselineBuffers == finalBuffers
            && baselineJobs == finalJobs && baselineTargets == finalTargets
            && baselinePipelines == finalPipelines && baselineBufferBytes == finalBufferBytes
            && baselineTargetBytes == finalTargetBytes && sourceUnusedHost == 6 && sourceUnusedState == 8
    }
}
