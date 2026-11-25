// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.loopnative

import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.progressindicator.LinearProgressIndicator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.utils.ShaderPrecache
import java.util.Locale

/**
 * LoopNative's screens: Prepare (the first start of a game reads its ExeFS once and shows what it
 * found and how LoopNative will run it on this phone), then the start itself.
 */
object LoopNativeLauncher {
    /** Starts the game on LoopNative, preparing it first when it has never been prepared. */
    fun launch(activity: FragmentActivity, game: Game, emulate: () -> Unit) {
        if (LoopNative.isPrepared(activity, game)) {
            start(activity, game, emulate)
        } else {
            prepare(activity, game, emulate, startAfter = true)
        }
    }

    /** Prepare again and show the report (Game properties); [emulate] is offered after. */
    fun showReport(activity: FragmentActivity, game: Game, emulate: () -> Unit) =
        prepare(activity, game, emulate, startAfter = false)

    private fun start(activity: FragmentActivity, game: Game, emulate: () -> Unit) {
        runCatching { LoopNativeSession.arm(activity, game) }
            .onFailure {
                MaterialAlertDialogBuilder(activity)
                    .setTitle(R.string.loopnative_start_failed)
                    .setMessage(it.message ?: it.toString())
                    .setPositiveButton(R.string.game_engine_play_here) { _, _ -> emulate() }
                    .setNegativeButton(android.R.string.cancel, null)
                    .show()
                return
            }
        emulate()
    }

    private fun prepare(
        activity: FragmentActivity,
        game: Game,
        emulate: () -> Unit,
        startAfter: Boolean
    ) {
        val fullWidth = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        )
        val progress = LinearProgressIndicator(activity).apply { isIndeterminate = true }
        val box = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            val pad = (24 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad / 2, pad, 0)
            addView(
                TextView(activity).apply { setText(R.string.loopnative_preparing_description) },
                fullWidth
            )
            addView(
                progress,
                LinearLayout.LayoutParams(fullWidth).apply { topMargin = pad / 2 }
            )
        }
        val working = MaterialAlertDialogBuilder(activity)
            .setTitle(activity.getString(R.string.loopnative_preparing_title, game.title))
            .setView(box)
            .setCancelable(false)
            .show()
        activity.lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    LoopNative.prepare(activity, game) to LoopNative.plan(activity, game)
                }
            }
            working.dismiss()
            result.onSuccess { (summary, plan) ->
                val ready = MaterialAlertDialogBuilder(activity)
                    .setTitle(activity.getString(R.string.loopnative_ready_title, game.title))
                    .setMessage(describe(activity, game, plan, summary))
                    .setPositiveButton(R.string.loopnative_play) { _, _ ->
                        start(activity, game, emulate)
                    }
                    .setNegativeButton(
                        if (startAfter) android.R.string.cancel else R.string.loopnative_close,
                        null
                    )
                // Shaders built on the loading screen instead of while playing: less stutter
                if (!runCatching { ShaderPrecache.exists(game.programId) }.getOrDefault(true)) {
                    ready.setNeutralButton(R.string.shader_precache_first) { _, _ ->
                        ShaderPrecache.show(activity, game) { start(activity, game, emulate) }
                    }
                }
                ready.show()
            }.onFailure {
                MaterialAlertDialogBuilder(activity)
                    .setTitle(R.string.loopnative_prepare_failed)
                    .setMessage(it.message ?: it.toString())
                    .setPositiveButton(R.string.loopnative_play) { _, _ ->
                        start(activity, game, emulate)
                    }
                    .setNegativeButton(android.R.string.cancel, null)
                    .show()
            }
        }
    }

    /** "Picture: 0.5x render, upscaled with AMD FSR", or null when the session keeps the user's. */
    private fun renderLine(activity: FragmentActivity, plan: LoopNative.Plan): String? {
        val scale = when (plan.override("resolution_setup")) {
            "0" -> "0.25x"
            "1" -> "0.5x"
            "2" -> "0.75x"
            "3" -> "1x"
            else -> return null
        }
        val upscaler = when (plan.override("scaling_filter")) {
            "6" -> "AMD FSR"
            "13" -> "Qualcomm SGSR"
            else -> return activity.getString(R.string.loopnative_report_render, scale)
        }
        return activity.getString(R.string.loopnative_report_render_upscaled, scale, upscaler)
    }

    /** The Prepare report: the phone, the game's memory against it, and what the ExeFS holds. */
    fun describe(
        activity: FragmentActivity,
        game: Game,
        plan: LoopNative.Plan,
        summary: LoopNative.PrepareSummary?
    ): String = buildString {
        val usableGib = plan.usableRamBytes / (1024.0 * 1024.0 * 1024.0)
        appendLine(
            activity.getString(
                R.string.loopnative_report_device,
                plan.deviceName,
                String.format(Locale.ROOT, "%.1f", usableGib)
            )
        )
        if (plan.profileName != null && plan.ramNeedGib != null) {
            appendLine(
                activity.getString(
                    R.string.loopnative_report_memory,
                    String.format(Locale.ROOT, "%.1f", plan.ramNeedGib),
                    activity.getString(
                        when (plan.memoryFit) {
                            "fits" -> R.string.loopnative_fit_fits
                            "tight" -> R.string.loopnative_fit_tight
                            else -> R.string.loopnative_fit_unlikely
                        }
                    )
                )
            )
            plan.note?.let { appendLine(it) }
        } else {
            appendLine(activity.getString(R.string.loopnative_report_no_profile))
        }
        val storage = LoopNative.option(activity, game, LoopNative.Option.STORAGE_MEMORY)
        appendLine(
            activity.getString(
                if (storage) {
                    R.string.loopnative_report_storage_on
                } else {
                    R.string.loopnative_report_storage_off
                }
            )
        )
        if (!LoopNative.option(activity, game, LoopNative.Option.KEEP_SETTINGS)) {
            renderLine(activity, plan)?.let { appendLine(it) }
        }
        if (plan.recommendTurnip) {
            appendLine(activity.getString(R.string.loopnative_report_turnip))
        }
        if (plan.recommendRadv) {
            appendLine(activity.getString(R.string.loopnative_report_radv))
        }
        if (LoopNative.option(activity, game, LoopNative.Option.FRAME_GEN)) {
            appendLine(activity.getString(R.string.loopnative_report_frame_gen))
        }
        if (summary != null) {
            appendLine()
            appendLine(
                activity.getString(
                    R.string.loopnative_report_modules,
                    summary.validModules,
                    summary.modules,
                    summary.patchSites,
                    summary.svcSites
                )
            )
            if (summary.nvnEntry) {
                appendLine(activity.getString(R.string.loopnative_report_nvn, summary.nvnModule))
            }
            if (summary.sdk.isNotEmpty()) {
                appendLine(summary.sdk.removePrefix("SDK MW+Nintendo+"))
            }
            summary.errors.take(3).forEach { appendLine("• $it") }
        }
    }.trimEnd()
}
