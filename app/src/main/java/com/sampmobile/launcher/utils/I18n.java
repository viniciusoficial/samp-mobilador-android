package com.sampmobile.launcher.utils;

import java.util.Locale;

/**
 * Runtime bridge for legacy Chinese literals in code paths that cannot reasonably receive a Context
 * (download engines, mod importers, diagnostics and other non-UI utility layers).
 * Declarative XML layouts still use normal Android string resources.
 */
public final class I18n {
    private static volatile boolean english = false;

    private I18n() {}

    public static void setLanguage(String language) {
        // Legacy literals in the utility layer are stored in Chinese; every other
        // language (English, Portuguese...) falls back to the English table.
        english = !LocaleManager.LANG_ZH.equals(language);
    }

    public static boolean isEnglish() {
        return english;
    }

    public static String tr(String zh) {
        if (!english || zh == null) return zh;
        String translated = I18nZhEn.lookup(zh);
        return translated != null ? translated : zh;
    }

    public static String trf(String zhFormat, Object... args) {
        return String.format(Locale.US, tr(zhFormat), args);
    }
}
