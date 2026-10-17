// MetalTyrian, phase 0: the app shell. Opens a Metal window and checks that
// the Tyrian 2000 data is inside the app. The game arrives in phase 1.

import AppKit
import MetalKit

/// Finds the Tyrian 2000 data: inside the app bundle, or a folder given on the command line.
enum GameData {
    static func locate() -> URL? {
        let args = CommandLine.arguments
        if args.count > 1 {
            return URL(fileURLWithPath: args[1], isDirectory: true)
        }
        return Bundle.main.resourceURL?.appendingPathComponent("GameData", isDirectory: true)
    }

    /// Episodes present, by the same test the game uses: levelsN.dat exists.
    static func episodes(in dir: URL) -> [Int] {
        (1...5).filter {
            FileManager.default.fileExists(atPath: dir.appendingPathComponent("levels\($0).dat").path)
        }
    }
}

final class ClearRenderer: NSObject, MTKViewDelegate {
    private let queue: MTLCommandQueue

    init?(device: MTLDevice) {
        guard let q = device.makeCommandQueue() else { return nil }
        queue = q
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        guard let rpd = view.currentRenderPassDescriptor,
              let drawable = view.currentDrawable,
              let cmd = queue.makeCommandBuffer(),
              let enc = cmd.makeRenderCommandEncoder(descriptor: rpd) else { return }
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var renderer: ClearRenderer!

    func applicationDidFinishLaunching(_ notification: Notification) {
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"

        var status = "no game data found"
        if let dir = GameData.locate() {
            let eps = GameData.episodes(in: dir)
            status = eps.isEmpty ? "no game data in \(dir.path)" : "Tyrian 2000 data: episodes \(eps.map(String.init).joined(separator: ", "))"
        }

        guard let device = MTLCreateSystemDefaultDevice(), let r = ClearRenderer(device: device) else {
            let alert = NSAlert()
            alert.messageText = "MetalTyrian needs a Mac with Metal."
            alert.runModal()
            exit(1)
        }
        renderer = r

        let rect = NSRect(x: 0, y: 0, width: 1280, height: 800)
        let view = MTKView(frame: rect, device: device)
        view.clearColor = MTLClearColor(red: 0.02, green: 0.02, blue: 0.05, alpha: 1)
        view.delegate = renderer

        window = NSWindow(contentRect: rect,
                          styleMask: [.titled, .closable, .miniaturizable, .resizable],
                          backing: .buffered, defer: false)
        window.title = "MetalTyrian \(version)   |   \(status)"
        window.contentView = view
        window.collectionBehavior = [.fullScreenPrimary]
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        print("MetalTyrian \(version): \(status)")
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)

let mainMenu = NSMenu()
let appItem = NSMenuItem()
mainMenu.addItem(appItem)
let appMenu = NSMenu()
appMenu.addItem(withTitle: "Quit MetalTyrian", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
appItem.submenu = appMenu
app.mainMenu = mainMenu

let delegate = AppDelegate()
app.delegate = delegate
app.run()
