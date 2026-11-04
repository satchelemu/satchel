import Foundation

@main struct Test {
    static var checks = 0
    static func check(_ condition: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        if !condition() { fatalError("FAIL: \(message)") }
    }
    static func bytes<T>(_ value: T) -> Data {
        var copy = value
        return withUnsafeBytes(of: &copy) { Data($0) }
    }
    static func main() throws {
        var entries = SatchelGuestVIEntries(), borrow = SatchelGuestVIBorrow(), stop = SatchelCPUProbeResult()
        guard let cpu = vi_report_fixture_create(&entries, &borrow, &stop) else { fatalError("FAIL: original stopped CPU fixture creation") }
        defer { satchel_cpu_probe_destroy(cpu) }
        check(stop.reason == SATCHEL_CPU_BREAKPOINT && stop.steps == 0, "fixture is an actual CPU breakpoint")
        check(entries.version == 1 && entries.present_mask == 7, "three valid original entry definitions")
        let entriesBefore = bytes(entries), borrowBefore = bytes(borrow), stopBefore = bytes(stop)
        var direct = SatchelGuestVIResult(status: 0x12345678, operation: 0x87654321, events: 0x123456789abcdef0)
        let directBefore = bytes(direct)
        let cStatus = satchel_guest_vi_dispatch(&entries, nil, &borrow, &stop, &direct)
        check(cStatus == SATCHEL_GVI_INVALID, "actual C nil-owner path returns INVALID")
        check(bytes(direct) == directBefore, "actual C early refusal preserves all output sentinel bytes")
        check(vi_report_fixture_unchanged(cpu) == 1, "actual C refusal preserves CPU PC and arguments")
        let wrapped = runtimeVIDispatch(entries: &entries, owner: nil, borrow: &borrow, stop: &stop)
        check(wrapped.status == UInt32(SATCHEL_GVI_INVALID.rawValue), "Swift uses returned INVALID instead of zero-initialized OK")
        check(wrapped.status != UInt32(SATCHEL_GVI_OK.rawValue), "early refusal cannot be reported as success")
        check(wrapped.operation == 0 && wrapped.events == 0, "early refusal does not invent operation or events")
        check(runtimeVIOperationName(wrapped.operation) == nil, "unknown early-refusal operation is nil")
        check(bytes(entries) == entriesBefore && bytes(borrow) == borrowBefore && bytes(stop) == stopBefore, "Swift/C call preserves borrowed inputs")
        check(vi_report_fixture_unchanged(cpu) == 1, "Swift wrapper preserves actual stopped CPU state")
        var absent = SatchelGuestVIBorrow()
        let absentResult = runtimeVIDispatch(entries: &entries, owner: nil, borrow: &absent, stop: &stop)
        check(absentResult.status == UInt32(SATCHEL_GVI_INVALID.rawValue) && absentResult.operation == 0, "nil CPU preflight is also reported invalid")
        for op in [UInt32(0), 8, UInt32.max] { check(runtimeVIOperationName(op) == nil, "unknown operation stays unclassified") }
        let names = ["OpenDefaultDisplay", "CreateLayer", "GetNativeWindow", "DestroyLayer", "CloseDisplay", "CreateLayer (sized)", "CreateLayer (settings)"]
        for (index, name) in names.enumerated() { check(runtimeVIOperationName(UInt32(index + 1)) == name, "known operation label is exact") }
        check(runtimeVIClaim(nil, cpu, nil) == 0, "nil borrowed claim refuses")
        check(runtimeVIValidate(nil, cpu, nil) == 0, "nil borrowed validator refuses")
        check(runtimeVIOutputGuard(nil, cpu, 0, 8) == 0, "nil actual output guard refuses")

        let legacyData = try Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1]))
        var object = try JSONSerialization.jsonObject(with: legacyData) as! [String: Any]
        check(object["vi"] == nil, "public legacy report has no VI field")
        let legacy = try JSONDecoder().decode(SwitchRuntimeReport.self, from: legacyData)
        check(legacy.vi == nil, "actual synthesized report decoder accepts legacy absence")
        check(legacy.instructions == (object["instructions"] as! NSNumber).uint64Value, "legacy instruction counter retained")
        check(legacy.ipcServiced == (object["ipcServiced"] as! NSNumber).uint64Value, "legacy serviced counter retained")
        check(legacy.modules.count == (object["modules"] as! [[String: Any]]).count, "legacy module count retained")
        check(legacy.capturedVulkan != nil && legacy.provokingVulkan != nil && legacy.textureFrameVulkan != nil, "three existing graphics reports decode with actual source")
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        let legacyRoundTrip = try encoder.encode(legacy)
        let roundObject = try JSONSerialization.jsonObject(with: legacyRoundTrip) as! [String: Any]
        check(roundObject["vi"] == nil, "nil VI is not invented during legacy encoding")
        let legacyAgain = try JSONDecoder().decode(SwitchRuntimeReport.self, from: legacyRoundTrip)
        check(legacyAgain.instructions == legacy.instructions && legacyAgain.vi == nil, "legacy roundtrip preserves counters and absent VI")
        object["vi"] = ["entryCount": 3, "surfaceAttachedAtStart": true, "ownerCreated": false,
            "attempts": 1, "returnedCalls": 0, "liveDisplays": 0, "liveLayers": 0,
            "liveNativeWindows": 0, "retainedWindows": 0, "lastStatus": Int(SATCHEL_GVI_INVALID.rawValue)] as [String: Any]
        let modern = try JSONDecoder().decode(SwitchRuntimeReport.self, from: JSONSerialization.data(withJSONObject: object))
        guard let vi = modern.vi else { fatalError("FAIL: modern VI field missing") }
        check(vi.entryCount == 3 && vi.surfaceAttachedAtStart && !vi.ownerCreated, "modern attachment and ownership facts are distinct")
        check(vi.attempts == 1 && vi.returnedCalls == 0 && vi.lastStatus == UInt32(SATCHEL_GVI_INVALID.rawValue), "modern report records actual refusal status and no returned call")
        check(vi.lastOperation == nil, "unknown operation remains absent in modern report")
        check(vi.liveDisplays == 0 && vi.liveLayers == 0 && vi.liveNativeWindows == 0 && vi.retainedWindows == 0, "refusal invents no owned resources")
        let modernBytes = try encoder.encode(modern)
        let modernAgain = try JSONDecoder().decode(SwitchRuntimeReport.self, from: modernBytes)
        let modernAgainBytes = try encoder.encode(modernAgain)
        check(modernAgainBytes == modernBytes, "actual full modern report is stable across Codable roundtrip")
        check(modernAgain.instructions == legacy.instructions && modernAgain.scheduler.state == legacy.scheduler.state, "modern addition preserves legacy runtime facts")
        let viObject = (try JSONSerialization.jsonObject(with: modernBytes) as! [String: Any])["vi"] as! [String: Any]
        let expectedKeys: Set<String> = ["entryCount", "surfaceAttachedAtStart", "ownerCreated", "attempts", "returnedCalls", "liveDisplays", "liveLayers", "liveNativeWindows", "retainedWindows", "lastStatus"]
        check(Set(viObject.keys) == expectedKeys, "serialized VI contains only typed safe counters/status, no identity addresses or pointers")
        let large = SwitchVIReport(entryCount: 7, surfaceAttachedAtStart: false, ownerCreated: true,
            attempts: UInt64.max, returnedCalls: UInt64.max - 1, liveDisplays: 1, liveLayers: 2,
            liveNativeWindows: 3, retainedWindows: 4, lastOperation: "GetNativeWindow", lastStatus: UInt32.max)
        let largeAgain = try JSONDecoder().decode(SwitchVIReport.self, from: encoder.encode(large))
        check(largeAgain.attempts == UInt64.max && largeAgain.returnedCalls == UInt64.max - 1, "VI64-bit counters roundtrip without truncation")
        check(largeAgain.lastStatus == UInt32.max && largeAgain.lastOperation == "GetNativeWindow", "VI status/name roundtrip exactly")
        object["vi"] = NSNull()
        let nullVI = try JSONDecoder().decode(SwitchRuntimeReport.self, from: JSONSerialization.data(withJSONObject: object))
        check(nullVI.vi == nil, "explicit null VI remains absent")
        check(legacy.window == nil, "actual full report accepts legacy absent Window field")
        check(roundObject["window"] == nil, "legacy encode invents no Window activity")
        var withWindow = modern
        withWindow.window = SwitchWindowReport(ownerCreated: true, attempts: 2, returnedCalls: 1,
            waitReturns: 1, liveWindows: 1, pendingCalls: 1, retainedSnapshots: 0, leasedSnapshots: 0,
            acquires: 1, presents: 0, snapshotsCompleted: 0, lastOperation: "nvnQueueAcquireTexture",
            lastStatus: UInt32(SATCHEL_GUEST_DISPATCH_WINDOW_WAIT.rawValue))
        let windowBytes = try encoder.encode(withWindow)
        let windowAgain = try JSONDecoder().decode(SwitchRuntimeReport.self, from: windowBytes)
        check(windowAgain.window?.waitReturns == 1 && windowAgain.window?.presents == 0,
              "full report keeps Window wait distinct from presentation")
        check(windowAgain.window?.copiedFrames == 0 && windowAgain.window?.pendingLeaseReleases == 0,
              "full report retains zero copied-frame and release counts")
        let windowAgainBytes = try encoder.encode(windowAgain)
        check(windowAgainBytes == windowBytes, "full Window report roundtrip is stable")
        object["window"] = NSNull()
        let nullWindow = try JSONDecoder().decode(SwitchRuntimeReport.self, from: JSONSerialization.data(withJSONObject: object))
        check(nullWindow.window == nil, "explicit null Window remains absent")
        print("\(checks) VI Swift report and actual C early-refusal assertions passed.")
    }
}
