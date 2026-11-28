// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu

import android.content.DialogInterface
import android.net.Uri
import android.text.Html
import android.text.method.LinkMovementMethod
import android.view.Surface
import android.view.View
import android.widget.TextView
import androidx.annotation.Keep
import androidx.core.net.toUri
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.libsdl.app.SDL
import java.lang.ref.WeakReference
import org.yuzu.yuzu_emu.activities.EmulationActivity
import org.yuzu.yuzu_emu.fragments.CoreErrorDialogFragment
import org.yuzu.yuzu_emu.utils.DocumentsTree
import org.yuzu.yuzu_emu.utils.FileUtil
import org.yuzu.yuzu_emu.utils.Log
import org.yuzu.yuzu_emu.model.InstallResult
import org.yuzu.yuzu_emu.model.Patch
import org.yuzu.yuzu_emu.model.GameVerificationResult
import org.yuzu.yuzu_emu.network.NetPlayManager
import org.yuzu.yuzu_emu.applets.web.WebBrowser

/**
 * Class which contains methods that interact
 * with the native side of the Yuzu code.
 */
object NativeLibrary {
    @Keep
    data class UpdateResult(
        var tag: String = "",
        var title: String = "",
        var body: String = "",
        var url: String = "",
        var assets: MutableList<String> = mutableListOf()
    ) {
        fun addAsset(asset: String) {
            assets.add(asset)
        }
    }

    @JvmField
    var sEmulationActivity = WeakReference<EmulationActivity?>(null)

    init {
        try {
            System.loadLibrary("yuzu-android")
            SDL.setupJNI()
            initJvm()
        } catch (ex: UnsatisfiedLinkError) {
            error("[NativeLibrary] $ex")
        }
    }

    @Keep
    @JvmStatic
    fun openContentUri(path: String?, openmode: String?): Int {
        return if (DocumentsTree.isNativePath(path!!)) {
            YuzuApplication.documentsTree!!.openContentUri(path, openmode)
        } else {
            FileUtil.openContentUri(path, openmode)
        }
    }

    @Keep
    @JvmStatic
    fun getSize(path: String?): Long {
        return if (DocumentsTree.isNativePath(path!!)) {
            YuzuApplication.documentsTree!!.getFileSize(path)
        } else {
            FileUtil.getFileSize(path)
        }
    }

    @Keep
    @JvmStatic
    fun exists(path: String?): Boolean {
        return if (DocumentsTree.isNativePath(path!!)) {
            YuzuApplication.documentsTree!!.exists(path)
        } else {
            FileUtil.exists(path, suppressLog = true)
        }
    }

    @Keep
    @JvmStatic
    fun isDirectory(path: String?): Boolean {
        return if (DocumentsTree.isNativePath(path!!)) {
            YuzuApplication.documentsTree!!.isDirectory(path)
        } else {
            FileUtil.isDirectory(path)
        }
    }

    @Keep
    @JvmStatic
    fun getParentDirectory(path: String): String =
        if (DocumentsTree.isNativePath(path)) {
            YuzuApplication.documentsTree!!.getParentDirectory(path)
        } else {
            path
        }

    @Keep
    @JvmStatic
    fun getFilename(path: String): String =
        if (DocumentsTree.isNativePath(path)) {
            YuzuApplication.documentsTree!!.getFilename(path)
        } else {
            FileUtil.getFilename(Uri.parse(path))
        }

    @Keep
    @JvmStatic
    fun copyFileToStorage(source: String, destdir: String): Boolean {
        return FileUtil.copyUriToInternalStorage(
            source.toUri(),
            destdir
        ) != null
    }

    @Keep
    @JvmStatic
    fun getFileExtension(source: String): String {
        return FileUtil.getExtension(source.toUri())
    }

    external fun setAppDirectory(directory: String)

    /**
     * Installs a nsp or xci file to nand
     * @param filename String representation of file uri
     * @return int representation of [InstallResult]
     */
    external fun installFileToNand(
        filename: String,
        callback: (max: Long, progress: Long) -> Boolean
    ): Int

    external fun doesUpdateMatchProgram(programId: String, updatePath: String): Boolean

