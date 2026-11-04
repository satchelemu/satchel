import Combine
import CryptoKit
import Foundation
import UIKit

struct SwitchModuleReport: Codable {
    let name: String
    let imageBytes: UInt32
    let segmentHashesChecked: Int
}

struct SwitchControlReport: Codable {
    let applicationID: String
    let applicationVersion: UInt32
    let ratingCount: Int
    let metaHashBytes: UInt64
    let controlHashBytes: UInt64
    let metaVerifiedBlocks: Int
    let controlVerifiedBlocks: Int
    let attached: Bool
    let attachmentCount: UInt64
    let parentalPolicy: UInt32
    let programContentHashChecked: Bool
    let publisherSignaturesChecked: Bool
    let cnmtDevelopmentDigestRecomputed: Bool
}

struct SwitchParentalReport: Codable {
    let liveChildren: UInt32
    let initializedChildren: UInt32
    let metadataBoundChildren: UInt32
    let policy: UInt32
    let created: UInt64
    let initializeRequests: UInt64
    let initializeServiced: UInt64
}

struct SwitchTimeReport: Codable {
    let configured: Bool
    let networkInitialized: Bool
    let timezoneProfile: UInt32
    let sharedHandles: UInt32
    let seedAttachments: UInt64
    let clockReads: UInt64
    let timezoneReads: UInt64
    let sharedHandleRequests: UInt64
    let sharedMapRequests: UInt64
    let sharedUnmapRequests: UInt64
    let sharedMapsServiced: UInt64
    let sharedUnmapsServiced: UInt64
    let sharedMappings: UInt64
    let sharedBackingBytes: UInt64

    init(_ info: SatchelSwitchTimeInfo) {
        configured = info.configured != 0
        networkInitialized = info.network_initialized != 0
        timezoneProfile = info.timezone_profile
        sharedHandles = info.shared_handles
        seedAttachments = info.seed_attachments
        clockReads = info.clock_reads
        timezoneReads = info.timezone_reads
        sharedHandleRequests = info.shared_handle_requests
        sharedMapRequests = info.shared_map_requests
        sharedUnmapRequests = info.shared_unmap_requests
        sharedMapsServiced = info.shared_maps_serviced
        sharedUnmapsServiced = info.shared_unmaps_serviced
        sharedMappings = info.shared_mappings
        sharedBackingBytes = info.shared_backing_bytes
    }
}

struct SwitchGraphicsReport: Codable {
    let passed: Bool
    let status: UInt32
    let dynamicCodeFeatures: UInt32
    let maxwellSpirvBytes: UInt32
    let maxwellMSLBytes: UInt32
    let storeMSLBytes: UInt32
    let maxwellCompleted: Bool
    let bufferValueMatched: Bool
    let guardWordsMatched: Bool
}

struct SwitchVulkanReport: Codable {
    let passed: Bool
    let status: UInt32
    let stage: UInt32
    let apiResult: Int32
    let apiVersion: UInt32
    let portabilitySubset: Bool
    let commandCompleted: Bool
    let valueMatched: Bool
    let guardWordsMatched: Bool
    let resourcesRetained: Bool
}

struct SwitchRuntimeReport: Codable {
    let titleID: String
    let verifiedExeFSBytes: UInt64
    let verifiedBlocks: Int
    let modules: [SwitchModuleReport]
    let applicationControl: SwitchControlReport?
    let parentalControl: SwitchParentalReport?
    let time: SwitchTimeReport?
    let cpuOutcome: String
    let scheduler: SwitchSchedulerReport
    let instructions: UInt64
    let memoryQueries: UInt64
    let infoCallsServiced: UInt64
    let activeModule: String?
    let activeModuleOffset: String?
    let pc: String
    let svc: UInt16?
    let infoRequest: SwitchInfoRequest?
    let heapRequestBytes: UInt64?
    let physicalMemoryRequest: SwitchMappingRequest?
    let aliasMemoryRequest: SwitchAliasMappingRequest?
    let aliasMapping: SwitchAliasMappingReport
    let physicalMapping: SwitchPhysicalMappingReport
    let memoryAttributeRequest: SwitchMemoryAttributeRequest?
    let memoryPermissionRequest: SwitchMemoryPermissionRequest?
    let memoryPermissions: SwitchMemoryPermissionReport?
    let memoryAttributes: SwitchMemoryAttributeReport?
    let threadCreationRequest: SwitchThreadCreationRequest?
    let threadAffinityRequest: SwitchThreadAffinityRequest?
    let mutexRequest: SwitchMutexRequest?
    let mutexes: SwitchMutexReport
    let heapBytes: UInt64
    let heapRequests: UInt64
    let systemResourceCapacity: UInt64
    let systemResourceUsed: UInt64
    let portRequests: UInt64
    let smSessions: UInt64
    let sessionCloses: UInt64
    let closedSessionHandles: UInt32
    let ipcRequests: UInt64
    let ipcServiced: UInt64
    let ipcRequest: SwitchIPCReport?
    let logState: SwitchLogReport
    let appletState: SwitchAppletReport
    let appletMessages: SwitchAppletMessageReport
    let storageReads: UInt64
    let storageBytes: UInt64
    let seconds: Double
    let availableMemoryAtStart: UInt64
    let physicalMemory: UInt64
    let graphicsImplemented: Bool
    let graphicsSelfTest: SwitchGraphicsReport
    let compiledVulkanCompute: SwitchCompiledVulkanReport
    let vulkanSelfTest: SwitchVulkanReport
    let vulkanLifecycle: SwitchVulkanLifecycleReport
    let vulkanBuffers: SwitchVulkanBufferReport
    let vulkanTransfers: SwitchVulkanTransferReport
    let vulkanPools: SwitchVulkanPoolReport
    let interpretedVulkanPools: SwitchInterpretedVulkanPoolReport
    let interpretedVulkanTransfers: SwitchInterpretedVulkanTransferReport
    let interpretedVulkanCompute: SwitchInterpretedVulkanComputeReport
    let interpretedVulkanRaster: SwitchInterpretedVulkanRasterReport?
    let capturedVulkan: SwitchCapturedVulkanReport?
    let provokingVulkan: SwitchProvokingVulkanReport?
    let textureFrameVulkan: SwitchTextureFrameVulkanReport?
    let romFS: SwitchRomFSReport?
    let romFSError: String?
    let graphicsEntries: [SwitchGraphicsEntryReport]
    let guestDispatch: SwitchGuestDispatchReport?
    let vi: SwitchVIReport?
    var window: SwitchWindowReport? = nil
    let processConfiguration: SwitchProcessConfigurationReport
}

struct SwitchCompiledVulkanReport: Codable {
    let passed: Bool
    let status: UInt32
    let stage: UInt32
    let compilerStatus: UInt32
    let backendStatus: UInt32
    let runtimeFeatures: UInt32
    let spirvBytes: UInt32
    let activeBindings: UInt32
    let declaredBindings: UInt32
    let casesCompleted: UInt32
    let fencesCompleted: UInt32
    let baselineDevices: UInt32
    let finalDevices: UInt32
    let baselineBuffers: UInt32
    let finalBuffers: UInt32
    let baselineJobs: UInt32
    let finalJobs: UInt32
    let baselinePipelines: UInt32
    let finalPipelines: UInt32
    let compiled: Bool
    let preciseMathRequested: Bool
    let pipelineCreated: Bool
    let valuesMatched: Bool
    let guardsMatched: Bool
    let pipelineFinalized: Bool
    let buffersFinalized: Bool
    let deviceFinalized: Bool
    let cleanupMatched: Bool
    let resourcesRetained: Bool
    let priorReaped: Bool
    let baselineBytes: UInt64
    let finalBytes: UInt64

    init(passed: Bool, result: SatchelGuestComputeDiagnostic) {
        self.passed = passed
        status = result.status
        stage = result.stage
        compilerStatus = result.compiler_status
        backendStatus = result.backend_status
        runtimeFeatures = result.runtime_features
        spirvBytes = result.spirv_bytes
        activeBindings = result.active_bindings
        declaredBindings = result.declared_bindings
        casesCompleted = result.cases_completed
        fencesCompleted = result.fences_completed
        baselineDevices = result.baseline_devices
        finalDevices = result.final_devices
        baselineBuffers = result.baseline_buffers
        finalBuffers = result.final_buffers
        baselineJobs = result.baseline_jobs
        finalJobs = result.final_jobs
        baselinePipelines = result.baseline_pipelines
        finalPipelines = result.final_pipelines
        compiled = result.compiled != 0
        preciseMathRequested = result.precise_math_requested != 0
        pipelineCreated = result.pipeline_created != 0
        valuesMatched = result.values_matched != 0
        guardsMatched = result.guards_matched != 0
        pipelineFinalized = result.pipeline_finalized != 0
        buffersFinalized = result.buffers_finalized != 0
        deviceFinalized = result.device_finalized != 0
        cleanupMatched = result.cleanup_matched != 0
        resourcesRetained = result.resources_retained != 0
        priorReaped = result.prior_reaped != 0
        baselineBytes = result.baseline_bytes
        finalBytes = result.final_bytes
    }
}

struct SwitchInterpretedVulkanComputeReport: Codable {
    let passed: Bool
    let status: UInt32
    let stage: UInt32
    let poolStatus: UInt32
    let backendStatus: UInt32
    let dispatchStatus: UInt32
    let cpuStopReason: UInt32
    let programAdmissions: UInt32
    let pipelinesCompiled: UInt32
    let fencesCompleted: UInt32
    let casesCompleted: UInt32
    let commandsRecorded: UInt32
    let baselineDevices: UInt32
    let finalDevices: UInt32
    let baselineBuffers: UInt32
    let finalBuffers: UInt32
    let baselineJobs: UInt32
    let finalJobs: UInt32
    let baselinePipelines: UInt32
    let finalPipelines: UInt32
    let programInitialized: Bool
    let unflushedPreserved: Bool
    let flushedChanged: Bool
    let changedCodeRebound: Bool
    let valuesMatched: Bool
    let guardsMatched: Bool
    let statePreserved: Bool
    let flushCompleted: Bool
    let gpuCompleted: Bool
    let invalidateCompleted: Bool
    let programFinalized: Bool
    let handleFinalized: Bool
    let commandFinalized: Bool
    let queueFinalized: Bool
    let deviceFinalized: Bool
    let poolFinalized: Bool
    let cleanupMatched: Bool
    let resourcesRetained: Bool
    let priorReaped: Bool
    let instructions: UInt64
    let dispatchEvents: UInt64
    let baselineBytes: UInt64
    let finalBytes: UInt64

    init(passed: Bool, result: SatchelGuestNVNComputeDiagnostic) {
        self.passed = passed
        status = result.memory.status
        stage = result.memory.stage
        poolStatus = result.memory.pool_status
        backendStatus = result.memory.backend_status
        dispatchStatus = result.dispatch_status
        cpuStopReason = result.cpu_stop_reason
        programAdmissions = result.program_admissions
        pipelinesCompiled = result.pipelines_compiled
        fencesCompleted = result.fences_completed
        casesCompleted = result.cases_completed
        commandsRecorded = result.commands_recorded
        baselineDevices = result.memory.baseline_devices
        finalDevices = result.memory.final_devices
        baselineBuffers = result.memory.baseline_buffers
        finalBuffers = result.memory.final_buffers
        baselineJobs = result.memory.baseline_jobs
        finalJobs = result.memory.final_jobs
        baselinePipelines = result.baseline_pipelines
        finalPipelines = result.final_pipelines
        programInitialized = result.program_initialized != 0
        unflushedPreserved = result.unflushed_preserved != 0
        flushedChanged = result.flushed_changed != 0
        changedCodeRebound = result.changed_code_rebound != 0
        valuesMatched = result.memory.values_matched != 0
        guardsMatched = result.memory.guards_matched != 0
        statePreserved = result.memory.state_preserved != 0
        flushCompleted = result.memory.flush_completed != 0
        gpuCompleted = result.memory.gpu_completed != 0
        invalidateCompleted = result.memory.invalidate_completed != 0
        programFinalized = result.program_finalized != 0
        handleFinalized = result.handle_finalized != 0
        commandFinalized = result.command_finalized != 0
        queueFinalized = result.queue_finalized != 0
        deviceFinalized = result.device_finalized != 0
        poolFinalized = result.memory.pool_finalized != 0
        cleanupMatched = result.memory.cleanup_matched != 0
        resourcesRetained = result.memory.resources_retained != 0
        priorReaped = result.memory.prior_reaped != 0
        instructions = result.instructions
        dispatchEvents = result.dispatch_events
        baselineBytes = result.memory.baseline_bytes
        finalBytes = result.memory.final_bytes
    }
}

struct SwitchInterpretedVulkanRasterReport: Codable {
    let memory: SwitchVulkanPoolReport
    let instructions: UInt64
    let dispatchEvents: UInt64
    let dispatchStatus: UInt32
    let cpuStopReason: UInt32
    let programAdmissions: UInt32
    let pipelinesCompiled: UInt32
    let commandsRecorded: UInt32
    let fencesCompleted: UInt32
    let cpuCachePreserved: Bool
    let programsFinalized: UInt32
    let textureFinalized: Bool
    let handleFinalized: Bool
    let commandFinalized: Bool
    let queueFinalized: Bool
    let deviceFinalized: Bool
    let baselineTargets: UInt32
    let finalTargets: UInt32
    let baselinePipelines: UInt32
    let finalPipelines: UInt32
    let baselineTargetBytes: UInt64
    let finalTargetBytes: UInt64

