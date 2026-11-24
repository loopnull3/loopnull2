// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.loopnative

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import androidx.core.content.edit
import androidx.preference.PreferenceManager
import org.json.JSONArray
import org.json.JSONObject
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.features.settings.model.StringSetting
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.utils.Log
import java.io.File

/**
 * LoopNative, the engine built for the Galaxy Z Flip5 (Snapdragon 8 Gen 2, Adreno 740, 8 GB): the
 * emulator core started with the phone's and the game's tuning, guest memory on storage when the
 * game needs more RAM than the phone has, and a governor that trades frame rate for temperature.
 */
object LoopNative {
    /** One game's start plan on this phone (LoopNativeJni.sessionPlan). */
    data class Plan(
        val deviceName: String,
        val deviceClass: String,
        val ramTier: String,
        val usableRamBytes: Long,
        val turnipDriver: Boolean,
        val recommendTurnip: Boolean,
        val recommendRadv: Boolean,
        val profileName: String?,
        val ramNeedGib: Double?,
        val reportedOn8gb: Boolean,
        val note: String?,
        val memoryFit: String,
        val storageBackedMemory: Boolean,
        val startFpsLimit: Int,
        val overrides: List<Pair<String, String>>
    ) {
        val isFlip5 get() = deviceClass == "galaxy_z_flip5"
        val isS26Exynos get() = deviceClass == "galaxy_s26_exynos"

        /** The session's value for a setting, null when it keeps the user's. */
        fun override(key: String): String? = overrides.firstOrNull { it.first == key }?.second
    }

    /** What Prepare found in a game's ExeFS (the pack manifest's summary). */
    data class PrepareSummary(
        val modules: Int,
        val validModules: Int,
        val patchSites: Long,
        val svcSites: Long,
        val nvnModule: String,
        val nvnEntry: Boolean,
        val sdk: String,
        val errors: List<String>
    )

    /** LoopNative's per-game options (Game properties › LoopNative options). */
    enum class Option(val key: String) {
        /** Guest memory in a file on storage instead of RAM. Default: the plan's. */
        STORAGE_MEMORY("storage_memory"),

        /** The thermal governor. Default: on. */
        GOVERNOR("governor"),

        /** Start with the user's own settings instead of LoopNative's tuning. Default: off. */
        KEEP_SETTINGS("keep_settings"),

        /** FSR 3 frame generation: a drawn frame and a generated one in between. Default: off. */
        FRAME_GEN("frame_gen")
    }

    @Volatile
    private var deviceReady = false

    fun titleIdOf(game: Game): String = game.programIdHex.uppercase().padStart(16, '0')

    /** The phone's RAM as Android reports it (MemTotal), in GiB. */
    fun totalRamGib(context: Context): Double {
        val memory = ActivityManager.MemoryInfo()
        context.getSystemService(ActivityManager::class.java)?.getMemoryInfo(memory)
        return memory.totalMem / (1024.0 * 1024.0 * 1024.0)
    }