    external fun initializeGpuDriver(
        hookLibDir: String?,
        customDriverDir: String?,
        customDriverName: String?,
        fileRedirectDir: String?
    )

    external fun reloadKeys(): Boolean

    external fun initializeSystem(reload: Boolean)

    /**
     * Begins emulation.
     */
    external fun run(path: String?, programIndex: Int, frontendInitiated: Boolean)

    // Surface Handling
    external fun surfaceChanged(surf: Surface?)

    external fun surfaceDestroyed()

    external fun getAppletCaptureBuffer(): ByteArray
    external fun getAppletCaptureWidth(): Int
    external fun getAppletCaptureHeight(): Int

    /**
     * Unpauses emulation from a paused state.
     */
    external fun unpauseEmulation()

    /**
     * Pauses emulation.
     */
    external fun pauseEmulation()

    /**
     * Stops emulation.
     */
    external fun stopEmulation()

    /**
     * Returns true if emulation is running (or is paused).
     */
    external fun isRunning(): Boolean

    /**
     * Returns true if emulation is paused.
     */
    external fun isPaused(): Boolean

    /**
     * Returns the performance stats for the current game
     */
    external fun getPerfStats(): DoubleArray

    /**
     * Returns the number of shaders being built
     */
    external fun getShadersBuilding(): Int

    /**
     * Returns the current CPU backend.
     */
    external fun getCpuBackend(): String

    /**
     * Returns the current GPU Driver.
     */
    external fun getGpuDriver(): String

    external fun applySettings()

    external fun logSettings()

    external fun refreshThreadPolicies()

    external fun GetDebugKnobAt(index: Int): Boolean

    /**
     * Set the current speed limit to the configured turbo speed.
     */
    external fun setTurboSpeedLimit(enabled: Boolean)

    /**
     * Set the current speed limit to the configured slow speed.
     */
    external fun setSlowSpeedLimit(enabled: Boolean)

    /**
     * Set the current speed limit to the configured standard speed.
     */
    external fun setStandardSpeedLimit(enabled: Boolean)

    external fun isTurboMode(): Boolean
    external fun isSlowMode(): Boolean

    /**
     * Returns Vulkan driver version / API version / GPU model
     */
    external fun getVulkanDriverVersion(): String
    external fun getVulkanApiVersion(): String
    external fun getGpuModel(): String

    /**
     * Returns a summary of detailed information about the CPU.
     */
    external fun getCpuSummary(): String

    /**
     * Checks for available updates.
     */
    external fun checkForUpdate(): UpdateResult?

    /**
     * Returns whether the update checker is enabled through CMAKE options.
     */
    external fun isUpdateCheckerEnabled(): Boolean

    /**
     * Returns whether or not this is a nightly build.
     */
    external fun isNightlyBuild(): Boolean

    /**
     * Returns the build version generated by CMake (BUILD_VERSION).
     */
    external fun getBuildVersion(): String

    enum class CoreError {
        ErrorSystemFiles,
        ErrorSavestate,
        ErrorUnknown
    }

    /**
     *  playtime tracking
     */
    external fun playTimeManagerInit()
    external fun playTimeManagerStart()
    external fun playTimeManagerStop()
    external fun playTimeManagerGetPlayTime(programId: String): Long
    external fun playTimeManagerGetCurrentTitleId(): Long
    external fun playTimeManagerResetProgramPlayTime(programId: String)
    external fun playTimeManagerSetPlayTime(programId: String, playTimeSeconds: Long)

    var coreErrorAlertResult = false
    val coreErrorAlertLock = Object()

    private fun onCoreErrorImpl(title: String, message: String) {
        val emulationActivity = sEmulationActivity.get()
        if (emulationActivity == null) {
            Log.error("[NativeLibrary] EmulationActivity not present")
            return
        }

        val fragment = CoreErrorDialogFragment.newInstance(title, message)
        fragment.show(emulationActivity.supportFragmentManager, "coreError")
    }