    init(passed: Bool, result: SatchelGuestNVNRasterDiagnostic) {
        memory = SwitchVulkanPoolReport(passed: passed, result: result.memory)
        instructions = result.instructions
        dispatchEvents = result.dispatch_events
        dispatchStatus = result.dispatch_status
        cpuStopReason = result.cpu_stop_reason
        programAdmissions = result.program_admissions
        pipelinesCompiled = result.pipelines_compiled
        commandsRecorded = result.commands_recorded
        fencesCompleted = result.fences_completed
        cpuCachePreserved = result.cpu_cache_preserved != 0
        programsFinalized = result.programs_finalized
        textureFinalized = result.texture_finalized != 0
        handleFinalized = result.handle_finalized != 0
        commandFinalized = result.command_finalized != 0
        queueFinalized = result.queue_finalized != 0
        deviceFinalized = result.device_finalized != 0
        baselineTargets = result.baseline_targets
        finalTargets = result.final_targets
        baselinePipelines = result.baseline_pipelines
        finalPipelines = result.final_pipelines
        baselineTargetBytes = result.baseline_target_bytes
        finalTargetBytes = result.final_target_bytes
    }
}

struct SwitchVulkanLifecycleReport: Codable {
    let passed: Bool
    let status: UInt32
    let stage: UInt32
    let instructions: UInt64
    let dispatchEvents: UInt64
    let deviceCreated: Bool
    let deviceFinalized: Bool
    let guardsMatched: Bool
    let statePreserved: Bool
    let cleanupMatched: Bool
    let computeQueries: UInt32
}

struct SwitchVulkanPoolReport: Codable {
    let passed: Bool
    let status: UInt32
    let stage: UInt32
    let poolStatus: UInt32
    let backendStatus: UInt32
    let initialUpload: Bool
    let flushCompleted: Bool
    let gpuCompleted: Bool
    let invalidateCompleted: Bool
    let valuesMatched: Bool
    let guardsMatched: Bool
    let statePreserved: Bool
    let poolFinalized: Bool
    let cleanupMatched: Bool
    let resourcesRetained: Bool
    let priorReaped: Bool
    let baselineDevices: UInt32
    let finalDevices: UInt32
    let baselineBuffers: UInt32
    let finalBuffers: UInt32
    let baselineJobs: UInt32
    let finalJobs: UInt32
    let baselineBytes: UInt64
    let finalBytes: UInt64

    init(passed: Bool, result: SatchelGuestPoolDiagnostic) {
        self.passed = passed
        status = result.status; stage = result.stage
        poolStatus = result.pool_status; backendStatus = result.backend_status
        initialUpload = result.initial_upload != 0; flushCompleted = result.flush_completed != 0
        gpuCompleted = result.gpu_completed != 0; invalidateCompleted = result.invalidate_completed != 0
        valuesMatched = result.values_matched != 0; guardsMatched = result.guards_matched != 0
        statePreserved = result.state_preserved != 0; poolFinalized = result.pool_finalized != 0
        cleanupMatched = result.cleanup_matched != 0; resourcesRetained = result.resources_retained != 0
        priorReaped = result.prior_reaped != 0
        baselineDevices = result.baseline_devices; finalDevices = result.final_devices
        baselineBuffers = result.baseline_buffers; finalBuffers = result.final_buffers
        baselineJobs = result.baseline_jobs; finalJobs = result.final_jobs
        baselineBytes = result.baseline_bytes; finalBytes = result.final_bytes
    }
}

struct SwitchInterpretedVulkanPoolReport: Codable {
    let memory: SwitchVulkanPoolReport
    let instructions: UInt64
    let dispatchEvents: UInt64
    let dispatchStatus: UInt32
    let cpuStopReason: UInt32
    let deviceFinalized: Bool
}

struct SwitchInterpretedVulkanTransferReport: Codable {
    let memory: SwitchVulkanPoolReport
    let instructions: UInt64
    let dispatchEvents: UInt64
    let dispatchStatus: UInt32
    let cpuStopReason: UInt32
    let queueCreated: Bool
    let commandsRecorded: UInt32
    let queueSubmitted: Bool
    let queueFinished: Bool
    let handleFinalized: Bool
    let commandFinalized: Bool
    let queueFinalized: Bool
    let deviceFinalized: Bool
}

struct SwitchIPCReport: Codable {
    let parserStatus: UInt32
    let kind: UInt32
    let commandID: UInt32
    let serviceKind: UInt32
    let serviceName: String
    let objectKind: UInt32
    let blockedReason: UInt32
    let wireType: UInt32
    let receiveListMode: UInt32
    let receiveListOffset: UInt32
    let reservedHeaderBits: UInt32
    let rawWordCount: UInt32
    let hasSpecialHeader: UInt32
    let storagePlanStatus: UInt32
    let storageStatus: UInt32
    let storageWriteStatus: UInt32
}

struct SwitchLogReport: Codable {
    let packets: UInt64
    let retainedBytes: UInt64
    let droppedPackets: UInt64
    let destination: UInt32
}

struct SwitchAppletReport: Codable {
    let exitToLibraryOnNextProgram: Bool
    let readableEventHandles: UInt64
    let messageEventSignaled: Int32
    let suspendedTicks: UInt64
    let suspendedTickEventSignaled: Int32
    let gpuErrorDetected: Bool
    let gpuErrorEventSignaled: Int32
    let focusState: UInt8
    let operationMode: UInt8
    let foregroundRights: Bool
    let operationNotifications: Bool
    let performanceNotifications: Bool
    let focusHandling: [UInt8]?
    let outOfFocusSuspending: Bool?
}

struct SwitchVulkanTransferReport: Codable {
    let passed: Bool
    let status: UInt32
    let stage: UInt32
    let apiResult: Int32
    let fillCompleted: Bool
    let copyCompleted: Bool
    let valuesMatched: Bool
    let guardsMatched: Bool
    let cleanupMatched: Bool
    let resourcesRetained: Bool
}

struct SwitchVulkanBufferReport: Codable {
    let passed: Bool
    let status: UInt32
    let stage: UInt32
    let createdBuffers: UInt32
    let allocatedBytes: UInt64
    let zeroInitialized: Bool
    let writeCompleted: Bool
    let readCompleted: Bool
    let guardsMatched: Bool
    let boundsRejected: Bool
    let explicitDestroy: Bool
    let parentCleanup: Bool
    let cleanupMatched: Bool
}

struct SwitchProcessConfigurationReport: Codable {
    let addressSpaceBits: UInt8
    let defaultCPU: UInt8
    let mainPriority: UInt8
    let stackBytes: UInt32
    let systemResourceBytes: UInt32
    let hasCorePriority: Bool
    let allowedCoreMask: String
    let allowedPriorityMask: String
    let performanceMode: UInt32
}

enum SwitchSchedulerState: String, Codable {
    case cpuStop, blocked, exited
}

struct SwitchThreadWaitReport: Codable {
    // Counts of retained wait entries, not guest handles or event payloads.
    let appletMessages: UInt32
    let gpuErrors: UInt32
    let suspensionChanges: UInt32
    let threads: UInt32
    let otherEvents: UInt32
}

struct SwitchThreadReport: Codable {
    let id: UInt64
    let priority: UInt32 // Base policy; inheritance is reported separately.
    let effectivePriority: UInt32
    let core: UInt32
    let preferredCore: Int32
    let affinityMask: String
    let state: UInt32
    let instructions: UInt64
    let handleReferences: UInt32
    let waitReferences: UInt32
    let waits: SwitchThreadWaitReport
}

struct SwitchSchedulerReport: Codable {
    let state: SwitchSchedulerState
    let lastThreadID: UInt64?
    let nextDeadlineTick: UInt64?
    let timedOutWhileWaiting: Bool
    let threads: [SwitchThreadReport]
}

struct SwitchGuestDispatchReport: Codable {
    let status: UInt32
    let events: UInt64
    let operation: String?
}

struct SwitchGraphicsEntryReport: Codable {
    let module: String
    let status: String
}

struct SwitchRomFSReport: Codable {
    let directories: Int
    let files: Int
    let imageBytes: UInt64
    let verifiedBlocks: Int
    let sampledBytes: Int
}

struct SwitchInfoRequest: Codable {
    let type: UInt64
    let handle: UInt64
    let subID: UInt64
}

struct SwitchAliasMappingRequest: Codable {
    let destination: String
    let source: String
    let bytes: UInt64
}

struct SwitchAliasMappingReport: Codable {
    let virtualBytes: UInt64
    let mapRequests: UInt64
    let unmapRequests: UInt64
    let mapsServiced: UInt64
    let unmapsServiced: UInt64
}

struct SwitchMappingRequest: Codable {
    let address: String
    let bytes: UInt64
}

struct SwitchPhysicalMappingReport: Codable {
    let bytes: UInt64
    let mapRequests: UInt64
    let unmapRequests: UInt64
    let mapsServiced: UInt64
    let unmapsServiced: UInt64
}

struct SwitchMemoryRegionReport: Codable {
    let address: String
    let bytes: UInt64
    let state: UInt32
    let attributes: UInt32
    let permissions: UInt32
}

struct SwitchMemoryAttributeReport: Codable {
    let requests: UInt64
    let serviced: UInt64
    let permissionLockedBytes: UInt64
}

struct SwitchMemoryPermissionReport: Codable {
    let requests: UInt64
    let serviced: UInt64
    let changedBytes: UInt64
}

struct SwitchMemoryPermissionRequest: Codable {
    let address: String
    let bytes: UInt64
    let permissions: UInt32
    // QueryMemory describes the block containing the first requested address.
    // It does not establish uniform state or permissions over the whole request.
    let firstMapping: SwitchMemoryRegionReport?
}

struct SwitchMemoryAttributeRequest: Codable {
    let address: String
    let bytes: UInt64
    let mask: UInt32
    let attributes: UInt32
}

struct SwitchThreadCreationRequest: Codable {
    let entry: String
    let stackTop: String
    let priority: Int32
    let coreID: Int32
}

struct SwitchThreadAffinityRequest: Codable {
    let handle: UInt32
    let preferredCore: Int32
    let affinityMask: String
}

struct SwitchAppletMessageReport: Codable {
    let phase: UInt32
    let pending: UInt32
    let enqueued: UInt64
    let received: UInt64
}

struct SwitchMutexReport: Codable {
    let lockRequests: UInt64
    let unlockRequests: UInt64
    let waits: UInt64
    let handoffs: UInt64
    let currentWaiters: UInt64
}

struct SwitchMutexRequest: Codable {
    let operation: String
    let address: String
    let ownerHandle: UInt32?
}

private struct DecodedSwitchModule {
    let name: String
    let info: SatchelNSOInfo
    let image: Data
    let header: Data
}

private struct SwitchCPUCheck {
    let result: SatchelCPUProbeResult
    let scheduler: SwitchSchedulerReport
    let queries: UInt64
    let activeModule: String?
    let activeModuleOffset: String?
    let applicationControl: SwitchControlReport
    let parentalControl: SwitchParentalReport
    let time: SwitchTimeReport
    let infoRequest: SwitchInfoRequest?
    let heapRequestBytes: UInt64?
    let physicalMemoryRequest: SwitchMappingRequest?
    let aliasMemoryRequest: SwitchAliasMappingRequest?
    let aliasMapping: SwitchAliasMappingReport
    let physicalMapping: SwitchPhysicalMappingReport
    let memoryAttributeRequest: SwitchMemoryAttributeRequest?
    let memoryPermissionRequest: SwitchMemoryPermissionRequest?
    let memoryPermissions: SwitchMemoryPermissionReport
    let memoryAttributes: SwitchMemoryAttributeReport
    let threadCreationRequest: SwitchThreadCreationRequest?
    let threadAffinityRequest: SwitchThreadAffinityRequest?
    let mutexRequest: SwitchMutexRequest?
    let mutexes: SwitchMutexReport
    let heapBytes: UInt64
    let heapRequests: UInt64
    let systemResourceCapacity: UInt64
    let systemResourceUsed: UInt64
    let portRequests: UInt64
    let smSessions: UInt64
    let sessionCloses: UInt64
    let closedSessionHandles: UInt32
    let ipcRequests: UInt64
    let ipcServiced: UInt64
    let ipcRequest: SwitchIPCReport?
    let logState: SwitchLogReport
    let appletState: SwitchAppletReport
    let appletMessages: SwitchAppletMessageReport
    let storageReads: UInt64
    let storageBytes: UInt64
    let infoCalls: UInt64
    let performanceMode: UInt32
    let graphicsEntries: [SwitchGraphicsEntryReport]
    let guestDispatch: SwitchGuestDispatchReport?
    let vi: SwitchVIReport?
    let window: SwitchWindowReport?
}

