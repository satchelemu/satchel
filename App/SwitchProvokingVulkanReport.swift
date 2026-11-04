import Foundation

struct SwitchProvokingResources: Codable, Equatable {
    let devices: UInt32
    let buffers: UInt32
    let jobs: UInt32
    let targets: UInt32
    let pipelines: UInt32
    let indexBuffers: UInt32
    let bufferBytes: UInt64
    let targetBytes: UInt64
    let indexBytes: UInt64

    init(_ result: SatchelGuestProvokingResources) {
        devices = result.devices
        buffers = result.buffers
        jobs = result.jobs
        targets = result.targets
        pipelines = result.pipelines
        indexBuffers = result.index_buffers
        bufferBytes = result.buffer_bytes
        targetBytes = result.target_bytes
        indexBytes = result.index_bytes
    }
}

// Original offscreen shader fixtures. Renderer modes do not establish NVN state.
struct SwitchProvokingVulkanReport: Codable {
    let synthetic: Bool
    let passed: Bool
    let status: UInt32
    let reportedStatus: UInt32
    let stage: UInt32
    let backendStatus: UInt32
    let compilerStatus: UInt32
    let pairsCompiled: UInt32
    let pipelinesCreated: UInt32
    let drawsSubmitted: UInt32
    let fencesCompleted: UInt32
    let preSubmitCancellations: UInt32
    let pendingCancellations: UInt32
    let valuesMatched: Bool
    let coverageMatched: Bool
    let guardsMatched: Bool
    let verticesUnchanged: Bool
    let nativeExtension: Bool
    let nativeLastSupported: Bool
    let nativeLastEnabled: Bool
    let rotatedLastSupported: Bool
    let deviceFinalized: Bool
    let cleanupMatched: Bool
    let resourcesRetained: Bool
    let priorReaped: Bool
    let baseline: SwitchProvokingResources
    let final: SwitchProvokingResources
    let liveIndexBytes: UInt64

    init(status: UInt32, result: SatchelGuestProvokingDiagnostic) {
        synthetic = true
        self.status = status
        reportedStatus = result.status
        stage = result.stage
        backendStatus = result.backend_status
        compilerStatus = result.compiler_status
        pairsCompiled = result.pairs_compiled
        pipelinesCreated = result.pipelines_created
        drawsSubmitted = result.draws_submitted
        fencesCompleted = result.fences_completed
        preSubmitCancellations = result.pre_submit_cancellations
        pendingCancellations = result.pending_cancellations
        valuesMatched = result.values_matched == 1
        coverageMatched = result.coverage_matched == 1
        guardsMatched = result.guards_matched == 1
        verticesUnchanged = result.vertices_unchanged == 1
        nativeExtension = result.native_extension != 0
        nativeLastSupported = result.native_last_supported != 0
        nativeLastEnabled = result.native_last_enabled != 0
        rotatedLastSupported = result.rotated_last_supported == 1
        deviceFinalized = result.device_finalized == 1
        cleanupMatched = result.cleanup_matched == 1
        resourcesRetained = result.resources_retained != 0
        priorReaped = result.prior_reaped != 0
        baseline = SwitchProvokingResources(result.baseline)
        final = SwitchProvokingResources(result.final)
        liveIndexBytes = result.live_index_bytes
        // Zero is the success enum, but an untouched zeroed output cannot pass.
        passed = status == SATCHEL_GUEST_PROVOKING_OK && result.status == SATCHEL_GUEST_PROVOKING_OK
            && result.version == 1 && result.size == MemoryLayout<SatchelGuestProvokingDiagnostic>.size
            && stage == 10 && pairsCompiled == 1 && pipelinesCreated == 2
            && drawsSubmitted == 2 && fencesCompleted == 2
            && preSubmitCancellations == 2 && pendingCancellations == 2
            && valuesMatched && coverageMatched && guardsMatched && verticesUnchanged
            && !nativeLastEnabled && rotatedLastSupported && deviceFinalized
            && cleanupMatched && !resourcesRetained && baseline == final
            && liveIndexBytes >= 16_380 && liveIndexBytes <= 131_072
    }
}
