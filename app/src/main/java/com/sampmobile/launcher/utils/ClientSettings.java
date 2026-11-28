package com.sampmobile.launcher.utils;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Single source of truth for all cross-process settings shared between the launcher
 * process and the isolated {@code :game} process (both Java and native {@code Settings.cpp}).
 *
 * <p>Backing store: {@code <externalFilesDir>/SAMP/settings.json} written atomically via
 * temp-file + rename. The {@code :game} process reads ONLY this file and never touches
 * {@link SharedPreferences}. On first launch after upgrading from older APKs, the launcher
 * migrates existing {@code SharedPreferences} ("samp_settings" / "samp_server") and any
 * pre-v2 {@code settings.json} losslessly into {@code schema_version = 2}.</p>
 */
public final class ClientSettings {
    public static final int SCHEMA_VERSION = 2;
    public static final String REL_PATH = "SAMP/settings.json";

    public static final class StringOption {
        public final String id;
        public final String label;
        public final int legacyInt;

        public StringOption(String id, String label, int legacyInt) {
            this.id = id;
            this.label = label;
            this.legacyInt = legacyInt;
        }
    }

    public static final class IntOption {
        public final int value;
        public final String label;

        public IntOption(int value, String label) {
            this.value = value;
            this.label = label;
        }
    }

    // Explicit named option contracts (1:1 with C++ SettingsContract.h; no implicit index coupling)
    public static final String SAMP_VER_037 = "0.3.7";
    public static final String SAMP_VER_037_R4 = "0.3.7-R4";
    public static final String SAMP_VER_037_R5 = "0.3.7-R5";
    public static final String SAMP_VER_03DL_R1 = "0.3.DL-R1";

    public static final StringOption[] SAMP_VERSIONS = {
            new StringOption(SAMP_VER_037, "0.3.7", 0),
            new StringOption(SAMP_VER_037_R4, "0.3.7-R4", 1),
            new StringOption(SAMP_VER_037_R5, "0.3.7-R5", 2),
            new StringOption(SAMP_VER_03DL_R1, "0.3.DL-R1", 3),
    };

    public static final StringOption[] ASPECT_RATIOS = {
            new StringOption("auto", "自动适配屏幕", 0),
            new StringOption("16:9", "16:9 标准宽屏", 1),
            new StringOption("20:9", "20:9 全面屏", 2),
            new StringOption("21:9", "21:9 超宽屏", 3),
            new StringOption("16:10", "16:10 平板比例", 4),
            new StringOption("4:3", "4:3 经典比例", 5),
    };

    public static final StringOption[] CROSSHAIR_STYLES = {
            new StringOption("default", "原版经典准星", 0),
            new StringOption("cross", "精简十字准星", 1),
            new StringOption("dot", "中心高亮圆点", 2),
            new StringOption("cross_dot", "十字加圆点", 3),
            new StringOption("hidden", "隐藏准星", 4),
    };

    public static final StringOption[] FONT_FAMILIES = {
            new StringOption("chinese", "默认中文黑体", 0),
            new StringOption("arial_bold", "经典西文粗体", 1),
            new StringOption("custom", "自定义字体", 2),
    };

    public static final String SCREENSHOT_MODE_HUD = "hud";
    public static final String SCREENSHOT_MODE_GAME = "game";
    public static final StringOption[] SCREENSHOT_MODES = {
            new StringOption(SCREENSHOT_MODE_HUD, "含界面与聊天", 0),
            new StringOption(SCREENSHOT_MODE_GAME, "仅游戏画面", 1),
    };

    public static final StringOption[] VOICE_MIC_MODES = {
            new StringOption("ptt", "按住说话", 0),
            new StringOption("toggle", "点击切换", 1),
    };

    public static final StringOption[] CHARSETS = {
            new StringOption("GBK", "GBK", 0),
            new StringOption("UTF-8", "UTF-8", 1),
            new StringOption("BIG5", "BIG5", 2),
            new StringOption("CP1251", "CP1251", 3),
    };

    public static final IntOption[] FPS_LIMITS = {
            new IntOption(0, "无限制"),
            new IntOption(30, "30"),
            new IntOption(45, "45"),
            new IntOption(60, "60"),
            new IntOption(75, "75"),
            new IntOption(90, "90"),
            new IntOption(120, "120"),
            new IntOption(144, "144"),
    };

