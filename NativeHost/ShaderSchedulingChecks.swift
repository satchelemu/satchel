import Foundation
import Metal
import Darwin

// Original resource-free Maxwell EXIT fixture. No game inputs are required.
@main struct ShaderSchedulingChecks {
    static func main() throws {
        precondition(CommandLine.arguments.count == 2)
        let root = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        var control = Data(count: 0x880), code = Data(count: 32)
        ShaderRecord.put(0x98761234, &control, 0)
        ShaderRecord.put(0x880, &control, 0x20)
        ShaderRecord.put(32, &control, 0x700)
        ShaderRecord.put(32, &control, 0x704)
        for offset in [0x748, 0x74c, 0x750] { ShaderRecord.put(1, &control, offset) }
        ShaderRecord.put(0x0007000f, &code, 8)
        ShaderRecord.put(0xe3000000, &code, 12)
        let request = try SatchelShaderRequest(control: control, code: code, stage: .compute)
        let key = try SatchelShaderTranslator.normalized(request).key
        let a = try ShaderWarmupResources.get(directory: root.appendingPathComponent("a"), reserve: 0)
        let alias = root.appendingPathComponent("alias")
        if !FileManager.default.fileExists(atPath: alias.path) {
            try FileManager.default.createSymbolicLink(at: alias, withDestinationURL: a.directory)
        }
        let aliased = try ShaderWarmupResources.get(directory: alias, reserve: 0)
        precondition(aliased === a)
        let previousSignal = signal(SIGVTALRM, SIG_IGN)
        defer { _ = signal(SIGVTALRM, previousSignal) }
        var before = sigaction(), after = sigaction()
        precondition(sigaction(SIGVTALRM, nil, &before) == 0)
        a.prefetch(request); a.prefetch(request)
        let prepared = try a.prepare(request)
        precondition(sigaction(SIGVTALRM, nil, &after) == 0)
        precondition(unsafeBitCast(before.__sigaction_u.__sa_handler, to: UInt.self) ==
                     unsafeBitCast(after.__sigaction_u.__sa_handler, to: UInt.self))
        let oracle = try SatchelShaderTranslator.translate(request)
        precondition(prepared.sources == oracle.sources && prepared.reflectionJSON == oracle.reflectionJSON)
        let first = try a.compile(prepared, variant: "b")
        let second = try a.compile(prepared, variant: "b")
        precondition(first.library === second.library && second.liveCacheHit)
        a.endSession()
        let startup = try JSONSerialization.jsonObject(with: Data(contentsOf: a.directory.appendingPathComponent("startup.json"))) as! [[String: Any]]
        precondition(startup.count == 1 && startup[0]["request"] as? String == prepared.requestSHA256)
        let entry = try JSONSerialization.jsonObject(with: prepared.reflectionJSON) as! [String: Any]
        let function = first.library.makeFunction(name: entry["entry"] as! String)!
        let pipeline = try a.compiler.device.makeComputePipelineState(function: function)
        let commands = a.compiler.device.makeCommandQueue()!.makeCommandBuffer()!
        let encoder = commands.makeComputeCommandEncoder()!
        encoder.setComputePipelineState(pipeline)
        encoder.dispatchThreads(MTLSize(width: 1, height: 1, depth: 1), threadsPerThreadgroup: MTLSize(width: 1, height: 1, depth: 1))
        encoder.endEncoding(); commands.commit(); commands.waitUntilCompleted()
        precondition(commands.status == .completed)
        try a.mergeWanted([key: 5]); try a.mergeWanted([key: 2])
        _ = try ShaderWarmupResources.get(directory: root.appendingPathComponent("b"), reserve: 0)
        _ = try ShaderWarmupResources.get(directory: root.appendingPathComponent("c"), reserve: 0)
        let returned = try ShaderWarmupResources.get(directory: root.appendingPathComponent("a"), reserve: 0)
        precondition(returned === a)
        // Move A out of the bounded pool, then verify the persistent source/mask.
        for name in ["d", "e", "f"] { _ = try ShaderWarmupResources.get(directory: root.appendingPathComponent(name), reserve: 0) }
        let reloaded = try ShaderWarmupResources.get(directory: root.appendingPathComponent("a"), reserve: 0)
        precondition(reloaded !== a && reloaded.mask(key) == 7)
        let hit = try reloaded.store.prepareMeasured(request)
        precondition(hit.sourceCacheHit && hit.shader.sources == prepared.sources)
        reloaded.warmKnown()
        _ = try reloaded.compile(hit.shader, variant: "b")
        setenv("DM_PHONE_SHADER_CACHE", root.appendingPathComponent("a").path, 1)
        let runtime = PhoneShaderRuntime.shared
        for _ in 0..<3 {
            runtime.resetSession()
            runtime.prefetch(key: key, control: control, code: code)
            precondition(runtime.prepare(key: key, control: control, code: code, wanted: 0) == 1)
            precondition(runtime.prepare(key: key, control: control, code: code, wanted: 0) == 2)
            precondition(runtime.library(key + "_b", layer: nil) != nil)
            precondition(runtime.library(key + "_missing", layer: nil) == nil)
        }
        runtime.resetSession()
        // Bounded first use: an ahead job makes the same translation and library the draw makes itself.
        let ahead = try ShaderWarmupResources.get(directory: root.appendingPathComponent("ahead"), reserve: 0)
        ahead.useAheadLanes()
        let progress = ahead.aheadProgress()
        ahead.ahead(identity: "-/" + key, first: (key, request), vertex: nil)
        var waited = 0
        while !ahead.aheadIsReady(key) && waited < 1200 { usleep(50_000); waited += 1 }
        precondition(ahead.aheadIsReady(key) && ahead.aheadProgress() == progress &+ 1)
        ahead.ahead(identity: "-/" + key, first: (key, request), vertex: nil)   // the same program again: no second job
        usleep(200_000); precondition(ahead.aheadProgress() == progress &+ 1)
        let early = try ahead.prepareNow(request)
        precondition(early.sources == oracle.sources && early.reflectionJSON == oracle.reflectionJSON)
        let held = try ahead.compileNow(early, variant: "b")
        let again = try ahead.compileNow(early, variant: "b")
        precondition(held.library === again.library && again.liveCacheHit)
        ahead.endSession()
        precondition(!ahead.aheadIsReady(key))
        print("PASS: background translation equals serial, SIGVTALRM unchanged, library reuse, synthetic GPU dispatch, three-game pool, persistent source/mask, startup replay, three runtime resets, ahead job equals the draw's own work")
    }
}
