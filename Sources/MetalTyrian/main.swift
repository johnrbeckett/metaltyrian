// MetalTyrian: Tyrian 2000 on OpenTyrian2000's game code, drawn with Metal.
//
// The game runs on its own thread (see SDLShim). This file sets up the app,
// the window and the menus, then starts the game.

import AppKit
import MetalKit
import SDLShim

/// Finds the Tyrian 2000 data: inside the app bundle, or a folder given on the command line.
enum GameData {
    static func locate() -> URL? {
        let args = CommandLine.arguments
        if args.count > 1, !args[1].hasPrefix("-") {
            return URL(fileURLWithPath: args[1], isDirectory: true)
        }
        return Bundle.main.resourceURL?.appendingPathComponent("GameData", isDirectory: true)
    }

    static func isPresent(in dir: URL) -> Bool {
        FileManager.default.fileExists(atPath: dir.appendingPathComponent("tyrian1.lvl").path)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    var window: NSWindow!
    var view: GameView!
    var renderer: Renderer!
    var audio: Audio!

    private let defaults = UserDefaults.standard
    private var sharpItem: NSMenuItem!
    private var vgaItem: NSMenuItem!
    private var hdrItem: NSMenuItem!
    private var glowItems: [NSMenuItem] = []
    private var aspectItems: [NSMenuItem] = []
    private var borderItems: [NSMenuItem] = []
    private var crispItem: NSMenuItem?
    static let glowLevels: [Float] = [1.0, 1.2, 1.5, 2.0]
    private var fullScreenChanging = false
    /// Setup > Controls in the game; saved here.
    static let speedSettings: [(Int32, String)] = [(MT_SETTING_KEY_SPEED, "keySpeed"),
                                                   (MT_SETTING_PAD_SPEED, "padSpeed"),
                                                   (MT_SETTING_MOUSE_SPEED, "mouseSpeed"),
                                                   (MT_SETTING_MOMENTUM, "momentum"),
                                                   (MT_SETTING_MOUSE_MODE, "mouseMode"),
                                                   (MT_SETTING_STEREO, "stereo"),
                                                   (MT_SETTING_LIGHT, "light")]

    func applicationDidFinishLaunching(_ notification: Notification) {
        guard let dataDir = GameData.locate(), GameData.isPresent(in: dataDir) else {
            fail("The Tyrian 2000 game data is missing from the app.")
        }
        guard let device = MTLCreateSystemDefaultDevice() else {
            fail("MetalTyrian needs a Mac with Metal.")
        }

        let rect = NSRect(x: 0, y: 0, width: 1280, height: 800)
        view = GameView(frame: rect, device: device)
        do {
            renderer = try Renderer(view: view)
        } catch {
            fail("\(error)")
        }
        // First-run look, John's pick 2026-10-17: authentic VGA with HDR glow at 2.0.
        defaults.register(defaults: ["displayMode": DisplayMode.vga.rawValue, "hdrGlow": true, "glowStrength": Float(2.0),
                                     "aspect": Aspect.square.rawValue, "borders": Borders.black.rawValue,
                                     // Controls, John's picks 2026-10-18: fast keys, mouse a bit quicker, no drift.
                                     "keySpeed": 5, "padSpeed": 5, "mouseSpeed": 7,
                                     "momentum": 2,     // Off
                                     "mouseMode": 1,    // Direct
                                     "text": 0,         // Crisp
                                     "stereo": 0,       // Stereo
                                     "light": 2])       // Medium
        mt_set_setting(MT_SETTING_TEXT, Int32(defaults.integer(forKey: "text")))
        for (setting, key) in Self.speedSettings {
            mt_set_setting(setting, Int32(defaults.integer(forKey: key)))
        }
        renderer.display = DisplayMode(rawValue: defaults.integer(forKey: "displayMode")) ?? .vga
        renderer.hdr = defaults.bool(forKey: "hdrGlow")
        renderer.aspect = Aspect(rawValue: defaults.integer(forKey: "aspect")) ?? .square
        renderer.borders = Borders(rawValue: defaults.integer(forKey: "borders")) ?? .black
        let savedGlow = defaults.float(forKey: "glowStrength")
        renderer.glowStrength = Self.glowLevels.contains(savedGlow) ? savedGlow : 2.0
        view.delegate = renderer
        view.pictureRect = { [unowned self] in self.renderer.pictureRect(in: self.view) }
        renderer.onFrame = { [unowned self] in self.followGameSettings() }

        window = NSWindow(contentRect: rect,
                          styleMask: [.titled, .closable, .miniaturizable, .resizable],
                          backing: .buffered, defer: false)
        window.title = "MetalTyrian"
        // A game window: black title bar with white text, in light mode too.
        window.appearance = NSAppearance(named: .darkAqua)
        window.titlebarAppearsTransparent = true
        window.backgroundColor = .black
        window.contentView = view
        window.contentMinSize = NSSize(width: 640, height: 400)
        window.collectionBehavior = [.fullScreenPrimary]
        window.delegate = self
        window.center()
        renderer.window = window

        buildMenus()
        updateMenuChecks()
        // Back to full screen if that's how the game was left.
        mt_set_setting(MT_SETTING_FULLSCREEN, defaults.bool(forKey: "fullScreen") ? 1 : 0)

        // Saves and settings: ~/Library/Application Support/MetalTyrian.
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("MetalTyrian", isDirectory: true)
        try? FileManager.default.createDirectory(at: support, withIntermediateDirectories: true)
        setenv("XDG_CONFIG_HOME", support.path, 1)

        Gamepads.start()
        view.startRawMouse()
        audio = Audio()
        audio.start()

        // Title screen corner: "Metal" and the app's version.
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        mt_set_title_version(version.isEmpty ? "Metal" : "Metal \(version)")

        let args = ["metaltyrian", "-t", dataDir.path]
        var cargs = args.map { strdup($0) }
        cargs.withUnsafeMutableBufferPointer { buf in
            buf.baseAddress!.withMemoryRebound(to: UnsafePointer<CChar>?.self, capacity: buf.count) {
                mt_start_game(Int32(args.count), $0)
            }
        }
        cargs.forEach { free($0) }

        window.makeKeyAndOrderFront(nil)
        window.makeFirstResponder(view)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    /// Quitting from the Mac (Command-Q or the close button) asks the game to
    /// quit, so it saves its settings, high scores and saved games first.
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if mt_game_finished() { return .terminateNow }
        mt_push_quit()
        // If the game is busy and doesn't get to it, quit anyway.
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
            NSApp.reply(toApplicationShouldTerminate: true)
        }
        return .terminateLater
    }

