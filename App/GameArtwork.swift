import Foundation
import SwiftUI
import UIKit

/// Display metadata read from a package's own Control archive (NACP plus icon).
/// Cached in the app container only; never logged and never part of the repo.
struct GameInfo: Codable, Equatable {
    enum Kind: String, Codable { case application, update, addOnContent }
    let applicationID: UInt64
    let kind: Kind
    let version: UInt32
    let displayVersion: String
    let title: String
    let publisher: String
    let hasIcon: Bool
    let accent: [Double]?
    var bundlesUpdate = false
    var addOns: [SwitchAddOnInfo]?
    /// Other applications on the same cartridge, such as Castlevania's Sound & Art Gallery.
    /// They belong to this game's tile rather than getting tiles of their own.
    var extras: [Extra]? = nil

    struct Extra: Codable, Equatable {
        let titleID: UInt64
        let title: String
    }

    static func addOns(for applicationID: UInt64, in infos: [GameInfo]) -> [SwitchAddOnInfo] {
        var selected: [UInt64: SwitchAddOnInfo] = [:]
        for info in infos {
            for row in info.addOns ?? [] where row.applicationID == applicationID {
                if (selected[row.titleID]?.version ?? 0) <= row.version { selected[row.titleID] = row }
            }
        }
        return selected.values.sorted { $0.titleID < $1.titleID }
    }

    var accentColor: Color? {
        guard let accent, accent.count == 3 else { return nil }
        return Color(red: accent[0], green: accent[1], blue: accent[2])
    }
}

enum GameInfoReader {
    // NACP language slots, in table order. Icons use the same names.
    static let languages = ["AmericanEnglish", "BritishEnglish", "Japanese", "French", "German",
                            "LatinAmericanSpanish", "Spanish", "Italian", "Dutch", "CanadianFrench",
                            "Portuguese", "Russian", "Korean", "TraditionalChinese", "SimplifiedChinese",
                            "BrazilianPortuguese"]

    private static var preferredOrder: [Int] {
        let preferred = Locale.preferredLanguages.first?.lowercased() ?? "en-us"
        let first: Int
        switch preferred {
        case let code where code.hasPrefix("en-gb"): first = 1
        case let code where code.hasPrefix("ja"): first = 2
        case let code where code.hasPrefix("fr-ca"): first = 9
        case let code where code.hasPrefix("fr"): first = 3
        case let code where code.hasPrefix("de"): first = 4
        case let code where code.hasPrefix("es-419") || code.hasPrefix("es-mx"): first = 5
        case let code where code.hasPrefix("es"): first = 6
        case let code where code.hasPrefix("it"): first = 7
        case let code where code.hasPrefix("nl"): first = 8
        case let code where code.hasPrefix("pt-br"): first = 15
        case let code where code.hasPrefix("pt"): first = 10
        case let code where code.hasPrefix("ru"): first = 11
        case let code where code.hasPrefix("ko"): first = 12
        case let code where code.hasPrefix("zh-hant"): first = 13
        case let code where code.hasPrefix("zh"): first = 14
        default: first = 0
        }
        return [first] + Array(0..<16).filter { $0 != first }
    }