    /**
     * Handles a core error.
     *
     * @return true: continue; false: abort
     */
    fun onCoreError(error: CoreError?, details: String): Boolean {
        val emulationActivity = sEmulationActivity.get()
        if (emulationActivity == null) {
            Log.error("[NativeLibrary] EmulationActivity not present")
            return false
        }

        val title: String
        val message: String
        when (error) {
            CoreError.ErrorSystemFiles -> {
                title = emulationActivity.getString(R.string.system_archive_not_found)
                message = emulationActivity.getString(
                    R.string.system_archive_not_found_message,
                    details.ifEmpty { emulationActivity.getString(R.string.system_archive_general) }
                )
            }

            CoreError.ErrorSavestate -> {
                title = emulationActivity.getString(R.string.save_load_error)
                message = details
            }

            CoreError.ErrorUnknown -> {
                title = emulationActivity.getString(R.string.fatal_error)
                message = emulationActivity.getString(R.string.fatal_error_message)
            }

            else -> {
                return true
            }
        }

        // Show the AlertDialog on the main thread.
        emulationActivity.runOnUiThread { onCoreErrorImpl(title, message) }

        // Wait for the lock to notify that it is complete.
        synchronized(coreErrorAlertLock) { coreErrorAlertLock.wait() }

        return coreErrorAlertResult
    }

    @Keep
    @JvmStatic
    fun addNetPlayMessage(type: Int, message: String) {
        val emulationActivity = sEmulationActivity.get()
        if (emulationActivity != null) {
            emulationActivity.addNetPlayMessages(type, message)
        } else {
            NetPlayManager.addNetPlayMessage(type, message)
        }
    }

    @Keep
    @JvmStatic
    fun clearChat() {
        NetPlayManager.clearChat()
    }

    external fun initJvm()
    external fun initMultiplayer()

    @Keep
    @JvmStatic
    fun exitEmulationActivity(resultCode: Int) {
        val Success = 0
        val ErrorNotInitialized = 1
        val ErrorGetLoader = 2
        val ErrorSystemFiles = 3
        val ErrorSharedFont = 4
        val ErrorVideoCore = 5
        val ErrorUnknown = 6
        val ErrorLoader = 7
        val ErrorMemoryAllocation = 0x10007

        val captionId: Int
        var descriptionId: Int
        when (resultCode) {
            ErrorMemoryAllocation -> {
                captionId = R.string.loader_error_memory
                descriptionId = R.string.loader_error_memory_description
            }
            ErrorVideoCore -> {
                captionId = R.string.loader_error_video_core
                descriptionId = R.string.loader_error_video_core_description
            }

            else -> {
                captionId = R.string.loader_error_encrypted
                descriptionId = R.string.loader_error_encrypted_roms_description
                if (!reloadKeys()) {
                    descriptionId = R.string.loader_error_encrypted_keys_description
                }
            }
        }

        val emulationActivity = sEmulationActivity.get()
        if (emulationActivity == null) {
            Log.warning("[NativeLibrary] EmulationActivity is null, can't exit.")
            return
        }

        val builder = MaterialAlertDialogBuilder(emulationActivity)
            .setTitle(captionId)
            .setMessage(
                Html.fromHtml(
                    emulationActivity.getString(descriptionId),
                    Html.FROM_HTML_MODE_LEGACY
                )
            )
            .setPositiveButton(android.R.string.ok) { _: DialogInterface?, _: Int ->
                emulationActivity.finish()
            }
            .setOnDismissListener { emulationActivity.finish() }
        emulationActivity.runOnUiThread {
            val alert = builder.create()
            alert.show()
            (alert.findViewById<View>(android.R.id.message) as TextView).movementMethod =
                LinkMovementMethod.getInstance()
        }
    }

    fun setEmulationActivity(emulationActivity: EmulationActivity?) {
        Log.debug("[NativeLibrary] Registering EmulationActivity.")
        sEmulationActivity = WeakReference(emulationActivity)
    }

    fun clearEmulationActivity() {
        Log.debug("[NativeLibrary] Unregistering EmulationActivity.")
        sEmulationActivity.clear()
    }

    @Keep
    @JvmStatic
    fun onEmulationStarted() {
        sEmulationActivity.get()!!.onEmulationStarted()
    }

