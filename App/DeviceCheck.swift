import Foundation
import Darwin

struct DeviceCheck {
    struct Row: Identifiable {
        let id: String
        let title: String
        let value: String
    }

    let rows: [Row]
    let team: String?
    let sampledAt: Date

    static let measurementNote = "Memory values are a snapshot, not an allocation guarantee. The live process limit shows the current allowance; an increase cannot be attributed to the entitlement without a baseline on the same device. The VM probe reserves and immediately releases untouched address space, capped at 64 GiB. It reports a contiguous lower bound, not total free VM or usable RAM."

    func report(includeTeam: Bool) -> String {
        var lines = ["Satchel Device check", "Sampled: \(sampledAt.ISO8601Format())"]
        lines += rows.map { "\($0.title): \($0.value)" }
        if includeTeam { lines.append("Signing team: \(team ?? "Unknown")") }
        lines.append(Self.measurementNote)
        return lines.joined(separator: "\n")
    }

    static func entitlement(_ value: Int32) -> String {
        switch value {
        case 1: return "Present in running signature"
        case 0: return "Missing or disabled in running signature"
        default: return "Unknown (signature query unavailable)"
        }
    }

    static func bytes(_ value: UInt64) -> String {
        String(format: "%.2f GiB (%llu bytes)", Double(value) / 1_073_741_824, value)
    }

    static func read() -> DeviceCheck {
#if PREVIEW_GAME
        if let fixture = DeviceCheckFixture.snapshot { return fixture }
#endif
        let measurements = SatchelMeasureDevice()
        var system = utsname()
        let model: String
        if uname(&system) == 0 {
            let capacity = MemoryLayout.size(ofValue: system.machine)
            model = withUnsafePointer(to: &system.machine) {
                $0.withMemoryRebound(to: CChar.self, capacity: capacity) { String(cString: $0) }
            }
        } else { model = "Unknown" }
        let version = ProcessInfo.processInfo.operatingSystemVersion
        let container = URL(fileURLWithPath: NSHomeDirectory(), isDirectory: true)
        let free = (try? container.resourceValues(forKeys: [.volumeAvailableCapacityKey]))?.volumeAvailableCapacity
        var expiry: Date?
        var team: String?
#if !SATCHEL_UI_PREVIEW_MINIMAL && !PREVIEW_GAME
        expiry = Bundle.main.url(forResource: "embedded", withExtension: "mobileprovision")
            .flatMap { try? SignatureExpiry.boundedData($0) }
            .flatMap { SatchelSignatureMetadata.profileContent($0) }
            .flatMap(SignatureExpiry.profileExpiry)
#if !targetEnvironment(simulator)
        team = SatchelSigningIdentity.applicationTeam
#endif
#endif
#if targetEnvironment(simulator)
        let environment = "Simulator (host limits, not phone capabilities)"
#else
        let environment = "Device"
#endif
        return DeviceCheck(rows: [
            Row(id: "environment", title: "Environment", value: environment),
            Row(id: "model", title: "Device model", value: model),
            Row(id: "os", title: "iOS version", value: "\(version.majorVersion).\(version.minorVersion).\(version.patchVersion)"),
            Row(id: "memory-entitlement", title: "Increased memory limit", value: entitlement(measurements.increased_memory)),
            Row(id: "address-entitlement", title: "Extended virtual addressing", value: entitlement(measurements.extended_addressing)),
            Row(id: "available", title: "os_proc_available_memory", value: bytes(measurements.available_memory)),
            Row(id: "limit", title: "Current process memory limit", value: measurements.has_limit ? bytes(measurements.process_limit) : "Unknown (no positive remaining limit reported)"),
            Row(id: "footprint", title: "Process footprint", value: measurements.has_footprint ? bytes(measurements.footprint) : "Unknown"),
            Row(id: "address-span", title: "Process virtual address span", value: measurements.has_address_span ? bytes(measurements.address_span) : "Unknown"),
            Row(id: "reservation", title: "Contiguous VM reserve tested", value: measurements.reserved_bytes > 0 ? "At least " + bytes(measurements.reserved_bytes) : "No reservation of 1 GiB or more succeeded"),
            Row(id: "expiry", title: "Signature expiry (embedded profile)", value: expiry?.ISO8601Format() ?? "Unknown (profile absent or unreadable)"),
            Row(id: "free", title: "Container volume free space", value: free.map { bytes(UInt64(max(0, $0))) } ?? "Unknown")
        ], team: team, sampledAt: Date())
    }
}
