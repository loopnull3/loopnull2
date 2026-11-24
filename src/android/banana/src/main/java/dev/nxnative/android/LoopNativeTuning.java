// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package dev.nxnative.android;

import android.app.ActivityManager;
import android.content.Context;

/**
 * LoopNative's changes to how Banana Native starts a game, by phone. Banana Native asks for a phone of 12 GB or
 * more; its low-RAM class (vka_lowram.h, Prefs.lowRam) is "12 GB or less". LoopNative adds a class below it for
 * 8 GB phones (the Galaxy Z Flip5), where a game only stays alive with every memory saving on from its first start.
 * The settings are Banana Native's own (Game settings and Advanced rows): set once, so what the player changes stays.
 */
public final class LoopNativeTuning {
    private LoopNativeTuning() {}

    /** Raised when the starting settings change, so a game takes the new ones once. */
    private static final int VERSION = 1;
    private static final long GIB = 1L << 30;
    /** An 8 GB phone reports about 7.3 to 7.6 GiB; a 12 GB one about 11. */
    private static final long EIGHT_GB_CLASS_MAX = GIB * 17 / 2;
    /** The largest render the 8 GB class starts with. */
    private static final int EIGHT_GB_MAX_WIDTH = 1280, EIGHT_GB_MAX_HEIGHT = 720;

    public static long memTotal(Context c) {
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null) return 0;
        ActivityManager.MemoryInfo m = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(m);
        return m.totalMem;
    }

    public static boolean eightGbClass(long memTotal) {
        return memTotal > 0 && memTotal <= EIGHT_GB_CLASS_MAX;
    }

    /**
     * The starting settings of a game on this phone, after Hokkaido's (HokkaidoBridge.startingSettings). The 8 GB
     * class: RAM saver Strong (file-backed shader and texture pools, the texture cache at a quarter of RAM, textures
     * without their two finest levels, one background pipeline at a time), less vegetation, and memory pools placed
     * in parts of 256 MiB instead of whole. Ocarina of Time's RAM saver can leave holes in its terrain (seen on the
     * Galaxy S26, where Hokkaido turns it off): on 8 GB the game does not fit without it.
     */
    static void apply(Context c, String id) {
        String key = "loopnative_settings_" + id;
        int had = Prefs.of(c).getInt(key, 0);
        if (had >= VERSION) return;
        if (eightGbClass(memTotal(c))) {
            Prefs.ramSaver(c, id, true);
            Prefs.ramSaverStrong(c, id, true);
            Prefs.fullVegetation(c, id, false);
            AdvancedSettings.set(c, "DM_VK_PLACE_PART_MB", id, "256");
        }
        Prefs.of(c).edit().putInt(key, VERSION).commit();
    }

    /**
     * The render size a game starts with: Hokkaido's resolution scale gives [window] ("&lt;width&gt;x&lt;height&gt;" or
     * "original"); the 8 GB class takes at most 1280x720, whose render targets are under half of 1920x1080's.
     */
    public static String window(long memTotal, String window) {
        if (!eightGbClass(memTotal) || window == null) return window;
        String capped = EIGHT_GB_MAX_WIDTH + "x" + EIGHT_GB_MAX_HEIGHT;
        int x = window.indexOf('x');
        if (x <= 0) return capped;   // "original": the game's own size, 1920x1080 or more
        try {
            int width = Integer.parseInt(window.substring(0, x)), height = Integer.parseInt(window.substring(x + 1));
            return width > EIGHT_GB_MAX_WIDTH || height > EIGHT_GB_MAX_HEIGHT ? capped : window;
        } catch (NumberFormatException e) {
            return capped;
        }
    }

    /**
     * The RAM in GiB a game's profile calls required (its readiness rows: "required|ram_gib|14|..."), 0 when it
     * names none. Banana Native measured these on its own engine: Mario Kart World 14, Donkey Kong Bananza 10.
     */
    public static double requiredRamGib(String reqs) {
        if (reqs == null) return 0;
        double most = 0;
        for (String row : reqs.split(";")) {
            String[] cell = row.split("\\|");
            if (cell.length < 3 || !cell[0].trim().equals("required") || !cell[1].trim().equals("ram_gib")) continue;
            try {
                most = Math.max(most, Double.parseDouble(cell[2].trim()));
            } catch (NumberFormatException e) {
                // not a number: no requirement to read
            }
        }
        return most;
    }
}
