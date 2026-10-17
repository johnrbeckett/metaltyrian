import AVFoundation
import SDLShim

/// Plays the game's sound: AVAudioEngine pulls mono samples from the game's
/// own mixer (music from the emulated AdLib, plus effects) through the shim.
final class Audio {
    private let engine = AVAudioEngine()
    private var source: AVAudioSourceNode!
    private var observer: NSObjectProtocol?

    init() {
        let format = AVAudioFormat(standardFormatWithSampleRate: Double(MT_AUDIO_RATE), channels: 1)!
        source = AVAudioSourceNode(format: format) { _, _, frameCount, bufferList -> OSStatus in
            // Real-time thread: only the C call, no Swift allocation.
            let buffers = UnsafeMutableAudioBufferListPointer(bufferList)
            if let data = buffers[0].mData {
                mt_audio_render(data.assumingMemoryBound(to: Float.self), Int32(frameCount))
            }
            return noErr
        }
        engine.attach(source)
        engine.connect(source, to: engine.mainMixerNode, format: format)

        // Headphones in or out, or a new output device: macOS stops the engine
        // and expects it to be started again.
        observer = NotificationCenter.default.addObserver(
            forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main
        ) { [weak self] _ in
            self?.start()
        }
    }

    func start() {
        guard !engine.isRunning else { return }
        engine.prepare()
        do {
            try engine.start()
        } catch {
            print("MetalTyrian: sound could not start: \(error)")
        }
    }

    /// Silent while MetalTyrian is in the background; the game keeps running.
    var muted = false {
        didSet { engine.mainMixerNode.outputVolume = muted ? 0 : 1 }
    }
}