    /// Reads title, publisher, version and icon through the verified package
    /// reader. The Control archive is hash-checked against its CNMT first.
    static func read(_ url: URL, keys: [String: Data], cancelled: @escaping () -> Bool) throws -> (GameInfo, Data?) {
        let package = try SwitchExecutableReader.ContentPackage(url, keys: keys, cancelled: cancelled)
        defer { package.close() }
        let dlc = try SwitchAddOnContent.verifiedMetadata(package).map(SwitchAddOnInfo.init)
        if package.metadata.allSatisfy({ $0.type == 0x82 }), let first = dlc.first {
            var info = GameInfo(applicationID: first.applicationID, kind: .addOnContent,
                version: first.version, displayVersion: "", title: first.name,
                publisher: "", hasIcon: false, accent: nil)
            info.addOns = dlc
            return (info, nil)
        }
        // A cartridge can hold several applications (Castlevania's carries its Sound & Art
        // Gallery). The main game is the one with a reviewed profile, else the largest program;
        // the update read is that game's own.
        let applications = package.metadata.filter { $0.type == 0x80 }
        func programBytes(_ meta: SwitchContentMeta) -> UInt64 { meta.contents.filter { $0.type == 1 }.reduce(0) { $0 + $1.size } }
        func reviewed(_ meta: SwitchContentMeta) -> Bool { NativeGameProfile.find(titleID: meta.titleID.titleIDString) != nil }
        let application = applications.max { (reviewed($0) ? 1 : 0, programBytes($0)) < (reviewed($1) ? 1 : 0, programBytes($1)) }
        let patch = package.metadata.filter { $0.type == 0x81 && (application == nil || $0.applicationID == application?.titleID) }
            .max { $0.version < $1.version }
        // A bundled update carries the newer name, icon and display version.
        guard let meta = patch ?? application else { throw SwitchExecutableError.noProgram }
        let control = try readControl(package, meta, cancelled: cancelled)
        let applicationID = application?.titleID ?? patch?.applicationID ?? meta.applicationID
        var info = GameInfo(applicationID: applicationID, kind: application == nil ? .update : .application,
                            version: meta.version, displayVersion: control.displayVersion,
                            title: control.title, publisher: control.publisher, hasIcon: control.icon != nil,
                            accent: control.icon.flatMap(accent(of:)))
        info.addOns = dlc
        info.bundlesUpdate = application != nil && patch != nil
        let others = applications.filter { $0.titleID != application?.titleID }
        if !others.isEmpty {
            info.extras = others.map { other in
                GameInfo.Extra(titleID: other.titleID,
                               title: (try? readControl(package, other, cancelled: cancelled).title).flatMap { $0.isEmpty ? nil : $0 } ?? other.titleID.titleIDString)
            }
        }
        return (info, control.icon)
    }

    /// Title, publisher, display version and icon from one application's verified Control archive.
    private static func readControl(_ package: SwitchExecutableReader.ContentPackage, _ meta: SwitchContentMeta,
                                    cancelled: @escaping () -> Bool) throws -> (title: String, publisher: String, displayVersion: String, icon: Data?) {
        guard let reference = meta.contents.first(where: { $0.type == 3 && $0.idOffset == 0 }),
              let control = package.archives.first(where: { $0.contentID == reference.id }),
              control.header[0x205] == 2 else { throw SwitchExecutableError.noProgram }
        guard control.entry.size <= 64 * 1024 * 1024 else { throw SwitchExecutableError.limit }
        try package.verify(control, in: meta)
        guard let section = control.sections.first(where: { $0.index == 0 }),
              section.fs[2] == 0, section.fs[3] == 3 else { throw SwitchExecutableError.unsupported }
        let raw = try package.storage(control, section: section)
        let verified = try SwitchIntegrityStorage(header: section.fs, sectionSize: raw.size,
                                                  cancelled: cancelled, read: raw.bytes)
        defer { verified.invalidate() }
        let romfs = try SwitchRomFS(storage: verified, cancelled: cancelled)
        guard let nacpFile = try romfs.open("/control.nacp"), nacpFile.size == 0x4000 else { throw SwitchExecutableError.invalid }
        let nacp = Data(try romfs.read(nacpFile, offset: 0, count: nacpFile.size))

        var title = "", publisher = "", iconData: Data?
        for slot in preferredOrder {
            let base = slot * 0x300
            let name = text(nacp, base, 0x200), maker = text(nacp, base + 0x200, 0x100)
            if title.isEmpty, !name.isEmpty { title = name; publisher = maker }
            if iconData == nil, let file = try romfs.open("/icon_\(languages[slot]).dat"),
               file.size > 4, file.size <= SwitchIntegrityStorage.maximumRead {
                let bytes = try romfs.read(file, offset: 0, count: file.size)
                if bytes.starts(with: [0xFF, 0xD8]) { iconData = bytes }
            }
            if !title.isEmpty && iconData != nil { break }
        }
        return (title, publisher, text(nacp, 0x3060, 0x10), iconData)
    }