    // GTA:SA MobileSettings explicit numeric enum tables (value = GTA engine enum value)
    public static final IntOption[] VISUAL_FX_OPTIONS = {
            new IntOption(0, "低"),
            new IntOption(1, "中"),
            new IntOption(2, "高"),
            new IntOption(3, "极高"),
    };
    public static final IntOption[] SHADOWS_OPTIONS = {
            new IntOption(0, "关闭"),
            new IntOption(1, "经典"),
            new IntOption(2, "高级实时"),
    };
    public static final IntOption[] CAR_REFL_OPTIONS = {
            new IntOption(0, "关闭"),
            new IntOption(1, "低"),
            new IntOption(2, "中"),
            new IntOption(3, "高"),
    };
    public static final IntOption[] TRAFFIC_OPTIONS = {
            new IntOption(0, "稀少"),
            new IntOption(1, "适中"),
            new IntOption(2, "密集"),
    };
    public static final IntOption[] PED_ZOOM_OPTIONS = {
            new IntOption(1, "近距"),
            new IntOption(2, "标准"),
            new IntOption(3, "远距"),
    };
    public static final IntOption[] VEH_CAM_OPTIONS = {
            new IntOption(0, "低视角"),
            new IntOption(1, "高视角"),
    };
    public static final IntOption[] TARGETING_OPTIONS = {
            new IntOption(0, "锁定辅助"),
            new IntOption(1, "自由准星"),
    };
    public static final IntOption[] STEERING_OPTIONS = {
            new IntOption(0, "虚拟摇杆"),
            new IntOption(1, "左右按键"),
            new IntOption(2, "滑动转向"),
    };
    public static final IntOption[] LAYOUT_OPTIONS = {
            new IntOption(0, "经典布局"),
            new IntOption(1, "双侧自适应"),
    };
    public static final IntOption[] ACCEL_OPTIONS = {
            new IntOption(0, "关闭"),
            new IntOption(1, "轻度"),
            new IntOption(2, "重度"),
    };
    public static final IntOption[] RADIO_STATION_OPTIONS = {
            new IntOption(0, "关闭电台"),
            new IntOption(1, "Playback FM"),
            new IntOption(2, "K-Rose"),
            new IntOption(3, "K-DST"),
            new IntOption(4, "Bounce FM"),
            new IntOption(5, "SF-UR"),
            new IntOption(6, "Radio Los Santos"),
            new IntOption(7, "Radio X"),
            new IntOption(8, "CSR 103.9"),
            new IntOption(9, "K-Jah West"),
            new IntOption(10, "Master Sounds"),
            new IntOption(11, "WCTR"),
            new IntOption(12, "自定义音乐"),
    };

    // Single canonical defaults map (key -> Boolean / Integer / Float / String)
    private static final Map<String, Object> DEFAULTS;
    private static final Map<String, StringOption[]> ENUM_STRING_FIELDS;

