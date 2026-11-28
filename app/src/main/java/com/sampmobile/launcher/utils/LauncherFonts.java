package com.sampmobile.launcher.utils;

import android.content.Context;
import android.graphics.Typeface;
import android.widget.TextView;

/**
 * GTA typeface that ships inside the user-supplied GTA:SA APK assets (assets/Fonts).
 * A missing file (compile-only builds) falls back to the system bold face.
 */
public final class LauncherFonts {
    private static Typeface sCondensed;
    private static boolean sCondensedTried;

    private LauncherFonts() {
    }

    /** Chalet Comprime: the condensed HUD numerals/labels face. */
    public static Typeface condensed(Context ctx) {
        if (!sCondensedTried) {
            sCondensedTried = true;
            try {
                sCondensed = Typeface.createFromAsset(
                        ctx.getApplicationContext().getAssets(), "Fonts/ChaletComprime-CologneSixty.otf");
            } catch (Exception e) {
                sCondensed = null;
            }
        }
        return sCondensed != null ? sCondensed : Typeface.DEFAULT_BOLD;
    }

    public static void applyCondensed(TextView... views) {
        for (TextView v : views) {
            if (v != null) v.setTypeface(condensed(v.getContext()));
        }
    }
}
