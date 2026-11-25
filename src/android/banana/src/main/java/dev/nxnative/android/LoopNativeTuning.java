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
    private static final int VERSION = 4;
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

    /** Banana Native's own low-RAM class: MemTotal of 12 GiB or less (vka_lowram.h), above the 8 GB class. */
    public static boolean twelveGbClass(long memTotal) {
        return memTotal > EIGHT_GB_CLASS_MAX && memTotal <= 12 * GIB;
    }

    /** Whether this phone is in a memory class (12 GB or 8 GB): Banana Native is tested on 16 GB phones. */
    public static boolean lowMemoryPhone(Context c) {
        long total = memTotal(c);
        return eightGbClass(total) || twelveGbClass(total);
    }

    /**
     * The memory class: a profile's low_ram_place_part_mb (dm/games/README.md), given to every game through the
     * switch that overrides it. Memory pools of 256 MiB or more are placed by parts instead of whole; upstream
     * measured the placed memory of Fire Emblem Fortune's Weave going from 887 to 553 MiB with it on an 11 GiB phone,
     * and Hokkaido sets it for Ocarina of Time on phones of 12 GB or less.
     *
     * The texture cache keeps its measured limit (vka_texlimit.h): what the phone can hold now, the scene's working
     * set never taken back. A fixed DM_VK_IMG_LIMIT_MB replaces that guard too, and a limit under a game's working
     * set uploads its textures again every frame (docs/reference/memory.md "Texture cache thrash").
     *
     * Not in the class: the smaller total memory a game is told (low_ram_app_pool_mb, DM_APP_POOL_MB), which earlier
     * versions set to 5632 and 4096 MiB. A game sizes its heap from that total once, at startup, and Mario Kart World
     * then takes one 7.04 GiB block from it: with 5632 MiB reported the heap was 3.97 GiB, the block was refused and
     * the game crashed in nnMain (a write to 0x10 at main+0x5dd3ac, Galaxy S26). It fits from 8773 MiB up, so the
     * report saves that game nothing. The key stays what upstream made it: a value measured for one game.
     */
    private static final String[] MEMORY_CLASS_ROWS = {"DM_VK_PLACE_PART_MB"};
    private static final String[] TWELVE_GB_VALUES = {"256"};
    private static final String[] EIGHT_GB_VALUES = {"256"};
    /** The profile with low-RAM keys of its own for the 12 GB class (Banana Native v0.3.45). */
    private static final String FIRE_EMBLEM = "04007EF01F8AE000";

    private static String memoryClassKey(String id) { return "loopnative_memory_class_" + id; }

    /** Whether the game takes this phone's memory class (Game properties: on unless the player turned it off). */
    public static boolean memoryClass(Context c, String id) {
        return Prefs.of(c).getBoolean(memoryClassKey(id), true);
    }

    /** Turns the game's memory class on or off; its rows change at once and act at the game's next start. */
    public static void memoryClass(Context c, String id, boolean on) {
        Prefs.of(c).edit().putBoolean(memoryClassKey(id), on).commit();
        writeMemoryClass(c, id, on);
    }

    private static void writeMemoryClass(Context c, String id, boolean on) {
        long total = memTotal(c);
        String[] values = !on ? null : eightGbClass(total) ? EIGHT_GB_VALUES
                : twelveGbClass(total) && !FIRE_EMBLEM.equalsIgnoreCase(id) ? TWELVE_GB_VALUES : null;
        // A phone above the classes, or a game with its own keys: nothing of LoopNative's stays set
        for (int i = 0; i < MEMORY_CLASS_ROWS.length; i++) AdvancedSettings.set(c, MEMORY_CLASS_ROWS[i], id, values == null ? null : values[i]);
        dropReportedTotal(c, id);
    }

    /** The reported total versions 2 and 3 set (5632 or 4096 MiB) is taken back; a value the player set stays. */
    private static void dropReportedTotal(Context c, String id) {
        String total = AdvancedSettings.own(c, "DM_APP_POOL_MB", id);
        if ("5632".equals(total) || "4096".equals(total)) AdvancedSettings.set(c, "DM_APP_POOL_MB", id, null);
    }

    /**
     * The starting settings of a game on this phone, after Hokkaido's (HokkaidoBridge.startingSettings). The 8 GB
     * class: RAM saver Strong (file-backed shader and texture pools, the texture cache at a quarter of RAM, textures
     * without their two finest levels, one background pipeline at a time) and less vegetation. Ocarina of Time's
     * RAM saver can leave holes in its terrain (seen on the Galaxy S26, where Hokkaido turns it off): on 8 GB the
     * game does not fit without it. Then the phone's memory class (writeMemoryClass), unless the player turned it off.
     */
    static void apply(Context c, String id) {
        String key = "loopnative_settings_" + id;
        int had = Prefs.of(c).getInt(key, 0);
        if (had >= VERSION) return;
        if (had < 1 && eightGbClass(memTotal(c))) {
            Prefs.ramSaver(c, id, true);
            Prefs.ramSaverStrong(c, id, true);
            Prefs.fullVegetation(c, id, false);
        }
        if (had == 2) {
            // Version 2 also fixed the texture cache at 1024 or 768 MiB: taken back, a player's own limit stays
            String limit = AdvancedSettings.own(c, "DM_VK_IMG_LIMIT_MB", id);
            if ("1024".equals(limit) || "768".equals(limit)) AdvancedSettings.set(c, "DM_VK_IMG_LIMIT_MB", id, null);
        }
        dropReportedTotal(c, id);
        if (memoryClass(c, id)) writeMemoryClass(c, id, true);
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
