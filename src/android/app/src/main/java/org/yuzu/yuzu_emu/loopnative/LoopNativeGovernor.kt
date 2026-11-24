// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.loopnative

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import android.os.PowerManager
import android.os.SystemClock
import android.widget.Toast
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.utils.Log

/**
 * Samples the phone's temperature forecast and memory every two seconds while a LoopNative game
 * plays; the native governor turns them into a frame cap (jni/loopnative_jni.cpp applies it).
 */
class LoopNativeGovernor(context: Context) {
    private val context = context.applicationContext
    private val power = context.getSystemService(PowerManager::class.java)
    private val activity = context.getSystemService(ActivityManager::class.java)
    private var job: Job? = null
    private var warnedMemory = false

    fun start(scope: CoroutineScope, baseFpsLimit: Int) {
        stop()
        LoopNativeJni.governorStart(baseFpsLimit)
        warnedMemory = false
        job = scope.launch(Dispatchers.Default) {
            while (isActive) {
                delay(TICK_MS)
                val decision = runCatching { tick() }.getOrNull() ?: continue
                report(decision)
            }
        }
    }

    fun stop() {
        if (job == null) {
            return
        }
        job?.cancel()
        job = null
        runCatching { LoopNativeJni.governorStop() }
    }

    private fun tick(): JSONObject {
        // getThermalHeadroom returns NaN when asked more than once a second or when unsupported.
        val headroom = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            power?.getThermalHeadroom(FORECAST_SECONDS)?.takeUnless { it.isNaN() } ?: -1f
        } else {
            -1f
        }
        val status = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            power?.currentThermalStatus ?: 0
        } else {
            0
        }
        val memory = ActivityManager.MemoryInfo()
        activity?.getMemoryInfo(memory)
        val json = LoopNativeJni.governorTick(
            headroom,
            status,
            memory.lowMemory,
            SystemClock.elapsedRealtime() / 1000.0
        )
        return JSONObject(json)
    }

    private suspend fun report(decision: JSONObject) {
        val message = when {
            decision.optBoolean("changed") -> when (decision.optString("thermal_level")) {
                "capped_30" -> context.getString(R.string.loopnative_thermal_capped)
                "skipping" -> context.getString(R.string.loopnative_thermal_skipping)
                "full" -> context.getString(R.string.loopnative_thermal_full)
                else -> null
            }
            decision.optString("memory_pressure") == "critical" && !warnedMemory -> {
                warnedMemory = true
                context.getString(R.string.loopnative_memory_critical)
            }
            else -> null
        } ?: return
        Log.info("[LoopNative] $decision")
        withContext(Dispatchers.Main) {
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        }
    }

    companion object {
        private const val TICK_MS = 2000L
        private const val FORECAST_SECONDS = 10
    }
}
