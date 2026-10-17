import AppKit
import MetalKit
import QuartzCore
import SDLShim

enum DisplayMode: Int {
    case sharp = 0
    case vga = 1
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

    var display: DisplayMode = .sharp
    var hdr = false
    var glowStrength: Float = 1.2

    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let inFlight = DispatchSemaphore(value: 1)

    private let displayPSO: MTLRenderPipelineState
    private let brightPSO: MTLRenderPipelineState
    private let blurPSO: MTLRenderPipelineState

    private let srcTex: MTLTexture
    private let brightTex: MTLTexture
    private let blurTmp: MTLTexture
    private let glowN: MTLTexture
    private let glowW: MTLTexture

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
        super.init()
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
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

        guard let rpd = view.currentRenderPassDescriptor, let drawable = view.currentDrawable,
              let enc = cmd.makeRenderCommandEncoder(descriptor: rpd) else {
            cmd.commit()
            return
        }

        // Letterbox the 16:10 picture into the drawable.
        let ds = view.drawableSize
        var w = ds.width, h = ds.width / 1.6
        if h > ds.height { h = ds.height; w = h * 1.6 }
        let ox = ((ds.width - w) / 2).rounded(), oy = ((ds.height - h) / 2).rounded()
        w = w.rounded(); h = h.rounded()
        enc.setViewport(MTLViewport(originX: Double(ox), originY: Double(oy), width: Double(w), height: Double(h), znear: 0, zfar: 1))

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

    /// Where the game picture sits in the view, in view points, for mapping the mouse.
    func pictureRect(in bounds: CGRect) -> CGRect {
        var w = bounds.width, h = bounds.width / 1.6
        if h > bounds.height { h = bounds.height; w = h * 1.6 }
        return CGRect(x: (bounds.width - w) / 2, y: (bounds.height - h) / 2, width: w, height: h)
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
