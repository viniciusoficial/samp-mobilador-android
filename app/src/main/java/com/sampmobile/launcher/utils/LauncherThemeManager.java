package com.sampmobile.launcher.utils;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.StateListDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.Switch;
import android.widget.TextView;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputLayout;
import com.sampmobile.launcher.R;
import com.sampmobile.launcher.ui.widget.GtaOptionView;
import com.sampmobile.launcher.ui.widget.GtaSaSliderView;
import com.sampmobile.launcher.ui.widget.GtaTabStrip;

import java.util.ArrayList;
import java.util.List;

public class LauncherThemeManager {
    public static final String PREF_HUE = "launcher_theme_hue";
    public static final int DEFAULT_HUE = 38; // Classic GTA/SAMP Gold (#F2B32C)
    public static final int XML_DEFAULT_ACCENT = 0xFFF2B32C;

    private static final int TAG_THEME_ROLE = R.id.tag_theme_role;
    private static final String ROLE_ACCENT_TEXT = "role_accent_text";
    private static final String ROLE_ACCENT_BG_SOLID = "role_accent_bg_solid";
    private static final String ROLE_ACCENT_BTN_OUTLINE = "role_accent_btn_outline";
    private static final String ROLE_ACCENT_IMAGE_TINT = "role_accent_image_tint";

    private static final List<ThemeListener> sListeners = new ArrayList<>();

    public interface ThemeListener {
        void onThemeColorChanged(int accentColor);
    }

    public static void addListener(ThemeListener l) {
        if (!sListeners.contains(l)) {
            sListeners.add(l);
        }
    }

    public static void removeListener(ThemeListener l) {
        sListeners.remove(l);
    }

    public static int getHue(Context context) {
        SharedPreferences prefs = context.getSharedPreferences("samp_settings", Context.MODE_PRIVATE);
        return prefs.getInt(PREF_HUE, DEFAULT_HUE);
    }

    public static void setHue(Context context, int hue) {
        int clamped = Math.max(0, Math.min(360, hue));
        context.getSharedPreferences("samp_settings", Context.MODE_PRIVATE)
                .edit()
                .putInt(PREF_HUE, clamped)
                .apply();
        int accent = getAccentColor(context);
        for (ThemeListener l : new ArrayList<>(sListeners)) {
            l.onThemeColorChanged(accent);
        }
    }

    public static int hueToAccentColor(int hue) {
        float[] hsv = new float[]{hue % 360, 0.84f, 0.96f};
        return Color.HSVToColor(hsv);
    }

    public static int getAccentColor(Context context) {
        return hueToAccentColor(getHue(context));
    }

    public static int getAccentColorAlpha(Context context, int alpha) {
        int c = getAccentColor(context);
        return Color.argb(alpha, Color.red(c), Color.green(c), Color.blue(c));
    }

    private static boolean isDefaultGoldColor(int color) {
        if (color == XML_DEFAULT_ACCENT || color == hueToAccentColor(DEFAULT_HUE)) {
            return true;
        }
        int r = Color.red(color);
        int g = Color.green(color);
        int b = Color.blue(color);
        return (Math.abs(r - 0xF2) <= 10 && Math.abs(g - 0xB3) <= 18 && Math.abs(b - 0x2C) <= 18);
    }

    private static int withAlpha(int color, int alpha) {
        return Color.argb(alpha, Color.red(color), Color.green(color), Color.blue(color));
    }

    /**
     * Selected list row: rounded accent wash fading to the right + a short accent bar on the left edge.
     */
    public static Drawable createSelectedRowDrawable(Context ctx, int accentColor) {
        float dp = ctx.getResources().getDisplayMetrics().density;

        GradientDrawable fill = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{withAlpha(accentColor, 0x57), withAlpha(accentColor, 0x12)});
        fill.setCornerRadius(8 * dp);

        GradientDrawable leftBar = new GradientDrawable();
        leftBar.setColor(accentColor);
        leftBar.setCornerRadius(2 * dp);
        int barWidth = Math.max(2, Math.round(3 * dp));

