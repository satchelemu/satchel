import Foundation

/// When each game was last played and for how long in all, by title ID. Kept in
/// UserDefaults on this iPhone; a game's entry goes when the whole game is deleted.
enum PlayHistory {
    struct Entry: Codable, Equatable {
        var lastPlayed: Date
        var seconds: Double
    }
    static let key = "SatchelPlayHistory"
    static let changed = Notification.Name("SatchelPlayHistoryChanged")
    /// The game on screen and when it got there. Main thread only.
    private static var started: (title: String, date: Date)?

    static func entry(title: String) -> Entry? { all()[title.uppercased()] }
    static func entry(_ applicationID: UInt64) -> Entry? { entry(title: String(format: "%016llX", applicationID)) }

    static func all(_ defaults: UserDefaults = .standard) -> [String: Entry] {
        guard let data = defaults.data(forKey: key) else { return [:] }
        return (try? JSONDecoder().decode([String: Entry].self, from: data)) ?? [:]
    }

    /// The game is on screen and running: it was played now.
    static func begin(title: String, now: Date = Date()) {
        guard title.count == 16 else { return }
        end(now: now)
        started = (title.uppercased(), now)
        update(title) { $0.lastPlayed = now }
    }

    /// The game left the screen (paused to the library, quit, or the app went to the background).
    /// Spans over a day are not counted: the clock moved or the app was suspended mid-game.
    static func end(now: Date = Date()) {
        guard let started else { return }
        self.started = nil
        let seconds = now.timeIntervalSince(started.date)
        guard seconds > 0, seconds < 24 * 3600 else { return }
        update(started.title) { $0.seconds += seconds; $0.lastPlayed = now }
    }

    static func forget(title: String) {
        if started?.title == title.uppercased() { started = nil }
        var entries = all()
        guard entries.removeValue(forKey: title.uppercased()) != nil else { return }
        save(entries)
    }

    private static func update(_ title: String, _ change: (inout Entry) -> Void) {
        var entries = all()
        var entry = entries[title.uppercased()] ?? Entry(lastPlayed: Date(), seconds: 0)
        change(&entry)
        entries[title.uppercased()] = entry
        save(entries)
    }

    private static func save(_ entries: [String: Entry]) {
        if let data = try? JSONEncoder().encode(entries) { UserDefaults.standard.set(data, forKey: key) }
        NotificationCenter.default.post(name: changed, object: nil)
    }

    /// "3 hr, 12 min played" style text, or nil before the first full minute.
    static func playedText(_ entry: Entry?) -> String? {
        guard let entry, entry.seconds >= 60 else { return nil }
        let formatter = DateComponentsFormatter()
        formatter.allowedUnits = entry.seconds >= 3600 ? [.hour, .minute] : [.minute]
        formatter.unitsStyle = .short
        return formatter.string(from: entry.seconds).map { $0 + " played" }
    }

    /// "Played today", "Played yesterday" or "Played 3 days ago".
    static func lastPlayedText(_ entry: Entry?, now: Date = Date()) -> String? {
        guard let entry else { return nil }
        if Calendar.current.isDate(entry.lastPlayed, inSameDayAs: now) { return "Played today" }
        if let yesterday = Calendar.current.date(byAdding: .day, value: -1, to: now),
           Calendar.current.isDate(entry.lastPlayed, inSameDayAs: yesterday) { return "Played yesterday" }
        let formatter = RelativeDateTimeFormatter()
        formatter.unitsStyle = .full
        return "Played " + formatter.localizedString(for: entry.lastPlayed, relativeTo: now)
    }
}