private final class RuntimeCancellation: @unchecked Sendable {
    private let lock = NSLock()
    private var stopped = false
    var deadline = Double.greatestFiniteMagnitude
    func cancel() { lock.lock(); stopped = true; lock.unlock() }
    var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return stopped }
}

private func runtimeProbeCancelled(_ context: UnsafeMutableRawPointer?) -> Int32 {
    guard let context else { return 1 }
    let token = Unmanaged<RuntimeCancellation>.fromOpaque(context).takeUnretainedValue()
    return token.cancelled || ProcessInfo.processInfo.systemUptime >= token.deadline ? 1 : 0
}

private final class RuntimeVICancellation {
    let token: RuntimeCancellation
    let surface: SwitchRuntimeSurfaceState
    let epoch: UInt64
    init(token: RuntimeCancellation, surface: SwitchRuntimeSurfaceState, epoch: UInt64) {
        self.token = token; self.surface = surface; self.epoch = epoch
    }
}

private func runtimeVIProbeCancelled(_ context: UnsafeMutableRawPointer?) -> Int32 {
    guard let context else { return 1 }
    let value = Unmanaged<RuntimeVICancellation>.fromOpaque(context).takeUnretainedValue()
    return runtimeProbeCancelled(Unmanaged.passUnretained(value.token).toOpaque()) != 0 ||
        !value.surface.validate(epoch: value.epoch) ? 1 : 0
}

private final class RuntimeSymbols {
    let cpu: OpaquePointer
    let token: RuntimeCancellation
    init(cpu: OpaquePointer, token: RuntimeCancellation) { self.cpu = cpu; self.token = token }
}

private func runtimeSymbolRead(_ context: UnsafeMutableRawPointer?, _ address: UInt64,
                               _ output: UnsafeMutableRawPointer?, _ size: Int) -> Int32 {
    guard let context else { return 0 }
    let symbols = Unmanaged<RuntimeSymbols>.fromOpaque(context).takeUnretainedValue()
    return satchel_cpu_probe_read(symbols.cpu, address, output, size)
}

private func runtimeSymbolCancelled(_ context: UnsafeMutableRawPointer?) -> Int32 {
    guard let context else { return 1 }
    return Unmanaged<RuntimeSymbols>.fromOpaque(context).takeUnretainedValue().token.cancelled ? 1 : 0
}

// Permanent cleanup-only callback; no borrowed session token survives a run.
private func runtimeCapturedCleanupCancelled(_ context: UnsafeMutableRawPointer?) -> Int32 { 1 }

private enum RuntimeGraphicsWorker {
    private static let queue = DispatchQueue(label: "com.dell.satchel.graphics", qos: .userInitiated)
    // Accessed only on queue. Pending resources retain their bounded C owner
    // after this retry window; a subsequent runtime check restarts collection.
    private static var collecting = false
    private static var retiringVI: [OpaquePointer] = []
    private struct FrameLease {
        let window: UInt64
        let frame: UInt64
    }
    private static var retiringFrameLeases: [FrameLease] = []

    // Runtime workers call this only from outside backend callbacks. Device
    // work and retirement share one queue, so diagnostic resource snapshots
    // cannot race another session's dispatch or cleanup.
    static func perform<T>(_ operation: () throws -> T) rethrows -> T { try queue.sync(execute: operation) }

    // Worker-only leases retain no CPU, Process, callback or UI object. Retry
    // before dispatcher retirement, which cannot destroy a still-leased frame.
    private static func releaseFrame(_ lease: FrameLease) -> Bool {
        let status = satchel_guest_dispatch_window_release(lease.window, lease.frame)
        return status == SATCHEL_GWIN_OK || status == SATCHEL_GWIN_BAD_OBJECT || status == SATCHEL_GWIN_INVALID
    }

    private static func collectFrameLeases() {
        retiringFrameLeases.removeAll { releaseFrame($0) }
    }

    static func pendingFrameLeases(window: UInt64) -> UInt32 {
        UInt32(retiringFrameLeases.filter { $0.window == window }.count)
    }

    static func copyWindowFrames(ticket: UInt64, epoch: UInt64,
                                 surface: SwitchRuntimeSurfaceState,
                                 presenter: SwitchRuntimeFramePresenter,
                                 cancelled: () -> Bool) throws -> UInt64 {
        collectFrameLeases()
        let status = satchel_guest_dispatch_window_poll(ticket, 0,
            cancelled() || !surface.validate(epoch: epoch) ? 1 : 0)
        guard status == SATCHEL_GWIN_OK || status == SATCHEL_GWIN_WAIT || status == SATCHEL_GWIN_BUSY ||
              status == SATCHEL_GWIN_NO_SURFACE || status == SATCHEL_GWIN_CANCELLED else {
            throw SwitchImportFailure(message: "The game frame readback stopped (status \(status.rawValue)).")
        }
        guard !cancelled(), surface.validate(epoch: epoch) else { return 0 }
        var copied: UInt64 = 0
        for _ in 0..<Int(SATCHEL_FRAME_CAPACITY) {
            guard !cancelled(), surface.validate(epoch: epoch) else { break }
            var info = SatchelFrameInfo()
            let taken = satchel_guest_dispatch_window_take(ticket, epoch, &info)
            if taken == SATCHEL_GWIN_WAIT || taken == SATCHEL_GWIN_BUSY || taken == SATCHEL_GWIN_NO_SURFACE { break }
            guard taken == SATCHEL_GWIN_OK else {
                throw SwitchImportFailure(message: "The completed game frame could not be leased (status \(taken.rawValue)).")
            }
            var copyStatus = SATCHEL_GWIN_OK
            // C frame tokens are globally monotonic and never reused, including
            // across runs sharing this live UIKit attachment.
            let frame = SwitchRuntimeFrame.copy(info: info, sequence: info.token, surface: surface) { pointer, size in
                copyStatus = satchel_guest_dispatch_window_copy(ticket, info.token, 0, pointer, size)
                return copyStatus == SATCHEL_GWIN_OK
            }
            let lease = FrameLease(window: ticket, frame: info.token)
            if !releaseFrame(lease) {
                precondition(retiringFrameLeases.count < Int(SATCHEL_GWIN_OWNERS * SATCHEL_FRAME_CAPACITY))
                retiringFrameLeases.append(lease)
            }
            guard copyStatus == SATCHEL_GWIN_OK else {
                throw SwitchImportFailure(message: "The game frame could not be copied (status \(copyStatus.rawValue)).")
            }
            if let frame, !cancelled() { presenter.offer(frame); copied += 1 }
        }
        return copied
    }

    // Called on this queue after graphics handoff, before the Process is freed.
    // A future Window may retain VI identities; pending retirement remains
    // CPU-free and must survive until those private references are released.
    static func retireVI(_ owner: inout OpaquePointer?) {
        guard owner != nil else { return }
        _ = satchel_guest_vi_try_destroy(&owner)
        if let retained = owner {
            precondition(retiringVI.count < Int(SATCHEL_GVI_OWNER_LIMIT))
            retiringVI.append(retained)
            owner = nil
        }
    }

    static func collect() {
        queue.async {
            guard !collecting else { return }
            collecting = true
            poll(remaining: 50)
        }
    }

    private static func poll(remaining: Int) {
        collectFrameLeases()
        var report = SatchelGuestRetirementReport()
        let status = satchel_guest_dispatch_retirement_poll(nil, nil, &report)
        retiringVI.removeAll { retained in
            var owner: OpaquePointer? = retained
            _ = satchel_guest_vi_try_destroy(&owner)
            return owner == nil
        }
        var captured = SatchelGuestCapturedProgramDiagnostic()
        let capturedStatus = satchel_guest_captured_program_diagnostic(&captured, runtimeCapturedCleanupCancelled, nil)
        let capturedPending = captured.resources_retained != 0 || capturedStatus == SATCHEL_GUEST_CAPTURED_DIAG_BUSY
        var provoking = SatchelGuestProvokingDiagnostic()
        let provokingStatus = satchel_guest_provoking_diagnostic(&provoking, runtimeCapturedCleanupCancelled, nil)
        let provokingPending = provoking.resources_retained != 0 || provokingStatus == SATCHEL_GUEST_PROVOKING_BUSY
        var textureFrame = SatchelGuestTextureFrameDiagnostic()
        let textureFrameStatus = satchel_guest_texture_frame_diagnostic(&textureFrame, runtimeCapturedCleanupCancelled, nil)
        let textureFramePending = textureFrame.resources_retained != 0 || textureFrameStatus == SATCHEL_GUEST_TFD_BUSY
        guard remaining > 1, status == SATCHEL_GUEST_RETIRE_PENDING || status == SATCHEL_GUEST_RETIRE_BUSY || capturedPending || provokingPending || textureFramePending || !retiringVI.isEmpty || !retiringFrameLeases.isEmpty else {
            collecting = false
            return
        }
        queue.asyncAfter(deadline: .now() + .milliseconds(100)) { poll(remaining: remaining - 1) }
    }
}

final class SwitchRuntimeSession: ObservableObject {
    let surface = SwitchRuntimeSurfaceState()
    @MainActor lazy var framePresenter = SwitchRuntimeFramePresenter(surface: surface)
    @Published private(set) var keysAvailable = false
    @Published private(set) var busy = false
    @Published private(set) var status: String?
    @Published private(set) var programs: [SwitchProgramCandidate] = []
    @Published private(set) var report: SwitchRuntimeReport?
    @Published var error: String?
    private let queue = DispatchQueue(label: "com.dell.satchel.runtime", qos: .userInitiated)
    private var token: RuntimeCancellation?
    private var coordinator: NSFileCoordinator?
    private var lifecycleObserver: AnyCancellable?

    init() {
        keysAvailable = (try? SwitchKeys.load()) != nil
        // Observe UIKit directly as well as the view's scene phase. Presented
        // detail views must not keep a worker alive through scene deactivation.
        lifecycleObserver = NotificationCenter.default.publisher(for: UIScene.willDeactivateNotification)
            .merge(with: NotificationCenter.default.publisher(for: UIApplication.willResignActiveNotification))
            .sink { [weak self] _ in self?.stop() }
    }

    func stop() { token?.cancel(); coordinator?.cancel() }

    func importKeys(_ url: URL) {
        begin("Opening the console key file…") { token in
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            try self.coordinated(url) { readable in
                let file = try FileHandle(forReadingFrom: readable)
                defer { try? file.close() }
                let data = try file.read(upToCount: SwitchKeys.maximumFileSize + 1) ?? Data()
                let keys = try SwitchKeys.parse(data)
                if token.cancelled { throw CancellationError() }
                try SwitchKeys.save(keys)
            }
            DispatchQueue.main.async {
                self.keysAvailable = true
                self.programs = []; self.report = nil
                self.status = "Console keys saved in this app's device-only secure storage."
            }
        }
    }

    func scan(_ url: URL) {
        begin("Reading encrypted program headers…") { token in
            guard let keys = try SwitchKeys.load() else { throw SwitchKeyFailure.unavailable }
            let found = try self.coordinated(url) { readable in
                try SwitchExecutableReader.programs(readable, keys: keys, cancelled: { token.cancelled })
            }
            DispatchQueue.main.async {
                self.programs = found; self.report = nil
                self.status = "Found \(found.count) program archives. Select the internal title ID to check."
            }
        }
    }

