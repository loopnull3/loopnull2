// SPDX-FileCopyrightText: Copyright 2026 Hokkaido contributors
// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.app.Application
import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import androidx.core.content.edit
import androidx.fragment.app.FragmentActivity
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.features.settings.model.StringSetting
import org.yuzu.yuzu_emu.features.settings.utils.SettingsFile
import org.yuzu.yuzu_emu.loopnative.LoopNative
import org.yuzu.yuzu_emu.loopnative.LoopNativeLauncher
import org.yuzu.yuzu_emu.model.Game
import java.io.File

/**
 * Which engine runs a game:
 * - Hokkaido: the emulator core as configured;
 * - LoopNative: the same core started with the phone's and the game's tuning, storage-backed guest
 *   memory and the thermal governor;
 * - LoopNative Pack: Banana Native with LoopNative's changes (the :banana module, in the mainline
 *   flavor). It rewrites the game's code and translates its shaders into a pack once (Prepare), then
 *   runs the code natively with NVN on Vulkan. Only the games it has a profile for.
 */
object GameEngine {
    enum class Choice { AUTO, EMULATE, NATIVE, PACK }

    /** The pack engine's own processes (its manifest): they run none of the emulator's setup. */
    private val embeddedProcesses = listOf(":game", ":pack", ":readiness", ":selftest")

    /** The pack engine's UI, in the mainline flavor only. */
    private val pack: NativeEngineUi? by lazy {
        runCatching {
            Class.forName("org.yuzu.yuzu_emu.banana.BananaLauncher").getDeclaredConstructor()
                .newInstance() as NativeEngineUi
        }.getOrNull()
    }

    /** Whether this build carries the pack engine (the mainline flavor). */
    val isPackAvailable: Boolean get() = pack != null

    /** What the pack engine's application class did in every process (PickedNup). */
    fun installEmbedded(context: Context) {
        runCatching { pack?.install(context) }
            .onFailure { android.util.Log.e("GameEngine", "Pack engine setup failed", it) }
    }

    fun isEmbeddedProcess(): Boolean {
        val name = Application.getProcessName() ?: return false
        return embeddedProcesses.any { name.endsWith(it) }
    }

    private fun key(game: Game) = "game_engine_" + game.programIdHex.uppercase()

    private fun titleIdOf(game: Game) = LoopNative.titleIdOf(game)

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

    /** The pack engine's status for the game ("playable", "bring-up"), null when it cannot run it. */
    private fun packStatus(game: Game): String? =
        runCatching { pack?.status(titleIdOf(game)) }.getOrNull()

    /** Whether the pack engine has a profile for the game: only then is it a choice. */
    fun supportsPack(game: Game): Boolean = supports(game) && packStatus(game) != null

    private const val OCARINA_OF_TIME = "0400486024DEC000"

    /**
     * Whether Auto takes the pack engine: a game its profile calls playable, on a phone with the RAM
     * that profile requires (Mario Kart World: 14 GiB, measured on the pack engine) or, short of
     * it, a 12 GB phone with LoopNative's memory class on for the game.
     * - On Xclipse it needs a custom RADV driver: Samsung's own has no placed memory maps.
     * - Ocarina of Time's profile is still "bring-up", but upstream (Hokkaido 0.7.1) reports it
     *   playable on the Galaxy S26 with RADV, and its emulator path short of memory on 12 GB.
     */
    private fun autoIsPack(context: Context, game: Game): Boolean {
        if (!supports(game)) {
            return false
        }
        val status = packStatus(game) ?: return false
        val device = runCatching { LoopNative.device(context) }.getOrNull()
        if (device?.recommendRadv == true) {
            return false
        }
        val provenHere = device?.isS26Exynos == true && titleIdOf(game) == OCARINA_OF_TIME
        if (status != "playable" && !provenHere) {
            return false
        }
        val required = runCatching { pack?.requiredRamGib(titleIdOf(game)) }.getOrNull() ?: 0.0
        if (required <= 0.0 || LoopNative.totalRamGib(context) >= required) {
            return true
        }
        // Short of the profile's RAM: only a 12 GB phone with LoopNative's memory class on.
        return runCatching { pack?.memoryClassCovers(context, titleIdOf(game)) }.getOrNull() ?: false
    }

