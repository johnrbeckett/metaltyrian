import CoreGraphics
import CoreText
import Foundation
import SDLShim

/// Crisp text: Orbitron drawn over Tyrian's lettering, fitted to the original
/// letters' pixel area, on a grid 3 times finer than the game's pixels, with
/// Tyrian-style shading (lower and right stroke edges darker).
///
/// The game reports what text it drew and where (Engine/src/mt_text.c); the
/// Renderer erases the original letters' pixels and composites this overlay.
final class CrispText {
    static let n = 3
    static let width = 320 * n, height = 200 * n

    /// A hard-edged letter mask on the fine grid. Row 0 is the cap line.
    private struct Mask {
        var w = 0, h = 0
        var capRows = 1                // rows from the top of the mask to the baseline
        var bits: [Bool] = []
    }

    private struct Draw {
        var x: Double, y: Double       // game pixels: left edge, cap line
        var mask: Mask
        var hue: Int, level: Int
        var outlineDark: UInt32, outlineBlack: UInt32   // see mt_text_item
        var outlineLevel: Int = 255
        var clipTop = Int.min, clipBottom = Int.max   // fine-grid rows shown
        /// Per fine-grid column from colOrigin: the rows shown there, for words
        /// with letters partly hidden (cut by a window edge, a box, a sprite).
        var colOrigin = 0
        var colClip: [(top: Int, bottom: Int)]? = nil
    }

    private let font: CTFont           // Orbitron at size 100, for measuring
    private let capInk: CGFloat        // height of "H" at size 100
    private let capTop: CGFloat        // top of "H" above the baseline at size 100
    private var cache: [String: Mask] = [:]

    init?() {
        guard let url = Bundle.main.url(forResource: "Orbitron-Bold", withExtension: "ttf", subdirectory: "Fonts")
                ?? Bundle.main.url(forResource: "Orbitron-Bold", withExtension: "ttf"),
              let descs = CTFontManagerCreateFontDescriptorsFromURL(url as CFURL) as? [CTFontDescriptor],
              let desc = descs.first else {
            print("MetalTyrian: Orbitron not found; crisp text is off")
            return nil
        }
        font = CTFontCreateWithFontDescriptor(desc, 100, nil)
        var ch: [UniChar] = [UniChar(UInt8(ascii: "H"))]
        var g: CGGlyph = 0
        CTFontGetGlyphsForCharacters(font, &ch, &g, 1)
        let r = CTFontGetBoundingRectsForGlyphs(font, .horizontal, &g, nil, 1)
        capInk = r.height
        capTop = r.maxY
    }

