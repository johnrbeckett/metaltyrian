import AppKit
import GameController
import SDLShim

/// Gives the game two gamepads, one per player. The first controller to
/// connect (Xbox, PlayStation, Switch and other MFi pads) takes slot 1, the
/// next slot 2; a slot frees up when its controller disconnects. Controllers
/// that have player lights show their number. Polled every display refresh
/// and on every change, so input reaches the game at once.
enum Gamepads {
    private static var slots: [GCController?] = Array(repeating: nil, count: Int(MT_PADS))
    private static var last: [(buttons: UInt32, hat: UInt8, axes: [Int16])?] = Array(repeating: nil, count: Int(MT_PADS))

    static func start() {
        // Controllers that connect later still arrive by notification; this
        // only makes macOS look for wireless ones promptly.
        GCController.startWirelessControllerDiscovery(completionHandler: nil)

        GCController.controllers().forEach(connect)
        NotificationCenter.default.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { note in
            if let c = note.object as? GCController { connect(c) }
        }
        NotificationCenter.default.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { note in
            if let c = note.object as? GCController { disconnect(c) }
        }
    }

    private static func connect(_ controller: GCController) {
        guard controller.extendedGamepad != nil,
              !slots.contains(where: { $0 === controller }),
              let free = slots.firstIndex(where: { $0 == nil }) else { return }
        slots[free] = controller
        controller.playerIndex = free == 0 ? .index1 : .index2
        controller.handlerQueue = .main
        controller.extendedGamepad?.valueChangedHandler = { _, _ in
            poll(active: NSApp.isActive && NSApp.keyWindow != nil)
        }
    }

    private static func disconnect(_ controller: GCController) {
        guard let i = slots.firstIndex(where: { $0 === controller }) else { return }
        slots[i] = nil
        // Any controller waiting without a slot can take it.
        GCController.controllers().forEach(connect)
    }

    static func poll(active: Bool) {
        for i in 0..<slots.count {
            var axes: [Int16] = [0, 0, 0, 0]
            var buttons: UInt32 = 0
            var hat: UInt8 = 0

            if active, let pad = slots[i]?.extendedGamepad {
                func press(_ bit: Int32, _ b: GCControllerButtonInput?) {
                    if b?.isPressed == true { buttons |= 1 << UInt32(bit) }
                }
                press(MT_PAD_A, pad.buttonA)
                press(MT_PAD_B, pad.buttonB)
                press(MT_PAD_X, pad.buttonX)
                press(MT_PAD_Y, pad.buttonY)
                press(MT_PAD_LB, pad.leftShoulder)
                press(MT_PAD_RB, pad.rightShoulder)
                press(MT_PAD_LT, pad.leftTrigger)
                press(MT_PAD_RT, pad.rightTrigger)
                press(MT_PAD_START, pad.buttonMenu)
                press(MT_PAD_BACK, pad.buttonOptions)
                press(MT_PAD_L3, pad.leftThumbstickButton)
                press(MT_PAD_R3, pad.rightThumbstickButton)

                if pad.dpad.up.isPressed { hat |= 0x01 }
                if pad.dpad.right.isPressed { hat |= 0x02 }
                if pad.dpad.down.isPressed { hat |= 0x04 }
                if pad.dpad.left.isPressed { hat |= 0x08 }

                // GameController's y axis points up; the game's points down.
                let sticks: [Float] = [pad.leftThumbstick.xAxis.value, -pad.leftThumbstick.yAxis.value,
                                       pad.rightThumbstick.xAxis.value, -pad.rightThumbstick.yAxis.value]
                for a in 0..<4 {
                    axes[a] = Int16(max(-32767, min(32767, sticks[a] * 32767)))
                }
            }

            if let l = last[i], l.buttons == buttons, l.hat == hat, l.axes == axes { continue }
            last[i] = (buttons, hat, axes)
            axes.withUnsafeBufferPointer { mt_set_gamepad(Int32(i), $0.baseAddress, buttons, hat) }
        }
    }
}
