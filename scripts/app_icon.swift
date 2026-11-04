// Satchel app icon generator. The icon is the in-app BrandMark (App/SatchelDesign.swift): two
// tilted capsules, Ember on the left and a shorter Tide on the right, on the dark shell. It is
// drawn here as vectors and written as the asset catalog icon sets for both apps:
//
//   AppIcon      Satchel (target Satchel)
//   AppIconDev   Satchel Dev (NativeHost/build.py): light shell plus a yellow DEV band
//
// Each set has the 1024 px master plus the iOS 18 dark and tinted appearances.
//
//   swift scripts/app_icon.swift                 regenerate App/Assets.xcassets icon sets
//   swift scripts/app_icon.swift --check         fail if the committed PNGs differ from this source
//   swift scripts/app_icon.swift --previews DIR  write home-screen previews (not committed)
//   swift scripts/app_icon.swift --brand DIR     write the GitHub avatar, social preview and SVG mark

import AppKit
import CoreGraphics
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

let side = 1024
let space = CGColorSpace(name: CGColorSpace.sRGB)!

func rgb(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> CGColor { CGColor(colorSpace: space, components: [r, g, b, a])! }
func hex(_ v: UInt32, _ a: CGFloat = 1) -> CGColor { rgb(CGFloat(v >> 16 & 255) / 255, CGFloat(v >> 8 & 255) / 255, CGFloat(v & 255) / 255, a) }
func gray(_ l: CGFloat, _ a: CGFloat = 1) -> CGColor { rgb(l, l, l, a) }
func mix(_ c: CGColor, _ d: CGColor, _ t: CGFloat) -> CGColor {
    let a = c.components!, b = d.components!
    return rgb(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t)
}

// SatchelTheme values.
let ember = rgb(1.0, 0.29, 0.33), tide = rgb(0.13, 0.71, 1.0), spark = rgb(1.0, 0.82, 0.25)
let shell = hex(0x111318), white = gray(1), black = gray(0)

enum Look: String, CaseIterable { case light = "", dark = "-Dark", tinted = "-Tinted" }
enum Variant: String, CaseIterable { case app = "AppIcon", dev = "AppIconDev" }

/// A square bitmap context with a y-down coordinate space.
func canvas(_ px: Int, opaque: Bool = false, flipped: Bool = true) -> CGContext {
    let ctx = CGContext(data: nil, width: px, height: px, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                        bitmapInfo: (opaque ? CGImageAlphaInfo.noneSkipLast : .premultipliedLast).rawValue)!
    if flipped { ctx.translateBy(x: 0, y: CGFloat(px)); ctx.scaleBy(x: 1, y: -1) }
    return ctx
}

func gradient(_ colors: [CGColor]) -> CGGradient {
    CGGradient(colorsSpace: space, colors: colors as CFArray, locations: nil)!
}

func capsule(_ r: CGRect) -> CGPath { CGPath(roundedRect: r, cornerWidth: r.width / 2, cornerHeight: r.width / 2, transform: nil) }

/// The BrandMark, centered at `center`, 560 * scale tall.
func mark(_ ctx: CGContext, center: CGPoint, scale s: CGFloat, look: Look, glow: Bool, shadow: CGColor?) {
    let h = 560 * s, w = 0.42 * h, gap = 0.13 * h, short = 0.72 * h
    let pills = [(CGRect(x: -(w + gap / 2), y: -h / 2, width: w, height: h), ember, 1.0 as CGFloat),
                 (CGRect(x: gap / 2, y: -short / 2, width: w, height: short), tide, 0.64 as CGFloat)]
    ctx.saveGState()
    ctx.translateBy(x: center.x, y: center.y)
    ctx.rotate(by: -12 * .pi / 180)
    if glow {
        for (rect, color, _) in pills {
            let c = CGPoint(x: rect.midX, y: rect.midY + 0.12 * h)
            ctx.drawRadialGradient(gradient([mix(color, black, 0.1).copy(alpha: 0.3)!, color.copy(alpha: 0)!]),
                                   startCenter: c, startRadius: 0, endCenter: c, endRadius: 0.9 * h, options: [])
        }
    }
    if let shadow { ctx.setShadow(offset: CGSize(width: 0, height: -0.05 * h), blur: 0.1 * h, color: shadow) }
    ctx.beginTransparencyLayer(auxiliaryInfo: nil)
    for (rect, color, level) in pills {
        let top: CGColor, bottom: CGColor
        switch look {
        case .tinted: top = gray(level); bottom = gray(level * 0.86)
        default: top = mix(color, white, 0.24); bottom = mix(color, black, 0.12)
        }
        ctx.saveGState()
        ctx.addPath(capsule(rect)); ctx.clip()
        ctx.drawLinearGradient(gradient([top, bottom]), start: CGPoint(x: rect.midX, y: rect.minY),
                               end: CGPoint(x: rect.midX, y: rect.maxY), options: [])
        if look != .tinted {
            // Soft gloss down the leading edge; it disappears at home-screen sizes, as it should.
            let gloss = CGRect(x: rect.minX + 0.2 * w, y: rect.minY + 0.1 * w, width: 0.17 * w, height: rect.height * 0.5)
            ctx.addPath(capsule(gloss)); ctx.setFillColor(gray(1, 0.24)); ctx.fillPath()
        }
        ctx.restoreGState()
    }
    ctx.endTransparencyLayer()
    ctx.restoreGState()
}

/// "DEV" built from round-capped strokes, so it needs no font.
func devLetters(center: CGPoint, height h: CGFloat) -> CGPath {
    let p = CGMutablePath(), w = 0.66 * h, gap = 0.5 * h, top = center.y - h / 2, bottom = center.y + h / 2
    let d = center.x - (3 * w + 2 * gap) / 2, r = h / 2
    p.move(to: CGPoint(x: d, y: top)); p.addLine(to: CGPoint(x: d + w - r, y: top))
    p.addArc(center: CGPoint(x: d + w - r, y: center.y), radius: r, startAngle: -.pi / 2, endAngle: .pi / 2, clockwise: false)
    p.addLine(to: CGPoint(x: d, y: bottom)); p.closeSubpath()
    let e = d + w + gap
    p.move(to: CGPoint(x: e + 0.86 * w, y: top)); p.addLine(to: CGPoint(x: e, y: top))
    p.addLine(to: CGPoint(x: e, y: bottom)); p.addLine(to: CGPoint(x: e + 0.86 * w, y: bottom))
    p.move(to: CGPoint(x: e, y: center.y)); p.addLine(to: CGPoint(x: e + 0.7 * w, y: center.y))
    let v = e + w + gap
    p.move(to: CGPoint(x: v - 0.06 * w, y: top)); p.addLine(to: CGPoint(x: v + w / 2, y: bottom))
    p.addLine(to: CGPoint(x: v + 1.06 * w, y: top))
    return p
}

/// Renders one icon at 1024 px. Light icons are opaque; dark and tinted ones are transparent so
/// the system draws its own background behind them.
func render(_ variant: Variant, _ look: Look) -> CGImage {
    let ctx = canvas(side, opaque: look == .light), full = CGFloat(side)
    let dev = variant == .dev
    if look == .light {
        let colors = dev ? [hex(0xF7F8FA), hex(0xD5D9E1)] : [hex(0x2A2F38), hex(0x0B0D11)]
        ctx.drawLinearGradient(gradient(colors), start: .zero, end: CGPoint(x: 0, y: full), options: [])
    }
    let shadow: CGColor? = look == .light ? gray(0, dev ? 0.22 : 0.55) : nil
    mark(ctx, center: CGPoint(x: full / 2, y: dev ? 404 : full / 2), scale: dev ? 0.86 : 1.08,
         look: look, glow: look == .light && !dev, shadow: shadow)
    if dev {
        let band = CGRect(x: 0, y: 768, width: full, height: full - 768)
        ctx.setFillColor(look == .tinted ? gray(0.92) : spark); ctx.fill(band)
        ctx.addPath(devLetters(center: CGPoint(x: full / 2, y: 878), height: 116))
        ctx.setLineWidth(30); ctx.setLineCap(.round); ctx.setLineJoin(.round)
        if look == .light { ctx.setStrokeColor(shell) } else { ctx.setBlendMode(.clear) }
        ctx.strokePath()
    }
    return ctx.makeImage()!
}

func writePNG(_ image: CGImage, _ url: URL) {
    let dest = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, image, nil)
    precondition(CGImageDestinationFinalize(dest), "Could not write \(url.path)")
}

