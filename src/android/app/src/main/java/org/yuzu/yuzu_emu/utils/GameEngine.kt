// SPDX-FileCopyrightText: Copyright 2026 Hokkaido contributors
// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.content.Context
import androidx.core.content.edit
import androidx.fragment.app.FragmentActivity
import androidx.preference.PreferenceManager
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.loopnative.LoopNative
import org.yuzu.yuzu_emu.loopnative.LoopNativeLauncher
import org.yuzu.yuzu_emu.model.Game
import java.io.File

/**
 * Which engine runs a game: Hokkaido's emulator as configured, or LoopNative (the same core
 * started with the Galaxy Z Flip5's and the game's tuning, storage-backed guest memory and the
 * thermal governor).
 */
object GameEngine {
    enum class Choice { AUTO, EMULATE, NATIVE }

    private fun key(game: Game) = "game_engine_" + game.programIdHex.uppercase()

    fun choice(context: Context, game: Game): Choice {
        val stored = PreferenceManager.getDefaultSharedPreferences(context)
            .getString(key(game), null)
        return Choice.entries.firstOrNull { it.name == stored } ?: Choice.AUTO
    }

    fun setChoice(context: Context, game: Game, choice: Choice) {
        PreferenceManager.getDefaultSharedPreferences(context).edit {
            if (choice == Choice.AUTO) remove(key(game)) else putString(key(game), choice.name)
        }
    }

    /** LoopNative runs any game but homebrew. */
    fun supports(game: Game): Boolean = !game.isHomebrew

    /** What Auto picks: LoopNative for the games it has a profile for and on 8 GB phones. */
    fun autoIsNative(context: Context, game: Game): Boolean =
        supports(game) && LoopNative.preferredFor(context, game)

    fun usesNative(context: Context, game: Game): Boolean = when (choice(context, game)) {
        Choice.NATIVE -> supports(game)
        Choice.EMULATE -> false
        Choice.AUTO -> autoIsNative(context, game)
    }

    fun nativeName(context: Context, game: Game): String = context.getString(
        if (LoopNative.hasProfile(game)) {
            R.string.game_engine_native
        } else {
            R.string.game_engine_native_generic
        }
    )

    fun describe(context: Context, game: Game): String = when (choice(context, game)) {
        Choice.EMULATE -> context.getString(R.string.game_engine_emulate)
        Choice.NATIVE -> nativeName(context, game)
        Choice.AUTO -> context.getString(
            R.string.game_engine_auto_now,
            if (autoIsNative(context, game)) {
                nativeName(context, game)
            } else {
                context.getString(R.string.game_engine_emulate)
            }
        )
    }

    /** LoopNative's game options Hokkaido's Game properties show; they apply at the next start. */
    enum class GameOption(val option: LoopNative.Option, val title: Int, val description: Int) {
        STORAGE_MEMORY(
            LoopNative.Option.STORAGE_MEMORY,
            R.string.loopnative_option_storage_memory,
            R.string.loopnative_option_storage_memory_description
        ),
        GOVERNOR(
            LoopNative.Option.GOVERNOR,
            R.string.loopnative_option_governor,
            R.string.loopnative_option_governor_description
        ),
        KEEP_SETTINGS(
            LoopNative.Option.KEEP_SETTINGS,
            R.string.loopnative_option_keep_settings,
            R.string.loopnative_option_keep_settings_description
        )
    }

    fun gameOptions(game: Game): List<GameOption> =
        if (supports(game)) GameOption.entries else emptyList()

    fun gameOption(context: Context, game: Game, option: GameOption): Boolean =
        LoopNative.option(context, game, option.option)

    fun setGameOption(context: Context, game: Game, option: GameOption, on: Boolean) =
        LoopNative.setOption(context, game, option.option, on)

    /** Prepares the game again and shows LoopNative's report; [emulate] is offered after. */
    fun showNativeReport(activity: FragmentActivity, game: Game, emulate: () -> Unit) =
        LoopNativeLauncher.showReport(activity, game, emulate)

    /**
     * Starts a game in its engine (Game properties › Engine): every way of starting a game comes here.
     * [useCustom] is the game's own settings rather than the global ones (Start › Global or Custom);
     * [emulate] opens the emulation screen.
     */
    @Suppress("UNUSED_PARAMETER")
    fun start(
        activity: FragmentActivity,
        game: Game,
        useCustom: Boolean = true,
        emulate: () -> Unit
    ) {
        if (usesNative(activity, game)) {
            LoopNativeLauncher.launch(activity, game, emulate)
        } else {
            emulate()
        }
    }

    /** Removes the game's save folder. Blocks: call it off the main thread. False when something stayed. */
    fun deleteSave(context: Context, game: Game): Boolean {
        val save = File(game.saveDir)
        return !save.exists() || save.deleteRecursively()
    }
}
