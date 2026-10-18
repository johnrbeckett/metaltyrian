import CoreGraphics
import CoreText
import Foundation
import Metal
import SDLShim

/// The controls overlay: a keycap panel over the bottom of the playfield,
/// shown as each level starts and while the game is paused. The game says
/// when (and which hand's keys the player last steered with); this draws it.
final class ControlsOverlay {
    /// Where it sits, in game pixels (inside the 264x184 playfield).
    static let rect = CGRect(x: 8, y: 108, width: 248, height: 68)
    private static let scale: CGFloat = 4
    static let width = Int(rect.width * scale), height = Int(rect.height * scale)

    let texture: MTLTexture
    private var drawnKey = ""
    private let font: CTFont?

    init?(device: MTLDevice) {
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: Self.width,
                                                         height: Self.height, mipmapped: false)
        d.usage = [.shaderRead]
        guard let t = device.makeTexture(descriptor: d) else { return nil }
        texture = t
        if let url = Bundle.main.url(forResource: "Orbitron-Bold", withExtension: "ttf", subdirectory: "Fonts")
                ?? Bundle.main.url(forResource: "Orbitron-Bold", withExtension: "ttf"),
           let descs = CTFontManagerCreateFontDescriptorsFromURL(url as CFURL) as? [CTFontDescriptor],
           let desc = descs.first {
            font = CTFontCreateWithFontDescriptor(desc, 24, nil)
        } else {
            font = nil
        }
    }

    /// Redraws the panel when the hand or the autofire state has changed.
    func update(lefty: Bool, start: Bool, autofire: Int) {
        // Redrawn when the hand, autofire or any key binding changes.
        var key = "\(lefty ? 1 : 0)\(start ? 1 : 0)\(autofire & 3)"
        for a in 0..<MT_ACT_COUNT { for s in Int32(0)..<2 { key += ",\(mt_key_binding(a, s))" } }
        guard key != drawnKey else { return }
        drawnKey = key
        let pixels = Self.draw(lefty: lefty, start: start, autofire: autofire, font: font ?? CTFontCreateWithName("Helvetica-Bold" as CFString, 24, nil))
        pixels.withUnsafeBytes {
            texture.replace(region: MTLRegionMake2D(0, 0, Self.width, Self.height), mipmapLevel: 0,
                            withBytes: $0.baseAddress!, bytesPerRow: Self.width * 4)
        }
    }

    // MARK: - Drawing

    private enum Piece {
        case key(String)
        case label(String, highlight: Bool)
    }

    private static func draw(lefty: Bool, start: Bool, autofire: Int, font baseFont: CTFont) -> [UInt32] {
        var pixels = [UInt32](repeating: 0, count: width * height)
        let mainOn = autofire & 1 != 0, sideOn = autofire & 2 != 0
        let slot: Int32 = lefty ? 1 : 0
        func name(_ action: Int32) -> String {
            String(cString: mt_key_name(mt_key_binding(action, slot)))
        }
        let steerKeys = [MT_ACT_UP, MT_ACT_DOWN, MT_ACT_LEFT, MT_ACT_RIGHT].map { mt_key_binding($0, slot) }
        let arrows: [Int32] = [82, 81, 80, 79]   // SDL scancodes for up, down, left, right
        let steer = steerKeys == arrows ? "Arrows"
            : [MT_ACT_UP, MT_ACT_LEFT, MT_ACT_DOWN, MT_ACT_RIGHT].map { name($0) }.joined(separator: " ")
        var rows: [[Piece]] = [
            [.key(name(MT_ACT_FIRE)), .label("Fire", highlight: false),
             .key(name(MT_ACT_SIDEKICKS)), .label("Sidekicks", highlight: false),
             .key(name(MT_ACT_REAR)), .label("Rear mode", highlight: false),
             .key(name(MT_ACT_BOMB)), .label("Bomb", highlight: false)],
            [.key(steer), .label("or mouse / trackpad: steer", highlight: false), .key("P"), .label("Pause", highlight: false)],
            [.key(name(MT_ACT_AUTOFIRE_MAIN)), .label("Autofire main: " + (mainOn ? "on" : "off"), highlight: mainOn),
             .key(name(MT_ACT_AUTOFIRE_SIDEKICKS)), .label("Autofire sidekicks: " + (sideOn ? "on" : "off"), highlight: sideOn)],
        ]
        // How the special weapon fires, which autofire doesn't change.
        rows.append([.key(name(MT_ACT_FIRE)), .label("tap: special weapon (its lamp, top left, green = ready)", highlight: false)])
        // A new game's first level waits on this panel.
        if start {
            rows.append([.label("Press fire to start", highlight: true)])
        }
        pixels.withUnsafeMutableBytes { raw in
            guard let ctx = CGContext(data: raw.baseAddress, width: width, height: height, bitsPerComponent: 8,
                                      bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue)
            else { return }
            // Top-left origin, like the game.
            ctx.translateBy(x: 0, y: CGFloat(height))
            ctx.scaleBy(x: 1, y: -1)
            let W = CGFloat(width), H = CGFloat(height)

            // The panel.
            ctx.setFillColor(CGColor(red: 0.03, green: 0.04, blue: 0.08, alpha: 0.78))
            ctx.addPath(CGPath(roundedRect: CGRect(x: 1, y: 1, width: W - 2, height: H - 2), cornerWidth: 18, cornerHeight: 18, transform: nil))
            ctx.fillPath()
            ctx.setStrokeColor(CGColor(red: 0.85, green: 0.70, blue: 0.35, alpha: 0.55))
            ctx.setLineWidth(2)
            ctx.addPath(CGPath(roundedRect: CGRect(x: 2, y: 2, width: W - 4, height: H - 4), cornerWidth: 17, cornerHeight: 17, transform: nil))
            ctx.strokePath()

            // Each row centred; the font shrinks if the widest row would not fit.
            let rowH = (H - 20) / CGFloat(rows.count)
            var size: CGFloat = 26
            while size > 12 && rows.map({ rowWidth($0, size: size, font: baseFont) }).max()! > W - 40 { size -= 1 }
            for (r, row) in rows.enumerated() {
                let midY = 10 + rowH * (CGFloat(r) + 0.5)
                var x = (W - rowWidth(row, size: size, font: baseFont)) / 2
                for (i, piece) in row.enumerated() {
                    switch piece {
                    case .key(let s):
                        let line = makeLine(s, size: size * 0.85, font: baseFont, color: CGColor(red: 0.08, green: 0.08, blue: 0.10, alpha: 1))
                        let tw = lineWidth(line)
                        let capW = max(tw + size * 0.9, size * 1.5), capH = size * 1.45
                        let cap = CGRect(x: x, y: midY - capH / 2, width: capW, height: capH)
                        ctx.setFillColor(CGColor(red: 0.90, green: 0.88, blue: 0.82, alpha: 1))
                        ctx.addPath(CGPath(roundedRect: cap, cornerWidth: size * 0.25, cornerHeight: size * 0.25, transform: nil))
                        ctx.fillPath()
                        drawLine(ctx, line, x: x + (capW - tw) / 2, midY: midY)
                        x += capW + size * 0.4
                    case .label(let s, let highlight):
                        let color = highlight ? CGColor(red: 1.0, green: 0.80, blue: 0.35, alpha: 1)
                                              : CGColor(red: 0.92, green: 0.93, blue: 0.96, alpha: 1)
                        let line = makeLine(s, size: size, font: baseFont, color: color)
                        drawLine(ctx, line, x: x, midY: midY)
                        x += lineWidth(line) + (i < row.count - 1 ? size * 1.2 : 0)
                    }
                }
            }
        }
        return pixels
    }

    private static func makeLine(_ s: String, size: CGFloat, font: CTFont, color: CGColor) -> CTLine {
        let f = CTFontCreateCopyWithAttributes(font, size, nil, nil)
        let attrs: [NSAttributedString.Key: Any] = [
            NSAttributedString.Key(kCTFontAttributeName as String): f,
            NSAttributedString.Key(kCTForegroundColorAttributeName as String): color,
        ]
        return CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs))
    }

    private static func lineWidth(_ line: CTLine) -> CGFloat {
        CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
    }

    /// Draws `line` with its capitals centred on `midY` (top-left coordinates).
    private static func drawLine(_ ctx: CGContext, _ line: CTLine, x: CGFloat, midY: CGFloat) {
        var ascent: CGFloat = 0, descent: CGFloat = 0
        _ = CTLineGetTypographicBounds(line, &ascent, &descent, nil)
        let capHeight = ascent * 0.72
        ctx.saveGState()
        // Text draws upright in a flipped context only after flipping back locally.
        ctx.translateBy(x: x, y: midY + capHeight / 2)
        ctx.scaleBy(x: 1, y: -1)
        ctx.textPosition = .zero
        CTLineDraw(line, ctx)
        ctx.restoreGState()
    }

    private static func rowWidth(_ row: [Piece], size: CGFloat, font: CTFont) -> CGFloat {
        var w: CGFloat = 0
        for (i, piece) in row.enumerated() {
            switch piece {
            case .key(let s):
                let tw = lineWidth(makeLine(s, size: size * 0.85, font: font, color: CGColor(gray: 0, alpha: 1)))
                w += max(tw + size * 0.9, size * 1.5) + size * 0.4
            case .label(let s, _):
                w += lineWidth(makeLine(s, size: size, font: font, color: CGColor(gray: 0, alpha: 1))) + (i < row.count - 1 ? size * 1.2 : 0)
            }
        }
        return w
    }
}