func readPNG(_ url: URL) -> CGImage? {
    guard let source = CGImageSourceCreateWithURL(url as CFURL, nil) else { return nil }
    return CGImageSourceCreateImageAtIndex(source, 0, nil)
}

/// RGBA bytes of an image redrawn at `px`.
func pixels(_ image: CGImage, _ px: Int = side) -> [UInt8] {
    let ctx = canvas(px, flipped: false)
    ctx.interpolationQuality = .high
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: px, height: px))
    return Array(UnsafeBufferPointer(start: ctx.data!.assumingMemoryBound(to: UInt8.self), count: ctx.bytesPerRow * px))
}

func scaled(_ image: CGImage, _ px: Int) -> CGImage {
    let ctx = canvas(px, flipped: false)
    ctx.interpolationQuality = .high
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: px, height: px))
    return ctx.makeImage()!
}

func contents(_ variant: Variant) -> String {
    let images = Look.allCases.map { look -> [String: Any] in
        var row: [String: Any] = ["filename": "\(variant.rawValue)\(look.rawValue).png", "idiom": "universal",
                                  "platform": "ios", "size": "1024x1024"]
        if look != .light { row["appearances"] = [["appearance": "luminosity", "value": look == .dark ? "dark" : "tinted"]] }
        return row
    }
    let json = ["images": images, "info": ["author": "xcode", "version": 1]] as [String: Any]
    return String(data: try! JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted, .sortedKeys]), encoding: .utf8)! + "\n"
}