    func applicationWillTerminate(_ notification: Notification) {
        view?.releaseAllKeys()  // also frees the pointer if the game had it
    }

    func applicationDidResignActive(_ notification: Notification) {
        view?.releaseAllKeys()
        audio?.muted = true
    }

    func applicationDidBecomeActive(_ notification: Notification) {
        audio?.muted = false
    }

    // MARK: - Focus

    func windowDidBecomeKey(_ notification: Notification) {
        mt_push_focus(true)
    }

    func windowDidResignKey(_ notification: Notification) {
        view?.releaseAllKeys()
        mt_push_focus(false)
    }

    // MARK: - Full screen

    func windowWillEnterFullScreen(_ notification: Notification) { fullScreenChanging = true }
    func windowWillExitFullScreen(_ notification: Notification) { fullScreenChanging = true }

    func windowDidEnterFullScreen(_ notification: Notification) { fullScreenChanged(true) }
    func windowDidExitFullScreen(_ notification: Notification) { fullScreenChanged(false) }
    func windowDidFailToEnterFullScreen(_ window: NSWindow) { fullScreenChanged(false) }
    func windowDidFailToExitFullScreen(_ window: NSWindow) { fullScreenChanged(true) }

    private func fullScreenChanged(_ on: Bool) {
        fullScreenChanging = false
        mt_set_setting(MT_SETTING_FULLSCREEN, on ? 1 : 0)
        defaults.set(on, forKey: "fullScreen")
    }

    // MARK: - Settings shared with the game's Graphics menu

