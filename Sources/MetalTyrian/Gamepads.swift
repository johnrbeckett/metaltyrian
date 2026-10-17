import AppKit
import GameController
import SDLShim

/// Reads every connected controller (Xbox, PlayStation, Switch and other
/// MFi pads) and hands the game one merged gamepad. Polled once per display
/// refresh, which is several times per game frame.
enum Gamepads {
    private static var lastButtons: UInt32 = .max
    private static var lastAxes: [Int16] = []
    private static var lastHat: UInt8 = .max

    static func start() {
        // Controllers that connect later show up in GCController.controllers()
        // without further work; this only makes macOS look for them promptly.
        GCController.startWirelessControllerDiscovery(completionHandler: nil)
    }

    static func poll(active: Bool) {
        var axes: [Int16] = [0, 0, 0, 0]
        var buttons: UInt32 = 0
        var hat: UInt8 = 0

        if active {
            for controller in GCController.controllers() {
                guard let pad = controller.extendedGamepad else { continue }
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
                for i in 0..<4 {
                    let v = Int16(max(-32767, min(32767, sticks[i] * 32767)))
                    if abs(Int(v)) > abs(Int(axes[i])) { axes[i] = v }   // strongest pad wins
                }
            }
            // Opposite directions from two pads cancel out rather than flicker.
            if hat & 0x05 == 0x05 { hat &= ~0x05 }
            if hat & 0x0A == 0x0A { hat &= ~0x0A }
        }

        guard buttons != lastButtons || hat != lastHat || axes != lastAxes else { return }
        lastButtons = buttons
        lastHat = hat
        lastAxes = axes
        axes.withUnsafeBufferPointer { mt_set_gamepad($0.baseAddress, buttons, hat) }
    }
}
