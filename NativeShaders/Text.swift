import Foundation

// Generated MSL is ASCII. NSString offsets keep regular-expression ranges exact.
struct ShaderMatch {
    let source: NSString
    let result: NSTextCheckingResult
    var start: Int { result.range.location }
    var end: Int { NSMaxRange(result.range) }
    func range(_ n: Int = 0) -> NSRange { result.range(at: n) }
    subscript(_ n: Int) -> String {
        let r = range(n)
        return r.location == NSNotFound ? "" : source.substring(with: r)
    }
}
// Patterns are immutable and safe to share. Bound dynamic shader-derived
// patterns so a long session cannot grow this process-wide cache indefinitely.
private enum ShaderRegex {
    static let lock = NSLock()
    static var values: [String: NSRegularExpression] = [:]
    static func get(_ pattern: String) -> NSRegularExpression {
        lock.lock()
        if let value = values[pattern] { lock.unlock(); return value }
        lock.unlock()
        let value = try! NSRegularExpression(pattern: pattern)
        lock.lock(); defer { lock.unlock() }
        if values.count < 512 { values[pattern] = value }
        return value
    }
}
func matches(_ pattern: String, _ source: String) -> [ShaderMatch] {
    let text = source as NSString
    return ShaderRegex.get(pattern).matches(in: source, range: NSRange(location: 0, length: text.length))
        .map { ShaderMatch(source: text, result: $0) }
}
func match(_ pattern: String, _ source: String) -> ShaderMatch? {
    let text = source as NSString
    return ShaderRegex.get(pattern).firstMatch(in: source, range: NSRange(location: 0, length: text.length))
        .map { ShaderMatch(source: text, result: $0) }
}
func sub(_ pattern: String, _ source: String, _ replacement: (ShaderMatch) -> String) -> String {
    let found = matches(pattern, source)
    guard !found.isEmpty else { return source }
    let text = source as NSString
    var pieces: [String] = [], end = text.length
    pieces.reserveCapacity(found.count * 2 + 1)
    // Preserve reverse callback order, UTF-16 offsets and zero-length matches.
    // Join once instead of copying the entire growing source per replacement.
    for m in found.reversed() {
        pieces.append(text.substring(with: NSRange(location: m.end, length: end - m.end)))
        pieces.append(replacement(m)); end = m.start
    }
    pieces.append(text.substring(to: end))
    return pieces.reversed().joined()
}
func slice(_ text: String, _ start: Int, _ end: Int? = nil) -> String {
    let s = text as NSString
    return s.substring(with: NSRange(location: start, length: (end ?? s.length) - start))
}
func find(_ text: String, _ needle: String, _ start: Int = 0) -> Int? {
    let s = text as NSString
    let r = s.range(of: needle, range: NSRange(location: start, length: s.length - start))
    return r.location == NSNotFound ? nil : r.location
}
func replacingFirst(_ text: String, _ old: String, _ new: String) -> String {
    guard let i = find(text, old) else { return text }
    return slice(text, 0, i) + new + slice(text, i + (old as NSString).length)
}
func close(_ text: String, _ start: Int, _ open: UInt16, _ shut: UInt16) -> Int? {
    let s = text as NSString
    var depth = 1, i = start
    while depth > 0 && i < s.length {
        let c = s.character(at: i)
        if c == open { depth += 1 }
        if c == shut { depth -= 1 }
        i += 1
    }
    return depth == 0 ? i : nil
}
func trim(_ s: String) -> String { s.trimmingCharacters(in: .whitespacesAndNewlines) }
func escaped(_ s: String) -> String { NSRegularExpression.escapedPattern(for: s) }
struct ShaderVariant {
    let source: String
    let info: Any
}