    /// Applies any change made in the game's Graphics menu (or Alt+Return).
    private func followGameSettings() {
        if mt_game_finished() {
            NSApp.terminate(nil)
            return
        }
        var changed = false

        let look = DisplayMode(rawValue: Int(mt_setting(MT_SETTING_LOOK))) ?? renderer.display
        if look != renderer.display {
            renderer.display = look
            defaults.set(look.rawValue, forKey: "displayMode")
            changed = true
        }

        let glow = Int(mt_setting(MT_SETTING_GLOW))
        if glow != glowSetting() {
            renderer.hdr = glow > 0
            if glow > 0, glow <= Self.glowLevels.count {
                renderer.glowStrength = Self.glowLevels[glow - 1]
                defaults.set(renderer.glowStrength, forKey: "glowStrength")
            }
            defaults.set(renderer.hdr, forKey: "hdrGlow")
            changed = true
        }

        if let a = Aspect(rawValue: Int(mt_setting(MT_SETTING_ASPECT))), a != renderer.aspect {
            renderer.aspect = a
            defaults.set(a.rawValue, forKey: "aspect")
            changed = true
        }

        if let b = Borders(rawValue: Int(mt_setting(MT_SETTING_BORDERS))), b != renderer.borders {
            renderer.borders = b
            defaults.set(b.rawValue, forKey: "borders")
            changed = true
        }

        let text = Int(mt_setting(MT_SETTING_TEXT))
        if text != defaults.integer(forKey: "text") {
            defaults.set(text, forKey: "text")
            changed = true
        }

        if changed { updateMenuChecks() }

        for (setting, key) in Self.speedSettings {
            let v = Int(mt_setting(setting))
            if v != defaults.integer(forKey: key) { defaults.set(v, forKey: key) }
        }

        let wantFull = mt_setting(MT_SETTING_FULLSCREEN) != 0
        if !fullScreenChanging, wantFull != window.styleMask.contains(.fullScreen) {
            fullScreenChanging = true
            window.toggleFullScreen(nil)
        }
    }

    /// The glow as the game's menu numbers it: 0 off, 1-4 the strengths.
    private func glowSetting() -> Int {
        guard renderer.hdr, let i = Self.glowLevels.firstIndex(of: renderer.glowStrength) else { return 0 }
        return i + 1
    }

    // MARK: - Menus