    @MainActor func check(_ url: URL, expectedSHA256: String, titleID: String) {
        guard !busy else { return }
        guard let attached = surface.snapshot() else {
            error = "The game display is not attached to an active window yet. Keep this detail open and try again."
            return
        }
        let surfaceEpoch = attached.epoch
        let presenter = framePresenter
        presenter.detach()
        begin("Checking the local file, decrypting ExeFS and verifying executable hashes…") { token in
            let start = ProcessInfo.processInfo.systemUptime
            let availableMemory = UInt64(os_proc_available_memory())
            guard let keys = try SwitchKeys.load() else { throw SwitchKeyFailure.unavailable }
            let result = try self.coordinated(url) { readable in
                try self.verifyIdentity(readable, expected: expectedSHA256, token: token)
                let loaded = try SwitchExecutableReader.load(readable, keys: keys, titleID: titleID, cancelled: { token.cancelled })
                guard let npdm = loaded.processMetadata else {
                    throw SwitchImportFailure(message: "The selected program has no main.npdm process metadata; no process configuration was guessed.")
                }
                var metadata = SatchelProcessMetadata()
                var message = [CChar](repeating: 0, count: 256)
                let validMetadata = npdm.withUnsafeBytes { bytes in
                    satchel_npdm_inspect(bytes.bindMemory(to: UInt8.self).baseAddress, bytes.count, &metadata, &message, message.count)
                }
                guard validMetadata else { throw SwitchImportFailure(message: String(cString: message)) }
                guard metadata.program_id == UInt64(loaded.titleID, radix: 16) else {
                    throw SwitchImportFailure(message: "The process metadata title ID does not match the selected Program archive.")
                }
                guard satchel_switch_process_metadata_supported(&metadata) != 0,
                      (2...Int(SATCHEL_SWITCH_PROCESS_MODULE_LIMIT)).contains(loaded.modules.count) else {
                    throw SwitchImportFailure(message: "The program exceeds the supported process diagnostic profile.")
                }
                // Inspect every module and reserve stack/TLS before allocating any
                // decoded image. A compact compressed file must not cause an OOM.
                var infos: [SatchelNSOInfo] = []
                var dispatchLayout = SatchelGuestDispatchLayout()
                guard satchel_guest_dispatch_build_stubs(nil, 0, &dispatchLayout) != 0 else {
                    throw SwitchImportFailure(message: "The emulator graphics dispatch layout is unavailable.")
                }
                // Reserve TLS, immutable clock and the protected VI identity page.
                var mappedBytes = UInt64(metadata.main_thread_stack_size) + 8192 + UInt64(SATCHEL_SWITCH_IDENTITY_SIZE) + UInt64(dispatchLayout.region_size)
                    + UInt64(metadata.system_resource_size)
                guard mappedBytes <= UInt64(SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT) else {
                    throw SwitchImportFailure(message: "The process's initial memory and service reservations exceed this runtime's memory budget.")
                }
                for module in loaded.modules {
                    if token.cancelled { throw CancellationError() }
                    var info = SatchelNSOInfo()
                    let valid = module.data.withUnsafeBytes { bytes in
                        satchel_nso_inspect(bytes.bindMemory(to: UInt8.self).baseAddress, bytes.count, &info, &message, message.count)
                    }
                    guard valid else { throw SwitchImportFailure(message: String(cString: message)) }
                    let rounded = (UInt64(info.image_size) + 4095) & ~UInt64(4095)
                    guard info.flags & ~UInt32(63) == 0,
                          rounded <= UInt64(SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT) - mappedBytes else {
                        throw SwitchImportFailure(message: "The NSO features or memory requirement exceed the bounded CPU diagnostic limits.")
                    }
                    mappedBytes += rounded; infos.append(info)
                }
                DispatchQueue.main.async { self.status = "Verifying the selected application's linked control metadata…" }
                let control = try SwitchExecutableReader.controlMetadata(readable, keys: keys, titleID: titleID,
                    cancelled: { token.cancelled })
                guard control.applicationID == metadata.program_id else {
                    throw SwitchImportFailure(message: "The verified application metadata does not match the selected process.")
                }
                var reports: [SwitchModuleReport] = []
                var decodedModules: [DecodedSwitchModule] = []
                for (module, info) in zip(loaded.modules, infos) {
                    if token.cancelled { throw CancellationError() }
                    try autoreleasepool {
                        var image = Data(count: Int(info.image_size))
                        let decoded = module.data.withUnsafeBytes { source in
                            image.withUnsafeMutableBytes { target in
                                satchel_nso_decode(source.bindMemory(to: UInt8.self).baseAddress, source.count,
                                              target.bindMemory(to: UInt8.self).baseAddress, target.count, &message, message.count)
                            }
                        }
                        guard decoded else { throw SwitchImportFailure(message: String(cString: message)) }
                        reports.append(SwitchModuleReport(name: module.name, imageBytes: info.image_size,
                                                           segmentHashesChecked: (0..<3).filter { info.flags & (8 << $0) != 0 }.count))
                        decodedModules.append(DecodedSwitchModule(name: module.name, info: info, image: image,
                                                                  header: Data(module.data.prefix(256))))
                    }
                }
                if token.cancelled { throw CancellationError() }
                DispatchQueue.main.async { self.status = "Opening verified game storage for the guest filesystem…" }
                var romFS: SwitchRomFSReport?
                let check = try SwitchExecutableReader.withRomFS(readable, keys: keys, titleID: titleID,
                    cancelled: { token.cancelled }) { storage in
                    let provider = SwitchStorageProvider(storage)
                    return try withExtendedLifetime(provider) {
                        DispatchQueue.main.async { self.status = "Executing the original loader with verified game storage attached…" }
                        let result = try self.probe(decodedModules, metadata: metadata, control: control, storage: provider, token: token, surfaceEpoch: surfaceEpoch, presenter: presenter)
                        if token.cancelled { throw CancellationError() }
                        DispatchQueue.main.async { self.status = "Verifying the game asset index and a streamed RomFS read…" }
                        let sampled = try storage.verifySample()
                        romFS = SwitchRomFSReport(directories: storage.directoryCount, files: storage.fileCount,
                            imageBytes: storage.imageBytes, verifiedBlocks: storage.verifiedBlocks, sampledBytes: sampled)
                        return result
                    }
                }
                let cpu = check.result
                let outcome: String
                if check.scheduler.state == .blocked {
                    if check.scheduler.timedOutWhileWaiting {
                        outcome = "Stopped at the diagnostic time limit while guest threads were waiting."
                    } else if cpu.steps >= 100_000_000 {
                        outcome = "Stopped at the diagnostic instruction limit while guest threads were waiting."
                    } else {
                        outcome = "No guest thread is currently runnable."
                    }
                } else if check.scheduler.state == .exited {
                    outcome = "All guest threads have exited."
                } else {
                    switch cpu.reason {
                    case SATCHEL_CPU_SVC:
                        if cpu.svc == 0x21, let ipc = check.ipcRequest,
                           ipc.blockedReason == UInt32(SATCHEL_SWITCH_IPC_BLOCK_SERVICE) {
                            outcome = "Stopped before connecting the unimplemented \(ipc.serviceName) service."
                        } else if cpu.svc == 0x21, let ipc = check.ipcRequest,
                                  ipc.blockedReason == UInt32(SATCHEL_SWITCH_IPC_BLOCK_COMMAND) {
                            outcome = "Stopped before unimplemented \(ipc.serviceName) IPC operation \(ipc.commandID)."
                        } else {
                            outcome = "Stopped at an unsupported Horizon request (system call \(String(format: "0x%02x", cpu.svc)))."
                        }
                    case SATCHEL_CPU_BUDGET: outcome = "Stopped at the diagnostic instruction limit."
                    case SATCHEL_CPU_CANCELLED: outcome = "Stopped at the diagnostic time limit or cancellation request."
                    case SATCHEL_CPU_UNSUPPORTED: outcome = "Stopped at " + self.instructionFamily(cpu.instruction) + " outside the reviewed CPU subset."
                    case SATCHEL_CPU_MEMORY_FAULT: outcome = "Stopped at a guest memory access outside the mapped permissions."
                    case SATCHEL_CPU_BREAKPOINT:
                        if let vi = check.vi, let status = vi.lastStatus, status != UInt32(SATCHEL_GVI_OK.rawValue) {
                            outcome = "Stopped at display operation \(vi.lastOperation ?? "unknown") (status \(status))."
                        } else if let dispatch = check.guestDispatch {
                            switch dispatch.status {
                            case UInt32(SATCHEL_GUEST_DISPATCH_UNIMPLEMENTED.rawValue):
                                outcome = "Stopped before unimplemented NVN operation \(dispatch.operation ?? "(unknown)")."
                            case UInt32(SATCHEL_GUEST_DISPATCH_LIMIT.rawValue):
                                outcome = "Stopped at the graphics dispatch time or event limit."
                            case UInt32(SATCHEL_GUEST_DISPATCH_CANCELLED.rawValue):
                                outcome = "Stopped at the graphics dispatch time limit or cancellation request."
                            case UInt32(SATCHEL_GUEST_DISPATCH_UNKNOWN_NAME.rawValue):
                                outcome = "Stopped at an unsupported NVN entry-point request."
                            case UInt32(SATCHEL_GUEST_DISPATCH_UNSUPPORTED_QUERY.rawValue):
                                outcome = "Stopped before an unsupported graphics device capability query."
                            case UInt32(SATCHEL_GUEST_DISPATCH_SHADER_STATE_UNAVAILABLE.rawValue):
                                outcome = "Shader source was captured, but the required graphics state is not implemented yet."
                            case UInt32(SATCHEL_GUEST_DISPATCH_WINDOW_WAIT.rawValue):
                                outcome = "Stopped at the diagnostic limit while waiting for the game display queue."
                            case UInt32(SATCHEL_GUEST_DISPATCH_SURFACE_UNAVAILABLE.rawValue):
                                outcome = "Stopped because the game display is no longer attached."
                            case UInt32(SATCHEL_GUEST_DISPATCH_PRESENT_TIMING_UNAVAILABLE.rawValue):
                                outcome = "Stopped because this game's display timing or image format is not supported yet."
                            default: outcome = "Stopped at checked graphics-call validation (status \(dispatch.status))."
                            }
                        } else { outcome = "Stopped at the game's graphics entry point." }
                    default: outcome = "The CPU probe stopped with an internal diagnostic fault."
                    }
                }
                if token.cancelled { throw CancellationError() }
                // Keep this first: a fresh process must exercise the production
                // compiler export without an earlier diagnostic warming it up.
                token.deadline = Double.greatestFiniteMagnitude
                DispatchQueue.main.async { self.status = "Compiling an original shader on this device and checking its Vulkan output…" }
                var compiledCompute = SatchelGuestComputeDiagnostic()
                let compiledComputePassed = RuntimeGraphicsWorker.perform {
                    satchel_guest_compute_diagnostic(&compiledCompute, runtimeProbeCancelled,
                        Unmanaged.passUnretained(token).toOpaque())
                }
                if token.cancelled { throw CancellationError() }
                let compiledComputeReport = SwitchCompiledVulkanReport(passed: compiledComputePassed != 0,
                    result: compiledCompute)
                DispatchQueue.main.async { self.status = "Checking the embedded shader compiler with original synthetic fixtures…" }
                var graphics = SatchelGraphicsDiagnostic()
                let graphicsPassed = RuntimeGraphicsWorker.perform { satchel_graphics_diagnostic(&graphics) }
                if token.cancelled { throw CancellationError() }
                let graphicsReport = SwitchGraphicsReport(passed: graphicsPassed, status: graphics.status,
                    dynamicCodeFeatures: graphics.dynamic_code_features, maxwellSpirvBytes: graphics.maxwell_spirv_bytes,
                    maxwellMSLBytes: graphics.maxwell_msl_bytes, storeMSLBytes: graphics.store_msl_bytes,
                    maxwellCompleted: graphics.maxwell_completed, bufferValueMatched: graphics.buffer_value_matched,
                    guardWordsMatched: graphics.guard_words_matched)
                DispatchQueue.main.async { self.status = "Checking Vulkan portability on this device with an original synthetic shader…" }
                var vulkan = SatchelVulkanReport()
                // The CPU deadline does not apply to this separate GPU test.
                token.deadline = Double.greatestFiniteMagnitude
                let vulkanStatus = RuntimeGraphicsWorker.perform {
                    satchel_vulkan_compute_diagnostic(&vulkan, runtimeProbeCancelled, Unmanaged.passUnretained(token).toOpaque())
                }
                if token.cancelled { throw CancellationError() }
                let vulkanReport = SwitchVulkanReport(passed: vulkanStatus == SATCHEL_VULKAN_OK,
                    status: vulkan.status, stage: vulkan.stage, apiResult: vulkan.vk_result, apiVersion: vulkan.device_api_version,
                    portabilitySubset: vulkan.portability_subset_enabled != 0, commandCompleted: vulkan.command_completed != 0,
                    valueMatched: vulkan.value_matched != 0, guardWordsMatched: vulkan.guard_words_matched != 0,
                    resourcesRetained: vulkan.resources_retained != 0)
                DispatchQueue.main.async { self.status = "Checking interpreted Vulkan device creation and cleanup with an original fixture…" }
                var lifecycle = SatchelGuestVulkanDiagnostic()
                let lifecyclePassed = RuntimeGraphicsWorker.perform {
                    satchel_guest_vulkan_diagnostic(&lifecycle, runtimeProbeCancelled, Unmanaged.passUnretained(token).toOpaque())
                }
                if token.cancelled { throw CancellationError() }
                let lifecycleReport = SwitchVulkanLifecycleReport(passed: lifecyclePassed != 0,
                    status: lifecycle.status, stage: lifecycle.stage, instructions: lifecycle.instructions,
                    dispatchEvents: lifecycle.dispatch_events, deviceCreated: lifecycle.device_created != 0,
                    deviceFinalized: lifecycle.device_finalized != 0, guardsMatched: lifecycle.guards_matched != 0,
                    statePreserved: lifecycle.state_preserved != 0, cleanupMatched: lifecycle.cleanup_matched != 0,
                    computeQueries: lifecycle.compute_queries)
                DispatchQueue.main.async { self.status = "Checking bounded Vulkan buffer storage with original test data…" }
                var buffers = SatchelVulkanBufferReport()
                let bufferStatus = RuntimeGraphicsWorker.perform {
                    satchel_vulkan_buffer_diagnostic(&buffers, runtimeProbeCancelled, Unmanaged.passUnretained(token).toOpaque())
                }
                if token.cancelled { throw CancellationError() }
                let bufferReport = SwitchVulkanBufferReport(passed: bufferStatus == SATCHEL_VULKAN_OK,
                    status: buffers.status, stage: buffers.stage, createdBuffers: buffers.created_buffers,
                    allocatedBytes: buffers.allocated_bytes, zeroInitialized: buffers.zero_initialized != 0,
                    writeCompleted: buffers.write_completed != 0, readCompleted: buffers.read_completed != 0,
                    guardsMatched: buffers.guards_matched != 0, boundsRejected: buffers.bounds_rejected != 0,
                    explicitDestroy: buffers.explicit_destroy != 0, parentCleanup: buffers.parent_cleanup != 0,
                    cleanupMatched: buffers.cleanup_matched != 0)
                DispatchQueue.main.async { self.status = "Checking Vulkan GPU transfers and completion with original test data…" }
                var transfers = SatchelVulkanTransferReport()
                let transferStatus = RuntimeGraphicsWorker.perform {
                    satchel_vulkan_transfer_diagnostic(&transfers, runtimeProbeCancelled, Unmanaged.passUnretained(token).toOpaque())
                }
                if token.cancelled { throw CancellationError() }
                let transferReport = SwitchVulkanTransferReport(passed: transferStatus == SATCHEL_VULKAN_OK,
                    status: transfers.status, stage: transfers.stage, apiResult: transfers.vk_result,
                    fillCompleted: transfers.fill_completed != 0, copyCompleted: transfers.copy_completed != 0,
                    valuesMatched: transfers.values_matched != 0, guardsMatched: transfers.guards_matched != 0,
                    cleanupMatched: transfers.cleanup_matched != 0, resourcesRetained: transfers.resources_retained != 0)
                if token.cancelled { throw CancellationError() }
                DispatchQueue.main.async { self.status = "Checking Vulkan guest-memory pools with original test data…" }
                var pools = SatchelGuestPoolDiagnostic()
                let poolsPassed = RuntimeGraphicsWorker.perform {
                    satchel_guest_pool_diagnostic(&pools, runtimeProbeCancelled, Unmanaged.passUnretained(token).toOpaque())
                }
                if token.cancelled { throw CancellationError() }
                let poolReport = SwitchVulkanPoolReport(passed: poolsPassed != 0, result: pools)
                DispatchQueue.main.async { self.status = "Checking interpreted Vulkan memory-pool calls with an original fixture…" }
                var interpretedPools = SatchelGuestPoolDispatchDiagnostic()
                let interpretedPoolsPassed = RuntimeGraphicsWorker.perform {
                    satchel_guest_pool_dispatch_diagnostic(&interpretedPools, runtimeProbeCancelled, Unmanaged.passUnretained(token).toOpaque())
                }
                if token.cancelled { throw CancellationError() }
                let interpretedPoolReport = SwitchInterpretedVulkanPoolReport(
                    memory: SwitchVulkanPoolReport(passed: interpretedPoolsPassed != 0, result: interpretedPools.memory),
                    instructions: interpretedPools.instructions, dispatchEvents: interpretedPools.dispatch_events,
                    dispatchStatus: interpretedPools.dispatch_status, cpuStopReason: interpretedPools.cpu_stop_reason,
                    deviceFinalized: interpretedPools.device_finalized != 0)
                DispatchQueue.main.async { self.status = "Checking interpreted Vulkan command recording and GPU completion with an original fixture…" }
                var interpretedTransfers = SatchelGuestTransferDiagnostic()
                let interpretedTransfersPassed = RuntimeGraphicsWorker.perform {
                    satchel_guest_transfer_diagnostic(&interpretedTransfers, runtimeProbeCancelled,
                        Unmanaged.passUnretained(token).toOpaque())
                }
                if token.cancelled { throw CancellationError() }
                let interpretedTransferReport = SwitchInterpretedVulkanTransferReport(
                    memory: SwitchVulkanPoolReport(passed: interpretedTransfersPassed != 0, result: interpretedTransfers.memory),
                    instructions: interpretedTransfers.instructions, dispatchEvents: interpretedTransfers.dispatch_events,
                    dispatchStatus: interpretedTransfers.dispatch_status, cpuStopReason: interpretedTransfers.cpu_stop_reason,
                    queueCreated: interpretedTransfers.queue_created != 0, commandsRecorded: interpretedTransfers.commands_recorded,
                    queueSubmitted: interpretedTransfers.queue_submitted != 0, queueFinished: interpretedTransfers.queue_finished != 0,
                    handleFinalized: interpretedTransfers.handle_finalized != 0, commandFinalized: interpretedTransfers.command_finalized != 0,
                    queueFinalized: interpretedTransfers.queue_finalized != 0, deviceFinalized: interpretedTransfers.device_finalized != 0)
                DispatchQueue.main.async { self.status = "Checking interpreted Vulkan program and compute commands with original shaders…" }
                var interpretedCompute = SatchelGuestNVNComputeDiagnostic()
                let interpretedComputePassed = RuntimeGraphicsWorker.perform {
                    satchel_guest_nvn_compute_diagnostic(&interpretedCompute, runtimeProbeCancelled,
                        Unmanaged.passUnretained(token).toOpaque())
                }
                // The diagnostic hands off GPU owners before releasing its CPU.
                RuntimeGraphicsWorker.collect()
                if token.cancelled { throw CancellationError() }
                let interpretedComputeReport = SwitchInterpretedVulkanComputeReport(
                    passed: interpretedComputePassed != 0, result: interpretedCompute)
                DispatchQueue.main.async { self.status = "Checking interpreted Vulkan drawing with original shaders…" }
                var interpretedRaster = SatchelGuestNVNRasterDiagnostic()
                let interpretedRasterPassed = RuntimeGraphicsWorker.perform {
                    satchel_guest_nvn_raster_diagnostic(&interpretedRaster, runtimeProbeCancelled,
                        Unmanaged.passUnretained(token).toOpaque())
                }
                RuntimeGraphicsWorker.collect()
                if token.cancelled { throw CancellationError() }
                let interpretedRasterReport = SwitchInterpretedVulkanRasterReport(
                    passed: interpretedRasterPassed != 0, result: interpretedRaster)
                DispatchQueue.main.async { self.status = "Checking captured original shaders with Vulkan…" }
                var captured = SatchelGuestCapturedProgramDiagnostic()
                let capturedStatus = RuntimeGraphicsWorker.perform {
                    satchel_guest_captured_program_diagnostic(&captured, runtimeProbeCancelled,
                        Unmanaged.passUnretained(token).toOpaque())
                }
                RuntimeGraphicsWorker.collect()
                if token.cancelled { throw CancellationError() }
                let capturedReport = SwitchCapturedVulkanReport(status: capturedStatus, result: captured)
                DispatchQueue.main.async { self.status = "Checking Vulkan flat-shading modes with original shaders…" }
                var provoking = SatchelGuestProvokingDiagnostic()
                let provokingStatus = RuntimeGraphicsWorker.perform {
                    satchel_guest_provoking_diagnostic(&provoking, runtimeProbeCancelled,
                        Unmanaged.passUnretained(token).toOpaque())
                }
                RuntimeGraphicsWorker.collect()
                if token.cancelled { throw CancellationError() }
                let provokingReport = SwitchProvokingVulkanReport(status: provokingStatus, result: provoking)
                DispatchQueue.main.async { self.status = "Checking Vulkan texture-to-frame transfer with an original fixture…" }
                var textureFrame = SatchelGuestTextureFrameDiagnostic()
                let textureFrameStatus = RuntimeGraphicsWorker.perform {
                    satchel_guest_texture_frame_diagnostic(&textureFrame, runtimeProbeCancelled,
                        Unmanaged.passUnretained(token).toOpaque())
                }
                RuntimeGraphicsWorker.collect()
                if token.cancelled { throw CancellationError() }
                let textureFrameReport = SwitchTextureFrameVulkanReport(status: textureFrameStatus, result: textureFrame)
                return SwitchRuntimeReport(titleID: titleID, verifiedExeFSBytes: loaded.exeFSBytes,
                    verifiedBlocks: loaded.verifiedBlocks, modules: reports, applicationControl: check.applicationControl,
                    parentalControl: check.parentalControl, time: check.time,
                    cpuOutcome: outcome, scheduler: check.scheduler,
                    instructions: cpu.steps, memoryQueries: check.queries, infoCallsServiced: check.infoCalls,
                    activeModule: check.activeModule, activeModuleOffset: check.activeModuleOffset,
                    pc: String(format: "0x%llx", cpu.pc),
                    svc: check.scheduler.state == .cpuStop && cpu.reason == SATCHEL_CPU_SVC ? cpu.svc : nil,
                    infoRequest: check.infoRequest,
                    heapRequestBytes: check.heapRequestBytes,
                    physicalMemoryRequest: check.physicalMemoryRequest,
                    aliasMemoryRequest: check.aliasMemoryRequest,
                    aliasMapping: check.aliasMapping,
                    physicalMapping: check.physicalMapping,
                    memoryAttributeRequest: check.memoryAttributeRequest,
                    memoryPermissionRequest: check.memoryPermissionRequest,
                    memoryPermissions: check.memoryPermissions,
                    memoryAttributes: check.memoryAttributes,
                    threadCreationRequest: check.threadCreationRequest,
                    threadAffinityRequest: check.threadAffinityRequest,
                    mutexRequest: check.mutexRequest, mutexes: check.mutexes,
                    heapBytes: check.heapBytes, heapRequests: check.heapRequests,
                    systemResourceCapacity: check.systemResourceCapacity, systemResourceUsed: check.systemResourceUsed,
                    portRequests: check.portRequests, smSessions: check.smSessions,
                    sessionCloses: check.sessionCloses, closedSessionHandles: check.closedSessionHandles,
                    ipcRequests: check.ipcRequests, ipcServiced: check.ipcServiced, ipcRequest: check.ipcRequest,
                    logState: check.logState,
                    appletState: check.appletState, appletMessages: check.appletMessages,
                    storageReads: check.storageReads, storageBytes: check.storageBytes,
                    seconds: ProcessInfo.processInfo.systemUptime - start,
                    availableMemoryAtStart: availableMemory, physicalMemory: ProcessInfo.processInfo.physicalMemory,
                    graphicsImplemented: false,
                    graphicsSelfTest: graphicsReport, compiledVulkanCompute: compiledComputeReport, vulkanSelfTest: vulkanReport,
                    vulkanLifecycle: lifecycleReport, vulkanBuffers: bufferReport, vulkanTransfers: transferReport,
                    vulkanPools: poolReport,
                    interpretedVulkanPools: interpretedPoolReport,
                    interpretedVulkanTransfers: interpretedTransferReport,
                    interpretedVulkanCompute: interpretedComputeReport,
                    interpretedVulkanRaster: interpretedRasterReport,
                    capturedVulkan: capturedReport,
                    provokingVulkan: provokingReport,
                    textureFrameVulkan: textureFrameReport,
                    romFS: romFS, romFSError: nil,
                    graphicsEntries: check.graphicsEntries, guestDispatch: check.guestDispatch, vi: check.vi, window: check.window,
                    processConfiguration: SwitchProcessConfigurationReport(addressSpaceBits: metadata.address_space_bits,
                        defaultCPU: metadata.default_cpu_id, mainPriority: metadata.main_thread_priority,
                        stackBytes: metadata.main_thread_stack_size, systemResourceBytes: metadata.system_resource_size,
                        hasCorePriority: metadata.has_core_priority,
                        allowedCoreMask: String(format: "0x%llx", metadata.allowed_core_mask),
                        allowedPriorityMask: String(format: "0x%llx", metadata.allowed_priority_mask),
                        performanceMode: check.performanceMode))
            }
            DispatchQueue.main.async {
                self.report = result
                self.status = "Executable check finished. This is a loader diagnostic, not game startup."
            }
            let destination = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Runtime-check.json")
            let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            try encoder.encode(result).write(to: destination, options: .atomic)
        }
    }

