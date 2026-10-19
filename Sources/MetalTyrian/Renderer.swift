import AppKit
import MetalKit
import QuartzCore
import SDLShim

enum DisplayMode: Int {
    case sharp = 0
    case vga = 1
}

/// The picture's shape. Raw values match MT_SETTING_ASPECT.
enum Aspect: Int, CaseIterable {
    case original = 0   // 4:3, as on a VGA monitor (pixels 1.2 times taller than wide)
    case square = 1     // 16:10, square pixels
    case exact = 2      // square pixels at the largest whole-number size that fits
}

/// What fills the space around the picture. Raw values match MT_SETTING_BORDERS.
enum Borders: Int, CaseIterable {
    case black = 0
    case ambient = 1
}

struct AmbientParams {
    var origin: SIMD2<Float>
    var size: SIMD2<Float>
}

// Must match the Metal structs in Shaders.swift.
struct DisplayParams {
    var texSize: SIMD2<Float>
    var outSize: SIMD2<Float>
    var mode: Float
    var hdr: Float
    var glow: Float
    var headroom: Float
    var vgaLines: Float
    var textOn: Float
    var titleOn: Float
    var titleFade: Float        // the title picture's palette fade, 0-1
    var logo0: SIMD4<Float>     // TYRIAN logo box, game pixels (w = 0: hidden)
    var logo1: SIMD4<Float>     // year logo box
    var aspects: SIMD4<Float>   // width/height of the background and the two logos
    var hole: SIMD4<Float>      // x, y, w, h game pixels where the game's own picture shows (w = 0: none)
    var lightRect: SIMD4<Float> // the playfield, where dynamic light falls (w = 0: none)
    var light: Float            // dynamic light gain, 0 = off
    var edges: Float            // edge smoothing strength, 0 = off
    var controlsRect: SIMD4<Float> // the controls overlay's box, game pixels
    var controlsAlpha: Float    // its opacity, 0 = hidden
}

struct EmitParams {
    var rect: SIMD4<Float>
}

struct BlurParams {
    var dir: SIMD2<Float>
    var sigma: Float
    var radius: Float
}

/// Pulls the newest 320x200 frame from the game thread and shows it with Metal.
final class Renderer: NSObject, MTKViewDelegate {
    weak var window: NSWindow?
    /// Runs on the main thread at the start of every display refresh.
    var onFrame: (() -> Void)?

    var display: DisplayMode = .sharp
    var aspect: Aspect = .square
    var borders: Borders = .black
    var hdr = false
    var glowStrength: Float = 2.0

    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let inFlight = DispatchSemaphore(value: 1)

    private let displayPSO: MTLRenderPipelineState
    private let brightPSO: MTLRenderPipelineState
    private let blurPSO: MTLRenderPipelineState
    private let ambientPSO: MTLRenderPipelineState
    private let emitPSO: MTLRenderPipelineState

    private let srcTex: MTLTexture
    private let brightTex: MTLTexture
    private let blurTmp: MTLTexture
    private let glowN: MTLTexture
    private let glowW: MTLTexture
    private let ambTex: MTLTexture
    private let emitTex: MTLTexture
    // The playfield's floating overlays (score, lives...), for their soft shadow.
    private let floatTex: MTLTexture
    private var floatMask = [UInt8](repeating: 0, count: 320 * 200)
    /// Edges: Off, Soft, Smooth.
    static let edgeStrengths: [Float] = [0, 0.5, 1]
    private let lightTex: MTLTexture
    private var lightFrame: UInt64 = 0
    // Edge smoothing (Graphics > Edges): each playfield pixel's layer and the
    // colour it covered, from the game.
    private let layerTex: MTLTexture
    private let underTex: MTLTexture
    private var layerBytes = [UInt8](repeating: 0, count: 320 * 200)
    private var underIndices = [UInt8](repeating: 0, count: 320 * 200)
    private var underRGBA = [UInt32](repeating: 0, count: 320 * 200)
    private var edgesReady = false
    // The controls overlay (ControlsOverlay.swift).
    private let controls: ControlsOverlay?
    private var ambFrame: UInt64 = 0