    private func buildMenus() {
        let main = NSMenu()

        let appItem = NSMenuItem()
        main.addItem(appItem)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "About MetalTyrian", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Hide MetalTyrian", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Quit MetalTyrian", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu

        let viewItem = NSMenuItem()
        main.addItem(viewItem)
        let viewMenu = NSMenu(title: "View")
        sharpItem = viewMenu.addItem(withTitle: "Sharp", action: #selector(chooseSharp(_:)), keyEquivalent: "1")
        vgaItem = viewMenu.addItem(withTitle: "Retro", action: #selector(chooseVGA(_:)), keyEquivalent: "2")
        viewMenu.addItem(.separator())
        hdrItem = viewMenu.addItem(withTitle: "Glow", action: #selector(toggleHDR(_:)), keyEquivalent: "g")
        let glowMenu = NSMenu(title: "Glow Strength")
        for level in Self.glowLevels {
            let item = glowMenu.addItem(withTitle: String(format: "%.1f", level), action: #selector(chooseGlow(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = level
            glowItems.append(item)
        }
        viewMenu.addItem(withTitle: "Glow Strength", action: nil, keyEquivalent: "").submenu = glowMenu
        viewMenu.addItem(.separator())

        let aspectMenu = NSMenu(title: "Aspect")
        for (a, title) in [(Aspect.original, "Original (4:3)"), (.square, "Square Pixels (16:10)"), (.exact, "Exact Pixels")] {
            let item = aspectMenu.addItem(withTitle: title, action: #selector(chooseAspect(_:)), keyEquivalent: "")
            item.target = self
            item.tag = a.rawValue
            aspectItems.append(item)
        }
        viewMenu.addItem(withTitle: "Aspect", action: nil, keyEquivalent: "").submenu = aspectMenu

        let borderMenu = NSMenu(title: "Borders")
        for (b, title) in [(Borders.black, "Black"), (.ambient, "Ambient")] {
            let item = borderMenu.addItem(withTitle: title, action: #selector(chooseBorders(_:)), keyEquivalent: "")
            item.target = self
            item.tag = b.rawValue
            borderItems.append(item)
        }
        viewMenu.addItem(withTitle: "Borders", action: nil, keyEquivalent: "").submenu = borderMenu
        viewMenu.addItem(.separator())
        crispItem = viewMenu.addItem(withTitle: "Crisp Text", action: #selector(toggleCrispText(_:)), keyEquivalent: "t")
        crispItem?.target = self
        viewMenu.addItem(.separator())
        let fs = viewMenu.addItem(withTitle: "Enter Full Screen", action: #selector(NSWindow.toggleFullScreen(_:)), keyEquivalent: "f")
        fs.keyEquivalentModifierMask = [.command, .control]
        for item in [sharpItem, vgaItem, hdrItem] { item?.target = self }
        viewItem.submenu = viewMenu

        NSApp.mainMenu = main
    }

    @objc private func chooseSharp(_ sender: Any?) {
        renderer.display = .sharp
        defaults.set(DisplayMode.sharp.rawValue, forKey: "displayMode")
        updateMenuChecks()
    }

    @objc private func chooseVGA(_ sender: Any?) {
        renderer.display = .vga
        defaults.set(DisplayMode.vga.rawValue, forKey: "displayMode")
        updateMenuChecks()
    }

    @objc private func toggleHDR(_ sender: Any?) {
        renderer.hdr.toggle()
        defaults.set(renderer.hdr, forKey: "hdrGlow")
        updateMenuChecks()
    }

    @objc private func chooseGlow(_ sender: NSMenuItem) {
        guard let level = sender.representedObject as? Float else { return }
        renderer.glowStrength = level
        defaults.set(level, forKey: "glowStrength")
        if !renderer.hdr {
            renderer.hdr = true
            defaults.set(true, forKey: "hdrGlow")
        }
        updateMenuChecks()
    }

    @objc private func chooseAspect(_ sender: NSMenuItem) {
        guard let a = Aspect(rawValue: sender.tag) else { return }
        renderer.aspect = a
        defaults.set(a.rawValue, forKey: "aspect")
        updateMenuChecks()
    }

    @objc private func chooseBorders(_ sender: NSMenuItem) {
        guard let b = Borders(rawValue: sender.tag) else { return }
        renderer.borders = b
        defaults.set(b.rawValue, forKey: "borders")
        updateMenuChecks()
    }

    @objc private func toggleCrispText(_ sender: Any?) {
        let text = mt_setting(MT_SETTING_TEXT) == 0 ? 1 : 0
        mt_set_setting(MT_SETTING_TEXT, Int32(text))
        defaults.set(text, forKey: "text")
        updateMenuChecks()
    }

    private func updateMenuChecks() {
        crispItem?.state = mt_setting(MT_SETTING_TEXT) == 0 ? .on : .off
        for item in aspectItems { item.state = item.tag == renderer.aspect.rawValue ? .on : .off }
        for item in borderItems { item.state = item.tag == renderer.borders.rawValue ? .on : .off }
        mt_set_setting(MT_SETTING_ASPECT, Int32(renderer.aspect.rawValue))
        mt_set_setting(MT_SETTING_BORDERS, Int32(renderer.borders.rawValue))
        // Keep the game's Graphics menu showing the same settings.
        mt_set_setting(MT_SETTING_LOOK, Int32(renderer.display.rawValue))
        mt_set_setting(MT_SETTING_GLOW, Int32(glowSetting()))
        for item in glowItems {
            item.state = (item.representedObject as? Float) == renderer.glowStrength ? .on : .off
        }
        sharpItem?.state = renderer.display == .sharp ? .on : .off
        vgaItem?.state = renderer.display == .vga ? .on : .off
        hdrItem?.state = renderer.hdr ? .on : .off
    }

    private func fail(_ message: String) -> Never {
        print("MetalTyrian: \(message)")
        let alert = NSAlert()
        alert.messageText = "MetalTyrian could not start"
        alert.informativeText = message
        alert.runModal()
        exit(1)
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let delegate = AppDelegate()
app.delegate = delegate
app.run()