    /**
     * What Auto picks: the pack engine where it plays the game, else LoopNative where it has a
     * profile or the phone has 8 GB, else Hokkaido.
     */
    fun auto(context: Context, game: Game): Choice = when {
        autoIsPack(context, game) -> Choice.PACK
        supports(game) && LoopNative.preferredFor(context, game) -> Choice.NATIVE
        else -> Choice.EMULATE
    }

    /** The engine that starts the game: the stored choice where it can run it, else Auto's. */
    fun effective(context: Context, game: Game): Choice = when (choice(context, game)) {
        Choice.PACK -> if (supportsPack(game)) Choice.PACK else auto(context, game)
        Choice.NATIVE -> if (supports(game)) Choice.NATIVE else Choice.EMULATE
        Choice.EMULATE -> Choice.EMULATE
        Choice.AUTO -> auto(context, game)
    }

    /** Whether the game starts through an engine's own launcher (Prepare) rather than at once. */
    fun usesLauncher(context: Context, game: Game): Boolean =
        effective(context, game) != Choice.EMULATE

    fun name(context: Context, game: Game, choice: Choice): String = when (choice) {
        Choice.AUTO -> context.getString(
            R.string.game_engine_auto_now,
            name(context, game, auto(context, game))
        )
        Choice.EMULATE -> context.getString(R.string.game_engine_emulate)
        Choice.NATIVE -> context.getString(
            if (LoopNative.hasProfile(game)) {
                R.string.game_engine_native
            } else {
                R.string.game_engine_native_generic
            }
        )
        Choice.PACK -> context.getString(
            if (packStatus(game) == "playable") {
                R.string.game_engine_pack
            } else {
                R.string.game_engine_pack_experimental
            }
        )
    }

    /** The engines this game can be given (Game properties › Engine). */
    fun choices(game: Game): List<Choice> = Choice.entries.filter {
        when (it) {
            Choice.PACK -> supportsPack(game)
            Choice.NATIVE -> supports(game)
            else -> true
        }
    }

    fun describe(context: Context, game: Game): String = name(context, game, choice(context, game))