    @Keep
    @JvmStatic
    fun onEmulationStopped(status: Int) {
        sEmulationActivity.get()!!.onEmulationStopped(status)
    }

    @Keep
    @JvmStatic
    fun onProgramChanged(programIndex: Int) {
        sEmulationActivity.get()!!.onProgramChanged(programIndex)
    }

    /**
     * Logs the Yuzu version, Android version and, CPU.
     */
    external fun logDeviceInfo()

    /**
     * Submits inline keyboard text. Called on input for buttons that result text.
     * @param text Text to submit to the inline software keyboard implementation.
     */
    external fun submitInlineKeyboardText(text: String?)

    /**
     * Submits inline keyboard input. Used to indicate keys pressed that are not text.
     * @param key_code Android Key Code associated with the keyboard input.
     */
    external fun submitInlineKeyboardInput(key_code: Int)

    /**
     * Creates a generic user directory if it doesn't exist already
     */
    external fun initializeEmptyUserDirectory()

    /**
     * Gets the launch path for a given applet. It is the caller's responsibility to also
     * set the system's current applet ID before trying to launch the nca given by this function.
     *
     * @param id The applet entry ID
     * @return The applet's launch path
     */
    external fun getAppletLaunchPath(id: Long): String

    /**
     * Sets the system's current applet ID before launching.
     *
     * @param appletId One of the ids in the Service::AM::Applets::AppletId enum
     */
    external fun setCurrentAppletId(appletId: Int)

    /**
     * Launch external URL when Web applet and opens browser
     *
     * @param String URL
     */
    @Keep
    @JvmStatic
    fun openExternalUrl(url: String) {
        WebBrowser.openExternal(url)
    }

    /** A controller key as it went by: to the game, or to the list of users. */
    private class NotedKey(
        val at: Long,
        val keyCode: Int,
        val down: Boolean,
        val cancelled: Boolean,
        val port: Int,
        val toList: Boolean
    ) {
        fun text(): String =
            android.view.KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_") +
                (if (down) " down" else " up") + (if (cancelled) " (cancelled)" else "") +
                ", controller port $port" + (if (toList) ", to the list" else "")
    }

    private val notedKeys = ArrayDeque<NotedKey>()

    /** Until when (uptime) each noted key gets a line in the log. */
    @Volatile
    private var keysLoggedUntil = 0L

    /**
     * Notes a controller key on its way to the game, or to the list of users. The keys before a
     * game asks for a user and those after it has one say which controller asked, and whether
     * what it pressed was let go: for the log of a second player that does not stay.
     */
    fun noteControllerKey(event: android.view.KeyEvent, toList: Boolean) {
        val down = event.action == android.view.KeyEvent.ACTION_DOWN
        if (event.repeatCount != 0 || (!down && event.action != android.view.KeyEvent.ACTION_UP)) {
            return
        }
        val number = event.device?.controllerNumber ?: 0
        val key = NotedKey(
            android.os.SystemClock.uptimeMillis(),
            event.keyCode,
            down,
            event.isCanceled,
            org.yuzu.yuzu_emu.utils.InputHandler.androidControllers[number]?.getPort() ?: -1,
            toList
        )
        synchronized(notedKeys) {
            notedKeys.addLast(key)
            while (notedKeys.size > 8) {
                notedKeys.removeFirst()
            }
        }
        if (key.at < keysLoggedUntil) {
            Log.info("[UserSelect] key ${key.text()}")
        }
    }

    private val selectUserLock = Object()

    /** The newest request whose list came up, and the newest given up before its list did. */
    private var selectUserShown = 0
    private var selectUserGivenUp = 0