    static {
        Map<String, Object> d = new LinkedHashMap<>();
        d.put("offline_mode", false);
        d.put("app_language", LocaleManager.LANG_AUTO);
        d.put("nick_name", "samp_player");
        d.put("samp_version", SAMP_VER_037);
        d.put("system_keyboard", true);
        d.put("timestamp", false);
        d.put("fullscreen", false);
        d.put("fast_connect", false);
        d.put("voice_chat", false);
        d.put("display_fps", false);
        d.put("cleo_scripts", false);
        d.put("aml_scripts", false);
        d.put("jpatch_enabled", false);
        d.put("monet_scripts", false);
        d.put("modloader", false);
        d.put("modify_files", false);
        d.put("fps_limit", 0);
        d.put("chat_strings", 8);
        d.put("font_size", 25.0f);
        d.put("chat_posx", 100);
        d.put("chat_posy", 20);
        d.put("chat_sizex", 360);
        d.put("chat_sizey", 110);
        d.put("charset", "GBK");

        // Extended multiplayer & gameplay
        d.put("head_move", true);
        d.put("auto_climb", true);
        d.put("dl_custom_models", true);

        // Graphics & display (GTA:SA MobileSettings)
        d.put("gta_visual_fx", 2);
        d.put("gta_resolution", 100);
        d.put("gta_draw_distance", 70);
        d.put("gta_shadows", 1);
        d.put("gta_car_reflections", 2);
        d.put("gta_traffic_mode", 2);
        d.put("gta_brightness", 60);
        d.put("aspect_ratio", "auto");
        d.put("frame_limiter", false);

        // Camera, controls & single-player GTA:SA options
        d.put("cam_sens_x", 50);
        d.put("cam_sens_y", 50);
        d.put("cam_sens_foot", 100);
        d.put("cam_sens_veh", 100);
        d.put("cam_sens_aim", 70);
        d.put("free_cam", false);
        d.put("first_person_look", true);
        d.put("invert_look", false);
        d.put("veh_cam_height", 1);
        d.put("ped_cam_zoom", 2);
        d.put("targeting_mode", 0);
        d.put("joystick_size", 100);
        d.put("analog_sensitivity", 50);
        d.put("flick_sensitivity", 50);
        d.put("shoot_threshold", 50);
        d.put("touch_steering", 0);
        d.put("touch_layout", 0);
        d.put("bike_steering", false);
        d.put("accelerometer", 0);
        d.put("vibration", true);
        d.put("joypad_invert_x", false);
        d.put("joypad_invert_y", false);
        d.put("km_enabled", true);
        d.put("open_hud_edit_on_start", false);

        // Audio, radio & voice chat
        d.put("sfx_volume", 80);
        d.put("music_volume", 80);
        d.put("radio_autotune", true);
        d.put("radio_station", 0);
        d.put("voice_volume", 100);
        d.put("voice_mic_enable", true);
        d.put("voice_mic_volume", 75);
        d.put("voice_mic_mode", "ptt");
        d.put("voice_balancer", false);
        d.put("voice_filter", true);
        d.put("voice_speaker_list", true);
        d.put("voice_btn_size", 100);

        // HUD, fonts, chat & screenshot
        d.put("show_gta_hud", true);
        d.put("show_radar", true);
        d.put("radar_tap_map", true);
        d.put("crosshair_style", "default");
        d.put("show_death_window", true);
        d.put("show_nametags", true);
        d.put("show_nametag_weapon", false);
        d.put("nametag_size", 100);
        d.put("textlabel_size", 100);
        d.put("font_family", "chinese");
        d.put("show_button_panel", true);
        d.put("show_screenshot_btn", true);
        d.put("gta_subtitles", true);
        d.put("chat_shadow", true);
        d.put("screenshot_mode", SCREENSHOT_MODE_HUD);
        DEFAULTS = Collections.unmodifiableMap(d);

        Map<String, StringOption[]> enums = new LinkedHashMap<>();
        enums.put("samp_version", SAMP_VERSIONS);
        enums.put("aspect_ratio", ASPECT_RATIOS);
        enums.put("crosshair_style", CROSSHAIR_STYLES);
        enums.put("font_family", FONT_FAMILIES);
        enums.put("screenshot_mode", SCREENSHOT_MODES);
        enums.put("voice_mic_mode", VOICE_MIC_MODES);
        ENUM_STRING_FIELDS = Collections.unmodifiableMap(enums);
    }

    private static final Object LOCK = new Object();
    private static final ExecutorService WRITER = Executors.newSingleThreadExecutor();
    private static ClientSettings sLauncherCache = null;
    private static long sLauncherLoadedMtime = -1L;
    private static long sWriteGeneration = 0L;

    private String serverHost = "127.0.0.1";
    private int serverPort = 7777;
    private String serverPassword = "";
    private final Map<String, Object> values = new LinkedHashMap<>(DEFAULTS);

    private ClientSettings() {}

    public static ClientSettings createDefaults() {
        return new ClientSettings();
    }

    public ClientSettings copy() {
        ClientSettings c = new ClientSettings();
        c.serverHost = this.serverHost;
        c.serverPort = this.serverPort;
        c.serverPassword = this.serverPassword;
        c.values.putAll(this.values);
        return c;
    }

    // ------------------------------------------------------------------ Option helpers