    private static func text(_ data: Data, _ offset: Int, _ count: Int) -> String {
        let slice = data[data.startIndex + offset..<data.startIndex + offset + count]
        let end = slice.firstIndex(of: 0) ?? slice.endIndex
        return String(decoding: slice[slice.startIndex..<end], as: UTF8.self)
            .trimmingCharacters(in: .whitespacesAndNewlines.union(.controlCharacters))
    }

    /// Picks a lively colour from the icon for glows and fallbacks.
    static func accent(of jpeg: Data) -> [Double]? {
        guard let image = UIImage(data: jpeg)?.cgImage else { return nil }
        let side = 12
        var pixels = [UInt8](repeating: 0, count: side * side * 4)
        guard let context = CGContext(data: &pixels, width: side, height: side, bitsPerComponent: 8, bytesPerRow: side * 4,
                                      space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        context.interpolationQuality = .medium
        context.draw(image, in: CGRect(x: 0, y: 0, width: side, height: side))
        var best: (score: Double, rgb: [Double]) = (-1, [0.5, 0.5, 0.5])
        for i in stride(from: 0, to: pixels.count, by: 4) {
            let r = Double(pixels[i]) / 255, g = Double(pixels[i + 1]) / 255, b = Double(pixels[i + 2]) / 255
            let high = max(r, g, b), low = min(r, g, b)
            let saturation = high == 0 ? 0 : (high - low) / high
            let score = saturation * 0.7 + high * 0.3 - (high < 0.25 ? 1 : 0)
            if score > best.score { best = (score, [r, g, b]) }
        }
        return best.rgb
    }
}

/// A package prepared for native play (Documents/PreparedGames/<name>/manifest.json).
struct PreparedGame: Equatable {
    let folder: String
    let applicationID: UInt64
    let version: String
    /// When the package was finished (its manifest is written last).
    var date = Date.distantPast
    /// The phone's Prepare records the content version and build IDs; Mac-made packages do not.
    var preparedOnPhone = false
    /// Passes this build's engine identity check (`PreparedIdentity`). A package an older engine
    /// made is not current: launch refuses it, so it has to be prepared again.
    var current = false

    static func scan() -> [PreparedGame] {
        struct Manifest: Decodable { let titleID: String; let version: String; let contentVersion: UInt32?; let buildIDs: [String: String]? }
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let root = documents.appendingPathComponent("PreparedGames", isDirectory: true)
        guard let folders = try? FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: nil) else { return [] }
        return folders.compactMap { folder in
            let url = folder.appendingPathComponent("manifest.json")
            guard let size = (try? FileManager.default.attributesOfItem(atPath: url.path))?[.size] as? NSNumber,
                  size.intValue <= 32 * 1024 * 1024, let data = try? Data(contentsOf: url),
                  let manifest = try? JSONDecoder().decode(Manifest.self, from: data),
                  let id = UInt64(manifest.titleID, radix: 16) else { return nil }
            let date = (try? url.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate ?? .distantPast
            return PreparedGame(folder: folder.lastPathComponent, applicationID: id, version: manifest.version, date: date,
                                preparedOnPhone: manifest.contentVersion != nil || manifest.buildIDs != nil,
                                current: PreparedIdentity.isCurrent(data))
        }
    }
}

extension Notification.Name {
    static let satchelKeysChanged = Notification.Name("SatchelKeysChanged")
}

/// Reads and caches artwork for imported packages, one at a time, off the main thread.
final class GameArtworkStore: ObservableObject {
    enum Status: Equatable { case idle, reading, needsKeys, failed(String) }

