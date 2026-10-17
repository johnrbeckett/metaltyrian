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
    var pad: Float
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

    private let srcTex: MTLTexture
    private let brightTex: MTLTexture
    private let blurTmp: MTLTexture
    private let glowN: MTLTexture
    private let glowW: MTLTexture
    private let ambTex: MTLTexture
    private var ambFrame: UInt64 = 0

    private var indices = [UInt8](repeating: 0, count: 320 * 200)
    private var palette = [UInt32](repeating: 0, count: 256)
    private var rgba = [UInt32](repeating: 0, count: 320 * 200)
    private var frameNumber: UInt64 = 0
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
        super.init()
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        onFrame?()
        (view as? GameView)?.syncPointer()
        Gamepads.poll(active: NSApp.isActive && window?.isKeyWindow == true)
        inFlight.wait()

        // Newest frame from the game thread, palette-converted on the CPU.
        let n = indices.withUnsafeMutableBufferPointer { ip in
            palette.withUnsafeMutableBufferPointer { pp in
                mt_copy_frame(frameNumber, ip.baseAddress, pp.baseAddress)
            }
        }
        let newFrame = n != frameNumber && n != 0
        if newFrame {
            frameNumber = n
            for i in 0..<(320 * 200) {
                rgba[i] = palette[Int(indices[i])] | 0xFF00_0000
            }
            rgba.withUnsafeBytes {
                srcTex.replace(region: MTLRegionMake2D(0, 0, 320, 200), mipmapLevel: 0,
                               withBytes: $0.baseAddress!, bytesPerRow: 320 * 4)
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

        var p = DisplayParams(
            texSize: SIMD2(320, 200),
            outSize: SIMD2(Float(w), Float(h)),
            mode: Float(display.rawValue),
            hdr: hdr ? 1 : 0,
            glow: glowStrength,
            headroom: Float(window?.screen?.maximumExtendedDynamicRangeColorComponentValue ?? 1),
            vgaLines: 400,
            pad: 0
        )
        enc.setRenderPipelineState(displayPSO)
        enc.setFragmentTexture(srcTex, index: 0)
        enc.setFragmentTexture(glowN, index: 1)
        enc.setFragmentTexture(glowW, index: 2)
        enc.setFragmentBytes(&p, length: MemoryLayout<DisplayParams>.stride, index: 0)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
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
