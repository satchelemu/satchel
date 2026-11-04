import Foundation

// Development diagnostic only. Uses canonical loaders/process/interpreter and
// stops before NVN execution. Never writes decoded code, assets or key material.
private enum Failure: Error { case invalid }
private final class Deadline {
    var end = ProcessInfo.processInfo.systemUptime + 180
    #if HOST_NVN
    var nvn: OpaquePointer?
    #endif
}
private func cancelled(_ raw: UnsafeMutableRawPointer?) -> Int32 {
    guard let raw else { return 1 }
    let token = Unmanaged<Deadline>.fromOpaque(raw).takeUnretainedValue()
    #if HOST_NVN
    if let owner = token.nvn, satchel_host_nvn_should_cancel(owner) != 0 { return 1 }
    #endif
    return ProcessInfo.processInfo.systemUptime >= token.end ? 1 : 0
}
private final class Symbols {
    let cpu: OpaquePointer
    init(_ cpu: OpaquePointer) { self.cpu = cpu }
}
private func readSymbol(_ raw: UnsafeMutableRawPointer?, _ address: UInt64, _ out: UnsafeMutableRawPointer?, _ size: Int) -> Int32 {
    guard let raw else { return 0 }
    return satchel_cpu_probe_read(Unmanaged<Symbols>.fromOpaque(raw).takeUnretainedValue().cpu, address, out, size)
}
private struct Module { let name: String; let info: SatchelNSOInfo; let image: Data; let header: Data }

