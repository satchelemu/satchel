import Darwin
import Foundation

@main struct ClockSamplingTests {
    static func main() throws {
        var checks = 0
        func check(_ value: Bool) {
            checks += 1
            precondition(value, "Clock sampling assertion failed")
        }
        func rejects(_ expected: SwitchClockSamplingError, _ action: () throws -> Void) {
            do { try action(); preconditionFailure("Expected clock sampling failure") }
            catch let error as SwitchClockSamplingError {
                check(String(describing: error) == String(describing: expected))
            } catch { preconditionFailure("Unexpected error") }
        }
        func sample(_ ticks: [UInt64], nanos: Int64 = 123_456_789,
                    seconds: Int64 = 1_793_480_000) throws -> SwitchClockSample {
            var index = 0
            return try SwitchClockSampler.capture(frequency: 19_200_000,
                readTick: { defer { index += 1 }; return ticks[index] },
                readWallTime: { (seconds, nanos) }, locationName: "America/New_York")
        }
        let first = try sample([10, 15])
        check(first.systemTick == 12)
        check(first.unixSeconds == 1_793_480_000)
        check(first.unixNanoseconds == 123_456_789)
        check(first.locationName == "America/New_York")
        let seed = try first.seed()
        check(seed.version == 1 && seed.size == 64)
        check(seed.unix_seconds == first.unixSeconds && seed.unix_nanoseconds == first.unixNanoseconds)
        check(seed.system_tick == first.systemTick)
        var locationField = seed.location_name
        let packedName = withUnsafeBytes(of: &locationField) { Array($0) }
        let terminatedName = Array("America/New_York".utf8) + [UInt8(0)]
        check(Array(packedName.prefix(terminatedName.count)) == terminatedName)
        check(packedName.dropFirst(terminatedName.count).allSatisfy { $0 == 0 })

        check(try sample([UInt64.max - 1, UInt64.max]).systemTick == UInt64.max - 1)
        check(try sample([5, 5], nanos: 0, seconds: -1).unixSeconds == -1)
        check(try sample([5, 5], nanos: 999_999_999).unixNanoseconds == 999_999_999)
        check(try sample([0, 1_920_000]).systemTick == 960_000)
        check(try sample([0, 1_920_001, 2_000_000, 2_000_010]).systemTick == 2_000_005)
        rejects(.inconsistent) { _ = try sample([0, 2_000_000, 3_000_000, 5_000_000, 6_000_000, 8_000_000]) }
        rejects(.inconsistent) { _ = try sample([10, 9]) }
        rejects(.inconsistent) { _ = try sample([0, 1], nanos: -1) }
        rejects(.inconsistent) { _ = try sample([0, 1], nanos: 1_000_000_000) }
        for name in ["", "Not/AZone", "America/New_York\0", "America/\nNew_York", "é", String(repeating: "A", count: 36)] {
            rejects(.unsupportedLocation) {
                _ = try SwitchClockSampler.capture(frequency: 19_200_000,
                    readTick: { preconditionFailure("Invalid name reached clock") }, locationName: name)
            }
        }
        rejects(.unavailable) {
            _ = try SwitchClockSampler.capture(frequency: 19_200_000,
                readTick: { throw SwitchClockSamplingError.unavailable }, locationName: "UTC")
        }
        rejects(.unavailable) {
            _ = try SwitchClockSampler.capture(frequency: 19_200_000,
                readTick: { 0 }, readWallTime: { throw SwitchClockSamplingError.unavailable }, locationName: "UTC")
        }
        rejects(.inconsistent) {
            _ = try SwitchClockSampler.capture(frequency: 0,
                readTick: { preconditionFailure("Invalid frequency reached clock") }, locationName: "UTC")
        }
        let lower = Date().timeIntervalSince1970
        let live = try SwitchClockSampler.capture(frequency: 1_000_000_000, readTick: {
            var time = timespec()
            guard clock_gettime(CLOCK_MONOTONIC_RAW, &time) == 0 else { throw SwitchClockSamplingError.unavailable }
            return UInt64(time.tv_sec) * 1_000_000_000 + UInt64(time.tv_nsec)
        })
        let upper = Date().timeIntervalSince1970
        let wall = Double(live.unixSeconds) + Double(live.unixNanoseconds) / 1_000_000_000
        check(wall >= lower && wall <= upper)
        check(live.systemTick > 0)
        check(live.locationName == TimeZone.current.identifier)
        print("\(checks) clock sampling assertions passed")
    }
}
