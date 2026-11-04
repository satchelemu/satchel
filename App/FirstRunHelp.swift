import Foundation

/// Shared recovery text for import and Prepare. Never includes key material.
enum FirstRunHelp {
    static let noKeys = "Import your own prod.keys in Settings > Console keys, then try again."
    static let newerKeys = "Your prod.keys is missing a key this game needs. Export a fresh prod.keys from your own updated console and replace it in Settings > Console keys."
    static let invalidKeys = "These keys could not read the game. Replace prod.keys from your own console in Settings > Console keys. If it still fails, check that the game dump is complete."
    static let space = "There is not enough space on this iPhone. Free space in Settings > General > iPhone Storage, then try again. Your original file in Files is unchanged."
    static let unsupported = "This file type is not supported. Choose an XCI, NSP, XCZ or NSZ game, or NRO homebrew. Extract archives first, then choose the game file."

    static func message(_ error: Error) -> String {
        var current = error as NSError
        for _ in 0..<8 {
            if (current.domain == NSCocoaErrorDomain && current.code == NSFileWriteOutOfSpaceError)
                || (current.domain == NSPOSIXErrorDomain && current.code == Int(ENOSPC)) { return space }
            guard let underlying = current.userInfo[NSUnderlyingErrorKey] as? NSError else { break }
            current = underlying
        }
        return error.localizedDescription
    }
}
