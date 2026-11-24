// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.ui

import android.widget.Toast
import androidx.fragment.app.FragmentActivity
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.features.input.NativeInput
import org.yuzu.yuzu_emu.utils.InputHandler
import org.yuzu.yuzu_emu.utils.NativeConfig
import org.yuzu.yuzu_emu.utils.ParamPackage

/**
 * Auto-map two Joy-Cons: a pair of Joy-Cons is two controllers to Android, and Auto-map takes one.
 * The player picks the controller held in the left hand, then the one in the right, and gets the
 * left half of the controls from the first and the right half from the second
 * (NativeInput.updateMappingsWithDualDevices).
 */
object DualControllerMapping {
    // InputHandler's GUID is the product ID then the vendor ID, 16 hex digits each.
    private const val JOYCON_LEFT_GUID = "0000000000002006000000000000057e"
    private const val JOYCON_RIGHT_GUID = "0000000000002007000000000000057e"

    private class Device(val params: ParamPackage, val name: String) {
        private val guid = params.get("guid", "").lowercase()
        val looksLeft = guid == JOYCON_LEFT_GUID || name.contains("(L)") ||
            name.contains("Left", ignoreCase = true)
        val looksRight = guid == JOYCON_RIGHT_GUID || name.contains("(R)") ||
            name.contains("Right", ignoreCase = true)
    }

    /** The connected controllers, without the touch overlay (port 100). */
    private fun devices(activity: FragmentActivity): List<Device> {
        InputHandler.updateControllerData()
        val unknown = activity.getString(R.string.unknown)
        return InputHandler.registeredControllers
            .filter { it.get("port", -1) != 100 && it.get("port", -1) != -1 }
            .map { Device(it, it.get("display", unknown)) }
    }

    fun show(activity: FragmentActivity, playerIndex: Int, onMapped: () -> Unit) {
        val devices = devices(activity)
        if (devices.size < 2) {
            MaterialAlertDialogBuilder(activity)
                .setTitle(R.string.auto_map_dual)
                .setMessage(R.string.auto_map_dual_need_two)
                .setPositiveButton(android.R.string.ok, null)
                .show()
            return
        }
        // Two controllers of one name are told apart by their place in the list.
        val names = devices.mapIndexed { index, device -> "${index + 1}. ${device.name}" }
            .toTypedArray()
        val leftGuess = devices.indexOfFirst { it.looksLeft }.takeIf { it >= 0 } ?: 0
        pick(activity, R.string.auto_map_dual_left, names, leftGuess) { left ->
            val others = devices.indices.filter { it != left }
            val rightGuess = others.firstOrNull { devices[it].looksRight } ?: others.first()
            pick(activity, R.string.auto_map_dual_right, names, rightGuess) { right ->
                if (right == left) {
                    Toast.makeText(activity, R.string.auto_map_dual_same, Toast.LENGTH_LONG).show()
                    return@pick
                }
                NativeInput.updateMappingsWithDualDevices(
                    playerIndex,
                    devices[left].params,
                    devices[right].params,
                    devices[left].name,
                    devices[right].name
                )
                NativeConfig.saveGlobalConfig()
                Toast.makeText(
                    activity,
                    activity.getString(
                        R.string.auto_map_dual_done,
                        devices[left].name,
                        devices[right].name
                    ),
                    Toast.LENGTH_LONG
                ).show()
                onMapped()
            }
        }
    }

    private fun pick(
        activity: FragmentActivity,
        title: Int,
        names: Array<String>,
        preselected: Int,
        picked: (Int) -> Unit
    ) {
        var choice = preselected
        MaterialAlertDialogBuilder(activity)
            .setTitle(title)
            .setSingleChoiceItems(names, preselected) { _, which -> choice = which }
            .setPositiveButton(android.R.string.ok) { _, _ -> picked(choice) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }
}