// MARK: Previews

/// Approximation of the iOS icon mask (a superellipse).
func squircle(_ r: CGRect) -> CGPath {
    let p = CGMutablePath(), n = 5.0
    for i in 0..<720 {
        let t = Double(i) / 720 * 2 * .pi, c = cos(t), s = sin(t)
        let point = CGPoint(x: r.midX + CGFloat(copysign(pow(abs(c), 2 / n), c)) * r.width / 2,
                            y: r.midY + CGFloat(copysign(pow(abs(s), 2 / n), s)) * r.height / 2)
        i == 0 ? p.move(to: point) : p.addLine(to: point)
    }
    p.closeSubpath()
    return p
}

/// An icon as the home screen would show it: masked, with the system's dark background or tint applied.
func homeIcon(_ variant: Variant, _ look: Look, _ px: Int, tint: CGColor) -> CGImage {
    let icon = scaled(render(variant, look), px)
    let ctx = canvas(px, flipped: false), full = CGFloat(px), rect = CGRect(x: 0, y: 0, width: full, height: full)
    ctx.addPath(squircle(rect)); ctx.clip()
    switch look {
    case .light:
        ctx.draw(icon, in: rect)
    case .dark:
        ctx.drawLinearGradient(gradient([hex(0x18181B), hex(0x3A3A3F)]), start: .zero, end: CGPoint(x: 0, y: full), options: [])
        ctx.draw(icon, in: rect)
    case .tinted:
        // Luminance maps onto the tint over a dark tinted background.
        var bytes = pixels(icon, px)
        let t = tint.components!, row = bytes.count / px
        for y in 0..<px {
            let base = 0.26 - 0.14 * CGFloat(y) / full
            for x in 0..<px {
                let i = y * row + x * 4, a = CGFloat(bytes[i + 3]) / 255
                let l = a > 0 ? CGFloat(bytes[i]) / 255 / a : 0
                for k in 0..<3 {
                    let v = t[k] * base * (1 - a) + t[k] * (0.12 + 0.88 * l) * a
                    bytes[i + k] = UInt8(max(0, min(255, (v * 255).rounded())))
                }
                bytes[i + 3] = 255
            }
        }
        let provider = CGDataProvider(data: Data(bytes) as CFData)!
        let tinted = CGImage(width: px, height: px, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: row, space: space,
                             bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                             provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)!
        ctx.draw(tinted, in: rect)
    }
    return ctx.makeImage()!
}