    @Published private(set) var infos: [String: GameInfo] = [:]
    @Published private(set) var images: [String: UIImage] = [:]
    @Published private(set) var status: [String: Status] = [:]
    @Published private(set) var prepared: [PreparedGame] = []
    /// Artwork kept per game (title ID) rather than per imported file, so a prepared game keeps its
    /// art after its import is removed, and a game prepared elsewhere can bring its own.
    @Published private(set) var titleInfos: [UInt64: GameInfo] = [:]
    @Published private(set) var titleImages: [UInt64: UIImage] = [:]
    @Published private(set) var keysAvailable = false
    private let queue = DispatchQueue(label: "com.dell.satchel.artwork", qos: .utility)
    private let directory: URL
    private var pending: [(id: String, url: URL)] = []
    private var working = false
    private var observer: NSObjectProtocol?
    private var lastItems: [HomebrewItem] = []
    private var resolver: ((HomebrewItem) -> URL)?

    init() {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        directory = support.appendingPathComponent("GameArt", isDirectory: true)
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        var values = URLResourceValues(); values.isExcludedFromBackup = true
        var excluded = directory; try? excluded.setResourceValues(values)
        keysAvailable = (try? SwitchKeys.load()) != nil
        prepared = PreparedGame.scan()
        loadTitleArt()
        observer = NotificationCenter.default.addObserver(forName: .satchelKeysChanged, object: nil, queue: .main) { [weak self] _ in
            self?.keysChanged()
        }
    }

    deinit { if let observer { NotificationCenter.default.removeObserver(observer) } }

    func info(_ item: HomebrewItem) -> GameInfo? {
        if let info = infos[item.id] { return info }
        guard let source = item.preparedSource, !source.isUpdate else { return nil }
        return titleInfos[source.titleID]
    }
    func image(_ item: HomebrewItem) -> UIImage? {
        if let image = images[item.id] { return image }
        guard let id = item.preparedSource?.titleID ?? newestPackage(for: item)?.applicationID else { return nil }
        return titleImages[id]
    }
    /// The art saved for a game, by title ID.
    func titleInfo(_ applicationID: UInt64) -> GameInfo? { titleInfos[applicationID] }

    private static func titleName(_ id: UInt64) -> String { "title-" + String(format: "%016llX", id) }
    /// Keeps a copy of an imported application's art under its title ID.
    private static func saveTitleArt(_ info: GameInfo, icon: Data?, in directory: URL) {
        guard info.kind == .application else { return }
        let base = directory.appendingPathComponent(titleName(info.applicationID))
        if let icon { try? icon.write(to: base.appendingPathExtension("jpg"), options: .atomic) }
        if let data = try? JSONEncoder().encode(info) { try? data.write(to: base.appendingPathExtension("json"), options: .atomic) }
    }
    private func loadTitleArt() {
        let decoder = JSONDecoder()
        for game in prepared where titleInfos[game.applicationID] == nil {
            let base = directory.appendingPathComponent(Self.titleName(game.applicationID))
            guard let data = try? Data(contentsOf: base.appendingPathExtension("json")),
                  let info = try? decoder.decode(GameInfo.self, from: data) else { continue }
            titleInfos[game.applicationID] = info
            if let image = UIImage(contentsOfFile: base.appendingPathExtension("jpg").path) { titleImages[game.applicationID] = image }
        }
    }

    func isPrepared(_ item: HomebrewItem) -> Bool {
        return preparedPackage(for: item) != nil
    }

    func refreshPrepared() { prepared = PreparedGame.scan(); loadTitleArt() }

    /// Shelf IDs of games that are prepared on this iPhone but whose file is not in the library.
    static let preparedPrefix = "prepared:"
    static func preparedFolder(_ id: String) -> String? {
        id.hasPrefix(preparedPrefix) ? String(id.dropFirst(preparedPrefix.count)) : nil
    }

    /// The package that Play starts for an imported game: the newest current one prepared for
    /// its title. A package made by an older engine is never started; it needs Prepare again.
    func preparedPackage(for item: HomebrewItem) -> PreparedGame? {
        packages(for: item).filter(\.current).max { $0.date < $1.date }
    }

    /// The newest package of the item's title, current or not: for its art, title ID and the
    /// "prepare again" wording.
    func newestPackage(for item: HomebrewItem) -> PreparedGame? {
        packages(for: item).max { $0.date < $1.date }
    }

