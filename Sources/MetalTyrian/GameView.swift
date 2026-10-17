import AppKit
import MetalKit
import SDLShim

/// The game's view: draws with Metal and passes the keyboard to the game.
/// Mouse and gamepads arrive in phase 2.
final class GameView: MTKView {
    private var heldKeys = Set<Int>()

    override var acceptsFirstResponder: Bool { true }

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

    /// Releases every held key, so nothing sticks when the window loses focus.
    func releaseAllKeys() {
        for sc in heldKeys {
            mt_push_key(Int32(sc), false, 0)
        }
        heldKeys.removeAll()
    }

    private func mods(_ f: NSEvent.ModifierFlags) -> Int32 {
        var m: Int32 = 0
        if f.contains(.shift) { m |= MT_MOD_SHIFT }
        if f.contains(.control) { m |= MT_MOD_CTRL }
        if f.contains(.option) { m |= MT_MOD_ALT }
        if f.contains(.command) { m |= MT_MOD_GUI }
        return m
    }
}