    private func begin(_ message: String, operation: @escaping (RuntimeCancellation) throws -> Void) {
        guard !busy else { return }
        let current = RuntimeCancellation(); token = current
        busy = true; error = nil; report = nil; status = message
        queue.async {
            RuntimeGraphicsWorker.collect()
            do { try operation(current) }
            catch {
                DispatchQueue.main.async {
                    if current.cancelled || error is CancellationError { self.status = "Runtime check cancelled." }
                    else { self.error = error.localizedDescription; self.status = nil }
                }
            }
            DispatchQueue.main.async {
                // Cancellation wins over already queued progress/completion UI.
                if current.cancelled { self.report = nil; self.status = "Runtime check cancelled." }
                self.busy = false; self.token = nil; self.coordinator = nil
            }
        }
    }

    private func coordinated<T>(_ url: URL, operation: (URL) throws -> T) throws -> T {
        let access = NSFileCoordinator()
        DispatchQueue.main.async {
            self.coordinator = access
            if self.token?.cancelled == true { access.cancel() }
        }
        var result: Result<T, Error>?
        var error: NSError?
        access.coordinate(readingItemAt: url, options: .withoutChanges, error: &error) { readable in
            result = Result { try operation(readable) }
        }
        guard let result else {
            if let error { throw error }
            throw SwitchImportFailure(message: "The local game file could not be coordinated for reading.")
        }
        return try result.get()
    }

