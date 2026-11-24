// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.loopnative

import android.content.Context
import android.system.Os
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.utils.Log

/**
 * One game started on LoopNative: armed by the launcher (GameEngine) just before the emulation
 * screen opens, taken by EmulationFragment once the game's config is loaded and before the core
 * boots it, and ended when that screen goes away.
 */
object LoopNativeSession {
    /** Read by common/host_memory.cpp when the guest memory is made. */
    private const val MEMORY_BACKING_ENV = "LOOPNATIVE_MEMORY_BACKING_DIR"

    data class Request(
        val titleId: String,
        val plan: LoopNative.Plan,
        val storageMemory: Boolean,
        val governor: Boolean,
        val keepSettings: Boolean
    )

    @Volatile
    private var pending: Request? = null

    @Volatile
    var active: Request? = null
        private set

    fun arm(context: Context, game: Game) {
        val plan = LoopNative.plan(context, game)
        pending = Request(
            titleId = LoopNative.titleIdOf(game),
            plan = plan,
            storageMemory = LoopNative.option(context, game, LoopNative.Option.STORAGE_MEMORY),
            governor = LoopNative.option(context, game, LoopNative.Option.GOVERNOR),
            keepSettings = LoopNative.option(context, game, LoopNative.Option.KEEP_SETTINGS)
        )
    }

    /**
     * Applies the armed session when it is for this game. Any other start (Hokkaido engine, a
     * shortcut, another game) clears it, and its guest memory stays in RAM.
     */
    fun begin(context: Context, game: Game?): Request? {
        val titleId = game?.let { LoopNative.titleIdOf(it) }
        // A recreated emulation screen of the running game keeps its session as it is.
        if (pending == null && titleId != null && active?.titleId == titleId) {
            return active
        }
        val request = pending?.takeIf { it.titleId == titleId }
        pending = null
        active = request
        if (request == null) {
            setMemoryBacking(null)
            return null
        }
        if (!request.keepSettings && request.plan.overrides.isNotEmpty()) {
            val keys = request.plan.overrides.map { it.first }.toTypedArray()
            val values = request.plan.overrides.map { it.second }.toTypedArray()
            val applied = LoopNativeJni.applySession(keys, values)
            Log.info("[LoopNative] ${request.titleId}: $applied of ${keys.size} settings applied")
        }
        setMemoryBacking(if (request.storageMemory) LoopNative.memoryBackingDir(context) else null)
        return request
    }

    fun end() {
        val request = active
        active = null
        setMemoryBacking(null)
        if (request != null && !request.keepSettings && request.plan.overrides.isNotEmpty()) {
            runCatching {
                LoopNativeJni.restoreSession(request.plan.overrides.map { it.first }.toTypedArray())
            }
        }
    }

    private fun setMemoryBacking(directory: java.io.File?) {
        runCatching {
            if (directory == null) {
                Os.unsetenv(MEMORY_BACKING_ENV)
                return
            }
            directory.mkdirs()
            // Files of a session that ended in a crash: unlinked ones need no cleanup, mkstemp's may.
            directory.listFiles()?.forEach { it.delete() }
            Os.setenv(MEMORY_BACKING_ENV, directory.absolutePath, true)
            Log.info("[LoopNative] Guest memory on storage: ${directory.absolutePath}")
        }.onFailure { Log.warning("[LoopNative] Memory backing: ${it.message}") }
    }
}
