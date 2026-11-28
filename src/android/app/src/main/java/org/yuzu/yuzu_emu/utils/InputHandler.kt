// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.Toast
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.features.input.NativeInput
import org.yuzu.yuzu_emu.features.input.YuzuInputOverlayDevice
import org.yuzu.yuzu_emu.features.input.YuzuPhysicalDevice
import org.yuzu.yuzu_emu.features.settings.model.Settings

object InputHandler {
    var androidControllers = mapOf<Int, YuzuPhysicalDevice>()
    var registeredControllers = mutableListOf<ParamPackage>()

    private val controllerButtons = intArrayOf(
        KeyEvent.KEYCODE_BUTTON_A,
        KeyEvent.KEYCODE_BUTTON_B,
        KeyEvent.KEYCODE_BUTTON_X,
        KeyEvent.KEYCODE_BUTTON_Y,
        KeyEvent.KEYCODE_BUTTON_L1,
        KeyEvent.KEYCODE_BUTTON_R1,
        KeyEvent.KEYCODE_BUTTON_L2,
        KeyEvent.KEYCODE_BUTTON_R2,
        KeyEvent.KEYCODE_BUTTON_THUMBL,
        KeyEvent.KEYCODE_BUTTON_THUMBR,
        KeyEvent.KEYCODE_BUTTON_START,
        KeyEvent.KEYCODE_BUTTON_SELECT,
        KeyEvent.KEYCODE_DPAD_UP,
        KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_DPAD_LEFT,
        KeyEvent.KEYCODE_DPAD_RIGHT
    )

    private val controllerAxes = intArrayOf(
        MotionEvent.AXIS_X,
        MotionEvent.AXIS_Y,
        MotionEvent.AXIS_Z,
        MotionEvent.AXIS_RX,
        MotionEvent.AXIS_RY,
        MotionEvent.AXIS_RZ,
        MotionEvent.AXIS_HAT_X,
        MotionEvent.AXIS_HAT_Y,
        MotionEvent.AXIS_LTRIGGER,
        MotionEvent.AXIS_RTRIGGER
    )

    // Currently, Android doesn't support Joy-Con D-pad buttons. We fall back to the scan code
    private const val LINUX_BUTTON_DPAD_UP = 0x220
    private const val LINUX_BUTTON_DPAD_DOWN = 0x221
    private const val LINUX_BUTTON_DPAD_LEFT = 0x222
    private const val LINUX_BUTTON_DPAD_RIGHT = 0x223

    fun isPhysicalGameController(device: InputDevice?): Boolean {
        device ?: return false

        if (device.isVirtual) {
            return false
        }

        val sources = device.sources
        val hasControllerSource =
            sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
                sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
        if (!hasControllerSource) {
            return false
        }

        val hasControllerButtons = device.hasKeys(*controllerButtons).any { it }
        val hasControllerAxes = device.motionRanges.any { range ->
            controllerAxes.contains(range.axis)
        }
        return hasControllerButtons || hasControllerAxes
    }

    fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val action = when (event.action) {
            KeyEvent.ACTION_DOWN -> NativeInput.ButtonState.PRESSED
            KeyEvent.ACTION_UP -> NativeInput.ButtonState.RELEASED
            else -> return false
        }

        var controllerData = androidControllers[event.device.controllerNumber]
        if (controllerData == null) {
            updateControllerData()
            controllerData = androidControllers[event.device.controllerNumber] ?: return false
        }

        // A controller that no player's mapping names becomes the next free player
        if (action == NativeInput.ButtonState.PRESSED && event.repeatCount == 0) {
            val player = NativeInput.joinUnmappedController(
                controllerData.getGUID(),
                controllerData.getPort()
            )
            if (player >= 0) {
                Toast.makeText(
                    YuzuApplication.appContext,
                    "${controllerData.getName()}: ${Settings.getPlayerString(player + 1)}",
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

        NativeInput.onGamePadButtonEvent(
            controllerData.getGUID(),
            controllerData.getPort(),
            getButtonIdFromEvent(event),
            action
        )
        return true
    }

    fun getButtonIdFromEvent(event: KeyEvent): Int {
        if (event.keyCode == 0) {
            return when (event.scanCode) {
                LINUX_BUTTON_DPAD_UP -> KeyEvent.KEYCODE_DPAD_UP
                LINUX_BUTTON_DPAD_DOWN -> KeyEvent.KEYCODE_DPAD_DOWN
                LINUX_BUTTON_DPAD_LEFT -> KeyEvent.KEYCODE_DPAD_LEFT
                LINUX_BUTTON_DPAD_RIGHT -> KeyEvent.KEYCODE_DPAD_RIGHT
                else -> return 0
            }
        }
        return event.keyCode
    }

    fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        val controllerData =
            androidControllers[event.device.controllerNumber] ?: return false
        event.device.motionRanges.forEach {
            NativeInput.onGamePadAxisEvent(
                controllerData.getGUID(),
                controllerData.getPort(),
                it.axis,
                event.getAxisValue(it.axis)
            )
        }
        return true
    }

    fun getDevices(): Map<Int, YuzuPhysicalDevice> {
        val gameControllerDeviceIds = mutableMapOf<Int, YuzuPhysicalDevice>()
        val inputSettings = NativeConfig.getInputSettings(true)
        // Ports follow the controllers themselves, not the order they connected in: mappings
        // name a controller by its port, and a pair of Joy-Cons (two controllers to Android)
        // must keep theirs whichever of the two reconnects first. So a controller that a mapping
        // names gets the port the mapping names, whichever other controllers are connected, and
        // the others get a number of their own (NativeInput.resolvePorts).
        val devices = InputDevice.getDeviceIds().toList()
            .mapNotNull { InputDevice.getDevice(it) }
            .filter { isPhysicalGameController(it) }
            .sortedWith(compareBy({ it.vendorId }, { it.productId }, { it.descriptor }))
        val ports = NativeInput.resolvePorts(
            devices.map { it.getGUID() }.toTypedArray(),
            devices.map { it.descriptor }.toTypedArray(),
            devices.map { it.name }.toTypedArray()
        )
        devices.forEachIndexed { index, device ->
            if (!gameControllerDeviceIds.contains(device.controllerNumber)) {
                val port = ports.getOrElse(index) { index }
                gameControllerDeviceIds[device.controllerNumber] = YuzuPhysicalDevice(
                    device,
                    port,
                    inputSettings.getOrNull(port)?.useSystemVibrator ?: false
                )
            }
        }
        return gameControllerDeviceIds
    }

    fun updateControllerData() {
        androidControllers = getDevices()
        androidControllers.forEach {
            NativeInput.registerController(it.value)
        }

        // Register the input overlay on a dedicated port for all player 1 vibrations
        NativeInput.registerController(YuzuInputOverlayDevice(androidControllers.isEmpty(), 100))
        registeredControllers.clear()
        NativeInput.getInputDevices().forEach {
            registeredControllers.add(ParamPackage(it))
        }
        registeredControllers.sortBy { it.get("port", 0) }
    }

    fun InputDevice.getGUID(): String = String.format("%016x%016x", productId, vendorId)
}