    public static String normalizeStringOption(StringOption[] options, Object raw, String fallbackId) {
        if (raw instanceof String) {
            String s = ((String) raw).trim();
            for (StringOption o : options) {
                if (o.id.equalsIgnoreCase(s)) return o.id;
            }
            // Handle numeric strings gracefully
            try {
                int idx = Integer.parseInt(s);
                for (StringOption o : options) {
                    if (o.legacyInt == idx) return o.id;
                }
            } catch (NumberFormatException ignored) {
            }
        } else if (raw instanceof Number) {
            int idx = ((Number) raw).intValue();
            for (StringOption o : options) {
                if (o.legacyInt == idx) return o.id;
            }
        }
        return fallbackId;
    }

    public static String labelForStringOption(StringOption[] options, String id) {
        if (id != null) {
            for (StringOption o : options) {
                if (o.id.equalsIgnoreCase(id)) return I18n.tr(o.label);
            }
        }
        return I18n.tr(options[0].label);
    }

    public static String[] labelsOf(StringOption[] options) {
        String[] out = new String[options.length];
        for (int i = 0; i < options.length; i++) out[i] = I18n.tr(options[i].label);
        return out;
    }

    public static String labelForIntOption(IntOption[] options, int value) {
        for (IntOption o : options) {
            if (o.value == value) return I18n.tr(o.label);
        }
        return I18n.tr(options[0].label);
    }

    public static String[] labelsOf(IntOption[] options) {
        String[] out = new String[options.length];
        for (int i = 0; i < options.length; i++) out[i] = I18n.tr(options[i].label);
        return out;
    }

    // ------------------------------------------------------------------ Value accessors

    public String getServerHost() { return serverHost; }
    public int getServerPort() { return serverPort; }
    public String getServerPassword() { return serverPassword; }

    public void setServer(String host, int port, String password) {
        this.serverHost = (host != null && !host.isEmpty()) ? host : "127.0.0.1";
        this.serverPort = (port >= 1 && port <= 65535) ? port : 7777;
        this.serverPassword = password != null ? password : "";
    }

    public boolean getBoolean(String key) {
        Object v = values.get(key);
        if (v instanceof Boolean) return (Boolean) v;
        Object def = DEFAULTS.get(key);
        return def instanceof Boolean && (Boolean) def;
    }

    public int getInt(String key) {
        Object v = values.get(key);
        if (v instanceof Number) return ((Number) v).intValue();
        Object def = DEFAULTS.get(key);
        return def instanceof Number ? ((Number) def).intValue() : 0;
    }

    public float getFloat(String key) {
        Object v = values.get(key);
        if (v instanceof Number) return ((Number) v).floatValue();
        Object def = DEFAULTS.get(key);
        return def instanceof Number ? ((Number) def).floatValue() : 0.0f;
    }

    public String getString(String key) {
        Object v = values.get(key);
        if (v instanceof String) return (String) v;
        Object def = DEFAULTS.get(key);
        return def instanceof String ? (String) def : "";
    }

    public boolean put(String key, Object value) {
        Object def = DEFAULTS.get(key);
        if (def == null || value == null) return false;
        Object coerced = coerceValue(key, def, value);
        Object prev = values.get(key);
        if (coerced.equals(prev)) return false;
        values.put(key, coerced);
        return true;
    }

    // Typed convenience accessors for the :game process
    public boolean isOfflineMode() { return getBoolean("offline_mode"); }
    public boolean isFullscreen() { return getBoolean("fullscreen"); }
    public boolean isAmlEnabled() { return getBoolean("aml_scripts"); }
    public boolean isJPatchEnabled() { return getBoolean("jpatch_enabled"); }
    public boolean isMonetEnabled() { return getBoolean("monet_scripts"); }
    public boolean isOpenHudEditOnStart() { return getBoolean("open_hud_edit_on_start"); }
    public String getCharset() {
        String cs = getString("charset");
        return cs.isEmpty() ? "GBK" : cs;
    }