    /// The title has prepared packages, but this build can start none of them.
    func needsPrepareAgain(_ item: HomebrewItem) -> Bool {
        !isPrepared(item) && newestPackage(for: item) != nil
    }

    private func packages(for item: HomebrewItem) -> [PreparedGame] {
        if let folder = Self.preparedFolder(item.id) { return prepared.filter { $0.folder == folder } }
        guard item.preparedSource?.isUpdate != true, info(item)?.kind != .update, info(item)?.kind != .addOnContent,
              let id = item.preparedSource?.titleID ?? info(item)?.applicationID else { return [] }
        return prepared.filter { $0.applicationID == id }
    }

    /// Every prepared game belongs in the library: packages whose game was never imported here
    /// (one prepared elsewhere, or after its file was removed) get a shelf item of their own.
    func preparedOnlyItems(_ items: [HomebrewItem], name: (UInt64) -> String?) -> [HomebrewItem] {
        let imported = Set(items.compactMap { item -> UInt64? in
            guard item.preparedSource?.isUpdate != true, info(item)?.kind != .update, info(item)?.kind != .addOnContent else { return nil }
            return item.preparedSource?.titleID ?? info(item)?.applicationID
        })
        // One tile per title: its newest current package, else its newest one (which then
        // shows as needing Prepare again).
        var newest: [UInt64: PreparedGame] = [:]
        for game in prepared where !imported.contains(game.applicationID) {
            guard let kept = newest[game.applicationID] else { newest[game.applicationID] = game; continue }
            if kept.current == game.current ? kept.date <= game.date : game.current { newest[game.applicationID] = game }
        }
        return newest.values.map { game in
            HomebrewItem(id: Self.preparedPrefix + game.folder, name: name(game.applicationID) ?? game.folder, bytes: 0,
                         textBytes: 0, roBytes: 0, dataBytes: 0, bssBytes: 0, importedAt: game.date)
        }
    }

    /// Loads cached entries, then queues extraction for packages without one.
    func sync(_ items: [HomebrewItem], fileURL: @escaping (HomebrewItem) -> URL) {
        let decoder = JSONDecoder()
        for item in items where item.package != nil && infos[item.id] == nil {
            let json = directory.appendingPathComponent(item.id + ".json")
            if let data = try? Data(contentsOf: json), let info = try? decoder.decode(GameInfo.self, from: data),
               info.addOns != nil || item.preparedSource != nil {
                infos[item.id] = info
                // Imports read by older builds have art only under the file's ID.
                if info.kind == .application,
                   !FileManager.default.fileExists(atPath: directory.appendingPathComponent(Self.titleName(info.applicationID) + ".json").path) {
                    Self.saveTitleArt(info, icon: try? Data(contentsOf: directory.appendingPathComponent(item.id + ".jpg")), in: directory)
                }
                if info.hasIcon, let image = UIImage(contentsOfFile: directory.appendingPathComponent(item.id + ".jpg").path) {
                    images[item.id] = image
                }
            } else if status[item.id] == nil, FileManager.default.fileExists(atPath: fileURL(item).path) {
                enqueue(item.id, fileURL(item))
            }
        }
        lastItems = items
        resolver = fileURL
        let known = Set(items.map(\.id))
        for id in infos.keys where !known.contains(id) { infos[id] = nil; images[id] = nil }
    }

    func retry(_ item: HomebrewItem, url: URL) {
        status[item.id] = nil
        enqueue(item.id, url)
    }

    func forget(_ item: HomebrewItem) {
        infos[item.id] = nil; images[item.id] = nil; status[item.id] = nil
        for ext in ["json", "jpg"] { try? FileManager.default.removeItem(at: directory.appendingPathComponent(item.id + "." + ext)) }
    }