@main struct HostRuntime {
    static func main() {
        do { try run() }
        catch { FileHandle.standardError.write(Data("Host runtime diagnostic failed; private input details suppressed.\n".utf8)); exit(1) }
    }
    static func run() throws {
        #if HOST_CPU_DEVELOPMENT
        // Refuse before opening private inputs, even if a manifest is relabeled.
        guard ProcessInfo.processInfo.environment["SATCHEL_HOST_CPU_DEVELOPMENT"] == "1" else { throw Failure.invalid }
        #endif
        #if HOST_NVN
        let nvn = CommandLine.arguments.count == 6 && CommandLine.arguments[5] == "dispatch-nvn"
        guard CommandLine.arguments.count == 5 || nvn else { throw Failure.invalid }
        #else
        let nvn = false
        guard CommandLine.arguments.count == 5 else { throw Failure.invalid }
        #endif
        guard let seconds = Int(CommandLine.arguments[4]), (1...300).contains(seconds) else { throw Failure.invalid }
        let title = CommandLine.arguments[3]
        guard title.count == 16, title.utf8.allSatisfy({ (48...57).contains($0) || (97...102).contains($0) }) else { throw Failure.invalid }
        let keyFile = try FileHandle(forReadingFrom: URL(fileURLWithPath: CommandLine.arguments[1]))
        defer { try? keyFile.close() }
        let keys = try SwitchKeys.parse(keyFile.read(upToCount: SwitchKeys.maximumFileSize + 1) ?? Data())
        let url = URL(fileURLWithPath: CommandLine.arguments[2])
        var result: Result<Void, Error>?, coordinationError: NSError?
        NSFileCoordinator().coordinate(readingItemAt: url, options: .withoutChanges, error: &coordinationError) { readable in
            result = Result { try loadAndRun(readable, keys: keys, title: title, seconds: seconds, nvn: nvn) }
        }
        guard let result else { throw Failure.invalid }
        try result.get()
    }
    private static func loadAndRun(_ url: URL, keys: [String: Data], title: String, seconds: Int, nvn: Bool) throws {
        let token = Deadline(), raw = Unmanaged.passUnretained(token).toOpaque()
        let loaded = try SwitchExecutableReader.load(url, keys: keys, titleID: title, cancelled: { cancelled(raw) != 0 })
        guard let npdm = loaded.processMetadata else { throw Failure.invalid }
        var metadata = SatchelProcessMetadata(), error = [CChar](repeating: 0, count: 256)
        let valid = npdm.withUnsafeBytes { satchel_npdm_inspect($0.bindMemory(to: UInt8.self).baseAddress, $0.count, &metadata, &error, error.count) }
        guard valid, metadata.program_id == UInt64(title, radix: 16), satchel_switch_process_metadata_supported(&metadata) != 0,
              (2...Int(SATCHEL_SWITCH_PROCESS_MODULE_LIMIT)).contains(loaded.modules.count) else { throw Failure.invalid }
        var layout = SatchelGuestDispatchLayout()
        guard satchel_guest_dispatch_build_stubs(nil, 0, &layout) != 0 else { throw Failure.invalid }
        var mapped = UInt64(metadata.main_thread_stack_size) + 8192 + UInt64(layout.region_size) + UInt64(metadata.system_resource_size)
        guard mapped <= UInt64(SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT) else { throw Failure.invalid }
        var infos: [SatchelNSOInfo] = []
        for module in loaded.modules {
            var info = SatchelNSOInfo()
            let valid = module.data.withUnsafeBytes { satchel_nso_inspect($0.bindMemory(to: UInt8.self).baseAddress, $0.count, &info, &error, error.count) }
            let rounded = (UInt64(info.image_size) + 4095) & ~UInt64(4095)
            guard valid, info.flags & ~UInt32(63) == 0, rounded <= UInt64(SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT) - mapped else { throw Failure.invalid }
            mapped += rounded; infos.append(info)
        }
        let control = try SwitchExecutableReader.controlMetadata(url, keys: keys, titleID: title, cancelled: { cancelled(raw) != 0 })
        guard control.applicationID == metadata.program_id else { throw Failure.invalid }
        var modules: [Module] = []
        for (module, info) in zip(loaded.modules, infos) {
            var image = Data(count: Int(info.image_size))
            let valid = module.data.withUnsafeBytes { source in image.withUnsafeMutableBytes { dest in
                satchel_nso_decode(source.bindMemory(to: UInt8.self).baseAddress, source.count, dest.bindMemory(to: UInt8.self).baseAddress, dest.count, &error, error.count)
            } }
            guard valid, cancelled(raw) == 0 else { throw Failure.invalid }
            modules.append(Module(name: module.name, info: info, image: image, header: Data(module.data.prefix(256))))
        }
        try SwitchExecutableReader.withRomFS(url, keys: keys, titleID: title, cancelled: { cancelled(raw) != 0 }) { romFS in
            let provider = SwitchStorageProvider(romFS)
            try withExtendedLifetime(provider) { try execute(modules, metadata: metadata, control: control, provider: provider, token: token, seconds: seconds, nvn: nvn) }
        }
    }
    private static func execute(_ modules: [Module], metadata: SatchelProcessMetadata, control: SwitchControlMetadata, provider: SwitchStorageProvider, token: Deadline, seconds: Int, nvn: Bool) throws {
        var metadata = metadata, error = [CChar](repeating: 0, count: 256)
        let storage = modules.map { $0.image as NSData }, names = modules.map { strdup($0.name) }
        defer { names.forEach { free($0) } }
        guard names.allSatisfy({ $0 != nil }) else { throw Failure.invalid }
        let inputs = modules.indices.map { SatchelSwitchProcessModule(name: UnsafePointer(names[$0]!), info: modules[$0].info, image: storage[$0].bytes.assumingMemoryBound(to: UInt8.self), image_size: storage[$0].length) }
        var layout = SatchelGuestDispatchLayout()
        guard satchel_guest_dispatch_build_stubs(nil, 0, &layout) != 0 else { throw Failure.invalid }
        var bytes = Data(count: Int(layout.region_size))
        guard bytes.withUnsafeMutableBytes({ satchel_guest_dispatch_build_stubs($0.baseAddress, $0.count, &layout) }) != 0 else { throw Failure.invalid }
        let process = withExtendedLifetime(storage) { bytes.withUnsafeBytes { b in
            var config = SatchelSwitchDispatchCode(bytes: b.bindMemory(to: UInt8.self).baseAddress, size: b.count)
            return satchel_switch_process_create_with_dispatch(inputs, inputs.count, &metadata, &config, &error, error.count)
        } }
        guard let process else { throw Failure.invalid }
        var processDestroyed = false
        #if HOST_NVN
        var hostNVN: OpaquePointer?
        #endif
        defer {
            #if HOST_NVN
            token.nvn = nil
            if let owner = hostNVN { _ = satchel_host_nvn_handoff(owner) }
            #endif
            if !processDestroyed { satchel_switch_process_destroy(process) }
            #if HOST_NVN
            if hostNVN != nil { var ignored = SatchelHostNVNReport(); _ = satchel_host_nvn_finish(&hostNVN, 5000, &ignored) }
            #endif
        }
        var ac = SatchelSwitchApplicationControl()
        ac.application_id = control.applicationID; ac.add_on_content_base_id = control.addOnContentBaseID
        ac.application_version = control.applicationVersion; ac.parental_control_flags = control.parentalControlFlags
        guard control.ratingAges.count == MemoryLayout.size(ofValue: ac.rating_ages) else { throw Failure.invalid }
        control.ratingAges.withUnsafeBytes { s in withUnsafeMutableBytes(of: &ac.rating_ages) { $0.copyMemory(from: s) } }
        guard satchel_switch_process_attach_application_control(process, &ac) != 0 else { throw Failure.invalid }
        let sample = try SwitchClockSampler.capture(frequency: UInt64(SATCHEL_CPU_SYSTEM_TICK_FREQUENCY), readTick: {
            var tick: UInt64 = 0
            guard satchel_switch_process_get_system_tick(process, &tick) != 0 else { throw Failure.invalid }
            return tick
        })
        var seed = try sample.seed(), descriptor = provider.descriptor
        guard satchel_switch_process_attach_time_seed(process, &seed) != 0, satchel_switch_process_attach_storage(process, &descriptor) != 0,
              satchel_switch_process_begin_foreground_launch(process) != 0, let cpu = satchel_switch_process_cpu(process) else { throw Failure.invalid }
        let symbols = Symbols(cpu)
        var points: [UInt64] = []
        for (i, module) in modules.enumerated() {
            var table = SatchelGuestSymbolTable(), symbol = SatchelGuestSymbol()
            guard module.header.withUnsafeBytes({ satchel_guest_symbols_nso_table($0.baseAddress, $0.count, satchel_switch_process_module_address(process, i), &table) }) == SATCHEL_GUEST_SYMBOL_FOUND else { throw Failure.invalid }
            let status = withExtendedLifetime(symbols) { satchel_guest_symbols_find_function(&table, readSymbol, nil, Unmanaged.passUnretained(symbols).toOpaque(), "nvnBootstrapLoader", 18, &symbol) }
            if status == SATCHEL_GUEST_SYMBOL_FOUND { points.append(symbol.address) }
            else if status != SATCHEL_GUEST_SYMBOL_UNDEFINED && status != SATCHEL_GUEST_SYMBOL_NOT_FOUND { throw Failure.invalid }
        }
        guard points.count == 1 else { throw Failure.invalid }
        points.append(satchel_switch_process_dispatch_address(process))
        guard satchel_cpu_probe_set_breakpoints(cpu, points, points.count) != 0 else { throw Failure.invalid }
        let executionStart = ProcessInfo.processInfo.systemUptime
        token.end = executionStart + Double(seconds)
        #if HOST_NVN
        if nvn {
            hostNVN = satchel_host_nvn_create(points[0], points[1], satchel_host_nvn_now_ns() + UInt64(seconds) * 1_000_000_000, 30_000, 100_000)
            guard hostNVN != nil else { throw Failure.invalid }
            token.nvn = hostNVN
        }
        #endif
        var steps: UInt64 = 0, result = SatchelSwitchRunResult(), calls = 0
        repeat {
            guard calls < 100_000 else { throw Failure.invalid }; calls += 1
            result = satchel_switch_process_run_scheduled(process, 100_000_000 - steps, cancelled, Unmanaged.passUnretained(token).toOpaque())
            guard result.cpu.steps <= 100_000_000 - steps else { throw Failure.invalid }; steps += result.cpu.steps
            #if HOST_NVN
            if nvn && result.state == SATCHEL_SWITCH_RUN_CPU_STOP && result.cpu.reason == SATCHEL_CPU_BREAKPOINT && steps < 100_000_000 {
                // Borrow only the CPU that produced this fresh scheduler stop.
                guard let current = satchel_switch_process_cpu(process), let owner = hostNVN else { throw Failure.invalid }
                let status = satchel_host_nvn_handle(owner, current, &result.cpu, cancelled, Unmanaged.passUnretained(token).toOpaque())
                if status == SATCHEL_GUEST_DISPATCH_RETURNED { continue }
                break
            }
            #endif
            if result.state != SATCHEL_SWITCH_RUN_BLOCKED || steps == 100_000_000 || result.next_deadline_tick == .max || cancelled(Unmanaged.passUnretained(token).toOpaque()) != 0 { break }
            var now: UInt64 = 0
            guard satchel_switch_process_get_system_tick(process, &now) != 0 else { throw Failure.invalid }
            let wait = result.next_deadline_tick > now ? Double(result.next_deadline_tick - now) / Double(SATCHEL_CPU_SYSTEM_TICK_FREQUENCY) : 0
            if wait > 0 { Thread.sleep(forTimeInterval: min(0.01, wait)) }
        } while steps < 100_000_000
        let active = modules.indices.first { i in
            let base = satchel_switch_process_module_address(process, i), size = (UInt64(modules[i].image.count) + 4095) & ~UInt64(4095)
            return result.cpu.pc >= base && result.cpu.pc - base < size
        }
        // Deliberate safe whitelist: no raw instruction, operands, logs, entropy,
        // time seed, addresses of data, decoded content or keys are serialized.
        var report: [String: Any] = ["hostOnly": true, "graphicsImplemented": false, "stopsBeforeNVN": !nvn,
            "executionTimeoutSeconds": seconds, "executionElapsedMilliseconds": UInt64((ProcessInfo.processInfo.systemUptime - executionStart) * 1000),
            "instructions": steps, "cpuReason": result.cpu.reason.rawValue, "schedulerState": result.state.rawValue,
            "ipcRequests": satchel_switch_process_ipc_requests(process), "ipcServiced": satchel_switch_process_ipc_serviced(process),
            "storageReads": satchel_switch_process_storage_reads(process), "threads": satchel_switch_process_thread_count(process),
            "permissionsServiced": satchel_switch_process_permission_serviced(process), "permissionChangedBytes": satchel_switch_process_permission_changed_bytes(process),
            "attributesServiced": satchel_switch_process_attribute_serviced(process), "permissionLockedBytes": satchel_switch_process_permission_locked_bytes(process)]
        #if HOST_CPU_DEVELOPMENT
        report["cpuDevelopment"] = true
        #endif
        if result.state == SATCHEL_SWITCH_RUN_CPU_STOP {
            report["pc"] = String(format: "0x%llx", result.cpu.pc)
            if let active { report["activeModule"] = modules[active].name; report["activeModuleOffset"] = String(format: "0x%llx", result.cpu.pc - satchel_switch_process_module_address(process, active)) }
            if result.cpu.reason == SATCHEL_CPU_SVC { report["svc"] = result.cpu.svc }
        }
        #if HOST_NVN
        if nvn {
            token.nvn = nil
            guard let owner = hostNVN, satchel_host_nvn_handoff(owner) != 0 else { throw Failure.invalid }
            satchel_switch_process_destroy(process); processDestroyed = true
            var r = SatchelHostNVNReport()
            guard satchel_host_nvn_finish(&hostNVN, 5000, &r) != 0 else { throw Failure.invalid }
            report["nvnEnabled"] = true
            report["nvnReached"] = r.reached != 0; report["nvnDispatcherCreated"] = r.created != 0
            report["nvnWindowMilliseconds"] = r.window_ms; report["nvnEvents"] = r.events
            report["nvnReturnedCalls"] = r.returned_calls; report["nvnStopStatus"] = r.status
            report["nvnOperationID"] = r.operation_id; report["nvnElapsedMilliseconds"] = r.elapsed_ms
            report["nvnCleanupStatus"] = r.cleanup_status; report["nvnRetiringContexts"] = r.retiring_contexts
            report["nvnPendingDevices"] = r.pending_devices; report["nvnActiveContexts"] = r.active_contexts
            report["nvnBackendFailures"] = r.backend_failures; report["nvnLiveDevices"] = r.live_devices
            report["nvnLiveBuffers"] = r.live_buffers; report["nvnLiveSubmissions"] = r.live_submissions
            report["nvnLiveTargets"] = r.live_targets; report["nvnLivePipelines"] = r.live_pipelines
            report["nvnLiveComputePipelines"] = r.live_compute_pipelines; report["nvnLiveRasterRuns"] = r.live_raster_runs
            report["nvnRasterHostBytes"] = r.raster_host_bytes
            report["nvnBufferBytes"] = r.buffer_bytes; report["nvnTargetBytes"] = r.target_bytes
        }
        #endif
        let data = try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
        FileHandle.standardOutput.write(data); FileHandle.standardOutput.write(Data([10]))
    }
}
