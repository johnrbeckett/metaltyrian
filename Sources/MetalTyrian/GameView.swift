import AppKit
import CoreGraphics
import GameController
import MetalKit
import SDLShim

/// The game's view: draws with Metal and passes the keyboard and mouse to the game.
final class GameView: MTKView {
    private var heldKeys = Set<Int>()

    /// Where the 320x200 picture sits in the view; set by the renderer.
    var pictureRect: () -> CGRect = { .zero }

    // Mouse state.
    private var heldButtons = Set<Int>()
    private var lastGamePoint = (x: Int32(160), y: Int32(100))
    private var carryX: CGFloat = 0     // fractions of a game pixel not yet sent
    private var carryY: CGFloat = 0
    private var pointerLocked = false
    private var pointerHidden = false

    // Direct mouse mode reads raw mouse movement (no macOS acceleration) from
    // GameController, so the same hand position always means the same spot.
    private var lastRawTime: TimeInterval = 0
    private var lastEventDY: CGFloat = 0
    private var lastEventTime: TimeInterval = 0
    private var rawYSign: CGFloat = 0          // learnt by comparing with AppKit's deltas
    private var rawYVotes: CGFloat = 0
    private var directMode: Bool { mt_setting(MT_SETTING_MOUSE_MODE) == 1 }

    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    // MARK: - Keyboard

    override func keyDown(with event: NSEvent) {
        // Command shortcuts belong to the menus, not the game.
        if event.modifierFlags.contains(.command) {
            super.keyDown(with: event)
            return
        }
        if let sc = KeyMap.scancode(for: event.keyCode) {
            heldKeys.insert(sc)
            mt_push_key(Int32(sc), true, mods(event.modifierFlags))
        }
        // The game also reads typed characters, for names in saves and high scores.
        if let chars = event.characters, let scalar = chars.unicodeScalars.first,
           scalar.value >= 0x20, scalar.value < 0x7F {
            mt_push_text(chars)
        }
    }

    override func keyUp(with event: NSEvent) {
        if let sc = KeyMap.scancode(for: event.keyCode), heldKeys.contains(sc) {
            heldKeys.remove(sc)
            mt_push_key(Int32(sc), false, mods(event.modifierFlags))
        }
    }

    override func flagsChanged(with event: NSEvent) {
        guard let sc = KeyMap.scancode(for: event.keyCode),
              let flag = KeyMap.modifierFlag(for: event.keyCode) else { return }
        let down = event.modifierFlags.contains(flag) && !heldKeys.contains(sc)
        if down {
            heldKeys.insert(sc)
        } else {
            heldKeys.remove(sc)
        }
        mt_push_key(Int32(sc), down, mods(event.modifierFlags))
    }

    /// Releases every held key and button and frees the pointer, so nothing
    /// sticks when the window loses focus.
    func releaseAllKeys() {
        for sc in heldKeys {
            mt_push_key(Int32(sc), false, 0)
        }
        heldKeys.removeAll()
        for b in heldButtons {
            mt_push_mouse_button(Int32(b), false, lastGamePoint.x, lastGamePoint.y)
        }
        heldButtons.removeAll()
        setPointerLocked(false)
        setPointerHidden(false)
    }

    private func mods(_ f: NSEvent.ModifierFlags) -> Int32 {
        var m: Int32 = 0
        if f.contains(.shift) { m |= MT_MOD_SHIFT }
        if f.contains(.control) { m |= MT_MOD_CTRL }
        if f.contains(.option) { m |= MT_MOD_ALT }
        if f.contains(.command) { m |= MT_MOD_GUI }
        return m
    }

