// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.loopnative

/** LoopNative's native side (jni/loopnative_jni.cpp, in libyuzu-android.so). */
object LoopNativeJni {
    init {
        System.loadLibrary("yuzu-android")
    }

    external fun setDevice(
        manufacturer: String,
        model: String,
        device: String,
        soc: String,
        gpu: String,
        totalRam: Long,
        sdkInt: Int
    )

    /** The phone's tuning, as JSON. */
    external fun deviceTuning(): String

    /** What LoopNative starts the game with on this phone, as JSON. */
    external fun sessionPlan(programId: String): String

    external fun hasProfile(programId: String): Boolean

    /** Puts settings in the per-game layer for this session; returns how many it found. */
    external fun applySession(keys: Array<String>, values: Array<String>): Int

    /** Session end: those settings follow the global ones again. */
    external fun restoreSession(keys: Array<String>)

    /** Reads every module of the game's ExeFS and writes the pack manifest; returns it as JSON. */
    external fun prepare(
        path: String,
        programId: String,
        title: String,
        manifestPath: String
    ): String

    external fun governorStart(baseFpsLimit: Int)

    /** One governor step; returns its decision as JSON. */
    external fun governorTick(
        headroom: Float,
        thermalStatus: Int,
        lowMemory: Boolean,
        seconds: Double
    ): String

    external fun governorStop()
}
