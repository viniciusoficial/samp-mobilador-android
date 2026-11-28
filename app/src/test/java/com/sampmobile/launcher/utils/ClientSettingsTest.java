package com.sampmobile.launcher.utils;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class ClientSettingsTest {

    @Test
    public void normalizeStringOption_acceptsExplicitIdsCaseInsensitivelyAndLegacyInts() {
        // Explicit string IDs & case-insensitivity
        assertEquals("0.3.7", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, "0.3.7", "0.3.7"));
        assertEquals("0.3.7-R4", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, "0.3.7-r4", "0.3.7"));
        assertEquals("0.3.7-R5", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, " 0.3.7-R5 ", "0.3.7"));
        assertEquals("0.3.DL-R1", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, "0.3.dl-r1", "0.3.7"));

        // Legacy integer migration (from pre-v2 SharedPreferences / settings.json)
        assertEquals("0.3.7", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, 0, "0.3.7"));
        assertEquals("0.3.7-R4", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, 1, "0.3.7"));
        assertEquals("0.3.7-R5", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, 2, "0.3.7"));
        assertEquals("0.3.DL-R1", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, 3, "0.3.7"));
        assertEquals("0.3.DL-R1", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, "3", "0.3.7"));

        // Invalid / out-of-range fallback
        assertEquals("0.3.7", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, "unknown", "0.3.7"));
        assertEquals("0.3.7", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, 99, "0.3.7"));
        assertEquals("0.3.7", ClientSettings.normalizeStringOption(ClientSettings.SAMP_VERSIONS, null, "0.3.7"));
    }

    @Test
    public void enumCoercion_matchesCppSettingsContract() {
        ClientSettings s = ClientSettings.createDefaults();
        assertEquals("0.3.7", s.getString("samp_version"));
        assertEquals("auto", s.getString("aspect_ratio"));
        assertEquals("default", s.getString("crosshair_style"));
        assertEquals("chinese", s.getString("font_family"));
        assertEquals("hud", s.getString("screenshot_mode"));
        assertEquals("ptt", s.getString("voice_mic_mode"));
        assertEquals("GBK", s.getCharset());

        // Legacy integer inputs coerce cleanly to explicit string IDs
        assertTrue(s.put("samp_version", 3));
        assertEquals("0.3.DL-R1", s.getString("samp_version"));

        assertTrue(s.put("aspect_ratio", 2));
        assertEquals("20:9", s.getString("aspect_ratio"));

        assertTrue(s.put("crosshair_style", 3));
        assertEquals("cross_dot", s.getString("crosshair_style"));

        assertTrue(s.put("font_family", 1));
        assertEquals("arial_bold", s.getString("font_family"));

        assertTrue(s.put("screenshot_mode", 1));
        assertEquals("game", s.getString("screenshot_mode"));

        assertTrue(s.put("voice_mic_mode", 1));
        assertEquals("toggle", s.getString("voice_mic_mode"));

        // Re-putting identical value returns false (no redundant write)
        assertFalse(s.put("voice_mic_mode", "TOGGLE"));
    }

    @Test
    public void copyAndServerBounds_preserveIsolationAndValidPort() {
        ClientSettings a = ClientSettings.createDefaults();
        a.setServer("play.example.org", 7778, "secret");
        a.put("timestamp", true);
        a.put("fps_limit", 90);

        ClientSettings b = a.copy();
        b.put("timestamp", false);
        assertTrue(a.getBoolean("timestamp"));
        assertFalse(b.getBoolean("timestamp"));

        assertEquals("play.example.org", a.getServerHost());
        assertEquals(7778, a.getServerPort());
        assertEquals("secret", a.getServerPassword());

        // Invalid host / port fall back safely
        b.setServer("", -1, null);
        assertEquals("127.0.0.1", b.getServerHost());
        assertEquals(7777, b.getServerPort());
        assertEquals("", b.getServerPassword());
    }

    @Test
    public void labels_lookupExplicitValuesNotArrayIndices() {
        assertEquals("无限制", ClientSettings.labelForIntOption(ClientSettings.FPS_LIMITS, 0));
        assertEquals("90", ClientSettings.labelForIntOption(ClientSettings.FPS_LIMITS, 90));
        assertEquals("无限制", ClientSettings.labelForIntOption(ClientSettings.FPS_LIMITS, 999));

        assertEquals("标准", ClientSettings.labelForIntOption(ClientSettings.PED_ZOOM_OPTIONS, 2));
        assertEquals("仅游戏画面", ClientSettings.labelForStringOption(ClientSettings.SCREENSHOT_MODES, "game"));
        assertEquals("点击切换", ClientSettings.labelForStringOption(ClientSettings.VOICE_MIC_MODES, "toggle"));
    }
}
