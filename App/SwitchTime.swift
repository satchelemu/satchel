import Darwin
import Foundation

struct SwitchClockSample {
    let unixSeconds: Int64
    let unixNanoseconds: UInt32
    let systemTick: UInt64
    let locationName: String

    func seed() throws -> SatchelSwitchTimeSeed {
        var value = SatchelSwitchTimeSeed()
        guard MemoryLayout<SatchelSwitchTimeSeed>.size == 64 else { throw SwitchClockSamplingError.inconsistent }
        value.version = UInt32(SATCHEL_SWITCH_TIME_SEED_VERSION)
        value.size = UInt32(MemoryLayout<SatchelSwitchTimeSeed>.size)
        value.unix_seconds = unixSeconds
        value.unix_nanoseconds = unixNanoseconds
        value.system_tick = systemTick
        let location = Array(locationName.utf8) + [UInt8(0)]
        guard location.count <= MemoryLayout.size(ofValue: value.location_name) else {
            throw SwitchClockSamplingError.unsupportedLocation
        }
        withUnsafeMutableBytes(of: &value.location_name) { $0.copyBytes(from: location) }
        return value
    }
}

enum SwitchClockSamplingError: Error, LocalizedError {
    case unavailable, inconsistent, unsupportedLocation

    var errorDescription: String? {
        switch self {
        case .unavailable: return "The device clock could not be read."
        case .inconsistent: return "The device and emulated clocks could not be sampled consistently."
        case .unsupportedLocation: return "The device time zone cannot be represented by this runtime."
        }
    }
}

enum SwitchClockSampler {
    static func realTime() throws -> (seconds: Int64, nanoseconds: Int64) {
        var value = timespec()
        guard clock_gettime(CLOCK_REALTIME, &value) == 0,
              let seconds = Int64(exactly: value.tv_sec),
              let nanoseconds = Int64(exactly: value.tv_nsec) else {
            throw SwitchClockSamplingError.unavailable
        }
        return (seconds, nanoseconds)
    }

    // Capture once before execution. This establishes local wall time only;
    // it does not assert network synchronization or install timezone rules.
    static func capture(frequency: UInt64, readTick: () throws -> UInt64,
                        readWallTime: () throws -> (seconds: Int64, nanoseconds: Int64) = realTime,
                        locationName: String = TimeZone.current.identifier) throws -> SwitchClockSample {
        let name = Array(locationName.utf8)
        guard !name.isEmpty, name.count < 36,
              name.allSatisfy({ (65...90).contains($0) || (97...122).contains($0) ||
                  (48...57).contains($0) || [43, 45, 46, 47, 95].contains($0) }),
              TimeZone(identifier: locationName) != nil else {
            throw SwitchClockSamplingError.unsupportedLocation
        }
        guard frequency >= 10 else { throw SwitchClockSamplingError.inconsistent }
        for _ in 0..<3 {
            let before = try readTick()
            let wall = try readWallTime()
            let after = try readTick()
            guard after >= before, (0..<1_000_000_000).contains(wall.nanoseconds) else {
                throw SwitchClockSamplingError.inconsistent
            }
            // A stalled host scheduling interval must not become an inaccurate
            // clock seed. Retry a bounded number of times, never substitute time.
            let interval = after - before
            if interval > frequency / 10 { continue }
            return SwitchClockSample(unixSeconds: wall.seconds,
                unixNanoseconds: UInt32(wall.nanoseconds),
                systemTick: before + interval / 2, locationName: locationName)
        }
        throw SwitchClockSamplingError.inconsistent
    }
}
