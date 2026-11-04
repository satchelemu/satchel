import Foundation
import Security

enum SwitchKeyFailure: Error, LocalizedError {
    case invalid, unavailable
    var errorDescription: String? {
        switch self {
        case .invalid: return "This file is not a usable prod.keys. Choose a fresh prod.keys exported from your own console. Your saved keys have not been changed."
        case .unavailable: return "Console keys could not be accessed in this app's secure storage. Unlock the phone and try again."
        }
    }
}

enum SwitchKeys {
    static let maximumFileSize = 1024 * 1024
    private static let service = "com.dell.satchel.console-keys"
    private static let account = "nca-loader-v1"

    // Retain only the material this loader uses; ignore unrelated console secrets.
    static func parse(_ data: Data) throws -> [String: Data] {
        guard data.count <= maximumFileSize, let text = String(data: data, encoding: .utf8) else { throw SwitchKeyFailure.invalid }
        var result: [String: Data] = [:]
        for raw in text.split(whereSeparator: \.isNewline) {
            let line = raw.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false)[0]
            let pair = line.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            guard pair.count == 2 else { continue }
            let name = pair[0].trimmingCharacters(in: .whitespaces).lowercased()
            let header = name == "header_key"
            let area = ["application", "ocean", "system"].contains { category in
                let prefix = "key_area_key_" + category + "_"
                return name.hasPrefix(prefix) && name.count == prefix.count + 2 && name.suffix(2).allSatisfy { $0.isHexDigit && $0.isASCII }
            }
            let title = name.hasPrefix("titlekek_") && name.count == 11 &&
                name.suffix(2).allSatisfy { $0.isHexDigit && $0.isASCII }
            guard header || area || title else { continue }
            let hex = Array(pair[1].trimmingCharacters(in: .whitespaces).utf8)
            guard result[name] == nil, hex.count == (header ? 64 : 32),
                  hex.allSatisfy({ (48...57).contains($0) || (65...70).contains($0) || (97...102).contains($0) }) else {
                throw SwitchKeyFailure.invalid
            }
            var value = Data(); value.reserveCapacity(hex.count / 2)
            for at in stride(from: 0, to: hex.count, by: 2) {
                guard let byte = UInt8(String(decoding: hex[at..<(at + 2)], as: UTF8.self), radix: 16) else { throw SwitchKeyFailure.invalid }
                value.append(byte)
            }
            result[name] = value
        }
        guard result["header_key"] != nil, result.count > 1 else { throw SwitchKeyFailure.invalid }
        return result
    }

    private static var query: [String: Any] {
        var result: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service, kSecAttrAccount as String: account]
        if let group = Bundle.main.object(forInfoDictionaryKey: "SatchelPrivateKeychainGroup") as? String {
            result[kSecAttrAccessGroup as String] = group
        }
        return result
    }

    static func load() throws -> [String: Data]? {
        var request = query
        request[kSecReturnData as String] = true
        request[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        let status = SecItemCopyMatching(request as CFDictionary, &item)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess, let data = item as? Data,
              let values = try? JSONDecoder().decode([String: Data].self, from: data), values["header_key"]?.count == 32 else {
            throw SwitchKeyFailure.unavailable
        }
        return values
    }

    static func save(_ keys: [String: Data]) throws {
        let data = try JSONEncoder().encode(keys)
        let attributes: [String: Any] = [kSecValueData as String: data,
                                        kSecAttrAccessible as String: kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
                                        kSecAttrSynchronizable as String: false]
        var status = SecItemUpdate(query as CFDictionary, attributes as CFDictionary)
        if status == errSecItemNotFound {
            var insert = query
            attributes.forEach { insert[$0.key] = $0.value }
            status = SecItemAdd(insert as CFDictionary, nil)
        }
        guard status == errSecSuccess else { throw SwitchKeyFailure.unavailable }
    }
}