    private func verifyIdentity(_ url: URL, expected: String, token: RuntimeCancellation) throws {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        var hash = SHA256()
        while true {
            if token.cancelled { throw CancellationError() }
            let done = try autoreleasepool {
                let bytes = try file.read(upToCount: 1024 * 1024) ?? Data()
                if bytes.isEmpty { return true }
                hash.update(data: bytes); return false
            }
            if done { break }
        }
        guard hash.finalize().map({ String(format: "%02x", $0) }).joined() == expected else {
            throw SwitchImportFailure(message: "The local package changed since import. Import the original file again before checking it.")
        }
    }

    private func probe(_ modules: [DecodedSwitchModule], metadata: SatchelProcessMetadata, control: SwitchControlMetadata,
                       storage provider: SwitchStorageProvider,
                       token: RuntimeCancellation, surfaceEpoch: UInt64, presenter: SwitchRuntimeFramePresenter) throws -> SwitchCPUCheck {
        var processMetadata = metadata
        var message = [CChar](repeating: 0, count: 256)
        // Retain bridged storage and C names until create has copied all regions.
        let storage = modules.map { $0.image as NSData }
        let names = modules.map { strdup($0.name) }
        defer { names.forEach { free($0) } }
        guard names.allSatisfy({ $0 != nil }) else {
            throw SwitchImportFailure(message: "Unable to allocate module names.")
        }
        let inputs = modules.indices.map { i in
            SatchelSwitchProcessModule(name: UnsafePointer(names[i]!), info: modules[i].info,
                image: storage[i].bytes.assumingMemoryBound(to: UInt8.self), image_size: storage[i].length)
        }
        var dispatchLayout = SatchelGuestDispatchLayout()
        guard satchel_guest_dispatch_build_stubs(nil, 0, &dispatchLayout) != 0 else {
            throw SwitchImportFailure(message: "The emulator graphics dispatch layout is unavailable.")
        }
        var dispatchBytes = Data(count: Int(dispatchLayout.region_size))
        let built = dispatchBytes.withUnsafeMutableBytes { bytes in
            satchel_guest_dispatch_build_stubs(bytes.baseAddress, bytes.count, &dispatchLayout)
        }
        guard built != 0 else { throw SwitchImportFailure(message: "Unable to build emulator-owned graphics dispatch data.") }
        let process = withExtendedLifetime(storage) {
            dispatchBytes.withUnsafeBytes { bytes in
                var config = SatchelSwitchDispatchCode(bytes: bytes.bindMemory(to: UInt8.self).baseAddress, size: bytes.count)
                return satchel_switch_process_create_with_dispatch_and_identity(inputs, inputs.count, &processMetadata, &config, &message, message.count)
            }
        }
        guard let process else { throw SwitchImportFailure(message: String(cString: message)) }
        defer { satchel_switch_process_destroy(process) }
        var applicationControl = SatchelSwitchApplicationControl()
        applicationControl.application_id = control.applicationID
        applicationControl.add_on_content_base_id = control.addOnContentBaseID
        applicationControl.application_version = control.applicationVersion
        applicationControl.parental_control_flags = control.parentalControlFlags
        guard control.ratingAges.count == MemoryLayout.size(ofValue: applicationControl.rating_ages) else {
            throw SwitchImportFailure(message: "The application rating metadata has an unsupported size.")
        }
        control.ratingAges.withUnsafeBytes { source in
            withUnsafeMutableBytes(of: &applicationControl.rating_ages) { $0.copyMemory(from: source) }
        }
        guard satchel_switch_process_attach_application_control(process, &applicationControl) != 0 else {
            throw SwitchImportFailure(message: "Unable to attach the verified application metadata to its process.")
        }
        let clockSample = try SwitchClockSampler.capture(frequency: UInt64(SATCHEL_CPU_SYSTEM_TICK_FREQUENCY), readTick: {
            var tick: UInt64 = 0
            guard satchel_switch_process_get_system_tick(process, &tick) != 0 else {
                throw SwitchClockSamplingError.unavailable
            }
            return tick
        })
        var timeSeed = try clockSample.seed()
        guard satchel_switch_process_attach_time_seed(process, &timeSeed) != 0 else {
            throw SwitchImportFailure(message: "Unable to initialize the process clock from this device.")
        }
        guard let initialCPU = satchel_switch_process_cpu(process) else {
            throw SwitchImportFailure(message: "The process has no CPU context.")
        }
        var storageDescriptor = provider.descriptor
        guard satchel_switch_process_attach_storage(process, &storageDescriptor) != 0 else {
            throw SwitchImportFailure(message: "Unable to attach the verified read-only game storage to this process.")
        }
        if token.cancelled { throw CancellationError() }
        // This is the user-started foreground run. Queue the modeled launch
        // transition once, before any original guest instruction executes.
        guard satchel_switch_process_begin_foreground_launch(process) != 0 else {
            throw SwitchImportFailure(message: "Unable to begin the application's foreground launch.")
        }
        var graphicsEntries: [SwitchGraphicsEntryReport] = []
        var graphicsEntryAddresses: [UInt64] = []
        var symbolTables: [SatchelGuestSymbolTable] = []
        // Symbol callbacks borrow the initial context only before threads run.
        do {
            let symbols = RuntimeSymbols(cpu: initialCPU, token: token)
            for (i, module) in modules.enumerated() {
                var table = SatchelGuestSymbolTable(), symbol = SatchelGuestSymbol()
                let parsed = module.header.withUnsafeBytes { header in
                    satchel_guest_symbols_nso_table(header.baseAddress, header.count,
                        satchel_switch_process_module_address(process, i), &table)
                }
                guard parsed == SATCHEL_GUEST_SYMBOL_FOUND else {
                    throw SwitchImportFailure(message: "The module's dynamic symbol metadata is unsupported or malformed.")
                }
                symbolTables.append(table)
                let found = withExtendedLifetime(symbols) {
                    satchel_guest_symbols_find_function(&table, runtimeSymbolRead, runtimeSymbolCancelled,
                        Unmanaged.passUnretained(symbols).toOpaque(), "nvnBootstrapLoader", 18, &symbol)
                }
                let status: String
                switch found {
                case SATCHEL_GUEST_SYMBOL_FOUND:
                    status = "defined"; graphicsEntryAddresses.append(symbol.address)
                case SATCHEL_GUEST_SYMBOL_UNDEFINED: status = "imported"
                case SATCHEL_GUEST_SYMBOL_NOT_FOUND: status = "absent"
                case SATCHEL_GUEST_SYMBOL_CANCELLED: throw CancellationError()
                default: throw SwitchImportFailure(message: "The graphics entry point could not be resolved through checked guest memory.")
                }
                graphicsEntries.append(SwitchGraphicsEntryReport(module: module.name, status: status))
            }
        }
        guard graphicsEntryAddresses.count <= 1 else {
            throw SwitchImportFailure(message: "The graphics entry point has multiple definitions and is ambiguous.")
        }
        token.deadline = ProcessInfo.processInfo.systemUptime + 10
        var dispatch: OpaquePointer?
        var viOwner: OpaquePointer?
        var viEntries = SatchelGuestVIEntries()
        var viPoints: [UInt64] = []
        var viReturned: UInt64 = 0
        var viLast: SatchelGuestVIResult?
        var windowTicket: UInt64 = 0
        var windowAttempts: UInt64 = 0, windowReturned: UInt64 = 0, windowWaits: UInt64 = 0
        var copiedFrames: UInt64 = 0
        var windowLastOperation: String?, windowLastStatus: UInt32?
        let viCancellation = RuntimeVICancellation(token: token, surface: surface, epoch: surfaceEpoch)
        defer {
            if dispatch != nil {
                // This worker exclusively owns the stopped dispatcher and is
                // outside all backend callbacks. Transfer before freeing CPU
                // or verified storage; retirement borrows neither of them.
                RuntimeGraphicsWorker.perform {
                    if windowTicket != 0 {
                        _ = satchel_guest_dispatch_window_detach(windowTicket, surfaceEpoch)
                    }
                    let accepted = satchel_guest_dispatch_handoff(&dispatch)
                    precondition(accepted != 0 && dispatch == nil, "Graphics ownership handoff failed")
                }
            }
            RuntimeGraphicsWorker.perform { RuntimeGraphicsWorker.retireVI(&viOwner) }
            RuntimeGraphicsWorker.collect()
        }
        if let bootstrap = graphicsEntryAddresses.first {
            dispatch = satchel_guest_dispatch_create(bootstrap, satchel_switch_process_dispatch_address(process), 100_000, 10_000)
            guard let dispatch else { throw SwitchImportFailure(message: "Unable to create the checked graphics call boundary.") }
            var backend = SatchelGuestDeviceBackendV2()
            guard satchel_vulkan_guest_device_backend_v2(&backend) != 0,
                  satchel_guest_dispatch_set_device_backend_v2(dispatch, &backend) != 0 else {
                throw SwitchImportFailure(message: "Unable to attach the Vulkan device, transfer and bounded compute/raster backend to the checked guest call boundary.")
            }
            guard surface.validate(epoch: surfaceEpoch) else {
                throw SwitchImportFailure(message: "The game display detached before execution began.")
            }
            let symbols = RuntimeSymbols(cpu: initialCPU, token: token)
            let resolved = withExtendedLifetime(symbols) {
                satchel_guest_vi_resolve_entries(symbolTables, symbolTables.count, runtimeSymbolRead,
                    runtimeSymbolCancelled, Unmanaged.passUnretained(symbols).toOpaque(), 1_000, &viEntries)
            }
            guard resolved == SATCHEL_GVI_OK else {
                throw SwitchImportFailure(message: "The program's display entry points are unsupported or unavailable (status \(resolved.rawValue)).")
            }
            var reservation = SatchelSwitchIdentityReservation()
            guard satchel_switch_process_identity_reservation(process, &reservation) != 0 else {
                throw SwitchImportFailure(message: "The protected display identity reservation is unavailable.")
            }
            var viConfig = SatchelGuestVIConfig(version: 1, size: UInt32(MemoryLayout<SatchelGuestVIConfig>.size),
                reservation: SatchelGuestVIReservation(version: reservation.version, size: reservation.size,
                    process_id: reservation.process_id, generation: reservation.generation,
                    address: reservation.address, length: reservation.length),
                surface_epoch: surfaceEpoch, max_events: 100_000, timeout_ms: 10_000)
            let claim = RuntimeVIClaimContext(process: process, surface: surface, epoch: surfaceEpoch,
                cancelled: { runtimeVIProbeCancelled(Unmanaged.passUnretained(viCancellation).toOpaque()) != 0 })
            let created = withExtendedLifetime((claim, viCancellation)) {
                RuntimeGraphicsWorker.perform {
                    guard surface.validate(epoch: surfaceEpoch), !token.cancelled else { return SATCHEL_GVI_NO_SURFACE }
                    return satchel_guest_vi_create(&viConfig, initialCPU, runtimeVIClaim,
                        Unmanaged.passUnretained(claim).toOpaque(), &viOwner)
                }
            }
            guard created == SATCHEL_GVI_OK, viOwner != nil else {
                throw SwitchImportFailure(message: "Unable to attach the checked display service (status \(created.rawValue)).")
            }
            guard RuntimeGraphicsWorker.perform({
                satchel_guest_dispatch_enable_vulkan_windows(dispatch, viOwner, &windowTicket)
            }) != 0, windowTicket != 0 else {
                throw SwitchImportFailure(message: "Unable to attach the checked Vulkan window backend.")
            }
            var entryStorage = [UInt64](repeating: 0, count: Int(SATCHEL_GVI_ENTRY_COUNT))
            let entryCount = satchel_guest_vi_entry_points(&viEntries, &entryStorage, entryStorage.count)
            guard entryCount > 0 else { throw SwitchImportFailure(message: "The display entry list is invalid.") }
            viPoints = Array(entryStorage.prefix(entryCount))
            var points = [UInt64](repeating: 0, count: 2)
            guard satchel_guest_dispatch_breakpoints(dispatch, &points) != 0 else {
                throw SwitchImportFailure(message: "The graphics breakpoint list is unavailable.")
            }
            points.append(contentsOf: viPoints)
            guard Set(points).count == points.count,
                  satchel_cpu_probe_set_breakpoints(initialCPU, points, points.count) != 0 else {
                throw SwitchImportFailure(message: "The display or graphics entries are ambiguous or not executable guest memory.")
            }
        }
        var cpu = SatchelCPUProbeResult(), steps: UInt64 = 0
        var scheduledState = SATCHEL_SWITCH_RUN_CPU_STOP
        var lastThreadID: UInt64 = 0, nextDeadline: UInt64 = .max
        var timedOutWhileWaiting = false, scheduledCalls = 0
        var guestDispatch: SwitchGuestDispatchReport?
        repeat {
            if token.cancelled { throw CancellationError() }
            // Preserve a waiting result when only the host diagnostic deadline
            // elapsed; no thread resumed or produced a new CPU stop.
            if scheduledState == SATCHEL_SWITCH_RUN_BLOCKED &&
                ProcessInfo.processInfo.systemUptime >= token.deadline {
                timedOutWhileWaiting = true
                break
            }
            scheduledCalls += 1
            guard scheduledCalls <= 100_000 else {
                throw SwitchImportFailure(message: "The scheduler reached its bounded continuation limit.")
            }
            let scheduled = satchel_switch_process_run_scheduled(process, 100_000_000 - steps,
                runtimeProbeCancelled, Unmanaged.passUnretained(token).toOpaque())
            cpu = scheduled.cpu
            guard cpu.steps <= 100_000_000 - steps else {
                throw SwitchImportFailure(message: "The scheduler exceeded its instruction budget.")
            }
            steps += cpu.steps
            scheduledState = scheduled.state
            lastThreadID = scheduled.thread_id
            nextDeadline = scheduled.next_deadline_tick
            if scheduledState == SATCHEL_SWITCH_RUN_EXITED { break }
            if scheduledState == SATCHEL_SWITCH_RUN_BLOCKED {
                if token.cancelled { throw CancellationError() }
                if steps >= 100_000_000 { break }
                guard nextDeadline != .max else { break }
                let remaining = token.deadline - ProcessInfo.processInfo.systemUptime
                if remaining <= 0 { timedOutWhileWaiting = true; break }
                var now: UInt64 = 0
                guard satchel_switch_process_get_system_tick(process, &now) != 0 else {
                    throw SwitchImportFailure(message: "The waiting process has no readable clock.")
                }
                // Wait against the actual shared clock; only the kernel can
                // complete the wait. Short slices keep Stop responsive.
                let untilWake = nextDeadline > now
                    ? Double(nextDeadline - now) / Double(SATCHEL_CPU_SYSTEM_TICK_FREQUENCY) : 0
                let delay = min(0.01, min(remaining, untilWake))
                if delay > 0 { Thread.sleep(forTimeInterval: delay) }
                continue
            }
            guard scheduledState == SATCHEL_SWITCH_RUN_CPU_STOP else {
                throw SwitchImportFailure(message: "The scheduler returned an unknown state.")
            }
            guard cpu.reason == SATCHEL_CPU_BREAKPOINT, let dispatch else { break }
            // The initial/main context may have exited. Never cache a context
            // across scheduling calls or send another thread's trap to it.
            guard let context = satchel_switch_process_cpu(process) else {
                throw SwitchImportFailure(message: "The stopped thread has no CPU context.")
            }
            if viPoints.contains(cpu.pc), let viOwner {
                let result = withExtendedLifetime(viCancellation) {
                    RuntimeGraphicsWorker.perform {
                        var borrow = SatchelGuestVIBorrow(cpu: context,
                            surface_epoch: surface.validate(epoch: surfaceEpoch) ? surfaceEpoch : 0,
                            validate: runtimeVIValidate, validation_context: UnsafeMutableRawPointer(process),
                            guard: runtimeVIOutputGuard, guard_context: UnsafeMutableRawPointer(dispatch),
                            cancelled: runtimeVIProbeCancelled,
                            cancel_context: Unmanaged.passUnretained(viCancellation).toOpaque())
                        return runtimeVIDispatch(entries: &viEntries, owner: viOwner, borrow: &borrow, stop: &cpu)
                    }
                }
                viLast = result
                guard result.status == UInt32(SATCHEL_GVI_OK.rawValue) else { break }
                viReturned += 1
                continue
            }
            var result = SatchelGuestDispatchReport()
            var waitPolls = 0
            repeat {
                result = withExtendedLifetime(viCancellation) {
                    RuntimeGraphicsWorker.perform {
                        var borrow = SatchelGuestVIBorrow(cpu: context,
                            surface_epoch: surface.validate(epoch: surfaceEpoch) ? surfaceEpoch : 0,
                            validate: runtimeVIValidate, validation_context: UnsafeMutableRawPointer(process),
                            guard: runtimeVIOutputGuard, guard_context: UnsafeMutableRawPointer(dispatch),
                            cancelled: runtimeVIProbeCancelled,
                            cancel_context: Unmanaged.passUnretained(viCancellation).toOpaque())
                        // No vendor pacing/color/origin mapping has been verified.
                        // Nil policy explicitly refuses Present rather
                        // than claiming a timing or image format default.
                        return runtimeWindowDispatch(process: process, dispatcher: dispatch,
                            cpu: context, viBorrow: &borrow, stop: &cpu)
                    }
                }
                let operation = satchel_guest_dispatch_name(result.operation_id).map { String(cString: $0) }
                if let operation, operation.hasPrefix("nvnWindow") ||
                    operation == "nvnQueueAcquireTexture" || operation == "nvnQueuePresentTexture" {
                    windowAttempts += 1
                    windowLastOperation = operation; windowLastStatus = result.status
                    if result.status == UInt32(SATCHEL_GUEST_DISPATCH_RETURNED.rawValue) { windowReturned += 1 }
                    if result.status == UInt32(SATCHEL_GUEST_DISPATCH_WINDOW_WAIT.rawValue) { windowWaits += 1 }
                }
                copiedFrames += try RuntimeGraphicsWorker.perform {
                    try RuntimeGraphicsWorker.copyWindowFrames(ticket: windowTicket, epoch: surfaceEpoch,
                        surface: surface, presenter: presenter,
                        cancelled: { token.cancelled || ProcessInfo.processInfo.systemUptime >= token.deadline })
                }
                guard result.status == UInt32(SATCHEL_GUEST_DISPATCH_WINDOW_WAIT.rawValue) else { break }
                if token.cancelled { throw CancellationError() }
                waitPolls += 1
                guard waitPolls < 1_000, surface.validate(epoch: surfaceEpoch),
                      ProcessInfo.processInfo.systemUptime < token.deadline else { break }
                // Park this exact stopped context: no scheduler call, guest
                // register mutation or saved borrowed callback across polls.
                // The adapter revalidates live context/PC/LR/args on every retry.
                Thread.sleep(forTimeInterval: 0.001)
            } while true
            let name = satchel_guest_dispatch_name(result.operation_id).map { String(cString: $0) }
            guestDispatch = SwitchGuestDispatchReport(status: result.status, events: result.events, operation: name)
            if result.status != UInt32(SATCHEL_GUEST_DISPATCH_RETURNED.rawValue) { break }
        } while steps < 100_000_000
        var viReport: SwitchVIReport?
        var windowReport: SwitchWindowReport?
        if windowTicket != 0 {
            var stats = SatchelGuestWindowStats()
            guard RuntimeGraphicsWorker.perform({ satchel_guest_dispatch_window_stats(windowTicket, &stats) }) == SATCHEL_GWIN_OK else {
                throw SwitchImportFailure(message: "Unable to read checked game window state.")
            }
            windowReport = SwitchWindowReport(ownerCreated: true, attempts: windowAttempts,
                returnedCalls: windowReturned, waitReturns: windowWaits, liveWindows: stats.windows,
                pendingCalls: stats.pending_calls, retainedSnapshots: stats.snapshots,
                leasedSnapshots: stats.leased, acquires: stats.acquires, presents: stats.presents,
                snapshotsCompleted: stats.snapshots_completed, copiedFrames: copiedFrames,
                pendingLeaseReleases: RuntimeGraphicsWorker.perform {
                    RuntimeGraphicsWorker.pendingFrameLeases(window: windowTicket)
                }, lastOperation: windowLastOperation, lastStatus: windowLastStatus)
        }
        if let viOwner {
            var stats = SatchelGuestVIStats()
            guard RuntimeGraphicsWorker.perform({ satchel_guest_vi_stats(viOwner, &stats) }) == SATCHEL_GVI_OK else {
                throw SwitchImportFailure(message: "Unable to read checked display service state.")
            }
            viReport = SwitchVIReport(entryCount: UInt32(viPoints.count), surfaceAttachedAtStart: true,
                ownerCreated: true, attempts: stats.events, returnedCalls: viReturned,
                liveDisplays: stats.display, liveLayers: stats.layer, liveNativeWindows: stats.native,
                retainedWindows: stats.references,
                lastOperation: viLast.flatMap { runtimeVIOperationName($0.operation) }, lastStatus: viLast?.status)
        }
        cpu.steps = steps
        let schedulerState: SwitchSchedulerState
        switch scheduledState {
        case SATCHEL_SWITCH_RUN_CPU_STOP: schedulerState = .cpuStop
        case SATCHEL_SWITCH_RUN_BLOCKED: schedulerState = .blocked
        case SATCHEL_SWITCH_RUN_EXITED: schedulerState = .exited
        default: throw SwitchImportFailure(message: "The scheduler returned an unknown state.")
        }
        var threads: [SwitchThreadReport] = []
        for index in 0..<UInt32(SATCHEL_SWITCH_THREAD_LIMIT) {
            var thread = SatchelSwitchThreadInfo()
            if satchel_switch_process_thread_info(process, index, &thread) != 0 {
                var waits = SatchelSwitchThreadWaitInfo()
                guard satchel_switch_process_thread_wait_info(process, index, &waits) != 0 else {
                    throw SwitchImportFailure(message: "The process wait snapshot is inconsistent.")
                }
                threads.append(SwitchThreadReport(id: thread.id, priority: thread.priority, effectivePriority: thread.effective_priority,
                    core: thread.cpu_id, preferredCore: thread.ideal_core_id,
                    affinityMask: String(format: "0x%llx", thread.affinity_mask), state: thread.state,
                    instructions: thread.instructions, handleReferences: thread.handle_references,
                    waitReferences: thread.wait_references,
                    waits: SwitchThreadWaitReport(appletMessages: waits.applet_message_events,
                        gpuErrors: waits.gpu_error_events, suspensionChanges: waits.suspension_events,
                        threads: waits.threads, otherEvents: waits.other_events)))
            }
        }
        guard threads.count == Int(satchel_switch_process_thread_count(process)) else {
            throw SwitchImportFailure(message: "The process thread snapshot is inconsistent.")
        }
        let schedulerReport = SwitchSchedulerReport(state: schedulerState,
            lastThreadID: lastThreadID == 0 ? nil : lastThreadID,
            nextDeadlineTick: nextDeadline == .max ? nil : nextDeadline,
            timedOutWhileWaiting: timedOutWhileWaiting, threads: threads)
        // BLOCKED/EXITED may have reclaimed the last context. Only a genuine
        // CPU stop permits reading registers for the public SVC metadata below.
        let stoppedCPU = schedulerState == .cpuStop ? satchel_switch_process_cpu(process) : nil
        let activeIndex = modules.indices.first { i in
            let base = satchel_switch_process_module_address(process, i)
            let size = (UInt64(modules[i].image.count) + 4095) & ~UInt64(4095)
            return cpu.pc >= base && cpu.pc - base < size
        }
        let active = activeIndex.map { modules[$0].name }
        // Derive the safe relative address from the actual mapped module, rather
        // than requiring manual subtraction when investigating a stopped CPU.
        let activeOffset = activeIndex.map {
            String(format: "0x%llx", cpu.pc - satchel_switch_process_module_address(process, $0))
        }
        var infoRequest: SwitchInfoRequest?
        if cpu.reason == SATCHEL_CPU_SVC && cpu.svc == 0x29, let context = stoppedCPU {
            // Only ABI request metadata; never export general guest registers.
            var type: UInt64 = 0, handle: UInt64 = 0, subID: UInt64 = 0
            satchel_cpu_probe_get_x(context, 1, &type)
            satchel_cpu_probe_get_x(context, 2, &handle)
            satchel_cpu_probe_get_x(context, 3, &subID)
            infoRequest = SwitchInfoRequest(type: type, handle: handle, subID: subID)
        }
        var heapRequestBytes: UInt64?
        if cpu.reason == SATCHEL_CPU_SVC && cpu.svc == 1, let context = stoppedCPU {
            var size: UInt64 = 0
            satchel_cpu_probe_get_x(context, 1, &size)
            heapRequestBytes = size
        }
        var physicalMemoryRequest: SwitchMappingRequest?
        if cpu.reason == SATCHEL_CPU_SVC && (cpu.svc == 0x2c || cpu.svc == 0x2d), let context = stoppedCPU {
            var address: UInt64 = 0, size: UInt64 = 0
            satchel_cpu_probe_get_x(context, 0, &address)
            satchel_cpu_probe_get_x(context, 1, &size)
            physicalMemoryRequest = SwitchMappingRequest(address: String(format: "0x%llx", address), bytes: size)
        }
        var aliasMemoryRequest: SwitchAliasMappingRequest?
        if cpu.reason == SATCHEL_CPU_SVC && (cpu.svc == 4 || cpu.svc == 5), let context = stoppedCPU {
            // Published MapMemory/UnmapMemory ABI addresses only, never memory contents.
            var destination: UInt64 = 0, source: UInt64 = 0, size: UInt64 = 0
            satchel_cpu_probe_get_x(context, 0, &destination)
            satchel_cpu_probe_get_x(context, 1, &source)
            satchel_cpu_probe_get_x(context, 2, &size)
            aliasMemoryRequest = SwitchAliasMappingRequest(destination: String(format: "0x%llx", destination),
                source: String(format: "0x%llx", source), bytes: size)
        }
        var ipc = SatchelSwitchIPCInfo()
        // Published SVC ABI metadata only. In particular, never inspect or
        // export CreateThread's arbitrary user argument (X2).
        var memoryAttributeRequest: SwitchMemoryAttributeRequest?
        var memoryPermissionRequest: SwitchMemoryPermissionRequest?
        var threadCreationRequest: SwitchThreadCreationRequest?
        var threadAffinityRequest: SwitchThreadAffinityRequest?
        var mutexRequest: SwitchMutexRequest?
        if cpu.reason == SATCHEL_CPU_SVC && cpu.svc == 2, let context = stoppedCPU {
            var address: UInt64 = 0, size: UInt64 = 0, permissions: UInt64 = 0
            guard satchel_cpu_probe_get_x(context, 0, &address) != 0,
                  satchel_cpu_probe_get_x(context, 1, &size) != 0,
                  satchel_cpu_probe_get_x(context, 2, &permissions) != 0 else {
                throw SwitchImportFailure(message: "Unable to inspect the stopped memory-permission request.")
            }
            var info = SatchelSwitchMemoryInfo()
            let firstMapping: SwitchMemoryRegionReport? = satchel_switch_process_query(process, address, &info) != 0
                ? SwitchMemoryRegionReport(address: String(format: "0x%llx", info.address), bytes: info.size,
                    state: info.state, attributes: info.attributes, permissions: info.permissions)
                : nil
            memoryPermissionRequest = SwitchMemoryPermissionRequest(address: String(format: "0x%llx", address),
                bytes: size, permissions: UInt32(truncatingIfNeeded: permissions), firstMapping: firstMapping)
        }
        if cpu.reason == SATCHEL_CPU_SVC && cpu.svc == 3, let context = stoppedCPU {
            var address: UInt64 = 0, size: UInt64 = 0, mask: UInt64 = 0, attributes: UInt64 = 0
            satchel_cpu_probe_get_x(context, 0, &address); satchel_cpu_probe_get_x(context, 1, &size)
            satchel_cpu_probe_get_x(context, 2, &mask); satchel_cpu_probe_get_x(context, 3, &attributes)
            memoryAttributeRequest = SwitchMemoryAttributeRequest(address: String(format: "0x%llx", address),
                bytes: size, mask: UInt32(truncatingIfNeeded: mask), attributes: UInt32(truncatingIfNeeded: attributes))
        } else if cpu.reason == SATCHEL_CPU_SVC && cpu.svc == 8, let context = stoppedCPU {
            var entry: UInt64 = 0, stack: UInt64 = 0, priority: UInt64 = 0, core: UInt64 = 0
            satchel_cpu_probe_get_x(context, 1, &entry); satchel_cpu_probe_get_x(context, 3, &stack)
            satchel_cpu_probe_get_x(context, 4, &priority); satchel_cpu_probe_get_x(context, 5, &core)
            threadCreationRequest = SwitchThreadCreationRequest(entry: String(format: "0x%llx", entry),
                stackTop: String(format: "0x%llx", stack), priority: Int32(truncatingIfNeeded: priority),
                coreID: Int32(truncatingIfNeeded: core))
        }
        if cpu.reason == SATCHEL_CPU_SVC && cpu.svc == 0x0f, let context = stoppedCPU {
            // Public SetThreadCoreMask ABI metadata, never arbitrary registers.
            var handle: UInt64 = 0, preferred: UInt64 = 0, mask: UInt64 = 0
            satchel_cpu_probe_get_x(context, 0, &handle)
            satchel_cpu_probe_get_x(context, 1, &preferred)
            satchel_cpu_probe_get_x(context, 2, &mask)
            threadAffinityRequest = SwitchThreadAffinityRequest(handle: UInt32(truncatingIfNeeded: handle),
                preferredCore: Int32(truncatingIfNeeded: preferred), affinityMask: String(format: "0x%llx", mask))
        }
        if cpu.reason == SATCHEL_CPU_SVC && (cpu.svc == 0x1a || cpu.svc == 0x1b), let context = stoppedCPU {
            // Address and typed owner handle only. The opaque tag and mutex word
            // may carry arbitrary guest data and are never read for telemetry.
            var address: UInt64 = 0, owner: UInt64 = 0
            let locking = cpu.svc == 0x1a
            satchel_cpu_probe_get_x(context, locking ? 1 : 0, &address)
            if locking {
                satchel_cpu_probe_get_x(context, 0, &owner)
            }
            mutexRequest = SwitchMutexRequest(operation: locking ? "lock" : "unlock",
                address: String(format: "0x%llx", address),
                ownerHandle: locking ? UInt32(truncatingIfNeeded: owner) : nil)
        }
        var mutexes = SatchelSwitchMutexInfo()
        guard satchel_switch_process_mutex_info(process, &mutexes) != 0 else {
            throw SwitchImportFailure(message: "Unable to read the process mutex counters.")
        }
        var appletMessages = SatchelSwitchAppletMessageInfo()
        guard satchel_switch_process_applet_message_info(process, &appletMessages) != 0 else {
            throw SwitchImportFailure(message: "Unable to read the application message state.")
        }
        var applet = SatchelSwitchAppletState()
        guard satchel_switch_process_applet_state(process, &applet) != 0 else {
            throw SwitchImportFailure(message: "Unable to read the emulated application state.")
        }
        let ipcRequest: SwitchIPCReport? = satchel_switch_process_last_ipc(process, &ipc) != 0
            ? SwitchIPCReport(parserStatus: ipc.parser_status, kind: ipc.kind, commandID: ipc.command_id,
                serviceKind: ipc.service_kind, serviceName: String(cString: satchel_switch_process_service_name(ipc.service_kind)),
                objectKind: ipc.object_kind,
                blockedReason: ipc.blocked_reason,
                wireType: ipc.wire_type, receiveListMode: ipc.receive_list_mode,
                receiveListOffset: ipc.receive_list_offset, reservedHeaderBits: ipc.reserved_header_bits,
                rawWordCount: ipc.raw_word_count, hasSpecialHeader: ipc.has_special_header,
                storagePlanStatus: ipc.storage_plan_status, storageStatus: ipc.storage_status,
                storageWriteStatus: ipc.storage_write_status) : nil
        var controlInfo = SatchelSwitchApplicationControlInfo()
        guard satchel_switch_process_application_control_info(process, &controlInfo) != 0,
              controlInfo.attached != 0, controlInfo.attachment_count == 1 else {
            throw SwitchImportFailure(message: "The process application-metadata state is inconsistent.")
        }
        let controlReport = SwitchControlReport(applicationID: String(format: "%016llx", control.applicationID),
            applicationVersion: control.applicationVersion, ratingCount: control.ratingAges.count,
            metaHashBytes: control.metaHashBytes, controlHashBytes: control.controlHashBytes,
            metaVerifiedBlocks: control.metaVerifiedBlocks, controlVerifiedBlocks: control.controlVerifiedBlocks,
            attached: true, attachmentCount: controlInfo.attachment_count, parentalPolicy: controlInfo.parental_policy,
            programContentHashChecked: false, publisherSignaturesChecked: false, cnmtDevelopmentDigestRecomputed: false)
        var parentalInfo = SatchelSwitchParentalInfo()
        guard satchel_switch_process_parental_info(process, &parentalInfo) != 0 else {
            throw SwitchImportFailure(message: "Unable to read the emulated service initialization state.")
        }
        let parentalReport = SwitchParentalReport(liveChildren: parentalInfo.live_children,
            initializedChildren: parentalInfo.initialized_children, metadataBoundChildren: parentalInfo.metadata_bound_children,
            policy: parentalInfo.policy, created: parentalInfo.created,
            initializeRequests: parentalInfo.initialize_requests, initializeServiced: parentalInfo.initialize_serviced)
        var timeInfo = SatchelSwitchTimeInfo()
        guard satchel_switch_process_time_info(process, &timeInfo) != 0,
              timeInfo.configured != 0, timeInfo.seed_attachments == 1 else {
            throw SwitchImportFailure(message: "The process clock state is inconsistent.")
        }
        return SwitchCPUCheck(result: cpu, scheduler: schedulerReport, queries: satchel_switch_process_queries(process), activeModule: active,
            activeModuleOffset: activeOffset, applicationControl: controlReport, parentalControl: parentalReport, time: SwitchTimeReport(timeInfo),
            infoRequest: infoRequest, heapRequestBytes: heapRequestBytes, physicalMemoryRequest: physicalMemoryRequest,
            aliasMemoryRequest: aliasMemoryRequest,
            aliasMapping: SwitchAliasMappingReport(virtualBytes: satchel_switch_process_alias_memory_size(process),
                mapRequests: satchel_switch_process_alias_map_requests(process),
                unmapRequests: satchel_switch_process_alias_unmap_requests(process),
                mapsServiced: satchel_switch_process_alias_map_serviced(process),
                unmapsServiced: satchel_switch_process_alias_unmap_serviced(process)),
            physicalMapping: SwitchPhysicalMappingReport(bytes: satchel_switch_process_physical_memory_size(process),
                mapRequests: satchel_switch_process_physical_map_requests(process),
                unmapRequests: satchel_switch_process_physical_unmap_requests(process),
                mapsServiced: satchel_switch_process_physical_map_serviced(process),
                unmapsServiced: satchel_switch_process_physical_unmap_serviced(process)),
            memoryAttributeRequest: memoryAttributeRequest, memoryPermissionRequest: memoryPermissionRequest,
            memoryPermissions: SwitchMemoryPermissionReport(requests: satchel_switch_process_permission_requests(process),
                serviced: satchel_switch_process_permission_serviced(process),
                changedBytes: satchel_switch_process_permission_changed_bytes(process)),
            memoryAttributes: SwitchMemoryAttributeReport(requests: satchel_switch_process_attribute_requests(process),
                serviced: satchel_switch_process_attribute_serviced(process),
                permissionLockedBytes: satchel_switch_process_permission_locked_bytes(process)), threadCreationRequest: threadCreationRequest, threadAffinityRequest: threadAffinityRequest, mutexRequest: mutexRequest,
            mutexes: SwitchMutexReport(lockRequests: mutexes.lock_requests, unlockRequests: mutexes.unlock_requests,
                waits: mutexes.waits, handoffs: mutexes.handoffs, currentWaiters: mutexes.current_waiters),
            heapBytes: satchel_switch_process_heap_size(process), heapRequests: satchel_switch_process_heap_requests(process),
            systemResourceCapacity: satchel_switch_process_system_resource_total(process),
            systemResourceUsed: satchel_switch_process_system_resource_used(process),
            portRequests: satchel_switch_process_port_requests(process), smSessions: satchel_switch_process_sm_sessions(process),
            sessionCloses: satchel_switch_process_session_closes(process), closedSessionHandles: satchel_switch_process_closed_sessions(process),
            ipcRequests: satchel_switch_process_ipc_requests(process), ipcServiced: satchel_switch_process_ipc_serviced(process), ipcRequest: ipcRequest,
            logState: SwitchLogReport(packets: satchel_switch_process_log_packets(process),
                retainedBytes: satchel_switch_process_log_retained_bytes(process), droppedPackets: satchel_switch_process_log_dropped_packets(process),
                destination: satchel_switch_process_log_destination(process)),
            appletState: SwitchAppletReport(exitToLibraryOnNextProgram: satchel_switch_process_applet_exit_to_library_enabled(process) != 0,
                readableEventHandles: UInt64(satchel_switch_process_readable_event_handles(process)),
                messageEventSignaled: satchel_switch_process_applet_event_signaled(process),
                suspendedTicks: satchel_switch_process_suspended_ticks(process),
                suspendedTickEventSignaled: satchel_switch_process_suspended_tick_event_signaled(process),
                gpuErrorDetected: satchel_switch_process_gpu_error_detected(process) != 0,
                gpuErrorEventSignaled: satchel_switch_process_gpu_error_event_signaled(process),
                focusState: applet.focus_state, operationMode: applet.operation_mode,
                foregroundRights: applet.foreground_rights != 0,
                operationNotifications: applet.operation_notifications != 0,
                performanceNotifications: applet.performance_notifications != 0,
                focusHandling: applet.focus_handling_set != 0
                    ? [applet.focus_handling.0, applet.focus_handling.1, applet.focus_handling.2] : nil,
                outOfFocusSuspending: applet.out_of_focus_suspending_set != 0
                    ? applet.out_of_focus_suspending != 0 : nil),
            appletMessages: SwitchAppletMessageReport(phase: appletMessages.phase, pending: appletMessages.pending,
                enqueued: appletMessages.enqueued, received: appletMessages.received),
            storageReads: satchel_switch_process_storage_reads(process), storageBytes: satchel_switch_process_storage_bytes(process),
            infoCalls: satchel_switch_process_info_serviced(process),
            performanceMode: satchel_switch_process_performance_mode(process), graphicsEntries: graphicsEntries,
            guestDispatch: guestDispatch, vi: viReport, window: windowReport)
    }

    // Classify a missing CPU feature without exporting game instruction bytes,
    // register operands, literals, or a disassembly of the imported program.
    private func instructionFamily(_ instruction: UInt32) -> String {
        if instruction & 0x3a000000 == 0x28000000 { return "a load/store-pair instruction" }
        if instruction & 0x3b000000 == 0x18000000 { return "a PC-relative literal load" }
        if instruction & 0xfff00000 == 0xd5300000 { return "a system-register read" }
        if instruction & 0x7e000000 == 0x36000000 { return "a test-bit branch" }
        return "an instruction"
    }
}