    /// Refuse cleanup unless the displayed metadata and icon are durable without the import.
    func preserveForCleanup(_ items: [HomebrewItem]) throws {
        for item in items {
            guard let info = info(item) else {
                throw NSError(domain: "GameArt", code: 1, userInfo: [NSLocalizedDescriptionKey: "Game artwork is still being read. Original imports were kept."])
            }
            let source = directory.appendingPathComponent(item.id)
            let icon = try info.hasIcon ? Data(contentsOf: source.appendingPathExtension("jpg")) : nil
            try JSONEncoder().encode(info).write(to: source.appendingPathExtension("json"), options: .atomic)
            if info.kind == .application {
                let title = directory.appendingPathComponent(Self.titleName(info.applicationID))
                if let icon { try icon.write(to: title.appendingPathExtension("jpg"), options: .atomic) }
                try JSONEncoder().encode(info).write(to: title.appendingPathExtension("json"), options: .atomic)
            }
        }
    }

    /// Refresh only art that can still be read from an original. Title art is durable library data.
    func clearCache(_ items: [HomebrewItem], fileURL: @escaping (HomebrewItem) -> URL) {
        for item in items where item.preparedSource == nil && FileManager.default.fileExists(atPath: fileURL(item).path) { forget(item) }
        sync(items, fileURL: fileURL)
    }

    var cacheBytes: Int64 {
        let files = (try? FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.fileSizeKey])) ?? []
        return files.reduce(0) { $0 + Int64((try? $1.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0) }
    }

    private func keysChanged() {
        keysAvailable = (try? SwitchKeys.load()) != nil
        for (id, value) in status {
            switch value {
            case .needsKeys, .failed: status[id] = nil
            default: break
            }
        }
        if let resolver { sync(lastItems, fileURL: resolver) }
    }

    private func enqueue(_ id: String, _ url: URL) {
        guard !pending.contains(where: { $0.id == id }) else { return }
        pending.append((id, url))
        status[id] = .reading
        pump()
    }

    private func pump() {
        guard !working, !pending.isEmpty else { return }
        working = true
        let job = pending.removeFirst()
        let directory = directory
        queue.async {
            let outcome: Result<(GameInfo, Data?), Error>
            let keys: [String: Data]?
            do { keys = try SwitchKeys.load() } catch { keys = nil }
            if let keys {
                outcome = Result { try GameInfoReader.read(job.url, keys: keys, cancelled: { false }) }
            } else {
                outcome = .failure(SwitchKeyFailure.unavailable)
            }
            var image: UIImage?
            if case .success(let (info, icon)) = outcome {
                if let icon {
                    try? icon.write(to: directory.appendingPathComponent(job.id + ".jpg"), options: .atomic)
                    image = UIImage(data: icon)?.preparingForDisplay()
                }
                if let data = try? JSONEncoder().encode(info) {
                    try? data.write(to: directory.appendingPathComponent(job.id + ".json"), options: .atomic)
                }
                Self.saveTitleArt(info, icon: icon, in: directory)
            }
            DispatchQueue.main.async {
                self.working = false
                switch outcome {
                case .success(let (info, _)):
                    self.infos[job.id] = info
                    if let image { self.images[job.id] = image }
                    self.status[job.id] = .idle
                case .failure(let error):
                    self.status[job.id] = keys == nil ? .needsKeys : .failed(GameInfoReader.plainReason(error))
                }
                self.pump()
            }
        }
    }
}

extension GameInfoReader {
    /// Short, non-technical reason. Never includes keys, paths or game bytes.
    static func plainReason(_ error: Error) -> String {
        switch error {
        case SwitchExecutableError.missingKey:
            return FirstRunHelp.newerKeys
        case SwitchExecutableError.invalidKeys:
            return FirstRunHelp.invalidKeys
        case SwitchExecutableError.missingTicket:
            return "This package has no ticket for its protected content."
        case SwitchExecutableError.personalizedTicket:
            return "This package uses a console-specific ticket, which is not supported yet."
        case SwitchExecutableError.integrity:
            return "The game's info failed its integrity check. The file may be damaged; import it again."
        case SwitchExecutableError.noProgram, SwitchExecutableError.unsupported:
            return "This package has no game info \(AppBrand.name) can read (it may be add-on content)."
        default:
            return error.localizedDescription
        }
    }
}