func draw(_ ctx: CGContext, _ image: CGImage, at origin: CGPoint) {
    ctx.saveGState()
    ctx.translateBy(x: origin.x, y: origin.y + CGFloat(image.height)); ctx.scaleBy(x: 1, y: -1)
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
    ctx.restoreGState()
}

func text(_ ctx: CGContext, _ s: String, center: CGPoint, size: CGFloat, color: CGColor) {
    let font = CTFontCreateUIFontForLanguage(.emphasizedSystem, size, nil)!
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): color]))
    let width = CTLineGetTypographicBounds(line, nil, nil, nil)
    ctx.saveGState()
    ctx.textMatrix = .identity
    ctx.translateBy(x: center.x - CGFloat(width) / 2, y: center.y + size * 0.35); ctx.scaleBy(x: 1, y: -1)
    ctx.textPosition = .zero
    CTLineDraw(line, ctx)
    ctx.restoreGState()
}

func previews(_ dir: URL) {
    try! FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    let tint = rgb(1.0, 0.72, 0.32), names = [Variant.app: "Satchel", .dev: "Satchel Dev"]
    // Large: every variant and appearance.
    let tile = 360, pad = 60, sheet = CGContext(data: nil, width: 3 * tile + 4 * pad, height: 2 * tile + 3 * pad + 120,
        bitsPerComponent: 8, bytesPerRow: 0, space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    sheet.translateBy(x: 0, y: CGFloat(sheet.height)); sheet.scaleBy(x: 1, y: -1)
    sheet.setFillColor(hex(0x8A8F99)); sheet.fill(CGRect(x: 0, y: 0, width: sheet.width, height: sheet.height))
    for (row, variant) in Variant.allCases.enumerated() {
        for (col, look) in Look.allCases.enumerated() {
            let x = pad + col * (tile + pad), y = pad + row * (tile + pad + 60)
            draw(sheet, homeIcon(variant, look, tile, tint: tint), at: CGPoint(x: x, y: y))
            let label = "\(names[variant]!), \(look == .light ? "default" : look == .dark ? "dark" : "tinted")"
            text(sheet, label, center: CGPoint(x: CGFloat(x + tile / 2), y: CGFloat(y + tile + 30)), size: 24, color: white)
        }
    }
    writePNG(sheet.makeImage()!, dir.appendingPathComponent("satchel-icon-large.png"))
    // Home-screen sizes, at actual pixels: 180 (60 pt @3x), 120 (60 pt @2x), 87 (29 pt @3x), 58, 40.
    let sizes = [180, 120, 87, 58, 40], gap = 56
    let width = sizes.reduce(0, +) + gap * (sizes.count + 1) + 160, height = 2 * (180 + 90) + 70
    for look in Look.allCases {
        let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.translateBy(x: 0, y: CGFloat(height)); ctx.scaleBy(x: 1, y: -1)
        let wall = look == .light ? [hex(0xE4EAF3), hex(0xA9B6CB)] : [hex(0x232838), hex(0x05060A)]
        ctx.drawLinearGradient(gradient(wall), start: .zero, end: CGPoint(x: 0, y: height), options: [])
        let ink = look == .light ? hex(0x1B1E24) : white
        for (row, variant) in Variant.allCases.enumerated() {
            let top = 40 + row * (180 + 90)
            text(ctx, names[variant]!, center: CGPoint(x: 90, y: CGFloat(top + 90)), size: 22, color: ink)
            var x = 160 + gap
            for px in sizes {
                draw(ctx, homeIcon(variant, look, px, tint: tint), at: CGPoint(x: x, y: top + (180 - px) / 2))
                text(ctx, "\(px) px", center: CGPoint(x: CGFloat(x + px / 2), y: CGFloat(top + 180 + 28)), size: 18, color: ink.copy(alpha: 0.7)!)
                x += px + gap
            }
        }
        let name = look == .light ? "light" : look == .dark ? "dark" : "tinted"
        writePNG(ctx.makeImage()!, dir.appendingPathComponent("satchel-home-\(name).png"))
    }
}

// MARK: Main

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let catalog = root.appendingPathComponent("App/Assets.xcassets")

/// The system font with the rounded design the app uses, as a CTFont.
func roundedFont(_ size: CGFloat, _ weight: NSFont.Weight) -> CTFont {
    let base = NSFont.systemFont(ofSize: size, weight: weight)
    let descriptor = base.fontDescriptor.withDesign(.rounded) ?? base.fontDescriptor
    return (NSFont(descriptor: descriptor, size: size) ?? base) as CTFont
}

/// Left-aligned text with its baseline at `origin` in a y-down context. Returns the advance.
@discardableResult
func label(_ ctx: CGContext, _ s: String, at origin: CGPoint, size: CGFloat, weight: NSFont.Weight, color: CGColor, tracking: CGFloat = 0) -> CGFloat {
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: [
        NSAttributedString.Key(kCTFontAttributeName as String): roundedFont(size, weight),
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): color,
        NSAttributedString.Key(kCTKernAttributeName as String): tracking]))
    ctx.saveGState()
    ctx.textMatrix = .identity
    ctx.translateBy(x: origin.x, y: origin.y); ctx.scaleBy(x: 1, y: -1)
    ctx.textPosition = .zero
    CTLineDraw(line, ctx)
    ctx.restoreGState()
    return CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
}

