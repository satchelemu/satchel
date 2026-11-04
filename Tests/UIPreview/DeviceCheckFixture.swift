import SwiftUI
import UIKit

enum DeviceCheckFixture {
    static var mode: String { UserDefaults.standard.string(forKey: "SatchelPreviewDeviceCheck") ?? "" }

    static var snapshot: DeviceCheck? {
        guard ["present", "missing", "unknown"].contains(mode) else { return nil }
        let entitlement: Int32 = mode == "missing" ? 0 : mode == "unknown" ? -1 : 1
        let gib: UInt64 = 1 << 30
        return DeviceCheck(rows: [
            .init(id: "environment", title: "Environment", value: "Synthetic device check fixture"),
            .init(id: "model", title: "Device model", value: "Synthetic phone"),
            .init(id: "os", title: "iOS version", value: "27.1.0 (synthetic)"),
            .init(id: "memory-entitlement", title: "Increased memory limit", value: DeviceCheck.entitlement(entitlement)),
            .init(id: "address-entitlement", title: "Extended virtual addressing", value: DeviceCheck.entitlement(entitlement)),
            .init(id: "available", title: "os_proc_available_memory", value: DeviceCheck.bytes(5 * gib)),
            .init(id: "limit", title: "Current process memory limit", value: DeviceCheck.bytes(6 * gib)),
            .init(id: "footprint", title: "Process footprint", value: DeviceCheck.bytes(gib)),
            .init(id: "address-span", title: "Process virtual address span", value: DeviceCheck.bytes(128 * gib)),
            .init(id: "reservation", title: "Contiguous VM reserve tested", value: "At least " + DeviceCheck.bytes(64 * gib)),
            .init(id: "expiry", title: "Signature expiry (embedded profile)", value: "2030-01-01T00:00:00Z"),
            .init(id: "free", title: "Container volume free space", value: DeviceCheck.bytes(40 * gib))
        ], team: "SYNTHETIC-TEAM", sampledAt: Date(timeIntervalSince1970: 1_800_000_000))
    }
}

/// Read the actual app clipboard after the production Copy action. Never compiled
/// into the phone target and only exposed with an explicit synthetic fixture.
struct DeviceCheckClipboardFixture: View {
    @State private var report = ""
    var body: some View {
        if DeviceCheckFixture.snapshot != nil {
            Button("Inspect synthetic clipboard") { report = UIPasteboard.general.string ?? "Clipboard is empty" }
                .accessibilityIdentifier("device-check-inspect-clipboard")
            Text(report).font(.system(size: 10)).accessibilityIdentifier("device-check-clipboard")
        }
    }
}