    /** LoopNative's game options (Game properties › LoopNative options); they apply at the next start. */
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
        ),
        FRAME_GEN(
            LoopNative.Option.FRAME_GEN,
            R.string.loopnative_option_frame_gen,
            R.string.loopnative_option_frame_gen_description
        )
    }

    fun gameOptions(game: Game): List<GameOption> =
        if (supports(game)) GameOption.entries else emptyList()

    fun gameOption(context: Context, game: Game, option: GameOption): Boolean =
        LoopNative.option(context, game, option.option)

    fun setGameOption(context: Context, game: Game, option: GameOption, on: Boolean) =
        LoopNative.setOption(context, game, option.option, on)

    /** The pack engine's game options ([id]: HokkaidoBridge.OPTION_*); they apply at the next start. */
    enum class PackOption(val id: Int, val title: Int, val description: Int) {
        FULL_VEGETATION(
            0,
            R.string.banana_option_vegetation,
            R.string.banana_option_vegetation_description
        ),
        RAM_SAVER(
            1,
            R.string.banana_option_ram_saver,
            R.string.banana_option_ram_saver_description
        ),
        RAM_SAVER_STRONG(
            3,
            R.string.banana_option_ram_saver_strong,
            R.string.banana_option_ram_saver_strong_description
        ),
        SOFT_TEXTURES(
            2,
            R.string.banana_option_soft_textures,
            R.string.banana_option_soft_textures_description
        ),
        MEMORY_CLASS(
            4,
            R.string.loopnative_option_memory_class,
            R.string.loopnative_option_memory_class_description
        )
    }

    fun packOptions(context: Context, game: Game): List<PackOption> {
        val engine = pack ?: return emptyList()
        val lowMemory = runCatching { engine.lowMemoryPhone(context) }.getOrDefault(false)
        return PackOption.entries.filter {
            (it != PackOption.MEMORY_CLASS || lowMemory) &&
                runCatching { engine.hasOption(titleIdOf(game), it.id) }.getOrDefault(false)
        }
    }

    fun packOption(context: Context, game: Game, option: PackOption): Boolean =
        runCatching { pack?.option(context, titleIdOf(game), option.id) }.getOrNull() ?: false

    fun setPackOption(context: Context, game: Game, option: PackOption, on: Boolean) {
        pack?.setOption(context, titleIdOf(game), option.id, on)
    }

    /**
     * Game properties › Build all shaders now, for the emulator engines: builds the game's shader
     * precache, then offers to play it on the engine it would start on (not offered when that is
     * the pack engine, whose shaders are its own).
     */
    fun buildEmulatorShaders(activity: FragmentActivity, game: Game, emulate: () -> Unit) {
        val onLoopNative: () -> Unit = { LoopNativeLauncher.launch(activity, game, emulate) }
        val play: (() -> Unit)? = when (effective(activity, game)) {
            Choice.PACK -> null
            Choice.NATIVE -> onLoopNative
            else -> emulate
        }
        ShaderPrecache.show(activity, game, play)
    }

    /** Prepares the game again and shows LoopNative's report; [emulate] is offered after. */
    fun showNativeReport(activity: FragmentActivity, game: Game, emulate: () -> Unit) =
        LoopNativeLauncher.showReport(activity, game, emulate)

    /**
     * Imports a pack that Banana Native's own app exported (Manage › Export pack…) for this game, from the zip
     * the user picked. Null when it is in, else why not. Blocks while it copies: call it off the main thread.
     */
    fun importPack(context: Context, game: Game, zip: Uri): String? {
        val engine = pack ?: return context.getString(R.string.banana_pack_import_unavailable)
        val titleId = titleIdOf(game)
        // A file on shared storage is read in place (the app has all files access for the pack engine);
        // any other document through its descriptor.
        storagePath(zip)?.let { File(it) }?.takeIf { it.isFile }?.let {
            return engine.importPack(context, it, titleId, filePath(game))
        }
        val descriptor = runCatching { context.contentResolver.openFileDescriptor(zip, "r") }.getOrNull()
            ?: return context.getString(R.string.banana_pack_import_unreadable)
        return descriptor.use {
            engine.importPack(context, File("/proc/self/fd/${it.fd}"), titleId, filePath(game))
        }
    }

    /**
     * The game's path for the pack engine, which opens games by path: a storage document URI becomes
     * /storage/<volume>/<path>. A loose game (its exefs/main is listed) is its dump folder, the one
     * holding base/ and updates/<version>/ (or exefs/), as Banana Native expects.
     */
    private fun filePath(game: Game): String? {
        val path = storagePath(Uri.parse(game.path)) ?: return null
        if (!path.endsWith("/exefs/main")) {
            return path
        }
        val folder = path.removeSuffix("/exefs/main")
        return when {
            Regex(".*/updates/[0-9]+$").matches(folder) -> folder.replace(Regex("/updates/[0-9]+$"), "")
            folder.endsWith("/base") -> folder.removeSuffix("/base")
            else -> folder
        }
    }

    /** A file URI's path, or a shared storage document's /storage/<volume>/<path>; null for other documents. */
    private fun storagePath(uri: Uri): String? {
        return if (uri.scheme == null || uri.scheme == "file") {
            uri.path ?: return null
        } else {
            if (uri.authority != "com.android.externalstorage.documents") {
                return null
            }
            val id = runCatching { DocumentsContract.getDocumentId(uri) }.getOrNull() ?: return null
            val volume = id.substringBefore(':')
            val relative = id.substringAfter(':', "")
            val root = if (volume == "primary") "/storage/emulated/0" else "/storage/$volume"
            if (relative.isEmpty()) root else "$root/$relative"
        }
    }

    /**
     * Starts the game in the pack engine: at once when it is prepared, else after preparing it.
     * False when this build has no pack engine or the game has no path it reads.
     */
    private fun launchPack(
        activity: FragmentActivity,
        game: Game,
        useCustom: Boolean,
        emulate: () -> Unit
    ): Boolean {
        val engine = pack ?: return false
        val path = filePath(game) ?: return false
        val (driver, window) = gameSettings(game, useCustom)
        engine.launch(activity, path, game.title, driver, game.saveDir, window, emulate)
        return true
    }

    /**
     * Translates every shader of the game for the pack engine now (Game properties), with the game's own settings
     * for the start offered after. False when this build has no pack engine or the game has no path it reads.
     */
    fun preparePackShaders(activity: FragmentActivity, game: Game, emulate: () -> Unit): Boolean {
        val engine = pack ?: return false
        val path = filePath(game) ?: return false
        val (driver, window) = gameSettings(game, true)
        engine.prepareShaders(activity, path, game.title, driver, game.saveDir, window, emulate)
        return true
    }

    /**
     * Starts a game in its engine (Game properties › Engine): every way of starting a game comes here.
     * [useCustom] is the game's own settings rather than the global ones (Start › Global or Custom);
     * [emulate] opens the emulation screen.
     */
    fun start(
        activity: FragmentActivity,
        game: Game,
        useCustom: Boolean = true,
        emulate: () -> Unit
    ) {
        when (effective(activity, game)) {
            Choice.EMULATE, Choice.AUTO -> emulate()
            Choice.NATIVE -> LoopNativeLauncher.launch(activity, game, emulate)
            Choice.PACK -> if (!launchPack(activity, game, useCustom, emulate)) {
                MaterialAlertDialogBuilder(activity)
                    .setTitle(R.string.game_engine_pack_missing)
                    .setMessage(R.string.game_engine_pack_missing_description)
                    .setPositiveButton(R.string.game_engine_play_loopnative) { _, _ ->
                        LoopNativeLauncher.launch(activity, game, emulate)
                    }
                    .setNegativeButton(android.R.string.cancel, null)
                    .show()
            }
        }
    }

    /**
     * What the app would start the game with: the GPU driver (a zip; empty: the system's) and the render
     * size its resolution scale gives, 1x being 1920x1080. With [useCustom] the game's own settings,
     * else the global ones.
     */
    private fun gameSettings(game: Game, useCustom: Boolean): Pair<String, String> {
        val loaded = NativeConfig.isPerGameConfigLoaded()
        if (useCustom && !loaded) {
            SettingsFile.loadCustomConfig(game)
        }
        val needsGlobal = !useCustom
        val driver = StringSetting.DRIVER_PATH.getString(needsGlobal)
        val window = windowOf(IntSetting.RENDERER_RESOLUTION.getInt(needsGlobal))
        if (useCustom && !loaded) {
            NativeConfig.unloadPerGameConfig()
            NativeConfig.reloadGlobalConfig()
        }
        return driver to window
    }

    /**
     * Removes the game's save folder and what the pack engine keeps beside it, which would bring the save back:
     * its transaction and journal (<save>.work, .new, .re, .old, .restore, .pend1, .pend2, .restore-set), its
     * lock, and its device save (<save>_device and that one's). The pack engine's game process is closed first
     * when it holds the game. Blocks: call it off the main thread. False when something stayed.
     */
    fun deleteSave(context: Context, game: Game): Boolean {
        val save = File(game.saveDir)
        runCatching { pack?.closeGame(context, game.programIdHex) }
        val parent = save.parentFile ?: return !save.exists() || save.deleteRecursively()
        var ok = true
        parent.listFiles()?.filter {
            it.name == save.name || it.name.startsWith(save.name + ".") ||
                it.name.startsWith(save.name + "_device")
        }?.forEach { ok = it.deleteRecursively() && ok }
        return ok
    }

    /** ResolutionSetup (Res1_4X … Res8X) as the pack engine's Game resolution; above 2x the game's own size. */
    private fun windowOf(setup: Int): String {
        val scale = listOf(0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0).getOrNull(setup) ?: return "original"
        return "${(1920 * scale).toInt()}x${(1080 * scale).toInt()}"
    }
}