    private static Object coerceValue(String key, Object def, Object raw) {
        StringOption[] enumOpts = ENUM_STRING_FIELDS.get(key);
        if (enumOpts != null) {
            return normalizeStringOption(enumOpts, raw, (String) def);
        }
        if (def instanceof Boolean) {
            if (raw instanceof Boolean) return raw;
            if (raw instanceof Number) return ((Number) raw).intValue() != 0;
            return def;
        }
        if (def instanceof Integer) {
            if (raw instanceof Number) return ((Number) raw).intValue();
            if (raw instanceof String) {
                try { return Integer.parseInt(((String) raw).trim()); } catch (NumberFormatException ignored) {}
            }
            return def;
        }
        if (def instanceof Float) {
            if (raw instanceof Number) return ((Number) raw).floatValue();
            if (raw instanceof String) {
                try { return Float.parseFloat(((String) raw).trim()); } catch (NumberFormatException ignored) {}
            }
            return def;
        }
        if (def instanceof String) {
            return String.valueOf(raw);
        }
        return def;
    }

    // ------------------------------------------------------------------ JSON codec

    public JSONObject toJson() throws JSONException {
        JSONObject root = new JSONObject();
        root.put("schema_version", SCHEMA_VERSION);

        JSONObject client = new JSONObject();
        JSONObject server = new JSONObject();
        server.put("host", serverHost);
        server.put("port", serverPort);
        server.put("password", serverPassword);
        client.put("server", server);

        JSONObject settingsObj = new JSONObject();
        for (Map.Entry<String, Object> entry : values.entrySet()) {
            Object val = entry.getValue();
            if (val instanceof Float) {
                settingsObj.put(entry.getKey(), ((Float) val).doubleValue());
            } else {
                settingsObj.put(entry.getKey(), val);
            }
        }
        client.put("settings", settingsObj);
        root.put("client", client);
        return root;
    }

    public static ClientSettings fromJson(JSONObject root) {
        ClientSettings out = createDefaults();
        if (root == null) return out;
        JSONObject client = root.optJSONObject("client");
        if (client == null) return out;

        JSONObject server = client.optJSONObject("server");
        if (server != null) {
            String host = server.optString("host", out.serverHost);
            int port = server.optInt("port", out.serverPort);
            if (port < 1 || port > 65535) port = 7777;
            String password = server.optString("password", out.serverPassword);
            out.setServer(host, port, password);
        }

        JSONObject sObj = client.optJSONObject("settings");
        if (sObj != null) {
            for (Map.Entry<String, Object> entry : DEFAULTS.entrySet()) {
                String key = entry.getKey();
                if (sObj.has(key) && !sObj.isNull(key)) {
                    out.put(key, sObj.opt(key));
                }
            }
        }
        return out;
    }

    public static File settingsFile(Context ctx) {
        if (ctx == null) return null;
        File extDir = ctx.getExternalFilesDir(null);
        return extDir != null ? new File(extDir, REL_PATH) : null;
    }

    // ------------------------------------------------------------------ :game process API (JSON-only, zero SharedPreferences)

    /**
     * Reads {@code SAMP/settings.json} directly from disk for the isolated {@code :game} process.
     * Never touches {@link SharedPreferences} and never overwrites the file.
     */
    public static ClientSettings readGameSnapshot(Context ctx) {
        File file = settingsFile(ctx);
        if (file == null || !file.isFile()) {
            return createDefaults();
        }
        try {
            return fromJson(new JSONObject(Utils.readJSONFromFile(file)));
        } catch (Exception e) {
            e.printStackTrace();
            return createDefaults();
        }
    }