    /**
     * A game's user selection: shows the list of users that can be picked. The game is not held
     * meanwhile; the answer goes back through [userSelected] when the list closes. A second local
     * player picks their own user here.
     *
     * @param names The users that can be picked. Empty when none is left: the person is told so.
     * @param request Names the selection in the answer.
     * @return false when no list came up (within two seconds): no answer will follow.
     */
    @Keep
    @JvmStatic
    fun selectUser(names: Array<String>, request: Int): Boolean {
        val activity = sEmulationActivity.get()
        if (activity == null) {
            Log.warning("[UserSelect] request $request: no emulation activity")
            return false
        }
        if (names.isEmpty()) {
            activity.runOnUiThread {
                android.widget.Toast.makeText(
                    activity,
                    R.string.select_user_none,
                    android.widget.Toast.LENGTH_LONG
                ).show()
            }
            return false
        }
        Log.info("[UserSelect] request $request: a list of ${names.size} users is asked for")
        val asked = android.os.SystemClock.uptimeMillis()
        val before = synchronized(notedKeys) { notedKeys.toList() }
            .joinToString("; ") { "${it.text()} ${asked - it.at} ms before" }
        Log.info(
            "[UserSelect] request $request: controller keys before it: " +
                before.ifEmpty { "none" }
        )
        keysLoggedUntil = asked + 60000
        activity.runOnUiThread { showUserList(activity, names, request) }
        // The list comes up on the UI thread. If that thread does not get to it, nobody can be
        // asked, and the caller must not wait for an answer.
        val deadline = android.os.SystemClock.uptimeMillis() + 2000
        synchronized(selectUserLock) {
            while (selectUserShown < request) {
                val left = deadline - android.os.SystemClock.uptimeMillis()
                if (left <= 0 || selectUserGivenUp >= request) {
                    selectUserGivenUp = maxOf(selectUserGivenUp, request)
                    Log.warning("[UserSelect] request $request: the list did not come up")
                    return false
                }
                selectUserLock.wait(left)
            }
        }
        return true
    }

    private fun showUserList(activity: EmulationActivity, names: Array<String>, request: Int) {
        val giveUp = {
            synchronized(selectUserLock) {
                selectUserGivenUp = maxOf(selectUserGivenUp, request)
                selectUserLock.notifyAll()
            }
        }
        synchronized(selectUserLock) {
            if (selectUserGivenUp >= request) {
                return
            }
        }
        if (activity.isFinishing || activity.isDestroyed) {
            giveUp()
            return
        }
        var picked = -1
        val shownAt = android.os.SystemClock.uptimeMillis()
        val dialog = try {
            MaterialAlertDialogBuilder(activity)
                .setTitle(R.string.select_user_title)
                .setItems(names) { _: DialogInterface?, which: Int -> picked = which }
                .setNegativeButton(android.R.string.cancel, null)
                .setOnKeyListener { list: DialogInterface?, keyCode: Int,
                                    event: android.view.KeyEvent? ->
                    onUserListKey(list, keyCode, event, shownAt)
                }
                .setOnDismissListener {
                    Log.info("[UserSelect] request $request: the list closed, picked $picked")
                    keysLoggedUntil = android.os.SystemClock.uptimeMillis() + 20000
                    userSelected(request, picked)
                }
                .show()
        } catch (e: Exception) {
            Log.error("[UserSelect] request $request: the list could not be shown: ${e.message}")
            giveUp()
            return
        }
        val late = synchronized(selectUserLock) {
            if (selectUserGivenUp >= request) {
                true
            } else {
                selectUserShown = request
                selectUserLock.notifyAll()
                false
            }
        }
        if (late) {
            // Given up while it came up: its answer would find nothing waiting.
            dialog.dismiss()
        } else {
            Log.info("[UserSelect] request $request: the list is up")
        }
    }