    /// Draws the frame's text into `out` (premultiplied BGRA, width x height).
    /// Returns false when there is nothing to draw.
    func render(items: UnsafeBufferPointer<mt_text_item>, palette: [UInt32], into out: inout [UInt32]) -> Bool {
        let draws = layout(items)
        out.withUnsafeMutableBytes { _ = memset($0.baseAddress!, 0, $0.count) }
        if draws.isEmpty { return false }
        if cache.count > 4000 { cache.removeAll() }
        let n = Double(Self.n)
        out.withUnsafeMutableBufferPointer { buf in
            // Shadows and outlines first, so no letter is darkened by a neighbour's.
            for d in draws where d.outlineDark | d.outlineBlack != 0 {
                setClip(d)
                defer { clearClip() }
                // An outline all round (title menu, score): grow the crisp letter
                // by a round brush, so it reads as one clean contour instead of
                // offset copies of thin strokes.
                if d.outlineBlack.nonzeroBitCount >= 2 {
                    let c = d.outlineLevel == 255 ? 0 : palette[d.hue * 16 + min(15, d.outlineLevel)] & 0xFF_FFFF
                    let x0 = Int((d.x * n).rounded()), y0 = Int((d.y * n).rounded())
                    for (dx, dy) in Self.brush {
                        forEach(d.mask, x0 + dx, y0 + dy) { i, _, _ in buf[i] = c | 0xFF00_0000 }
                    }
                }
                for bit in 0..<25 {
                    let dark = d.outlineDark & (1 << bit) != 0, black = d.outlineBlack & (1 << bit) != 0
                    if !dark && !black { continue }
                    let ox = Int((d.x * n).rounded()) + (bit % 5 - 2) * Self.n
                    let oy = Int((d.y * n).rounded()) + (bit / 5 - 2) * Self.n
                    if black && d.outlineBlack.nonzeroBitCount >= 2 { continue }   // drawn above
                    if black {
                        // Solid: black, or the letter's own colour drawn dark (title menu).
                        let c = d.outlineLevel == 255 ? 0 : palette[d.hue * 16 + min(15, d.outlineLevel)] & 0xFF_FFFF
                        forEach(d.mask, ox, oy) { i, _, _ in buf[i] = c | 0xFF00_0000 }
                        continue
                    }
                    let a: UInt32 = 150, k: UInt32 = 255 - a
                    forEach(d.mask, ox, oy) { i, _, _ in
                        let p = buf[i]
                        let b = (p & 0xFF) * k / 255, g = ((p >> 8) & 0xFF) * k / 255
                        let r = ((p >> 16) & 0xFF) * k / 255, al = a + ((p >> 24) & 0xFF) * k / 255
                        buf[i] = b | g << 8 | r << 16 | al << 24
                    }
                }
            }
            for d in draws {
                setClip(d)
                defer { clearClip() }
                let ox = Int((d.x * n).rounded()), oy = Int((d.y * n).rounded())
                func colour(_ level: Int) -> SIMD3<Float> {
                    let c = palette[d.hue * 16 + min(15, max(0, level))]
                    return SIMD3(Float((c >> 16) & 0xFF), Float((c >> 8) & 0xFF), Float(c & 0xFF))
                }
                let top = colour(d.level), bottom = colour(d.level - 2), rim = colour(d.level - 5)
                let m = d.mask
                let span = Float(max(m.capRows - 1, 1))
                forEach(m, ox, oy) { i, mx, my in
                    var c = top + (bottom - top) * min(1, Float(my) / span)
                    // Lower and right edges of each stroke are darker, as Tyrian's art shades them.
                    let below = my + 1 < m.h && m.bits[(my + 1) * m.w + mx]
                    let right = mx + 1 < m.w && m.bits[my * m.w + mx + 1]
                    if !below || !right { c = c * 0.5 + rim * 0.5 }
                    let q = (c + 0.5).clamped(lowerBound: SIMD3(repeating: 0), upperBound: SIMD3(repeating: 255))
                    buf[i] = UInt32(q.z) | UInt32(q.y) << 8 | UInt32(q.x) << 16 | 0xFF00_0000
                }
            }
        }
        return true
    }

    /// Offsets (fine pixels) of a round brush 2.5 fine pixels in radius.
    private static let brush: [(Int, Int)] = {
        var b: [(Int, Int)] = []
        for dy in -3...3 { for dx in -3...3 where (dx != 0 || dy != 0) && dx * dx + dy * dy <= 6 { b.append((dx, dy)) } }
        return b
    }()

    private var clipTop = Int.min, clipBottom = Int.max
    private var colOrigin = 0
    private var colClip: [(top: Int, bottom: Int)]? = nil

    private func setClip(_ d: Draw) {
        clipTop = d.clipTop; clipBottom = d.clipBottom
        colOrigin = d.colOrigin; colClip = d.colClip
    }

    private func clearClip() {
        clipTop = Int.min; clipBottom = Int.max
        colClip = nil
    }

    private func forEach(_ m: Mask, _ ox: Int, _ oy: Int, _ body: (Int, Int, Int) -> Void) {
        for my in 0..<m.h {
            let y = oy + my
            if y < 0 || y >= Self.height || y < clipTop || y >= clipBottom { continue }
            for mx in 0..<m.w where m.bits[my * m.w + mx] {
                let x = ox + mx
                if x < 0 || x >= Self.width { continue }
                if let cc = colClip {
                    let c = x - colOrigin
                    if c >= 0 && c < cc.count && (y < cc[c].top || y >= cc[c].bottom) { continue }
                }
                body(y * Self.width + x, mx, my)
            }
        }
    }

    // MARK: - Layout