    /**
     * Clears the one-shot {@code open_hud_edit_on_start} flag in {@code SAMP/settings.json}
     * after native {@code Settings::initialize()} has captured it for the current session.
     */
    public static void consumeOpenHudEditOnStart(Context ctx) {
        File file = settingsFile(ctx);
        if (file == null || !file.isFile()) return;
        synchronized (LOCK) {
            try {
                ClientSettings current = fromJson(new JSONObject(Utils.readJSONFromFile(file)));
                if (current.put("open_hud_edit_on_start", false)) {
                    Utils.writeJSONToFile(file, current.toJson());
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    // ------------------------------------------------------------------ Launcher process API

    /**
     * Loads the authoritative settings for the launcher process.
     * Automatically performs one-time lossless migration from legacy SharedPreferences when
     * {@code SAMP/settings.json} is absent or predates {@link #SCHEMA_VERSION}, and reloads
     * from disk whenever the {@code :game} process has updated the file (e.g. {@code /timestamp}).
     */
    public static ClientSettings loadLauncher(Context ctx) {
        if (ctx == null) return createDefaults();
        Context appCtx = ctx.getApplicationContext();
        File file = settingsFile(appCtx);

        synchronized (LOCK) {
            long diskMtime = (file != null && file.isFile()) ? file.lastModified() : -1L;
            if (sLauncherCache != null && diskMtime == sLauncherLoadedMtime && diskMtime > 0L) {
                return sLauncherCache.copy();
            }

            JSONObject diskJson = null;
            int schemaVersion = 0;
            if (file != null && file.isFile()) {
                try {
                    diskJson = new JSONObject(Utils.readJSONFromFile(file));
                    schemaVersion = diskJson.optInt("schema_version", 0);
                } catch (Exception ignored) {
                }
            }

            ClientSettings loaded;
            boolean needsWrite = false;
            if (diskJson != null && schemaVersion >= SCHEMA_VERSION) {
                loaded = fromJson(diskJson);
            } else {
                loaded = migrateFromLegacy(appCtx, diskJson);
                needsWrite = true;
            }

            sLauncherCache = loaded.copy();
            if (needsWrite && file != null) {
                writeLocked(file, sLauncherCache);
            } else {
                sLauncherLoadedMtime = diskMtime;
            }
            return sLauncherCache.copy();
        }
    }

    /** Forces a fresh read from {@code SAMP/settings.json} (e.g. after returning from {@code :game}). */
    public static ClientSettings reloadLauncher(Context ctx) {
        synchronized (LOCK) {
            sLauncherCache = null;
            sLauncherLoadedMtime = -1L;
        }
        return loadLauncher(ctx);
    }

    public static void ensureExists(Context ctx) {
        if (ctx == null) return;
        File file = settingsFile(ctx.getApplicationContext());
        if (file == null) return;
        synchronized (LOCK) {
            ClientSettings cur = loadLauncher(ctx);
            if (!file.isFile()) {
                writeLocked(file, cur);
            }
        }
    }

    public static String getCharsetForLauncher(Context ctx) {
        return loadLauncher(ctx).getCharset();
    }

    /** Updates one setting asynchronously (latest-generation wins; never races with sync launch writes). */
    public static void setAsync(Context ctx, String key, Object value) {
        if (ctx == null) return;
        Context appCtx = ctx.getApplicationContext();
        File file = settingsFile(appCtx);
        final ClientSettings snapshot;
        final long gen;
        synchronized (LOCK) {
            loadLauncher(appCtx);
            if (!sLauncherCache.put(key, value)) {
                return;
            }
            snapshot = sLauncherCache.copy();
            gen = ++sWriteGeneration;
        }
        if (file != null) {
            WRITER.execute(() -> {
                synchronized (LOCK) {
                    if (gen != sWriteGeneration) return;
                    writeLocked(file, snapshot);
                }
            });
        }
    }

    /** Updates one setting and flushes synchronously to {@code SAMP/settings.json}. */
    public static void setSync(Context ctx, String key, Object value) {
        if (ctx == null) return;
        Context appCtx = ctx.getApplicationContext();
        File file = settingsFile(appCtx);
        synchronized (LOCK) {
            loadLauncher(appCtx);
            sLauncherCache.put(key, value);
            ++sWriteGeneration;
            if (file != null) {
                writeLocked(file, sLauncherCache);
            }
        }
    }

    /** Applies a batch of setting updates and flushes synchronously. */
    public static void updateBatchSync(Context ctx, Map<String, Object> updates) {
        if (ctx == null || updates == null || updates.isEmpty()) return;
        Context appCtx = ctx.getApplicationContext();
        File file = settingsFile(appCtx);
        synchronized (LOCK) {
            loadLauncher(appCtx);
            for (Map.Entry<String, Object> e : updates.entrySet()) {
                sLauncherCache.put(e.getKey(), e.getValue());
            }
            ++sWriteGeneration;
            if (file != null) {
                writeLocked(file, sLauncherCache);
            }
        }
    }

    /** Persists target server + player profile + online flag synchronously before launching {@code :game}. */
    public static void prepareOnlineLaunch(Context ctx, String host, int port, String password,
                                           String nick, String sampVersionId) {
        if (ctx == null) return;
        Context appCtx = ctx.getApplicationContext();
        File file = settingsFile(appCtx);
        synchronized (LOCK) {
            loadLauncher(appCtx);
            sLauncherCache.put("offline_mode", false);
            if (nick != null && !nick.isEmpty()) {
                sLauncherCache.put("nick_name", nick);
            }
            if (sampVersionId != null && !sampVersionId.isEmpty()) {
                sLauncherCache.put("samp_version", sampVersionId);
            }
            sLauncherCache.setServer(host, port, password);
            ++sWriteGeneration;
            if (file != null) {
                writeLocked(file, sLauncherCache);
            }
        }
    }

    /** Persists offline flag synchronously before launching {@code :game}. */
    public static void prepareOfflineLaunch(Context ctx) {
        setSync(ctx, "offline_mode", true);
    }

    /** Resets all client settings to defaults while preserving the last server target. */
    public static ClientSettings resetDefaults(Context ctx) {
        if (ctx == null) return createDefaults();
        Context appCtx = ctx.getApplicationContext();
        File file = settingsFile(appCtx);
        synchronized (LOCK) {
            ClientSettings prev = loadLauncher(appCtx);
            ClientSettings fresh = createDefaults();
            fresh.setServer(prev.serverHost, prev.serverPort, prev.serverPassword);
            fresh.put("app_language", prev.getString("app_language"));
            sLauncherCache = fresh.copy();
            ++sWriteGeneration;
            if (file != null) {
                writeLocked(file, sLauncherCache);
            }
            return sLauncherCache.copy();
        }
    }

    private static void writeLocked(File file, ClientSettings snapshot) {
        try {
            Utils.writeJSONToFile(file, snapshot.toJson());
            sLauncherLoadedMtime = file.lastModified();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private static ClientSettings migrateFromLegacy(Context appCtx, JSONObject preV2DiskJson) {
        ClientSettings out = createDefaults();

        // 1. Seed from any existing pre-v2 SAMP/settings.json first
        if (preV2DiskJson != null) {
            out = fromJson(preV2DiskJson);
        }

        // 2. Overlay legacy SharedPreferences ("samp_settings" & "samp_server") if present
        SharedPreferences sp = appCtx.getSharedPreferences("samp_settings", Context.MODE_PRIVATE);
        Map<String, ?> allSp = sp.getAll();
        if (allSp != null && !allSp.isEmpty()) {
            for (Map.Entry<String, Object> defEntry : DEFAULTS.entrySet()) {
                String key = defEntry.getKey();
                if (allSp.containsKey(key) && allSp.get(key) != null) {
                    out.put(key, allSp.get(key));
                }
            }
            if (!sp.getBoolean("prefs_defaults_v2", false)) {
                out.put("voice_chat", false);
                out.put("show_nametag_weapon", false);
            }
        }

        // Preserve any in-game /timestamp toggle persisted in pre-v2 settings.json
        if (preV2DiskJson != null) {
            JSONObject c = preV2DiskJson.optJSONObject("client");
            JSONObject s = c != null ? c.optJSONObject("settings") : null;
            if (s != null && s.has("timestamp") && !s.isNull("timestamp")) {
                out.put("timestamp", s.opt("timestamp"));
            }
        }

        SharedPreferences srv = appCtx.getSharedPreferences("samp_server", Context.MODE_PRIVATE);
        Map<String, ?> allSrv = srv.getAll();
        if (allSrv != null && !allSrv.isEmpty()) {
            String host = srv.getString("host", out.serverHost);
            int port = out.serverPort;
            try {
                port = Integer.parseInt(srv.getString("port", String.valueOf(out.serverPort)));
            } catch (Exception ignored) {
            }
            String pass = srv.getString("password", out.serverPassword);
            out.setServer(host, port, pass);
        }

        // Normalize charset uppercase if needed
        String cs = out.getString("charset");
        if (cs.isEmpty()) {
            out.put("charset", "GBK");
        } else {
            out.put("charset", cs.toUpperCase(Locale.ROOT));
        }

        return out;
    }
}