    /**
     * A key on the list of users: true when the list is not to see it.
     *
     * The press that asked for the list (+ on a controller) reads as a confirm key to a list,
     * and must not pick its first user; nor may a key still held when it comes up. A Nintendo
     * controller's A is its right button, which Android reads as B (back), and its B the lower
     * one, read as A (confirm): here they do what their labels say.
     */
    private fun onUserListKey(
        list: DialogInterface?,
        keyCode: Int,
        event: android.view.KeyEvent?,
        shownAt: Long
    ): Boolean {
        if (event != null &&
            org.yuzu.yuzu_emu.utils.InputHandler.isPhysicalGameController(event.device)
        ) {
            noteControllerKey(event, true)
        }
        if (keyCode == android.view.KeyEvent.KEYCODE_BUTTON_START ||
            keyCode == android.view.KeyEvent.KEYCODE_BUTTON_SELECT ||
            android.os.SystemClock.uptimeMillis() - shownAt < 500
        ) {
            return true
        }
        val nintendo = event?.device?.vendorId == 0x057e
        if (!nintendo || (
                keyCode != android.view.KeyEvent.KEYCODE_BUTTON_A &&
                    keyCode != android.view.KeyEvent.KEYCODE_BUTTON_B
                )
        ) {
            return false
        }
        if (event?.action != android.view.KeyEvent.ACTION_UP) {
            return true
        }
        if (keyCode == android.view.KeyEvent.KEYCODE_BUTTON_A) {
            list?.cancel()
            return true
        }
        val entries = (list as? androidx.appcompat.app.AlertDialog)?.listView ?: return true
        val position = entries.selectedItemPosition
        if (position < 0) {
            entries.requestFocusFromTouch()
            entries.setSelection(0)
        } else {
            entries.performItemClick(
                entries.selectedView,
                position,
                entries.getItemIdAtPosition(position)
            )
        }
        return true
    }

    /**
     * The answer to a list of users.
     *
     * @param index The picked user's index in the list, -1 when the list was closed.
     */
    external fun userSelected(request: Int, index: Int)

    /**
     * Sets the cabinet mode for launching the cabinet applet.
     *
     * @param cabinetMode One of the modes that corresponds to the enum in Service::NFP::CabinetMode
     */
    external fun setCabinetMode(cabinetMode: Int)

    /**
     * Checks whether NAND contents are available and valid.
     *
     * @return 'true' if firmware is available
     */
    external fun isFirmwareAvailable(): Boolean

    /**
     * Gets the firmware version.
     *
     * @return Reported firmware version
     */
    external fun firmwareVersion(): String

    /**
     * Tells which console the firmware files in a folder are for, before they are installed.
     *
     * @param path Folder with the firmware's .nca files
     * @return 2 for a Switch 2 firmware, 1 for a Switch firmware, 0 when the installed keys open
     * none of the files
     */
    external fun firmwareFilesPlatform(path: String): Int

    /**
     * Why the last [firmwareFilesPlatform] returned 0: a first line that names the reason
     * (HEADER, GENERATION needed newest, OTHER), then lines of detail for the dialog. Empty after
     * a call that found a firmware.
     */
    external fun firmwareFilesProblem(): String

    /**
     * Check if a game requires firmware to be playable.
     *
     * @param programId The game's Program ID.
     * @return Whether or not the game requires firmware to be playable.
     */
    external fun gameRequiresFirmware(programId: String): Boolean

    /**
     * Installs keys from the specified path.
     *
     * @param path The path to install keys from.
     * @param ext What extension the keys should have.
     * @return The result code.
     */
    external fun installKeys(path: String, ext: String): Int

    /**
     * @return Whether this GPU can run the Lossless Scaling frame generation shaders,
     *         which are built against the Vulkan memory model.
     */
    external fun supportsFrameGeneration(): Boolean

    /**
     * @return Path the user-supplied Lossless Scaling library is expected at.
     */
    external fun getLosslessDllPath(): String

    /**
     * Parses the installed Lossless Scaling library and checks that every shader the
     * frame generation chain needs is present.
     *
     * @return The result code, matching the losslessDllResults array.
     */
    external fun validateLosslessDll(): Int

    /**
     * Translates the frame generation shaders out of the installed Lossless Scaling library
     * and writes them to the SPIR-V cache. Slow, so call it off the main thread.
     *
     * @return The result code, matching the losslessDllResults array.
     */
    external fun prepareLosslessDll(): Int

    /**
     * Deletes the installed Lossless Scaling library.
     *
     * @return Whether the library is gone after the call.
     */
    external fun removeLosslessDll(): Boolean

    /**
     * Checks the PatchManager for any addons that are available
     *
     * @param path Path to game file. Can be a [Uri].
     * @param programId String representation of a game's program ID
     * @return Array of available patches
     */
    external fun getPatchesForFile(path: String, programId: String): Array<Patch>?

    /**
     * Removes an update for a given [programId]
     * @param programId String representation of a game's program ID
     */
    external fun removeUpdate(programId: String)