    // MARK: - Mouse

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        window?.acceptsMouseMovedEvents = true
    }

    override func mouseMoved(with event: NSEvent) { motion(event) }
    override func mouseDragged(with event: NSEvent) { motion(event) }
    override func rightMouseDragged(with event: NSEvent) { motion(event) }
    override func otherMouseDragged(with event: NSEvent) { motion(event) }

    override func mouseDown(with event: NSEvent) { button(event, down: true) }
    override func mouseUp(with event: NSEvent) { button(event, down: false) }
    override func rightMouseDown(with event: NSEvent) { button(event, down: true) }
    override func rightMouseUp(with event: NSEvent) { button(event, down: false) }
    override func otherMouseDown(with event: NSEvent) { button(event, down: true) }
    override func otherMouseUp(with event: NSEvent) { button(event, down: false) }

    private func motion(_ event: NSEvent) {
        guard window?.isKeyWindow == true else { return }
        if pointerLocked {
            lastEventDY = event.deltaY
            lastEventTime = event.timestamp
            // Raw movement is arriving: Direct mode uses that instead.
            if directMode && ProcessInfo.processInfo.systemUptime - lastRawTime < 0.25 { return }
            // During play the game wants movement only, in game pixels, at the
            // same scale as the picture on screen. Fractions carry over.
            let r = pictureRect()
            guard r.width > 0, r.height > 0 else { return }
            sendMovement(dx: event.deltaX * 320 / r.width, dy: event.deltaY * 200 / r.height)   // deltaY is already downwards
        } else {
            let p = gamePoint(event)
            lastGamePoint = p
            mt_push_mouse_motion(p.x, p.y, 0, 0)
        }
    }

    /// Mouse movement in game pixels at the original scale, adjusted by Mouse Speed.
    private func sendMovement(dx: CGFloat, dy: CGFloat) {
        // Mouse Speed (Setup > Controls): 5 is the original; each step is about 19%.
        let speed = max(1, min(10, Int(mt_setting(MT_SETTING_MOUSE_SPEED))))
        let scale = CGFloat(pow(2.0, Double(speed - 5) / 4.0))
        carryX += dx * scale
        carryY += dy * scale
        let ix = carryX.rounded(.towardZero), iy = carryY.rounded(.towardZero)
        carryX -= ix
        carryY -= iy
        if ix != 0 || iy != 0 {
            mt_push_mouse_motion(lastGamePoint.x, lastGamePoint.y, Int32(ix), Int32(iy))
        }
    }

    /// Raw mouse movement for Direct mode. Counts are scaled so a typical
    /// mouse crosses the playfield with a comfortable sweep at Mouse Speed 5.
    func startRawMouse() {
        let hook: (GCMouse) -> Void = { [weak self] mouse in
            mouse.handlerQueue = .main
            mouse.mouseInput?.mouseMovedHandler = { _, dx, dy in
                self?.rawMoved(dx: CGFloat(dx), dy: CGFloat(dy))
            }
        }
        GCMouse.mice().forEach(hook)
        NotificationCenter.default.addObserver(forName: .GCMouseDidConnect, object: nil, queue: .main) { note in
            if let m = note.object as? GCMouse { hook(m) }
        }
    }

    private func rawMoved(dx: CGFloat, dy: CGFloat) {
        guard pointerLocked, directMode, window?.isKeyWindow == true else { return }
        // Which way is up in raw counts? Learn it from AppKit's own deltas
        // (downwards positive) arriving at the same moment.
        let now = ProcessInfo.processInfo.systemUptime
        if rawYSign == 0 {
            if dy != 0, lastEventDY != 0, abs(lastEventTime - now) < 0.05 {
                rawYVotes += (dy * lastEventDY > 0) ? 1 : -1
                if abs(rawYVotes) >= 8 { rawYSign = rawYVotes > 0 ? 1 : -1 }
            }
            return   // AppKit's deltas drive the ship until the direction is known
        }
        lastRawTime = now
        let perCount: CGFloat = 0.12   // game pixels per raw count
        sendMovement(dx: dx * perCount, dy: dy * rawYSign * perCount)
    }

    private func button(_ event: NSEvent, down: Bool) {
        if down, window?.isKeyWindow != true { return }
        // macOS numbers buttons 0 left, 1 right, 2 middle; SDL uses 1, 3, 2.
        let b: Int
        switch event.buttonNumber {
        case 0: b = 1
        case 1: b = 3
        case 2: b = 2
        default: return
        }
        if !pointerLocked {
            lastGamePoint = gamePoint(event)
        }
        if down {
            heldButtons.insert(b)
        } else {
            guard heldButtons.remove(b) != nil else { return }
        }
        mt_push_mouse_button(Int32(b), down, lastGamePoint.x, lastGamePoint.y)
    }

    /// The pointer position in game pixels; outside the picture gives values
    /// outside 0-319 x 0-199, which tells the game to show the Mac pointer.
    private func gamePoint(_ event: NSEvent) -> (x: Int32, y: Int32) {
        let r = pictureRect()
        guard r.width > 0, r.height > 0 else { return lastGamePoint }
        let p = convert(event.locationInWindow, from: nil)
        let fx = ((p.x - r.minX) / r.width * 320).rounded(.down)
        let fy = ((r.maxY - p.y) / r.height * 200).rounded(.down)
        // The game rounds -1 up to 0, so push points left of or above the
        // picture clearly outside it.
        let x = fx < 0 ? min(fx, -2) : fx
        let y = fy < 0 ? min(fy, -2) : fy
        return (Int32(max(min(x, 10_000), -10_000)), Int32(max(min(y, 10_000), -10_000)))
    }

    /// Called every display refresh: follows what the game wants from the pointer.
    func syncPointer() {
        guard let window, window.isKeyWindow, NSApp.isActive else {
            setPointerLocked(false)
            setPointerHidden(false)
            return
        }
        // Holding Command frees the pointer during play, for the menu bar.
        let relative = mt_relative_mouse() && !NSEvent.modifierFlags.contains(.command)
        setPointerLocked(relative)
        if relative {
            setPointerHidden(true)
        } else {
            // Hide only while the pointer is over the picture and the game asks.
            let inView = mouseInsidePicture()
            setPointerHidden(inView && !mt_cursor_wanted())
        }
    }

    private func mouseInsidePicture() -> Bool {
        guard let window else { return false }
        let p = convert(window.mouseLocationOutsideOfEventStream, from: nil)
        return pictureRect().contains(p)
    }

    private func setPointerLocked(_ lock: Bool) {
        guard lock != pointerLocked else { return }
        pointerLocked = lock
        carryX = 0
        carryY = 0
        if lock {
            // Park the pointer in the middle of the picture first, so clicks
            // can't land on another window while it's frozen.
            if let window, let screen = NSScreen.screens.first {
                let r = window.convertToScreen(convert(pictureRect(), to: nil))
                CGWarpMouseCursorPosition(CGPoint(x: r.midX, y: screen.frame.maxY - r.midY))
            }
            CGAssociateMouseAndMouseCursorPosition(0)
        } else {
            CGAssociateMouseAndMouseCursorPosition(1)
        }
    }

    private func setPointerHidden(_ hide: Bool) {
        guard hide != pointerHidden else { return }
        pointerHidden = hide
        if hide { NSCursor.hide() } else { NSCursor.unhide() }
    }
}