/// The mark as a standalone SVG: the same geometry as `mark`, without glow or shadow.
func markSVG() -> String {
    let s = 1.08, h = 560 * s, w = 0.42 * h, gap = 0.13 * h, short = 0.72 * h
    func hexString(_ c: CGColor) -> String {
        let v = c.components!; return String(format: "#%02X%02X%02X", Int(v[0] * 255 + 0.5), Int(v[1] * 255 + 0.5), Int(v[2] * 255 + 0.5))
    }
    func gradientDef(_ color: CGColor, _ id: String) -> String {
        let top = hexString(mix(color, white, 0.24)), bottom = hexString(mix(color, black, 0.12))
        return "<linearGradient id=\"\(id)\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"\(top)\"/><stop offset=\"1\" stop-color=\"\(bottom)\"/></linearGradient>"
    }
    func pill(_ x: Double, _ y: Double, _ height: Double, _ id: String) -> String {
        let body = String(format: "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" fill=\"url(#%@)\"/>", x, y, w, height, w / 2, id)
        let gloss = String(format: "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" fill=\"#FFFFFF\" fill-opacity=\"0.24\"/>",
                           x + 0.2 * w, y + 0.1 * w, 0.17 * w, height * 0.5, 0.085 * w)
        return "    " + body + "\n    " + gloss
    }
    return """
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">
      <title>Satchel</title>
      <defs>
        \(gradientDef(ember, "ember"))
        \(gradientDef(tide, "tide"))
      </defs>
      <g transform="translate(512 512) rotate(-12)">
    \(pill(-(w + gap / 2), -h / 2, h, "ember"))
    \(pill(gap / 2, -short / 2, short, "tide"))
      </g>
    </svg>

    """
}