    private func text(of item: mt_text_item) -> String {
        withUnsafeBytes(of: item.text) { raw in
            String(decoding: raw.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
    }

    private func layout(_ items: UnsafeBufferPointer<mt_text_item>) -> [Draw] {
        var draws: [Draw] = []
        // Letters: join neighbours on the same line into words, so Orbitron's
        // own spacing and shapes are kept, then fit each word to its letters'
        // area. Spaces between words stay where the game put them.
        let letters = items.filter { Int($0.kind) == Int(MT_TEXT_LETTER) && $0.text.0 != 0 }
            .sorted { ($0.y, $0.x) < ($1.y, $1.x) }
        var i = 0
        while i < letters.count {
            let first = letters[i]
            var s = text(of: first)
            var last = first
            var j = i + 1
            while j < letters.count {
                let l = letters[j]
                guard l.y == first.y, l.font == first.font, l.hue == first.hue,
                      l.level == first.level, l.outline_dark == first.outline_dark,
                      l.outline_black == first.outline_black,
                      l.outline_level == first.outline_level else { break }
                let gap = Int(l.x) - (Int(last.x) + Int(last.w))
                if gap < -1 || gap > 2 { break }
                s += text(of: l)
                last = l
                j += 1
            }
            let w = Double(Int(last.x) + Int(last.w) - Int(first.x))
            // Menu titles (the large font): a touch smaller, centred on the
            // original word and hung from its top, so tails on p, g and y
            // stay inside the title boxes.
            let shrink = first.font == 0 ? 0.90 : 1.0
            if let m = mask(s, width: w * shrink, cap: Double(first.cap) * shrink, maxSqueeze: nil) {
                var d = Draw(x: Double(first.x) + w * (1 - shrink) / 2, y: Double(first.y), mask: m,
                             hue: Int(first.hue), level: Int(first.level),
                             outlineDark: first.outline_dark, outlineBlack: first.outline_black,
                             outlineLevel: Int(first.outline_level))
                // Letters partly hidden: hide the same part of the crisp word,
                // column by column across each letter's cell.
                if letters[i..<j].contains(where: { $0.clip_y0 >= 0 || $0.clip_y1 >= 0 || $0.clip_x0 >= 0 || $0.clip_x1 >= 0 }) {
                    let n = Self.n
                    let origin = Int(first.x) * n
                    var cc = [(top: Int, bottom: Int)](repeating: (Int.min, Int.max), count: (Int(w) + 2) * n)
                    for l in letters[i..<j] {
                        let top = l.clip_y0 >= 0 ? Int(l.clip_y0) * n : Int.min
                        let bottom = l.clip_y1 >= 0 ? Int(l.clip_y1) * n : Int.max
                        let cellStart = Int(l.x) * n, cellEnd = (Int(l.x) + Int(l.w) + 1) * n
                        let showStart = l.clip_x0 >= 0 ? Int(l.clip_x0) * n : cellStart
                        let showEnd = l.clip_x1 >= 0 ? Int(l.clip_x1) * n : cellEnd
                        for fx in cellStart..<cellEnd {
                            let c = fx - origin
                            guard c >= 0 && c < cc.count else { continue }
                            cc[c] = fx >= showStart && fx < showEnd ? (top, bottom) : (Int.max, Int.min)
                        }
                    }
                    d.colOrigin = origin
                    d.colClip = cc
                }
                draws.append(d)
            }
            i = j
        }

        for it in items {
            let kind = Int(it.kind)
            if kind == Int(MT_TEXT_LABEL) {
                // Half a pixel of margin inside the original lettering's box.
                if let m = mask(text(of: it), width: Double(it.w) - 0.5, cap: Double(it.h), maxSqueeze: 0.7) {
                    draws.append(Draw(x: Double(it.x) + 0.5, y: Double(it.y), mask: m,
                                      hue: Int(it.hue), level: Int(it.level), outlineDark: 0, outlineBlack: 0))
                }
            } else if kind == Int(MT_TEXT_LABEL_V) {
                // Letters stacked 6 pixels apart, 5 high, each centred in its column.
                let centre = Double(it.x) + Double(it.w) / 2
                for (k, ch) in text(of: it).enumerated() {
                    let s = String(ch)
                    let natural = naturalWidth(s) / capInk * 5
                    let w = min(5.0, min(Double(it.w), natural))
                    if let m = mask(s, width: w, cap: 5, maxSqueeze: nil) {
                        draws.append(Draw(x: centre - w / 2, y: Double(it.y) + Double(k * 6), mask: m,
                                          hue: Int(it.hue), level: Int(it.level), outlineDark: 0, outlineBlack: 0))
                    }
                }
            }
        }
        return draws
    }

    // MARK: - Masks

    private func line(_ s: String, size: CGFloat) -> CTLine {
        let f = CTFontCreateCopyWithAttributes(font, size, nil, nil)
        let attrs: [NSAttributedString.Key: Any] = [
            NSAttributedString.Key(kCTFontAttributeName as String): f,
            NSAttributedString.Key(kCTForegroundColorFromContextAttributeName as String): true,
        ]
        return CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs))
    }