        LayerDrawable layer = new LayerDrawable(new Drawable[]{fill, leftBar});
        layer.setLayerGravity(1, Gravity.LEFT | Gravity.FILL_VERTICAL);
        layer.setLayerWidth(1, barWidth);
        int barInset = Math.round(9 * dp);
        layer.setLayerInset(1, 0, barInset, 0, barInset);
        return layer;
    }

    /**
     * Nav rail selection marker: soft accent pill behind the item + accent bar hugging the rail's left edge.
     */
    public static Drawable createNavIndicatorDrawable(Context ctx, int accentColor) {
        float dp = ctx.getResources().getDisplayMetrics().density;

        GradientDrawable pill = new GradientDrawable();
        pill.setColor(withAlpha(accentColor, 0x26));
        pill.setCornerRadius(12 * dp);

        GradientDrawable bar = new GradientDrawable();
        bar.setColor(accentColor);
        float r = 3 * dp;
        bar.setCornerRadii(new float[]{0, 0, r, r, r, r, 0, 0});
        int barWidth = Math.max(2, Math.round(3 * dp));

        LayerDrawable layer = new LayerDrawable(new Drawable[]{pill, bar});
        int padH = Math.round(7 * dp);
        int padV = Math.round(3 * dp);
        layer.setLayerInset(0, padH, padV, padH, padV);
        layer.setLayerGravity(1, Gravity.LEFT | Gravity.FILL_VERTICAL);
        layer.setLayerWidth(1, barWidth);
        int barInset = Math.round(11 * dp);
        layer.setLayerInset(1, 0, barInset, 0, barInset);
        return layer;
    }

    /**
     * Pill switch: accent track when checked, faint white track when off, with a short cross-fade.
     */
    public static void applySwitchTheme(Switch sw, int accentColor) {
        if (sw == null) return;
        float dp = sw.getResources().getDisplayMetrics().density;
        int w = Math.round(44 * dp);
        int h = Math.round(22 * dp);

        StateListDrawable track = new StateListDrawable();
        track.addState(new int[]{android.R.attr.state_checked}, buildSwitchTrack(accentColor, w, h));
        track.addState(new int[]{}, buildSwitchTrack(0x30FFFFFF, w, h));
        track.setEnterFadeDuration(120);
        track.setExitFadeDuration(160);
        sw.setSwitchMinWidth(w);
        sw.setTrackDrawable(track);
    }

    private static Drawable buildSwitchTrack(int color, int w, int h) {
        GradientDrawable pill = new GradientDrawable();
        pill.setColor(color);
        pill.setCornerRadius(h);
        pill.setSize(w, h);
        return pill;
    }

    /**
     * Recursively applies the user's accent color across an entire view hierarchy:
     * - GtaSaSliderView (track & thumb)
     * - Switch (ON track pill color)
     * - TextInputLayout (dropdown arrow tint & active box stroke)
     * - MaterialButton (outline stroke & text if originally gold)
     * - TextView (section headers, card titles, slider percentage labels, badges, links originally gold)
     * - ImageView (icons originally tinted gold)
     * - View (2dp accent underline bars originally gold)
     */
    public static void applyThemeToTree(View root, int accentColor) {
        if (root == null) return;

        if (root instanceof GtaSaSliderView) {
            ((GtaSaSliderView) root).setAccentColor(accentColor);
            return;
        }

        if (root instanceof Switch) {
            applySwitchTheme((Switch) root, accentColor);
            return;
        }

        if (root instanceof GtaTabStrip) {
            ((GtaTabStrip) root).setAccentColor(accentColor);
            return;
        }

        if (root instanceof GtaOptionView) {
            ((GtaOptionView) root).setAccentColor(accentColor);
            return;
        }

        if (root instanceof TextInputLayout) {
            TextInputLayout til = (TextInputLayout) root;
            til.setEndIconTintList(ColorStateList.valueOf(accentColor));
            til.setBoxStrokeColor(accentColor);
            til.setHintTextColor(ColorStateList.valueOf(accentColor));
        }

        Object tag = root.getTag();
        if (tag instanceof String) {
            String t = (String) tag;
            if ("theme_accent_text".equals(t) && root instanceof TextView) {
                ((TextView) root).setTextColor(accentColor);
            } else if ("theme_accent_bg".equals(t)) {
                root.setBackgroundTintList(ColorStateList.valueOf(accentColor));
            } else if ("theme_accent_btn".equals(t) && root instanceof MaterialButton) {
                MaterialButton mb = (MaterialButton) root;
                mb.setTextColor(accentColor);
                mb.setStrokeColor(ColorStateList.valueOf(accentColor));
            }
        }

        Object roleObj = root.getTag(TAG_THEME_ROLE);
        String role = (roleObj instanceof String) ? (String) roleObj : null;

        if (root instanceof MaterialButton) {
            MaterialButton mb = (MaterialButton) root;
            if (role == null) {
                if (isDefaultGoldColor(mb.getCurrentTextColor())) {
                    role = ROLE_ACCENT_BTN_OUTLINE;
                    mb.setTag(TAG_THEME_ROLE, role);
                }
            }
            if (ROLE_ACCENT_BTN_OUTLINE.equals(role)) {
                mb.setTextColor(accentColor);
                mb.setStrokeColor(ColorStateList.valueOf(accentColor));
            }
        } else if (root instanceof TextView) {
            TextView tv = (TextView) root;
            if (role == null) {
                if (isDefaultGoldColor(tv.getCurrentTextColor())) {
                    role = ROLE_ACCENT_TEXT;
                    tv.setTag(TAG_THEME_ROLE, role);
                }
            }
            if (ROLE_ACCENT_TEXT.equals(role)) {
                tv.setTextColor(accentColor);
            }
        } else if (root instanceof ImageView) {
            ImageView iv = (ImageView) root;
            if (role == null) {
                ColorStateList tint = iv.getImageTintList();
                if (tint != null && isDefaultGoldColor(tint.getDefaultColor())) {
                    role = ROLE_ACCENT_IMAGE_TINT;
                    iv.setTag(TAG_THEME_ROLE, role);
                }
            }
            if (ROLE_ACCENT_IMAGE_TINT.equals(role)) {
                iv.setImageTintList(ColorStateList.valueOf(accentColor));
            }
        } else {
            if (role == null) {
                Drawable bg = root.getBackground();
                if (bg instanceof ColorDrawable && isDefaultGoldColor(((ColorDrawable) bg).getColor())) {
                    role = ROLE_ACCENT_BG_SOLID;
                    root.setTag(TAG_THEME_ROLE, role);
                }
            }
            if (ROLE_ACCENT_BG_SOLID.equals(role)) {
                root.setBackgroundColor(accentColor);
            }
        }

        if (root instanceof ViewGroup) {
            ViewGroup vg = (ViewGroup) root;
            for (int i = 0; i < vg.getChildCount(); i++) {
                applyThemeToTree(vg.getChildAt(i), accentColor);
            }
        }
    }
}
