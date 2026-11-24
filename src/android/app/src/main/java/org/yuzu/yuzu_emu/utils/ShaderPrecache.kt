// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import androidx.annotation.Keep
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.progressindicator.LinearProgressIndicator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.model.Game

/**
 * "Build all shaders now" for the emulator engines (Hokkaido and LoopNative): the core's shader
 * precache (video_core/shader_warm, jni/native_shader_precache.cpp). Build reads the game's files
 * once and writes its precache file; every start after that builds its programs, and the graphics
 * pipelines saved from earlier play, on the loading screen before the game runs.
 */
object ShaderPrecache {
    init {
        System.loadLibrary("yuzu-android")
    }

    data class Result(
        val ok: Boolean,
        val stopped: Boolean,
        val error: String,
        val archives: Long,
        val programs: Long,
        val skipped: Long
    )

    @Volatile
    private var listener: ((stage: String, done: Long, total: Long) -> Unit)? = null

    /** Called by nativeBuild on the thread that runs it. */
    @Keep
    fun onBuildProgress(stage: String, done: Long, total: Long) {
        listener?.invoke(stage, done, total)
    }

    private external fun nativeBuild(path: String, programId: String): String

    external fun cancelBuild()

    /** Whether the game has a precache file. */
    external fun exists(programId: String): Boolean

    private external fun requestBeforeStart(programId: String): Boolean

    private external fun restoreAfterStop()

    /** Before the core boots a game: one with a precache file precaches on its loading screen. */
    fun beforeStart(game: Game?) {
        runCatching { requestBeforeStart(game?.programId ?: "0") }
            .onFailure { Log.warning("[ShaderPrecache] ${it.message}") }
    }

    fun afterStop() {
        runCatching { restoreAfterStop() }
    }

    /** Builds the precache file. Blocks for minutes: call it off the main thread. */
    private fun build(game: Game, progress: (String, Long, Long) -> Unit): Result {
        listener = progress
        try {
            val json = JSONObject(nativeBuild(game.path, game.programId))
            return Result(
                ok = json.getBoolean("ok"),
                stopped = json.getBoolean("stopped"),
                error = json.getString("error"),
                archives = json.getLong("archives"),
                programs = json.getLong("programs"),
                skipped = json.getLong("skipped")
            )
        } finally {
            listener = null
        }
    }

    /**
     * Game properties › Build all shaders now: builds the precache file with a progress dialog,
     * then offers [play] (null when the game's engine is the pack engine, which has its own).
     */
    fun show(activity: FragmentActivity, game: Game, play: (() -> Unit)?) {
        val stage = TextView(activity).apply { setText(R.string.shader_precache_reading) }
        val bar = LinearProgressIndicator(activity).apply { isIndeterminate = true }
        val box = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            val pad = (24 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad / 2, pad, 0)
            val fullWidth = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
            addView(stage, fullWidth)
            addView(bar, LinearLayout.LayoutParams(fullWidth).apply { topMargin = pad / 2 })
        }
        val working = MaterialAlertDialogBuilder(activity)
            .setTitle(activity.getString(R.string.shader_precache_building_title, game.title))
            .setView(box)
            .setCancelable(false)
            .setNegativeButton(android.R.string.cancel, null)
            .show()
        // Cancel asks the build to stop; the dialog closes when it has.
        working.getButton(android.content.DialogInterface.BUTTON_NEGATIVE).setOnClickListener {
            it.isEnabled = false
            cancelBuild()
        }
        activity.lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    build(game) { name, done, total ->
                        activity.runOnUiThread {
                            stage.text = stageText(activity, name, done, total)
                            bar.isIndeterminate = total <= 0
                            if (total > 0) {
                                bar.max = total.toInt()
                                bar.setProgressCompat(done.toInt(), true)
                            }
                        }
                    }
                }
            }
            working.dismiss()
            val built = result.getOrNull()
            val dialog = MaterialAlertDialogBuilder(activity)
            when {
                built == null -> dialog.setTitle(R.string.shader_precache_failed)
                    .setMessage(result.exceptionOrNull()?.message ?: "")
                built.stopped -> dialog.setTitle(R.string.shader_precache_stopped)
                    .setMessage(R.string.shader_precache_stopped_description)
                !built.ok -> dialog.setTitle(R.string.shader_precache_failed)
                    .setMessage(built.error)
                else -> {
                    dialog
                        .setTitle(
                            activity.getString(R.string.shader_precache_done_title, game.title)
                        )
                        .setMessage(
                            activity.getString(
                                R.string.shader_precache_done_description,
                                built.programs,
                                built.archives,
                                built.skipped
                            )
                        )
                    if (play != null) {
                        dialog.setPositiveButton(R.string.shader_precache_play) { _, _ -> play() }
                    }
                }
            }
            dialog.setNegativeButton(R.string.loopnative_close, null).show()
        }
    }

    /** The builder's stage names (shader_warm_builder.cpp) in the app's language. */
    private fun stageText(
        activity: FragmentActivity,
        stage: String,
        done: Long,
        total: Long
    ): String {
        val name = when (stage) {
            "Reading game files" -> activity.getString(R.string.shader_precache_reading)
            "Extracting shaders" -> activity.getString(R.string.shader_precache_extracting)
            "Converting" -> activity.getString(R.string.shader_precache_converting)
            "Writing" -> activity.getString(R.string.shader_precache_writing)
            else -> stage
        }
        return if (total > 0) "$name  $done / $total" else name
    }
}