    /** Tells the native side about the phone, once per process (the GPU name needs the driver). */
    fun ensureDevice(context: Context) {
        if (deviceReady) {
            return
        }
        val memory = ActivityManager.MemoryInfo()
        context.getSystemService(ActivityManager::class.java)?.getMemoryInfo(memory)
        val gpu = runCatching { NativeLibrary.getGpuModel() }.getOrDefault("")
        // A custom driver's file name says whether it is Mesa's Turnip before Vulkan reports it.
        val driver = runCatching { StringSetting.DRIVER_PATH.getString() }.getOrDefault("")
        val soc = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Build.SOC_MODEL
        } else {
            Build.BOARD
        }
        LoopNativeJni.setDevice(
            Build.MANUFACTURER ?: "",
            Build.MODEL ?: "",
            Build.DEVICE ?: "",
            soc ?: "",
            "$gpu ${File(driver).name}".trim(),
            memory.totalMem,
            Build.VERSION.SDK_INT
        )
        deviceReady = true
    }

    /** The GPU driver changed: the next plan reads the phone again. */
    fun invalidateDevice() {
        deviceReady = false
    }

    fun hasProfile(game: Game): Boolean =
        runCatching { LoopNativeJni.hasProfile(titleIdOf(game)) }.getOrDefault(false)

    fun plan(context: Context, game: Game): Plan {
        ensureDevice(context)
        val root = JSONObject(LoopNativeJni.sessionPlan(titleIdOf(game)))
        val device = root.getJSONObject("device")
        val profile = root.optJSONObject("profile")
        val overrides = mutableListOf<Pair<String, String>>()
        val list = root.getJSONArray("overrides")
        for (i in 0 until list.length()) {
            val entry = list.getJSONArray(i)
            overrides += entry.getString(0) to entry.getString(1)
        }
        return Plan(
            deviceName = device.getString("display_name"),
            deviceClass = device.getString("device_class"),
            ramTier = device.getString("ram_tier"),
            usableRamBytes = device.getLong("usable_ram_bytes"),
            turnipDriver = device.getBoolean("turnip_driver"),
            recommendTurnip = device.getBoolean("recommend_turnip"),
            recommendRadv = device.optBoolean("recommend_radv"),
            profileName = profile?.getString("name"),
            ramNeedGib = profile?.getDouble("ram_need_gib"),
            reportedOn8gb = profile?.getBoolean("reported_on_8gb") ?: false,
            note = profile?.getString("note"),
            memoryFit = root.getString("memory_fit"),
            storageBackedMemory = root.getBoolean("storage_backed_memory"),
            startFpsLimit = root.getInt("start_fps_limit"),
            overrides = overrides
        )
    }

    /** The phone as LoopNative classifies it (LoopNativeJni.deviceTuning). */
    data class Device(
        val deviceClass: String,
        val ramTier: String,
        val recommendTurnip: Boolean,
        val recommendRadv: Boolean
    ) {
        val isS26Exynos get() = deviceClass == "galaxy_s26_exynos"
    }

    fun device(context: Context): Device {
        ensureDevice(context)
        val root = JSONObject(LoopNativeJni.deviceTuning())
        return Device(
            deviceClass = root.getString("device_class"),
            ramTier = root.getString("ram_tier"),
            recommendTurnip = root.getBoolean("recommend_turnip"),
            recommendRadv = root.getBoolean("recommend_radv")
        )
    }

    /** Whether LoopNative is what Auto picks: games it has a profile for, and every game on 8 GB phones. */
    fun preferredFor(context: Context, game: Game): Boolean {
        if (hasProfile(game)) {
            return true
        }
        return runCatching {
            ensureDevice(context)
            val tier = JSONObject(LoopNativeJni.deviceTuning()).getString("ram_tier")
            tier == "8gb" || tier == "6gb_or_less"
        }.getOrDefault(false)
    }

    private fun optionKey(game: Game, option: Option) =
        "loopnative_${option.key}_${titleIdOf(game)}"

    fun option(context: Context, game: Game, option: Option): Boolean {
        val preferences = PreferenceManager.getDefaultSharedPreferences(context)
        val key = optionKey(game, option)
        if (preferences.contains(key)) {
            return preferences.getBoolean(key, false)
        }
        return when (option) {
            Option.STORAGE_MEMORY ->
                runCatching { plan(context, game).storageBackedMemory }.getOrDefault(false)
            Option.GOVERNOR -> true
            Option.KEEP_SETTINGS -> false
            Option.FRAME_GEN -> false
        }
    }

    fun setOption(context: Context, game: Game, option: Option, on: Boolean) {
        PreferenceManager.getDefaultSharedPreferences(context).edit {
            putBoolean(optionKey(game, option), on)
        }
    }

    /** Where Prepare keeps the game's pack manifest. */
    fun manifestFile(context: Context, game: Game): File =
        File(
            context.getExternalFilesDir(null) ?: context.filesDir,
            "loopnative/packs/${titleIdOf(game)}/manifest.json"
        )

    fun isPrepared(context: Context, game: Game): Boolean = manifestFile(context, game).isFile

    /** The guest memory file's directory: internal storage, where a file can be mapped shared. */
    fun memoryBackingDir(context: Context): File =
        File(context.noBackupFilesDir, "loopnative/memory")

    /** Runs Prepare: blocks for seconds (large games); call it off the main thread. */
    fun prepare(context: Context, game: Game): PrepareSummary {
        ensureDevice(context)
        val manifest = manifestFile(context, game)
        manifest.parentFile?.mkdirs()
        val json = LoopNativeJni.prepare(
            game.path,
            titleIdOf(game),
            game.title,
            manifest.absolutePath
        )
        Log.info("[LoopNative] Prepared ${game.title}")
        return summarize(JSONObject(json))
    }

    /** The summary of the manifest Prepare left, null when there is none or it can't be read. */
    fun lastPrepare(context: Context, game: Game): PrepareSummary? = runCatching {
        summarize(JSONObject(manifestFile(context, game).readText()))
    }.getOrNull()

    private fun summarize(root: JSONObject): PrepareSummary {
        val summary = root.getJSONObject("summary")
        val modules: JSONArray = root.getJSONArray("modules")
        var valid = 0
        val errors = mutableListOf<String>()
        for (i in 0 until modules.length()) {
            val module = modules.getJSONObject(i)
            if (module.getBoolean("valid")) {
                valid++
            } else {
                errors += module.getString("file") + ": " + module.optString("error")
            }
        }
        return PrepareSummary(
            modules = modules.length(),
            validModules = valid,
            patchSites = summary.getLong("patch_sites"),
            svcSites = summary.getLong("svc_sites"),
            nvnModule = summary.getString("nvn_module"),
            nvnEntry = !summary.isNull("nvn_bootstrap_loader"),
            sdk = summary.getString("sdk"),
            errors = errors
        )
    }
}
