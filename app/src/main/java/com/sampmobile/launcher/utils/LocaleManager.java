package com.sampmobile.launcher.utils;

import android.content.Context;
import android.content.res.Configuration;
import android.os.LocaleList;

import java.util.Locale;

/**
 * Fixed in-app language selection shared by the launcher and the isolated :game process.
 *
 * The first activity resolve turns the internal "auto" sentinel into a concrete zh/en
 * choice from the pre-override system locale list. From then on settings.json is the
 * only source of truth, so later system locale changes never override the user's choice.
 */
public final class LocaleManager {
    public static final String LANG_AUTO = "auto";
    public static final String LANG_ZH = "zh";
    public static final String LANG_EN = "en";
    public static final String LANG_PT = "pt";

    private LocaleManager() {}

    public static Context wrap(Context base) {
        String language = currentLanguage(base);
        I18n.setLanguage(language);
        Locale locale = localeFor(language);
        Locale.setDefault(locale);

        Configuration config = new Configuration(base.getResources().getConfiguration());
        config.setLocale(locale);
        config.setLayoutDirection(locale);
        return base.createConfigurationContext(config);
    }

    /** Returns the persisted concrete language, initializing it once from the device locale when needed. */
    public static String currentLanguage(Context context) {
        ClientSettings settings = ClientSettings.loadLauncher(context);
        String language = normalize(settings.getString("app_language"));
        if (LANG_AUTO.equals(language)) {
            language = detectSystemLanguage(context);
            ClientSettings.setSync(context, "app_language", language);
        }
        return language;
    }

    public static void setLanguage(Context context, String language) {
        String normalized = normalizeConcrete(language, LANG_ZH);
        ClientSettings.setSync(context, "app_language", normalized);
        I18n.setLanguage(normalized);
    }

    /** Detects the pre-override device locale preference order; unsupported locales fall back to English. */
    public static String detectSystemLanguage(Context context) {
        LocaleList locales = context.getResources().getConfiguration().getLocales();
        for (int i = 0; i < locales.size(); i++) {
            Locale locale = locales.get(i);
            if (locale != null && "zh".equalsIgnoreCase(locale.getLanguage())) {
                return LANG_ZH;
            }
        }
        for (int i = 0; i < locales.size(); i++) {
            Locale locale = locales.get(i);
            if (locale != null && "pt".equalsIgnoreCase(locale.getLanguage())) {
                return LANG_PT;
            }
        }
        return LANG_EN;
    }

    public static Locale localeFor(String language) {
        final String concrete = normalizeConcrete(language, LANG_EN);
        if (LANG_ZH.equals(concrete)) return new Locale("zh");
        if (LANG_PT.equals(concrete)) return new Locale("pt", "BR");
        return Locale.ENGLISH;
    }

    public static String normalize(String language) {
        if (LANG_ZH.equalsIgnoreCase(language)
                || LANG_EN.equalsIgnoreCase(language)
                || LANG_PT.equalsIgnoreCase(language)) {
            return language.toLowerCase(Locale.US);
        }
        return LANG_AUTO;
    }

    private static String normalizeConcrete(String language, String fallback) {
        String normalized = normalize(language);
        return LANG_AUTO.equals(normalized) ? fallback : normalized;
    }
}