/// GitHub assets: a square avatar, a transparent mark, a 1280 by 640 social preview and the SVG.
func brand(_ dir: URL) {
    try! FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    writePNG(render(.app, .light), dir.appendingPathComponent("satchel-avatar-1024.png"))
    let alone = canvas(side)
    mark(alone, center: CGPoint(x: 512, y: 512), scale: 1.08, look: .light, glow: false, shadow: nil)
    writePNG(alone.makeImage()!, dir.appendingPathComponent("satchel-mark-1024.png"))
    try! markSVG().write(to: dir.appendingPathComponent("satchel-mark.svg"), atomically: true, encoding: .utf8)

    let width = 1280, height = 640
    let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                        bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
    ctx.translateBy(x: 0, y: CGFloat(height)); ctx.scaleBy(x: 1, y: -1)
    ctx.drawLinearGradient(gradient([hex(0x2A2F38), hex(0x0B0D11)]), start: .zero, end: CGPoint(x: 0, y: CGFloat(height)), options: [])
    mark(ctx, center: CGPoint(x: 290, y: 320), scale: 0.74, look: .light, glow: true, shadow: gray(0, 0.55))
    let x: CGFloat = 540
    label(ctx, "Satchel", at: CGPoint(x: x - 6, y: 318), size: 148, weight: .heavy, color: white, tracking: -4)
    label(ctx, "Your own Switch games, prepared", at: CGPoint(x: x, y: 392), size: 38, weight: .semibold, color: gray(0.82))
    label(ctx, "and played natively on iPhone.", at: CGPoint(x: x, y: 442), size: 38, weight: .semibold, color: gray(0.82))
    label(ctx, "Open source, MIT. Bring your own dumps and keys.", at: CGPoint(x: x, y: 520), size: 26, weight: .medium, color: gray(0.52))
    writePNG(ctx.makeImage()!, dir.appendingPathComponent("satchel-social-preview-1280x640.png"))
    print("Wrote \(dir.path)")
}

let args = CommandLine.arguments.dropFirst()

if let i = args.firstIndex(of: "--brand") {
    guard args.index(after: i) < args.endIndex else { fatalError("--brand needs a directory") }
    brand(URL(fileURLWithPath: args[args.index(after: i)]))
} else if let i = args.firstIndex(of: "--previews") {
    guard args.index(after: i) < args.endIndex else { fatalError("--previews needs a directory") }
    previews(URL(fileURLWithPath: args[args.index(after: i)]))
} else if args.contains("--check") {
    var failures = 0
    for variant in Variant.allCases {
        for look in Look.allCases {
            let name = "\(variant.rawValue).appiconset/\(variant.rawValue)\(look.rawValue).png"
            guard let committed = readPNG(catalog.appendingPathComponent(name)) else { print("missing \(name)"); failures += 1; continue }
            let a = pixels(committed), b = pixels(render(variant, look))
            let worst = zip(a, b).reduce(0) { max($0, abs(Int($1.0) - Int($1.1))) }
            if worst > 2 { print("stale \(name): max channel difference \(worst)"); failures += 1 }
        }
        let json = catalog.appendingPathComponent("\(variant.rawValue).appiconset/Contents.json")
        if (try? String(contentsOf: json, encoding: .utf8)) != contents(variant) { print("stale \(variant.rawValue) Contents.json"); failures += 1 }
    }
    if failures > 0 { print("Run: swift scripts/app_icon.swift"); exit(1) }
    print("App icons match scripts/app_icon.swift")
} else {
    let fm = FileManager.default
    try! fm.createDirectory(at: catalog, withIntermediateDirectories: true)
    try! "{\n  \"info\" : { \"author\" : \"xcode\", \"version\" : 1 }\n}\n".write(to: catalog.appendingPathComponent("Contents.json"), atomically: true, encoding: .utf8)
    for variant in Variant.allCases {
        let set = catalog.appendingPathComponent("\(variant.rawValue).appiconset")
        try! fm.createDirectory(at: set, withIntermediateDirectories: true)
        for look in Look.allCases { writePNG(render(variant, look), set.appendingPathComponent("\(variant.rawValue)\(look.rawValue).png")) }
        try! contents(variant).write(to: set.appendingPathComponent("Contents.json"), atomically: true, encoding: .utf8)
        print("Wrote \(set.path)")
    }
}