    private func naturalWidth(_ s: String) -> Double {
        Double(CTLineGetBoundsWithOptions(line(s, size: 100), .useGlyphPathBounds).width)
    }

    /// Orbitron `s` fitted to `width` x `cap` game pixels: the ink fills the
    /// width exactly and the capitals fill the cap height. With `maxSqueeze`,
    /// text that would be squeezed harder is shrunk instead, keeping its baseline.
    private func mask(_ s: String, width: Double, cap: Double, maxSqueeze: Double?) -> Mask? {
        guard !s.isEmpty, width > 0, cap > 0 else { return nil }
        let key = "\(s)|\(Int(width * 8))|\(Int(cap * 8))|\(maxSqueeze ?? 0)"
        if let m = cache[key] { return m }

        let n = Double(Self.n)
        let W = width * n, Hc = cap * n
        let ink100 = CTLineGetBoundsWithOptions(line(s, size: 100), .useGlyphPathBounds)
        guard ink100.width > 0 else { return nil }
        var k = Hc / Double(capInk)                     // size-100 units -> fine pixels
        var sx = W / (Double(ink100.width) * k)
        var off = 0.0
        if let ms = maxSqueeze, sx < ms {
            let k2 = sx / ms
            k *= k2
            off = Hc - Hc * k2
            sx = ms
        }
        // Single narrow letters: don't stretch them wide, centre them instead.
        sx = min(sx, 1.35)
        let drawnW = Double(ink100.width) * k * sx
        let pad = max(0, (W - drawnW) / 2)

        let w = max(1, Int(W.rounded(.up)))
        let h = max(1, Int((off + Hc * 1.7).rounded(.up)))
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w,
                                  space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue) else {
            return nil
        }
        ctx.setFillColor(gray: 0, alpha: 1)
        ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
        ctx.setFillColor(gray: 1, alpha: 1)
        ctx.setShouldSmoothFonts(false)
        ctx.setAllowsAntialiasing(true)
        ctx.setShouldAntialias(true)

        let size = CGFloat(100 * k)
        let ln = line(s, size: size)
        let ink = CTLineGetBoundsWithOptions(ln, .useGlyphPathBounds)
        let baseline = CGFloat(h) - CGFloat(off) - capTop * CGFloat(k)
        ctx.translateBy(x: CGFloat(pad) - ink.minX * CGFloat(sx), y: baseline)
        ctx.scaleBy(x: CGFloat(sx), y: 1)
        ctx.textPosition = .zero
        CTLineDraw(ln, ctx)

        guard let data = ctx.data else { return nil }
        let px = data.bindMemory(to: UInt8.self, capacity: w * h)
        var m = Mask(w: w, h: h, capRows: Int((off + Hc).rounded()), bits: [Bool](repeating: false, count: w * h))
        var lastRow = -1
        for y in 0..<h {
            for x in 0..<w where px[y * w + x] > 96 {
                m.bits[y * w + x] = true
                lastRow = y
            }
        }
        // Trim empty rows below the letters.
        if lastRow >= 0 && lastRow + 1 < h {
            m.h = lastRow + 1
            m.bits = Array(m.bits[0..<(m.h * w)])
        }
        cache[key] = m
        return m
    }
}
