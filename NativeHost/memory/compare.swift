import Foundation
import CoreGraphics
import ImageIO

// Decode into the same sRGB layout. Report differences without silently accepting
// a threshold: animation and scene timing still require visual review.
func pixels(_ url: URL) -> (width: Int, height: Int, bytes: [UInt8]) {
    guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil),
          let space = CGColorSpace(name: CGColorSpace.sRGB),
          let context = CGContext(data: nil, width: image.width, height: image.height,
                                  bitsPerComponent: 8, bytesPerRow: image.width * 4,
                                  space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
          let data = context.data else { fatalError("Cannot decode capture: \(url.path)") }
    context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
    return (image.width, image.height,
            Array(UnsafeBufferPointer(start: data.assumingMemoryBound(to: UInt8.self), count: image.width * image.height * 4)))
}

guard CommandLine.arguments.count >= 2 else { fatalError("Usage: compare OUTPUT [LABEL]") }
let root = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
let label = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "route"
let selection = CommandLine.arguments.count > 3 ? CommandLine.arguments[3] : nil
let games = ["Castlevania", "Odyssey", "TOTK"]
if let selection { precondition(games.contains(selection), "Unknown game") }
var results: [[String: Any]] = []
for game in selection.map({ [$0] }) ?? games {
    let baseline = root.appendingPathComponent("runs/\(game)/baseline/\(label)")
    let route = try JSONSerialization.jsonObject(with: Data(contentsOf: baseline.appendingPathComponent("route.json"))) as! [String: Any]
    for frame in route["checkpoints"] as! [Int] {
        let filename = "shots/frame-\(frame).png"
        let beforeURL = baseline.appendingPathComponent(filename)
        let afterURL = root.appendingPathComponent("runs/\(game)/candidate/\(label)/\(filename)")
        let before = pixels(beforeURL), after = pixels(afterURL)
        guard before.width == after.width && before.height == after.height else { fatalError("Capture dimensions differ") }
        var total = UInt64(0), maximum = 0, changed = 0
        for index in stride(from: 0, to: before.bytes.count, by: 4) {
            var pixelChanged = false
            for channel in 0..<3 {
                let difference = abs(Int(before.bytes[index + channel]) - Int(after.bytes[index + channel]))
                total += UInt64(difference)
                maximum = max(maximum, difference)
                pixelChanged = pixelChanged || difference != 0
            }
            if pixelChanged { changed += 1 }
        }
        let count = before.width * before.height
        results.append(["game": game, "frame": frame, "width": before.width, "height": before.height,
                        "changedPixels": changed, "meanAbsoluteRGBDifference": Double(total) / Double(count * 3),
                        "maximumChannelDifference": maximum, "exactPixels": changed == 0,
                        "baseline": beforeURL.path, "candidate": afterURL.path])
    }
}
let data = try JSONSerialization.data(withJSONObject: results, options: [.prettyPrinted, .sortedKeys])
let filename = selection.map({ "capture-comparison-\($0).json" }) ?? "capture-comparison.json"
try data.write(to: root.appendingPathComponent(filename), options: .atomic)
print("Compared \(results.count) capture pairs. Visual scene review is required for unequal pixels.")