    private var indices = [UInt8](repeating: 0, count: 320 * 200)
    private var palette = [UInt32](repeating: 0, count: 256)
    private var rgba = [UInt32](repeating: 0, count: 320 * 200)
    private var frameNumber: UInt64 = 0

    // Crisp text (CrispText.swift).
    private let crisp = CrispText()
    private let overlayTex: MTLTexture
    private var textItems = [mt_text_item](repeating: mt_text_item(), count: Int(MT_TEXT_MAX))
    private var textCount: Int32 = 0
    private var textMask = [UInt8](repeating: 0, count: 320 * 200)
    private var fillKnown = [Bool](repeating: false, count: 320 * 200)
    private var textCursor = mt_text_cursor()

    // High-resolution title screen (John's art in App/Title).
    private var titleFrame = mt_title_frame()
    private var titleForeground = [UInt8](repeating: 0, count: 320 * 200)
    private var titleOn = false
    private var titleSeen = false
    private var titleFade: Float = 1
    private let titleTextures: [MTLTexture]?   // background, TYRIAN logo, year logo
    // Regenerated art for the other tyrian.pic pictures (App/Pictures/picNN.png).
    private let pictureTextures: [Int: MTLTexture]
    private var artTexture: MTLTexture?        // the background art the current frame shows
    private var overlay = [UInt32](repeating: 0, count: CrispText.width * CrispText.height)
    private var textOn = false
    private var glowFrame: UInt64 = 0

    struct SetupError: Error, CustomStringConvertible {
        let description: String
    }

