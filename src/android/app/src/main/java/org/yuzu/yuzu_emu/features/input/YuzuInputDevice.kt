// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.features.input

import android.view.InputDevice
import androidx.annotation.Keep
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.utils.InputHandler.getGUID
import org.yuzu.yuzu_emu.utils.Log

@Keep
interface YuzuInputDevice {
    fun getName(): String

    fun getGUID(): String

    fun getPort(): Int

    fun getSupportsVibration(): Boolean

    fun vibrate(intensity: Float)

    fun getAxes(): Array<Int> = arrayOf()
    fun hasKeys(keys: IntArray): BooleanArray = BooleanArray(0)
}

class YuzuPhysicalDevice(
    private val device: InputDevice,
    private val port: Int,
    useSystemVibrator: Boolean
) : YuzuInputDevice {
    // The controller's own motor when Android has one for it. Without one, the phone's when the
    // player's setting says so (a controller clipped to the phone), else none. The setting used
    // to send player 1's vibration to the phone whatever the controller could do.
    private val vibrator: YuzuVibrator

    init {
        val own = YuzuVibrator.getControllerVibrator(device)
        val ownMotor = own.supportsVibration()
        vibrator = if (ownMotor || !useSystemVibrator) own else YuzuVibrator.getSystemVibrator()
        val route = when {
            ownMotor -> "its own motor"
            useSystemVibrator -> "the phone's vibrator (Android has no motor for it)"
            else -> "none (Android has no motor for it, and the player's setting leaves the " +
                "phone's alone)"
        }
        // Said once for a controller and again when it changes: devices are listed often.
        if (loggedRoutes.put(device.descriptor, route) != route) {
            Log.info("[Input] Vibration of ${device.name} (port $port): $route")
        }
    }

    override fun getName(): String {
        return device.name
    }

    override fun getGUID(): String {
        return device.getGUID()
    }

    override fun getPort(): Int {
        return port
    }

    override fun getSupportsVibration(): Boolean {
        return vibrator.supportsVibration()
    }

    override fun vibrate(intensity: Float) {
        vibrator.vibrate(intensity)
    }

    override fun getAxes(): Array<Int> = device.motionRanges.map { it.axis }.toTypedArray()
    override fun hasKeys(keys: IntArray): BooleanArray = device.hasKeys(*keys)

    companion object {
        private val loggedRoutes = java.util.concurrent.ConcurrentHashMap<String, String>()
    }
}

class YuzuInputOverlayDevice(
    private val vibration: Boolean,
    private val port: Int
) : YuzuInputDevice {
    private val vibrator = YuzuVibrator.getSystemVibrator()

    override fun getName(): String {
        return YuzuApplication.appContext.getString(R.string.input_overlay)
    }

    override fun getGUID(): String {
        return "00000000000000000000000000000000"
    }

    override fun getPort(): Int {
        return port
    }

    override fun getSupportsVibration(): Boolean {
        if (vibration) {
            return vibrator.supportsVibration()
        }
        return false
    }

    override fun vibrate(intensity: Float) {
        if (vibration) {
            vibrator.vibrate(intensity)
        }
    }
}