    /**
     * Removes all DLC for a  [programId]
     * @param programId String representation of a game's program ID
     */
    external fun removeDLC(programId: String)

    /**
     * Removes a mod installed for a given [programId]
     * @param programId String representation of a game's program ID
     * @param name The name of a mod as given by [getPatchesForFile]. This corresponds with the name
     * of the mod's directory in a game's load folder.
     */
    external fun removeMod(programId: String, name: String)

    /**
     * Verifies all installed content
     * @param callback UI callback for verification progress. Return true in the callback to cancel.
     * @return Array of content that failed verification. Successful if empty.
     */
    external fun verifyInstalledContents(
        callback: (max: Long, progress: Long) -> Boolean
    ): Array<String>

    /**
     * Verifies the contents of a game
     * @param path String path to a game
     * @param callback UI callback for verification progress. Return true in the callback to cancel.
     * @return Int that is meant to be converted to a [GameVerificationResult]
     */
    external fun verifyGameContents(
        path: String,
        callback: (max: Long, progress: Long) -> Boolean
    ): Int

    /**
     * Gets the save location for a specific game
     *
     * @param programId String representation of a game's program ID
     * @return Save data path that may not exist yet
     */
    external fun getSavePath(programId: String): String

    /**
     * Gets the root save directory for the default profile as either
     * /user/save/account/<user id raw string> or /user/save/000...000/<user id>
     *
     * @param future If true, returns the /user/save/account/... directory
     * @return Save data path that may not exist yet
     */
    external fun getDefaultProfileSaveDataRoot(future: Boolean): String

    /**
     * Adds a file to the manual filesystem provider in our EmulationSession instance
     * @param path Path to the file we're adding. Can be a string representation of a [Uri] or
     * a normal path
     */
    external fun addFileToFilesystemProvider(path: String)

    /**
     * Adds a game-folder file to the manual filesystem provider, respecting the internal gate for
     * game-folder external-content mounting.
     */
    external fun addGameFolderFileToFilesystemProvider(path: String)

    /**
     * Clears all files added to the manual filesystem provider in our EmulationSession instance
     */
    external fun clearFilesystemProvider()

    /**
     * Tells the core that the game list scan that began with [clearFilesystemProvider] has
     * registered all its games: a home menu lists and starts the games of the provider.
     */
    external fun setGamesRegistered()

    /**
     * Whether a game list scan has filled the filesystem provider that is in use now.
     */
    external fun areGamesRegistered(): Boolean

    /**
     * Whether the applet set by [setCurrentAppletId] for the next launch is the home menu.
     */
    external fun isHomeMenuLaunch(): Boolean

    /**
     * Gets the current virtual amiibo state reported by the core.
     *
     * @return Native enum value for the current amiibo state.
     */
    external fun getVirtualAmiiboState(): Int

    /**
     * Loads amiibo data into the currently running emulation session.
     *
     * @param data Raw amiibo file contents.
     * @return Native enum value representing the load result.
     */
    external fun loadAmiibo(data: ByteArray): Int

    /**
     * Checks if all necessary keys are present for decryption
     */
    external fun areKeysPresent(): Boolean

    /**
     * Updates the device power state to global variables
     */
    external fun updatePowerState(percentage: Int, isCharging: Boolean, hasBattery: Boolean)

    /**
     * Profile manager native calls
     */
    external fun getAllUsers(): Array<String>?
    external fun getUserUsername(uuid: String): String?
    external fun getUserCount(): Long
    external fun canCreateUser(): Boolean
    external fun createUser(uuid: String, username: String): Boolean
    external fun updateUserUsername(uuid: String, username: String): Boolean
    external fun removeUser(uuid: String): Boolean
    external fun getCurrentUser(): String?
    external fun setCurrentUser(uuid: String): Boolean
    external fun getUserImagePath(uuid: String): String?
    external fun saveUserImage(uuid: String, imagePath: String): Boolean
    external fun reloadProfiles()
    external fun getFirmwareAvatarCount(): Int
    external fun getFirmwareAvatarImage(index: Int): ByteArray?
    external fun getDefaultAccountBackupJpeg(): ByteArray
}