    init(view: MTKView) throws {
        guard let device = view.device, let queue = device.makeCommandQueue() else {
            throw SetupError(description: "No Metal device")
        }
        self.device = device
        self.queue = queue

        view.colorPixelFormat = .rgba16Float
        view.colorspace = CGColorSpace(name: CGColorSpace.extendedLinearSRGB)
        (view.layer as? CAMetalLayer)?.wantsExtendedDynamicRangeContent = true
        view.clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
        // Black, not blank, before the first frame is drawn.
        view.layer?.backgroundColor = CGColor(red: 0, green: 0, blue: 0, alpha: 1)
        view.layer?.isOpaque = true
        view.preferredFramesPerSecond = 120
        // Lower latency: at most one finished frame waiting for the screen.
        (view.layer as? CAMetalLayer)?.maximumDrawableCount = 2

        let lib: MTLLibrary
        do {
            lib = try device.makeLibrary(source: ShaderSource.core, options: nil)
        } catch {
            throw SetupError(description: "Shaders failed to compile:\n\(error)")
        }
        func pso(_ f: String, _ fmt: MTLPixelFormat) throws -> MTLRenderPipelineState {
            let d = MTLRenderPipelineDescriptor()
            d.vertexFunction = lib.makeFunction(name: "fullscreen_vert")
            d.fragmentFunction = lib.makeFunction(name: f)
            d.colorAttachments[0].pixelFormat = fmt
            return try device.makeRenderPipelineState(descriptor: d)
        }
        displayPSO = try pso("display_frag", .rgba16Float)
        brightPSO = try pso("bright_frag", .rgba16Float)
        blurPSO = try pso("blur_frag", .rgba16Float)
        ambientPSO = try pso("ambient_frag", .rgba16Float)
        emitPSO = try pso("emit_frag", .rgba16Float)

        func tex(_ fmt: MTLPixelFormat, target: Bool) throws -> MTLTexture {
            let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: fmt, width: 320, height: 200, mipmapped: false)
            if target {
                d.usage = [.renderTarget, .shaderRead]
                d.storageMode = .private
            } else {
                d.usage = [.shaderRead]
            }
            guard let t = device.makeTexture(descriptor: d) else {
                throw SetupError(description: "Could not make a texture")
            }
            return t
        }
        srcTex = try tex(.bgra8Unorm, target: false)
        brightTex = try tex(.rgba16Float, target: true)
        blurTmp = try tex(.rgba16Float, target: true)
        glowN = try tex(.rgba16Float, target: true)
        glowW = try tex(.rgba16Float, target: true)
        ambTex = try tex(.rgba16Float, target: true)
        emitTex = try tex(.rgba16Float, target: true)
        floatTex = try tex(.r8Unorm, target: false)
        lightTex = try tex(.rgba16Float, target: true)
        layerTex = try tex(.r8Unorm, target: false)
        underTex = try tex(.bgra8Unorm, target: false)
        controls = ControlsOverlay(device: device)
        let od = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: CrispText.width,
                                                          height: CrispText.height, mipmapped: false)
        od.usage = [.shaderRead]
        guard let ot = device.makeTexture(descriptor: od) else {
            throw SetupError(description: "Could not make a texture")
        }
        overlayTex = ot

        let loader = MTKTextureLoader(device: device)
        var loaded: [MTLTexture]? = nil
        do {
            loaded = try ["background_planet", "logo_tyrian", "logo_2027"].map { name in
            guard let url = Bundle.main.url(forResource: name, withExtension: "png", subdirectory: "Title") else {
                throw SetupError(description: "Title art \(name) missing")
            }
            return try loader.newTexture(URL: url, options: [
                .SRGB: false,
                .generateMipmaps: true,
                .allocateMipmaps: true,
                .textureStorageMode: MTLStorageMode.private.rawValue,
            ])
        }
        } catch {
            print("MetalTyrian: title art not loaded (\(error)); the original title shows")
        }
        titleTextures = loaded
        if let t = loaded { print("MetalTyrian: title art loaded: \(t.map { "\($0.width)x\($0.height)" })") }

        var pictures: [Int: MTLTexture] = [:]
        for n in 1...14 {
            let name = String(format: "pic%02d", n)
            guard let url = Bundle.main.url(forResource: name, withExtension: "png", subdirectory: "Pictures") else { continue }
            do {
                pictures[n] = try loader.newTexture(URL: url, options: [
                    .SRGB: false,
                    .generateMipmaps: true,
                    .allocateMipmaps: true,
                    .textureStorageMode: MTLStorageMode.private.rawValue,
                ])
            } catch {
                print("MetalTyrian: picture art \(name) not loaded (\(error))")
            }
        }
        pictureTextures = pictures
        print("MetalTyrian: picture art loaded for \(pictures.keys.sorted())")
        super.init()
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        onFrame?()
        (view as? GameView)?.syncPointer()
        Gamepads.poll(active: NSApp.isActive && window?.isKeyWindow == true)
        inFlight.wait()

        // Newest frame from the game thread, palette-converted on the CPU,
        // with the text the game drew on it.
        var count: Int32 = 0
        let n = indices.withUnsafeMutableBufferPointer { ip in
            palette.withUnsafeMutableBufferPointer { pp in
                textItems.withUnsafeMutableBufferPointer { tp in
                    textMask.withUnsafeMutableBufferPointer { mp in
                      titleForeground.withUnsafeMutableBufferPointer { fgp in
                        mt_copy_frame_with_text(frameNumber, ip.baseAddress, pp.baseAddress,
                                                tp.baseAddress, MT_TEXT_MAX, &count, mp.baseAddress,
                                                &textCursor, &titleFrame, fgp.baseAddress)
                      }
                    }
                }
            }
        }
        let newFrame = n != frameNumber && n != 0
        if newFrame {
            frameNumber = n
            textCount = count
            for i in 0..<(320 * 200) {
                rgba[i] = palette[Int(indices[i])] | 0xFF00_0000
            }
            textOn = false
            if let crisp, textCount > 0 || textCursor.shown != 0 {
                if textCount > 0 { eraseOriginalText() }
                textOn = textItems.withUnsafeBufferPointer { tp in
                    crisp.render(items: UnsafeBufferPointer(rebasing: tp[0..<Int(textCount)]),
                                 palette: palette, into: &overlay)
                }
                if textCursor.shown != 0 {
                    drawCursor()
                    textOn = true
                }
            }
            if titleFrame.shown != 0 && titleFrame.picture == 0 && !titleSeen {
                titleSeen = true
                print("MetalTyrian: title screen seen; art \(titleTextures == nil ? "missing" : "ready")")
            }
            artTexture = nil
            if titleFrame.shown != 0 {
                artTexture = titleFrame.picture == 0 ? titleTextures?[0] : pictureTextures[Int(titleFrame.picture)]
            }
            titleOn = artTexture != nil
            if titleOn {
                if !textOn {
                    overlay.withUnsafeMutableBytes { _ = memset($0.baseAddress!, 0, $0.count) }
                }
                // Inside the playfield the game's frame shows as it is; marked pixels
                // there are the overlays that float, for the soft shadow.
                let hx = Int(titleFrame.hole.0), hy = Int(titleFrame.hole.1)
                let hw = Int(titleFrame.hole.2), hh = Int(titleFrame.hole.3)
                if hw > 0 && hh > 0 {
                    for y in max(0, hy)..<min(200, hy + hh) {
                        for x in max(0, hx)..<min(320, hx + hw) {
                            let k = y * 320 + x
                            // Crisp letters cast their own shadow (in the shader),
                            // so the pixel letters under them don't.
                            floatMask[k] = titleForeground[k] != 0 && !(textOn && textMask[k] != 0) ? 255 : 0
                            titleForeground[k] = 0
                        }
                    }
                    floatMask.withUnsafeBytes {
                        floatTex.replace(region: MTLRegionMake2D(0, 0, 320, 200), mipmapLevel: 0,
                                         withBytes: $0.baseAddress!, bytesPerRow: 320)
                    }
                }
                if titleFrame.smooth != 0 { drawSmoothForeground() } else { drawTitleForeground() }
                textOn = true
                // The picture fades with the palette (worked out by the game); the art fades with it.
                titleFade = min(1, Float(titleFrame.fade) / 4096)
            }
            do {
                if textOn {
                    overlay.withUnsafeBytes {
                        overlayTex.replace(region: MTLRegionMake2D(0, 0, CrispText.width, CrispText.height),
                                           mipmapLevel: 0, withBytes: $0.baseAddress!,
                                           bytesPerRow: CrispText.width * 4)
                    }
                }
            }
            rgba.withUnsafeBytes {
                srcTex.replace(region: MTLRegionMake2D(0, 0, 320, 200), mipmapLevel: 0,
                               withBytes: $0.baseAddress!, bytesPerRow: 320 * 4)
            }
            edgesReady = false
            if mt_setting(MT_SETTING_EDGES) > 0 && titleOn {
                let got = layerBytes.withUnsafeMutableBufferPointer { lp in
                    underIndices.withUnsafeMutableBufferPointer { up in
                        mt_copied_frame_edges(lp.baseAddress, up.baseAddress)
                    }
                }
                if got != 0 {
                    for i in 0..<(320 * 200) {
                        underRGBA[i] = palette[Int(underIndices[i])] | 0xFF00_0000
                    }
                    layerBytes.withUnsafeBytes {
                        layerTex.replace(region: MTLRegionMake2D(0, 0, 320, 200), mipmapLevel: 0,
                                         withBytes: $0.baseAddress!, bytesPerRow: 320)
                    }
                    underRGBA.withUnsafeBytes {
                        underTex.replace(region: MTLRegionMake2D(0, 0, 320, 200), mipmapLevel: 0,
                                         withBytes: $0.baseAddress!, bytesPerRow: 320 * 4)
                    }
                    edgesReady = true
                }
            }
        }

        guard let cmd = queue.makeCommandBuffer() else {
            inFlight.signal()
            return
        }
        cmd.addCompletedHandler { [weak self] _ in self?.inFlight.signal() }

        if hdr && glowFrame != frameNumber {
            encodePass(cmd, target: brightTex, pso: brightPSO) { enc in
                enc.setFragmentTexture(self.srcTex, index: 0)
            }
            blur(cmd, from: brightTex, to: glowN, sigma: 1.3, radius: 4)
            blur(cmd, from: brightTex, to: glowW, sigma: 4.0, radius: 12)
            glowFrame = frameNumber
        }

        // Dynamic light (Graphics > Light): only in play, inside the playfield.
        let lightGains: [Float] = [0, 22, 35, 50]   // Off, Subtle, Medium, Strong
        let lightSetting = Int(mt_setting(MT_SETTING_LIGHT))
        let lightGain = lightGains[max(0, min(lightGains.count - 1, lightSetting))]
        let playfield: SIMD4<Float> = titleFrame.shown != 0 && titleFrame.hole.2 > 0
            ? SIMD4(Float(titleFrame.hole.0), Float(titleFrame.hole.1), Float(titleFrame.hole.2), Float(titleFrame.hole.3))
            : .zero
        let lightOn = lightGain > 0 && playfield.z > 0
        if lightOn && lightFrame != frameNumber {
            encodePass(cmd, target: emitTex, pso: emitPSO) { enc in
                var e = EmitParams(rect: playfield)
                enc.setFragmentTexture(self.srcTex, index: 0)
                enc.setFragmentBytes(&e, length: MemoryLayout<EmitParams>.stride, index: 0)
            }
            blur(cmd, from: emitTex, to: lightTex, sigma: 11, radius: 33)
            lightFrame = frameNumber
        }

        if borders == .ambient && ambFrame != frameNumber {
            blur(cmd, from: srcTex, to: ambTex, sigma: 9, radius: 27)
            ambFrame = frameNumber
        }

        guard let rpd = view.currentRenderPassDescriptor, let drawable = view.currentDrawable,
              let enc = cmd.makeRenderCommandEncoder(descriptor: rpd) else {
            cmd.commit()
            return
        }

        let pic = pictureRectPixels(view.drawableSize)

        if borders == .ambient {
            var a = AmbientParams(origin: SIMD2(Float(pic.minX), Float(pic.minY)),
                                  size: SIMD2(Float(pic.width), Float(pic.height)))
            enc.setRenderPipelineState(ambientPSO)
            enc.setFragmentTexture(ambTex, index: 0)
            enc.setFragmentBytes(&a, length: MemoryLayout<AmbientParams>.stride, index: 0)
            enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        }

        let w = pic.width, h = pic.height
        enc.setViewport(MTLViewport(originX: Double(pic.minX), originY: Double(pic.minY),
                                    width: Double(w), height: Double(h), znear: 0, zfar: 1))

        // The controls overlay: as each level starts and while paused (the game says when).
        let controlsOn = controls != nil && titleOn && titleFrame.controls > 0 && titleFrame.hole.2 > 0
        if controlsOn {
            controls?.update(lefty: titleFrame.controls_lefty & 1 != 0, start: titleFrame.controls_lefty & 2 != 0,
                             autofire: Int(mt_setting(MT_SETTING_AUTOFIRE)) | (mt_setting(MT_SETTING_AUTOFIRE_SPECIALS) != 0 ? 4 : 0))
        }
        var p = DisplayParams(
            texSize: SIMD2(320, 200),
            outSize: SIMD2(Float(w), Float(h)),
            mode: Float(display.rawValue),
            hdr: hdr ? 1 : 0,
            glow: glowStrength,
            headroom: Float(window?.screen?.maximumExtendedDynamicRangeColorComponentValue ?? 1),
            vgaLines: 400,
            textOn: textOn ? 1 : 0,
            titleOn: titleOn ? 1 : 0,
            titleFade: titleFade,
            logo0: logoBox(0),
            logo1: logoBox(1),
            aspects: titleAspects(),
            hole: titleOn ? SIMD4(Float(titleFrame.hole.0), Float(titleFrame.hole.1),
                                  Float(titleFrame.hole.2), Float(titleFrame.hole.3)) : .zero,
            lightRect: lightOn ? playfield : .zero,
            light: lightOn ? lightGain : 0,
            edges: edgesReady && titleOn ? Self.edgeStrengths[max(0, min(2, Int(mt_setting(MT_SETTING_EDGES))))] : 0,
            controlsRect: SIMD4(Float(ControlsOverlay.rect.minX) + Float(titleFrame.hole.0),
                                Float(ControlsOverlay.rect.minY) + Float(titleFrame.hole.1),
                                Float(ControlsOverlay.rect.width), Float(ControlsOverlay.rect.height)),
            controlsAlpha: controlsOn ? Float(titleFrame.controls) / 255 : 0
        )
        enc.setRenderPipelineState(displayPSO)
        enc.setFragmentTexture(srcTex, index: 0)
        enc.setFragmentTexture(glowN, index: 1)
        enc.setFragmentTexture(glowW, index: 2)
        enc.setFragmentTexture(overlayTex, index: 3)
        enc.setFragmentTexture(lightTex, index: 7)
        enc.setFragmentTexture(floatTex, index: 8)
        enc.setFragmentTexture(layerTex, index: 9)
        enc.setFragmentTexture(underTex, index: 10)
        if let controls { enc.setFragmentTexture(controls.texture, index: 11) } else { enc.setFragmentTexture(srcTex, index: 11) }
        if let art = artTexture, titleFrame.picture != 0 {
            enc.setFragmentTexture(art, index: 4)
            enc.setFragmentTexture(srcTex, index: 5)
            enc.setFragmentTexture(srcTex, index: 6)
        } else if let tt = titleTextures {
            enc.setFragmentTexture(tt[0], index: 4)
            enc.setFragmentTexture(tt[1], index: 5)
            enc.setFragmentTexture(tt[2], index: 6)
        } else {
            enc.setFragmentTexture(srcTex, index: 4)
            enc.setFragmentTexture(srcTex, index: 5)
            enc.setFragmentTexture(srcTex, index: 6)
        }
        enc.setFragmentBytes(&p, length: MemoryLayout<DisplayParams>.stride, index: 0)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }

    /// On the title screen: what the game has drawn over the picture (menu,
    /// version, pointer) goes into the overlay at the game's pixel size, under
    /// any crisp text, so the high-resolution art can replace the picture.
    private func drawTitleForeground() {
        let n = CrispText.n
        do {
            for gy in 0..<200 {
                for gx in 0..<320 {
                    let k = gy * 320 + gx
                    let fg = titleForeground[k]
                    guard fg != 0, textMask[k] == 0 else { continue }
                    // 1: the game's pixel; 2-255: the art shaded darker (premultiplied black).
                    let c = fg == 1 ? (rgba[k] | 0xFF00_0000) : UInt32(min(255, Int(fg - 2) * 255 / 253)) << 24
                    for sy in 0..<n {
                        let row = (gy * n + sy) * CrispText.width + gx * n
                        for sx in 0..<n where overlay[row + sx] >> 24 == 0 { overlay[row + sx] = c }
                    }
                }
            }
        }
    }

    private func logoBox(_ i: Int) -> SIMD4<Float> {
        guard titleOn, titleFrame.picture == 0 else { return .zero }
        let shown = i == 0 ? titleFrame.logo_shown.0 : titleFrame.logo_shown.1
        guard shown != 0 else { return .zero }
        let x = i == 0 ? titleFrame.logo_x.0 : titleFrame.logo_x.1
        let y = i == 0 ? titleFrame.logo_y.0 : titleFrame.logo_y.1
        let w = i == 0 ? titleFrame.logo_w.0 : titleFrame.logo_w.1
        let h = i == 0 ? titleFrame.logo_h.0 : titleFrame.logo_h.1
        return SIMD4(Float(x), Float(y), Float(w), Float(h))
    }

    private func titleAspects() -> SIMD4<Float> {
        func a(_ t: MTLTexture) -> Float { Float(t.width) / Float(t.height) }
        if titleOn, titleFrame.picture != 0, let art = artTexture { return SIMD4(a(art), 1, 1, 1) }
        guard let tt = titleTextures else { return SIMD4(1, 1, 1, 1) }
        return SIMD4(a(tt[0]), a(tt[1]), a(tt[2]), 1)
    }

    /// Like drawTitleForeground, smoothed: each pixel the game drew over the
    /// picture becomes a 3x3 block by Scale3x (AdvMAME3x), with the picture's
    /// own pixels counting as transparent, so the HUD's bars and lights get
    /// rounded corners over the high-resolution art instead of square steps.
    private func drawSmoothForeground() {
        let n = CrispText.n   // 3
        @inline(__always) func at(_ x: Int, _ y: Int) -> UInt32 {
            guard x >= 0, x < 320, y >= 0, y < 200 else { return 0 }
            let k = y * 320 + x
            return titleForeground[k] == 1 && textMask[k] == 0 ? (rgba[k] | 0xFF00_0000) : 0
        }
        // Shaded pixels darken the art, square (they cover areas, not shapes).
        for k in 0..<(320 * 200) where titleForeground[k] >= 2 && textMask[k] == 0 {
            let a = UInt32(min(255, Int(titleForeground[k] - 2) * 255 / 253)) << 24
            let gx = k % 320, gy = k / 320
            for sy in 0..<n {
                let row = (gy * n + sy) * CrispText.width + gx * n
                for sx in 0..<n where overlay[row + sx] >> 24 == 0 { overlay[row + sx] = a }
            }
        }
        var e = [UInt32](repeating: 0, count: 9)
        for gy in 0..<200 {
            for gx in 0..<320 {
                let E = at(gx, gy)
                guard E != 0 else { continue }
                let A = at(gx - 1, gy - 1), B = at(gx, gy - 1), C = at(gx + 1, gy - 1)
                let D = at(gx - 1, gy), F = at(gx + 1, gy)
                let G = at(gx - 1, gy + 1), H = at(gx, gy + 1), I = at(gx + 1, gy + 1)
                if B != H && D != F {
                    e[0] = D == B ? D : E
                    e[1] = (D == B && E != C) || (B == F && E != A) ? B : E
                    e[2] = B == F ? F : E
                    e[3] = (D == B && E != G) || (D == H && E != A) ? D : E
                    e[4] = E
                    e[5] = (B == F && E != I) || (H == F && E != C) ? F : E
                    e[6] = D == H ? D : E
                    e[7] = (D == H && E != I) || (H == F && E != G) ? H : E
                    e[8] = H == F ? F : E
                } else {
                    for i in 0..<9 { e[i] = E }
                }
                for sy in 0..<n {
                    let row = (gy * n + sy) * CrispText.width + gx * n
                    for sx in 0..<n {
                        let c = e[sy * 3 + sx]
                        if c != 0 && overlay[row + sx] >> 24 == 0 { overlay[row + sx] = c }
                    }
                }
            }
        }
    }

    /// The game's mouse pointer, lifted out of the frame by the game while crisp
    /// text is on, drawn over the text at the game's pixel size.
    private func drawCursor() {
        let n = CrispText.n, cw = Int(MT_CURSOR_W), ch = Int(MT_CURSOR_H)
        let cx = Int(textCursor.x), cy = Int(textCursor.y)
        withUnsafeBytes(of: textCursor.pixels) { pix in
            withUnsafeBytes(of: textCursor.opaque) { op in
                for yy in 0..<ch {
                    for xx in 0..<cw {
                        let k = yy * cw + xx
                        let gx = cx + xx, gy = cy + yy
                        guard op[k] != 0, gx >= 0, gx < 320, gy >= 0, gy < 200 else { continue }
                        let c = palette[Int(pix[k])] | 0xFF00_0000
                        for sy in 0..<n {
                            let row = (gy * n + sy) * CrispText.width + gx * n
                            for sx in 0..<n { overlay[row + sx] = c }
                        }
                    }
                }
            }
        }
    }

    /// Paints out the original letters' pixels (and their shadows and outlines),
    /// so they don't show around the crisp letters drawn on top: each erased
    /// pixel takes the average of its already-known neighbours, filling in
    /// from the edges of each patch inwards.
    private func eraseOriginalText() {
        var pending: [Int] = []
        for i in 0..<(320 * 200) {
            let masked = textMask[i] != 0
            fillKnown[i] = !masked
            if masked { pending.append(i) }
        }
        var next: [Int] = []
        var updates: [(Int, UInt32)] = []
        while !pending.isEmpty {
            next.removeAll(keepingCapacity: true)
            updates.removeAll(keepingCapacity: true)
            for i in pending {
                let x = i % 320, y = i / 320
                var r = 0, g = 0, b = 0, n = 0
                for yy in max(0, y - 1)...min(199, y + 1) {
                    for xx in max(0, x - 1)...min(319, x + 1) {
                        let j = yy * 320 + xx
                        guard fillKnown[j] else { continue }
                        let c = rgba[j]
                        r += Int((c >> 16) & 0xFF); g += Int((c >> 8) & 0xFF); b += Int(c & 0xFF)
                        n += 1
                    }
                }
                if n > 0 {
                    updates.append((i, UInt32(r / n) << 16 | UInt32(g / n) << 8 | UInt32(b / n) | 0xFF00_0000))
                } else {
                    next.append(i)
                }
            }
            if updates.isEmpty {
                for i in next { rgba[i] = 0xFF00_0000 }
                break
            }
            for (i, c) in updates {
                rgba[i] = c
                fillKnown[i] = true
            }
            swap(&pending, &next)
        }
    }

    /// Where the picture sits in the drawable, in pixels, top-left origin.
    func pictureRectPixels(_ ds: CGSize) -> CGRect {
        var w: CGFloat, h: CGFloat
        switch aspect {
        case .exact:
            let k = max(1, (min(ds.width / 320, ds.height / 200)).rounded(.down))
            w = 320 * k
            h = 200 * k
        case .original, .square:
            let ratio: CGFloat = aspect == .original ? 4.0 / 3.0 : 1.6
            w = ds.width
            h = w / ratio
            if h > ds.height { h = ds.height; w = h * ratio }
            w = w.rounded()
            h = h.rounded()
        }
        let x = ((ds.width - w) / 2).rounded(.down), y = ((ds.height - h) / 2).rounded(.down)
        return CGRect(x: x, y: y, width: w, height: h)
    }

    /// Where the picture sits in the view, in view points (bottom-left origin),
    /// for mapping the mouse.
    func pictureRect(in view: MTKView) -> CGRect {
        let b = view.bounds
        let ds = view.drawableSize
        guard b.width > 0, b.height > 0, ds.width > 0, ds.height > 0 else { return .zero }
        let sx = ds.width / b.width, sy = ds.height / b.height
        let p = pictureRectPixels(ds)
        return CGRect(x: p.minX / sx, y: b.height - p.maxY / sy, width: p.width / sx, height: p.height / sy)
    }

    private func encodePass(_ cmd: MTLCommandBuffer, target: MTLTexture, pso: MTLRenderPipelineState,
                            setup: (MTLRenderCommandEncoder) -> Void) {
        let rpd = MTLRenderPassDescriptor()
        rpd.colorAttachments[0].texture = target
        rpd.colorAttachments[0].loadAction = .dontCare
        rpd.colorAttachments[0].storeAction = .store
        guard let enc = cmd.makeRenderCommandEncoder(descriptor: rpd) else { return }
        enc.setRenderPipelineState(pso)
        setup(enc)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
    }

    private func blur(_ cmd: MTLCommandBuffer, from: MTLTexture, to: MTLTexture, sigma: Float, radius: Float) {
        for (dir, src, dst) in [(SIMD2<Float>(1, 0), from, blurTmp), (SIMD2<Float>(0, 1), blurTmp, to)] {
            encodePass(cmd, target: dst, pso: blurPSO) { enc in
                var b = BlurParams(dir: dir, sigma: sigma, radius: radius)
                enc.setFragmentTexture(src, index: 0)
                enc.setFragmentBytes(&b, length: MemoryLayout<BlurParams>.stride, index: 0)
            }
        }
    }
}
