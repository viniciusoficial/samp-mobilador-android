package com.sampmobile.launcher.ui.fragment;


import com.sampmobile.launcher.utils.I18n;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.BitmapFactory;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.StatFs;
import android.provider.MediaStore;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.Filter;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.core.content.FileProvider;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.bumptech.glide.Glide;
import com.google.android.material.textfield.MaterialAutoCompleteTextView;
import com.joom.paranoid.Obfuscate;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

import com.sampmobile.launcher.BuildConfig;
import com.sampmobile.launcher.R;
import com.sampmobile.launcher.download.GamedataInstallerTask;
import com.sampmobile.launcher.download.GamedataState;
import com.sampmobile.launcher.ui.GameDataImportDialog;
import com.sampmobile.launcher.ui.widget.GtaOptionView;
import com.sampmobile.launcher.ui.widget.GtaSaSliderView;
import com.sampmobile.launcher.ui.widget.GtaTabStrip;
import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.GameDataImporter;
import com.sampmobile.launcher.utils.GameDataVerifier;
import com.sampmobile.launcher.utils.LauncherThemeManager;
import com.sampmobile.launcher.utils.LocaleManager;

@Obfuscate
public class SettingsPageFragment extends Fragment implements LauncherThemeManager.ThemeListener {

    private static final int CAT_MULTIPLAYER = 0;
    private static final int CAT_GAMEDATA    = 1;
    private static final int CAT_GRAPHICS    = 2;
    private static final int CAT_CAMERA      = 3;
    private static final int CAT_CONTROLS    = 4;
    private static final int CAT_AUDIO       = 5;
    private static final int CAT_LANGUAGE    = 6;
    private static final int CAT_HUD_CHAT    = 7;
    private static final int CAT_SCREENSHOTS = 8;
    private static final int CAT_LOGS        = 9;
    private static final int CAT_HELP_ABOUT  = 10;

    private static final int[] CATEGORY_TITLE_RES = {
            R.string.settings_cat_multiplayer,
            R.string.settings_cat_gamedata,
            R.string.settings_cat_graphics,
            R.string.settings_cat_camera,
            R.string.settings_cat_controls,
            R.string.settings_cat_audio,
            R.string.settings_cat_language,
            R.string.settings_cat_hud_chat,
            R.string.settings_cat_screenshots,
            R.string.settings_cat_logs,
            R.string.settings_cat_help_about
    };

    private static final String[] GAMEDATA_EDITION_ITEMS = {
            I18n.tr("精简版（推荐）"),
            I18n.tr("完整版（2.10 GiB）")
    };

    private static int sLastCategory = CAT_MULTIPLAYER;
    private int currentCategory = CAT_MULTIPLAYER;
    final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();
    private final SettingsScreenshotController screenshotController = new SettingsScreenshotController(this);
    private final SettingsDiagnosticsController diagnosticsController = new SettingsDiagnosticsController(this);
    private final SettingsGameDataController gameDataController = new SettingsGameDataController(this);
    private final SettingsArtworkCacheController artworkCacheController = new SettingsArtworkCacheController(this);
    private View rootView;
    private GtaTabStrip settingsTabs;

    private View[] categoryPanels;

    private EditText et_settings_nickname;
    private GtaOptionView samp_version, charset, fps_limit;
    private MaterialAutoCompleteTextView game_version;
    private GtaOptionView dropdown_aspect_ratio, dropdown_visual_fx, dropdown_shadows, dropdown_car_reflections, dropdown_traffic_mode;
    private GtaOptionView dropdown_ped_cam_zoom, dropdown_veh_cam_height;
    private GtaOptionView dropdown_targeting_mode, dropdown_touch_steering, dropdown_touch_layout, dropdown_accelerometer;
    private GtaOptionView dropdown_radio_station, dropdown_voice_mic_mode;
    private GtaOptionView dropdown_crosshair_style, dropdown_font_family;
    private GtaOptionView dropdown_app_language;

    private TextView btn_gfx_preset_low, btn_gfx_preset_med, btn_gfx_preset_high, btn_gfx_preset_ultra;
    private TextView btn_open_hud_editor, btn_reset_hud_editor;

    final ActivityResultLauncher<String[]> gameDataZipPicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null && isAdded()) {
                    GameDataImportDialog.startImport(requireActivity(), uri, gameDataController::refreshVerification);
                }
            });

    final ActivityResultLauncher<String[]> artworkCachePicker =
            registerForActivityResult(new ActivityResultContracts.OpenMultipleDocuments(), uris -> {
                if (uris != null && !uris.isEmpty() && isAdded()) {
                    artworkCacheController.onDocumentsPicked(uris);
                }
            });

    private Switch voice, timestamp, fullscreen, displayfps, systemkeyboard;
    private Switch modify_files, fastconnect;
    private Switch headmove_switch, dlmodels_switch, switch_invert_look, switch_auto_climb;
    private Switch switch_frame_limiter, switch_free_cam, switch_first_person_look;
    private Switch switch_bike_steering, switch_vibration, switch_radio_autotune;
    private Switch switch_km_enabled, switch_joypad_invert_x, switch_joypad_invert_y;
    private Switch switch_voice_mic_enable, switch_voice_speaker_list, switch_voice_balancer, switch_voice_filter;
    private Switch switch_show_gta_hud, switch_show_radar, switch_radar_tap_map, switch_show_nametag_weapon;
    private Switch switch_show_death_window, switch_show_nametags, switch_show_button_panel;
    private Switch switch_show_screenshot_btn, switch_gta_subtitles, switch_chat_shadow;

    private View restore_button;
    private View chat_minus, chat_plus;
    private TextView chat_strings;

    private GtaSaSliderView slider_theme_hue;
    private TextView tv_theme_hue_val;

    private GtaSaSliderView font_size_slider, chatposx_slider, chatposy_slider, chatsizex_slider, chatsizey_slider;
    private TextView font_size_value, chatposx_value, chatposy_value, chatsizex_value, chatsizey_value;

    private GtaSaSliderView slider_resolution, slider_draw_distance, slider_brightness;
    private TextView tv_resolution_val, tv_draw_distance_val, tv_brightness_val;

    private GtaSaSliderView slider_cam_sens_x, slider_cam_sens_y, slider_cam_sens_foot, slider_cam_sens_veh, slider_cam_sens_aim;
    private TextView tv_cam_sens_x_val, tv_cam_sens_y_val, tv_cam_sens_foot_val, tv_cam_sens_veh_val, tv_cam_sens_aim_val;

    private GtaSaSliderView slider_joystick_size, slider_shoot_threshold, slider_analog_sens, slider_flick_sens;
    private TextView tv_joystick_size_val, tv_shoot_threshold_val, tv_analog_sens_val, tv_flick_sens_val;

    private GtaSaSliderView slider_sfx_volume, slider_music_volume, slider_voice_volume, slider_voice_mic_volume, slider_voice_btn_size;
    private TextView tv_sfx_volume_val, tv_music_volume_val, tv_voice_volume_val, tv_voice_mic_volume_val, tv_voice_btn_size_val;

    private GtaSaSliderView slider_nametag_size, slider_textlabel_size;
    private TextView tv_nametag_size_val, tv_textlabel_size_val;

    // 截图模式按钮
    private TextView btn_ss_mode_hud, btn_ss_mode_game;

    // 帮助与关于
    private TextView tv_help_mods_body, tv_help_faq_body, tv_help_commands;
    private TextView tv_about_version_line, tv_about_summary;

    ClientSettings settings;
    private boolean updatingWidgets = false;
    private RelativeLayout game_version_layout;

    private static final int[] PANEL_LAYOUTS = {
            R.layout.settings_panel_multiplayer,
            R.layout.settings_panel_gamedata,
            R.layout.settings_panel_graphics,
            R.layout.settings_panel_camera,
            R.layout.settings_panel_controls,
            R.layout.settings_panel_audio,
            R.layout.settings_panel_language,
            R.layout.settings_panel_hud_chat,
            R.layout.settings_panel_screenshots,
            R.layout.settings_panel_logs,
            R.layout.settings_panel_help_about
    };

    /**
     * Inflates the settings page piecewise: the frame first, then one panel per {@link #step()}.
     * The launcher runs a step per idle slot, so pre-warming 600+ views never blocks a frame for long.
     */
    public static final class ViewBuilder {
        private final LayoutInflater inflater;
        private final View root;
        private final ViewGroup panels;
        private int next;

        public ViewBuilder(LayoutInflater inflater, ViewGroup parent) {
            this.inflater = inflater;
            this.root = inflater.inflate(R.layout.fragment_settings_page, parent, false);
            this.panels = root.findViewById(R.id.settings_panels);
        }

        /** Inflates the next panel; returns true while panels remain. */
        public boolean step() {
            if (next < PANEL_LAYOUTS.length) {
                inflater.inflate(PANEL_LAYOUTS[next++], panels, true);
            }
            return next < PANEL_LAYOUTS.length;
        }

        public View finish() {
            while (step()) {
            }
            return root;
        }
    }

    private View prebuiltView;

    public SettingsPageFragment() {
    }

    /** Hands over a page view built ahead of time by {@link ViewBuilder}. */
    public void setPrebuiltView(View view) {
        this.prebuiltView = view;
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View view = prebuiltView != null ? prebuiltView : new ViewBuilder(inflater, container).finish();
        prebuiltView = null;
        this.rootView = view;
        settings = ClientSettings.loadLauncher(requireContext());

        initViews(view);
        setupCategoryNavigation(view);
        setupListeners(view);
        setWidgetsForSettings();
        populateHelpAndAbout();

        LauncherThemeManager.addListener(this);
        onThemeColorChanged(LauncherThemeManager.getAccentColor(requireContext()));

        return view;
    }

    public void onPageShown() {
        if (!isAdded() || rootView == null) return;
        settings = ClientSettings.reloadLauncher(requireContext());
        setWidgetsForSettings();
        if (currentCategory == CAT_MULTIPLAYER) {
            artworkCacheController.refresh();
        } else if (currentCategory == CAT_GAMEDATA) {
            gameDataController.refreshVerification();
        } else if (currentCategory == CAT_SCREENSHOTS) {
            screenshotController.refresh();
        } else if (currentCategory == CAT_LOGS) {
            diagnosticsController.refresh();
        }
    }

    /** 供 MainActivity 在缺少游戏数据时一键切到「文件验证」选项卡。 */
    public void openGameDataTab() {
        if (categoryPanels == null) return;
        if (settingsTabs != null) {
            settingsTabs.setSelectedTab(CAT_GAMEDATA, false);
        }
        switchCategory(CAT_GAMEDATA);
    }

    @Override
    public void onDestroyView() {
        gameDataController.cancelActiveDownload();
        LauncherThemeManager.removeListener(this);
        super.onDestroyView();
    }

    @Override
    public void onDestroy() {
        gameDataController.cancelActiveDownload();
        super.onDestroy();
        ioExecutor.shutdownNow();
    }

    @Override
    public void onThemeColorChanged(int accentColor) {
        if (rootView == null || !isAdded()) return;
        LauncherThemeManager.applyThemeToTree(rootView, accentColor);
        updateGraphicsPresetUI();
        updateScreenshotModeUI();
        updateHudEditorButtonUI();
        gameDataController.updateFilterUI();
        gameDataController.updateDownloadButtonUI();
        artworkCacheController.updateTheme(accentColor);
        screenshotController.notifyThemeChanged();
    }

    private void initViews(View view) {

        et_settings_nickname = view.findViewById(R.id.et_settings_nickname);
        samp_version = view.findViewById(R.id.samp_version);
        game_version = view.findViewById(R.id.game_version);
        charset = view.findViewById(R.id.charset);
        game_version_layout = view.findViewById(R.id.game_version_layout);

        slider_theme_hue = view.findViewById(R.id.slider_theme_hue);
        tv_theme_hue_val = view.findViewById(R.id.tv_theme_hue_val);
        if (slider_theme_hue != null) {
            slider_theme_hue.setRainbowMode(true);
        }

        btn_gfx_preset_low = view.findViewById(R.id.btn_gfx_preset_low);
        btn_gfx_preset_med = view.findViewById(R.id.btn_gfx_preset_med);
        btn_gfx_preset_high = view.findViewById(R.id.btn_gfx_preset_high);
        btn_gfx_preset_ultra = view.findViewById(R.id.btn_gfx_preset_ultra);

        btn_open_hud_editor = view.findViewById(R.id.btn_open_hud_editor);
        btn_reset_hud_editor = view.findViewById(R.id.btn_reset_hud_editor);

        dropdown_aspect_ratio = view.findViewById(R.id.dropdown_aspect_ratio);
        dropdown_visual_fx = view.findViewById(R.id.dropdown_visual_fx);
        dropdown_shadows = view.findViewById(R.id.dropdown_shadows);
        dropdown_car_reflections = view.findViewById(R.id.dropdown_car_reflections);
        dropdown_traffic_mode = view.findViewById(R.id.dropdown_traffic_mode);
        dropdown_ped_cam_zoom = view.findViewById(R.id.dropdown_ped_cam_zoom);
        dropdown_veh_cam_height = view.findViewById(R.id.dropdown_veh_cam_height);
        dropdown_targeting_mode = view.findViewById(R.id.dropdown_targeting_mode);
        dropdown_touch_steering = view.findViewById(R.id.dropdown_touch_steering);
        dropdown_touch_layout = view.findViewById(R.id.dropdown_touch_layout);
        dropdown_accelerometer = view.findViewById(R.id.dropdown_accelerometer);
        dropdown_radio_station = view.findViewById(R.id.dropdown_radio_station);
        dropdown_voice_mic_mode = view.findViewById(R.id.dropdown_voice_mic_mode);
        dropdown_crosshair_style = view.findViewById(R.id.dropdown_crosshair_style);
        dropdown_font_family = view.findViewById(R.id.dropdown_font_family);
        dropdown_app_language = view.findViewById(R.id.dropdown_app_language);

        voice = view.findViewById(R.id.voice_switch);
        timestamp = view.findViewById(R.id.timestamp_switch);
        fullscreen = view.findViewById(R.id.fullscreen_switch);
        displayfps = view.findViewById(R.id.displayfps_switch);
        systemkeyboard = view.findViewById(R.id.systemkeyboard_switch);
        modify_files = view.findViewById(R.id.modify_switch);
        fastconnect = view.findViewById(R.id.fastconnect_switch);

        headmove_switch = view.findViewById(R.id.headmove_switch);
        dlmodels_switch = view.findViewById(R.id.dlmodels_switch);
        switch_invert_look = view.findViewById(R.id.switch_invert_look);
        switch_auto_climb = view.findViewById(R.id.switch_auto_climb);
        switch_frame_limiter = view.findViewById(R.id.switch_frame_limiter);
        switch_free_cam = view.findViewById(R.id.switch_free_cam);
        switch_first_person_look = view.findViewById(R.id.switch_first_person_look);
        switch_bike_steering = view.findViewById(R.id.switch_bike_steering);
        switch_vibration = view.findViewById(R.id.switch_vibration);
        switch_radio_autotune = view.findViewById(R.id.switch_radio_autotune);

        switch_km_enabled = view.findViewById(R.id.switch_km_enabled);
        switch_joypad_invert_x = view.findViewById(R.id.switch_joypad_invert_x);
        switch_joypad_invert_y = view.findViewById(R.id.switch_joypad_invert_y);

        switch_voice_mic_enable = view.findViewById(R.id.switch_voice_mic_enable);
        switch_voice_speaker_list = view.findViewById(R.id.switch_voice_speaker_list);
        switch_voice_balancer = view.findViewById(R.id.switch_voice_balancer);
        switch_voice_filter = view.findViewById(R.id.switch_voice_filter);

        switch_show_gta_hud = view.findViewById(R.id.switch_show_gta_hud);
        switch_show_radar = view.findViewById(R.id.switch_show_radar);
        switch_radar_tap_map = view.findViewById(R.id.switch_radar_tap_map);
        switch_show_nametag_weapon = view.findViewById(R.id.switch_show_nametag_weapon);
        switch_show_death_window = view.findViewById(R.id.switch_show_death_window);
        switch_show_nametags = view.findViewById(R.id.switch_show_nametags);
        switch_show_button_panel = view.findViewById(R.id.switch_show_button_panel);
        switch_show_screenshot_btn = view.findViewById(R.id.switch_show_screenshot_btn);
        switch_gta_subtitles = view.findViewById(R.id.switch_gta_subtitles);
        switch_chat_shadow = view.findViewById(R.id.switch_chat_shadow);

        restore_button = view.findViewById(R.id.restore_button);
        chat_minus = view.findViewById(R.id.chat_minus);
        chat_strings = view.findViewById(R.id.chat_strings);
        chat_plus = view.findViewById(R.id.chat_plus);
        fps_limit = view.findViewById(R.id.fps_limit);

        font_size_slider = view.findViewById(R.id.fontsize_slider);
        font_size_value = view.findViewById(R.id.fontsize_value);
        chatposx_slider = view.findViewById(R.id.chatposx_slider);
        chatposx_value = view.findViewById(R.id.chatposx_value);
        chatposy_slider = view.findViewById(R.id.chatposy_slider);
        chatposy_value = view.findViewById(R.id.chatposy_value);
        chatsizex_slider = view.findViewById(R.id.chatsizex_slider);
        chatsizex_value = view.findViewById(R.id.chatsizex_value);
        chatsizey_slider = view.findViewById(R.id.chatsizey_slider);
        chatsizey_value = view.findViewById(R.id.chatsizey_value);

        slider_resolution = view.findViewById(R.id.slider_resolution);
        tv_resolution_val = view.findViewById(R.id.tv_resolution_val);
        slider_draw_distance = view.findViewById(R.id.slider_draw_distance);
        tv_draw_distance_val = view.findViewById(R.id.tv_draw_distance_val);
        slider_brightness = view.findViewById(R.id.slider_brightness);
        tv_brightness_val = view.findViewById(R.id.tv_brightness_val);

        slider_cam_sens_x = view.findViewById(R.id.slider_cam_sens_x);
        tv_cam_sens_x_val = view.findViewById(R.id.tv_cam_sens_x_val);
        slider_cam_sens_y = view.findViewById(R.id.slider_cam_sens_y);
        tv_cam_sens_y_val = view.findViewById(R.id.tv_cam_sens_y_val);
        slider_cam_sens_foot = view.findViewById(R.id.slider_cam_sens_foot);
        tv_cam_sens_foot_val = view.findViewById(R.id.tv_cam_sens_foot_val);
        slider_cam_sens_veh = view.findViewById(R.id.slider_cam_sens_veh);
        tv_cam_sens_veh_val = view.findViewById(R.id.tv_cam_sens_veh_val);
        slider_cam_sens_aim = view.findViewById(R.id.slider_cam_sens_aim);
        tv_cam_sens_aim_val = view.findViewById(R.id.tv_cam_sens_aim_val);

        slider_joystick_size = view.findViewById(R.id.slider_joystick_size);
        tv_joystick_size_val = view.findViewById(R.id.tv_joystick_size_val);
        slider_shoot_threshold = view.findViewById(R.id.slider_shoot_threshold);
        tv_shoot_threshold_val = view.findViewById(R.id.tv_shoot_threshold_val);
        slider_analog_sens = view.findViewById(R.id.slider_analog_sens);
        tv_analog_sens_val = view.findViewById(R.id.tv_analog_sens_val);
        slider_flick_sens = view.findViewById(R.id.slider_flick_sens);
        tv_flick_sens_val = view.findViewById(R.id.tv_flick_sens_val);

        slider_sfx_volume = view.findViewById(R.id.slider_sfx_volume);
        tv_sfx_volume_val = view.findViewById(R.id.tv_sfx_volume_val);
        slider_music_volume = view.findViewById(R.id.slider_music_volume);
        tv_music_volume_val = view.findViewById(R.id.tv_music_volume_val);
        slider_voice_volume = view.findViewById(R.id.slider_voice_volume);
        tv_voice_volume_val = view.findViewById(R.id.tv_voice_volume_val);
        slider_voice_mic_volume = view.findViewById(R.id.slider_voice_mic_volume);
        tv_voice_mic_volume_val = view.findViewById(R.id.tv_voice_mic_volume_val);
        slider_voice_btn_size = view.findViewById(R.id.slider_voice_btn_size);
        tv_voice_btn_size_val = view.findViewById(R.id.tv_voice_btn_size_val);

        slider_nametag_size = view.findViewById(R.id.slider_nametag_size);
        tv_nametag_size_val = view.findViewById(R.id.tv_nametag_size_val);
        slider_textlabel_size = view.findViewById(R.id.slider_textlabel_size);
        tv_textlabel_size_val = view.findViewById(R.id.tv_textlabel_size_val);

        // 截图库
        btn_ss_mode_hud = view.findViewById(R.id.btn_ss_mode_hud);
        btn_ss_mode_game = view.findViewById(R.id.btn_ss_mode_game);
        screenshotController.bindViews(view);

        // 问题反馈
        diagnosticsController.bindViews(view);

        // 帮助与关于
        tv_help_mods_body = view.findViewById(R.id.tv_help_mods_body);
        tv_help_faq_body = view.findViewById(R.id.tv_help_faq_body);
        tv_help_commands = view.findViewById(R.id.tv_help_commands);
        tv_about_version_line = view.findViewById(R.id.tv_about_version_line);
        tv_about_summary = view.findViewById(R.id.tv_about_summary);

        // 文件验证与游戏资源选项卡
        gameDataController.bindViews(view);

        // 0.3.DL 自定义模型缓存管理
        artworkCacheController.bindViews(view);
    }

    private void setupCategoryNavigation(View view) {
        categoryPanels = new View[]{
                view.findViewById(R.id.panel_multiplayer),
                view.findViewById(R.id.panel_gamedata),
                view.findViewById(R.id.panel_graphics),
                view.findViewById(R.id.panel_camera),
                view.findViewById(R.id.panel_controls),
                view.findViewById(R.id.panel_audio),
                view.findViewById(R.id.panel_language),
                view.findViewById(R.id.panel_hud_chat),
                view.findViewById(R.id.panel_screenshots),
                view.findViewById(R.id.panel_logs),
                view.findViewById(R.id.panel_help_about)
        };

        settingsTabs = view.findViewById(R.id.settings_tabs);
        String[] titles = new String[CATEGORY_TITLE_RES.length];
        for (int i = 0; i < titles.length; i++) {
            titles[i] = i == CAT_LANGUAGE ? "语言 / Language" : getString(CATEGORY_TITLE_RES[i]);
        }
        settingsTabs.setTabs(titles);
        settingsTabs.setOnTabSelectedListener((index, reselected) -> {
            if (reselected) {
                if (categoryPanels[index] instanceof android.widget.ScrollView) {
                    ((android.widget.ScrollView) categoryPanels[index]).smoothScrollTo(0, 0);
                }
                return;
            }
            switchCategory(index);
            View panel = categoryPanels[index];
            panel.animate().cancel();
            panel.setAlpha(0f);
            panel.setTranslationY(8f * getResources().getDisplayMetrics().density);
            panel.animate().alpha(1f).translationY(0f).setDuration(180).start();
        });
        switchCategory(sLastCategory);
    }

    private void switchCategory(int catIdx) {
        currentCategory = catIdx;
        sLastCategory = catIdx;

        for (int i = 0; i < categoryPanels.length; i++) {
            categoryPanels[i].setVisibility((i == catIdx) ? View.VISIBLE : View.GONE);
        }

        if (catIdx == CAT_MULTIPLAYER) {
            artworkCacheController.refresh();
        } else if (catIdx == CAT_GAMEDATA) {
            gameDataController.refreshVerification();
        } else if (catIdx == CAT_SCREENSHOTS) {
            screenshotController.refresh();
        } else if (catIdx == CAT_LOGS) {
            diagnosticsController.refresh();
        } else if (catIdx == CAT_HELP_ABOUT) {
            populateHelpAndAbout();
        }
    }

    private void setupListeners(View view) {
        et_settings_nickname.setText(settings.getString("nick_name"));
        et_settings_nickname.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {}

            @Override
            public void afterTextChanged(Editable s) {
                if (updatingWidgets) return;
                String nick = s.toString().trim();
                if (!nick.isEmpty()) {
                    savePreference("nick_name", nick);
                }
            }
        });

        // 启动器主题配色预设与自定义滑条
        int[][] themePresets = {
                {R.id.btn_theme_gold, 38},
                {R.id.btn_theme_cyan, 186},
                {R.id.btn_theme_green, 145},
                {R.id.btn_theme_red, 355},
                {R.id.btn_theme_purple, 271},
                {R.id.btn_theme_blue, 216}
        };
        for (int[] preset : themePresets) {
            View btn = view.findViewById(preset[0]);
            final int hue = preset[1];
            if (btn != null) {
                btn.setOnClickListener(v -> {
                    if (slider_theme_hue != null) slider_theme_hue.setValue(hue);
                    if (tv_theme_hue_val != null) tv_theme_hue_val.setText(hue + "°");
                    LauncherThemeManager.setHue(requireContext(), hue);
                });
            }
        }
        if (slider_theme_hue != null) {
            int savedHue = LauncherThemeManager.getHue(requireContext());
            slider_theme_hue.setValue(savedHue);
            if (tv_theme_hue_val != null) tv_theme_hue_val.setText(savedHue + "°");
            slider_theme_hue.addOnChangeListener((s, value, fromUser) -> {
                int hue = Math.round(value);
                if (tv_theme_hue_val != null) tv_theme_hue_val.setText(hue + "°");
                if (fromUser) {
                    LauncherThemeManager.setHue(requireContext(), hue);
                }
            });
        }

        bindStringOptionDropdown(samp_version, ClientSettings.SAMP_VERSIONS, "samp_version");
        bindStringOptionDropdown(charset, ClientSettings.CHARSETS, "charset");
        bindIntOptionDropdown(fps_limit, ClientSettings.FPS_LIMITS, "fps_limit", false);

        if (game_version_layout != null) {
            game_version_layout.setVisibility(View.GONE);
        }

        bindStringOptionDropdown(dropdown_aspect_ratio, ClientSettings.ASPECT_RATIOS, "aspect_ratio");
        bindIntOptionDropdown(dropdown_visual_fx, ClientSettings.VISUAL_FX_OPTIONS, "gta_visual_fx", true);
        bindIntOptionDropdown(dropdown_shadows, ClientSettings.SHADOWS_OPTIONS, "gta_shadows", true);
        bindIntOptionDropdown(dropdown_car_reflections, ClientSettings.CAR_REFL_OPTIONS, "gta_car_reflections", true);
        bindIntOptionDropdown(dropdown_traffic_mode, ClientSettings.TRAFFIC_OPTIONS, "gta_traffic_mode", false);
        bindIntOptionDropdown(dropdown_veh_cam_height, ClientSettings.VEH_CAM_OPTIONS, "veh_cam_height", false);
        bindIntOptionDropdown(dropdown_ped_cam_zoom, ClientSettings.PED_ZOOM_OPTIONS, "ped_cam_zoom", false);
        bindIntOptionDropdown(dropdown_targeting_mode, ClientSettings.TARGETING_OPTIONS, "targeting_mode", false);
        bindIntOptionDropdown(dropdown_touch_steering, ClientSettings.STEERING_OPTIONS, "touch_steering", false);
        bindIntOptionDropdown(dropdown_touch_layout, ClientSettings.LAYOUT_OPTIONS, "touch_layout", false);
        bindIntOptionDropdown(dropdown_accelerometer, ClientSettings.ACCEL_OPTIONS, "accelerometer", false);
        bindIntOptionDropdown(dropdown_radio_station, ClientSettings.RADIO_STATION_OPTIONS, "radio_station", false);
        bindStringOptionDropdown(dropdown_voice_mic_mode, ClientSettings.VOICE_MIC_MODES, "voice_mic_mode");
        bindStringOptionDropdown(dropdown_crosshair_style, ClientSettings.CROSSHAIR_STYLES, "crosshair_style");
        bindStringOptionDropdown(dropdown_font_family, ClientSettings.FONT_FAMILIES, "font_family");
        setupLanguageDropdown();

        // 四档画质预设
        btn_gfx_preset_low.setOnClickListener(v -> applyGraphicsPreset(0, 0, 0, 60, 40, I18n.tr("省电流畅")));
        btn_gfx_preset_med.setOnClickListener(v -> applyGraphicsPreset(1, 1, 1, 80, 60, I18n.tr("均衡标准")));
        btn_gfx_preset_high.setOnClickListener(v -> applyGraphicsPreset(2, 1, 2, 100, 75, I18n.tr("高清画质")));
        btn_gfx_preset_ultra.setOnClickListener(v -> applyGraphicsPreset(3, 2, 3, 100, 100, I18n.tr("极致画质")));

        // 自定义游戏按键位置 (Change Button Position)
        if (btn_open_hud_editor != null) {
            btn_open_hud_editor.setOnClickListener(v -> {
                boolean next = !settings.getBoolean("open_hud_edit_on_start");
                savePreference("open_hud_edit_on_start", next);
                updateHudEditorButtonUI();
                if (next) {
                    Toast.makeText(requireContext(), I18n.tr("已开启：下次进入游戏将自动打开按键位置调整界面（游戏内也可随时点击底部 HUD 键或输入 /hudedit 打开）"), Toast.LENGTH_LONG).show();
                } else {
                    Toast.makeText(requireContext(), I18n.tr("已取消进服自动打开按键位置调整"), Toast.LENGTH_SHORT).show();
                }
            });
        }
        if (btn_reset_hud_editor != null) {
            btn_reset_hud_editor.setOnClickListener(v -> {
                showAlertDialog(I18n.tr("确定要删除自定义按键布局文件 (Adjustable.cfg)，恢复 GTA:SA 原版默认按键位置吗？"), (d, w) -> {
                    File adjFile = new File(requireContext().getExternalFilesDir(null), "Adjustable.cfg");
                    if (adjFile.exists()) {
                        adjFile.delete();
                    }
                    savePreference("open_hud_edit_on_start", false);
                    updateHudEditorButtonUI();
                    Toast.makeText(requireContext(), I18n.tr("已恢复默认游戏按键位置"), Toast.LENGTH_SHORT).show();
                    d.dismiss();
                }, (d, w) -> d.dismiss());
            });
        }

        // 重置聊天框布局
        view.findViewById(R.id.btn_reset_chat_layout).setOnClickListener(v -> {
            Map<String, Object> batch = new HashMap<>();
            batch.put("font_size", 25.0f);
            batch.put("chat_strings", 8);
            batch.put("chat_posx", 100);
            batch.put("chat_posy", 20);
            batch.put("chat_sizex", 360);
            batch.put("chat_sizey", 110);
            ClientSettings.updateBatchSync(requireContext(), batch);
            settings = ClientSettings.loadLauncher(requireContext());
            setWidgetsForSettings();
            Toast.makeText(requireContext(), I18n.tr("聊天窗口布局与字号已恢复默认"), Toast.LENGTH_SHORT).show();
        });

        // 截图模式切换
        btn_ss_mode_hud.setOnClickListener(v -> {
            savePreference("screenshot_mode", ClientSettings.SCREENSHOT_MODE_HUD);
            updateScreenshotModeUI();
            Toast.makeText(requireContext(), I18n.tr("已设为：含界面与聊天"), Toast.LENGTH_SHORT).show();
        });
        btn_ss_mode_game.setOnClickListener(v -> {
            savePreference("screenshot_mode", ClientSettings.SCREENSHOT_MODE_GAME);
            updateScreenshotModeUI();
            Toast.makeText(requireContext(), I18n.tr("已设为：仅游戏画面"), Toast.LENGTH_SHORT).show();
        });
        view.findViewById(R.id.btn_ss_refresh).setOnClickListener(v -> screenshotController.refresh());
        view.findViewById(R.id.btn_ss_clear_all).setOnClickListener(v -> screenshotController.confirmClear());

        // 问题反馈操作按钮
        view.findViewById(R.id.btn_log_refresh).setOnClickListener(v -> {
            diagnosticsController.refresh();
            Toast.makeText(requireContext(), I18n.tr("运行状态检测已刷新"), Toast.LENGTH_SHORT).show();
        });
        view.findViewById(R.id.btn_log_copy_report).setOnClickListener(v -> diagnosticsController.copyReport());
        view.findViewById(R.id.btn_log_share).setOnClickListener(v -> diagnosticsController.shareLogFile());
        view.findViewById(R.id.btn_log_clear).setOnClickListener(v -> diagnosticsController.confirmClearLogs());

        setupSwitchListeners();
        setupRestoreButton();
        setupChatButtons();
        setupSliders();
        updateGraphicsPresetUI();
        updateScreenshotModeUI();
        updateHudEditorButtonUI();
    }

    private void setupLanguageDropdown() {
        if (dropdown_app_language == null) return;
        final String[] labels = {"中文", "English", "Português"};
        final String[] ids = {LocaleManager.LANG_ZH, LocaleManager.LANG_EN, LocaleManager.LANG_PT};
        dropdown_app_language.setAdapter(createDropdownAdapter(labels));
        dropdown_app_language.setThreshold(0);
        String current = LocaleManager.currentLanguage(requireContext());
        int currentIndex = 1;
        for (int i = 0; i < ids.length; i++) {
            if (ids[i].equals(current)) {
                currentIndex = i;
                break;
            }
        }
        dropdown_app_language.setText(labels[currentIndex], false);
        dropdown_app_language.setOnClickListener(v -> dropdown_app_language.showDropDown());
        dropdown_app_language.setOnItemClickListener((av, v, i, l) -> {
            if (i < 0 || i >= ids.length || ids[i].equals(current)) return;
            LocaleManager.setLanguage(requireContext(), ids[i]);
            requireActivity().recreate();
        });
    }

    ArrayAdapter<String> createDropdownAdapter(final String[] items) {
        return new ArrayAdapter<String>(requireContext(), R.layout.layout_setting_version, new ArrayList<>(Arrays.asList(items))) {
            private final Filter noOpFilter = new Filter() {
                @Override
                protected FilterResults performFiltering(CharSequence constraint) {
                    FilterResults results = new FilterResults();
                    results.values = Arrays.asList(items);
                    results.count = items.length;
                    return results;
                }

                @Override
                protected void publishResults(CharSequence constraint, FilterResults results) {
                    clear();
                    addAll(items);
                    notifyDataSetChanged();
                }
            };

            @NonNull
            @Override
            public Filter getFilter() {
                return noOpFilter;
            }
        };
    }

    private void bindStringOptionDropdown(GtaOptionView view, ClientSettings.StringOption[] options, String key) {
        if (view == null) return;
        ArrayAdapter<String> adapter = createDropdownAdapter(ClientSettings.labelsOf(options));
        view.setAdapter(adapter);
        view.setThreshold(0);
        view.setText(ClientSettings.labelForStringOption(options, settings.getString(key)), false);
        view.setOnClickListener(v -> view.showDropDown());
        view.setOnItemClickListener((av, v, i, l) -> {
            if (i >= 0 && i < options.length) {
                savePreference(key, options[i].id);
            }
        });
    }

    private void bindIntOptionDropdown(GtaOptionView view, ClientSettings.IntOption[] options, String key, boolean syncPreset) {
        if (view == null) return;
        ArrayAdapter<String> adapter = createDropdownAdapter(ClientSettings.labelsOf(options));
        view.setAdapter(adapter);
        view.setThreshold(0);
        view.setText(ClientSettings.labelForIntOption(options, settings.getInt(key)), false);
        view.setOnClickListener(v -> view.showDropDown());
        view.setOnItemClickListener((av, v, i, l) -> {
            if (i >= 0 && i < options.length) {
                savePreference(key, options[i].value);
                if (syncPreset) {
                    updateGraphicsPresetUI();
                }
            }
        });
    }

    private void applyGraphicsPreset(int vfx, int shadows, int carRefl, int res, int dd, String label) {
        Map<String, Object> batch = new HashMap<>();
        batch.put("gta_visual_fx", vfx);
        batch.put("gta_shadows", shadows);
        batch.put("gta_car_reflections", carRefl);
        batch.put("gta_resolution", res);
        batch.put("gta_draw_distance", dd);
        ClientSettings.updateBatchSync(requireContext(), batch);
        settings = ClientSettings.loadLauncher(requireContext());
        setWidgetsForSettings();
        Toast.makeText(requireContext(), I18n.tr("已应用画质预设：") + label, Toast.LENGTH_SHORT).show();
    }

    private void updateGraphicsPresetUI() {
        if (btn_gfx_preset_low == null || !isAdded()) return;
        int vfx = settings.getInt("gta_visual_fx");
        int shd = settings.getInt("gta_shadows");
        int refl = settings.getInt("gta_car_reflections");
        int res = settings.getInt("gta_resolution");
        int dd = settings.getInt("gta_draw_distance");

        int activePreset = -1;
        if (vfx == 0 && shd == 0 && refl == 0 && res == 60 && dd == 40) activePreset = 0;
        else if (vfx == 1 && shd == 1 && refl == 1 && res == 80 && dd == 60) activePreset = 1;
        else if (vfx == 2 && shd == 1 && refl == 2 && res == 100 && dd == 75) activePreset = 2;
        else if (vfx == 3 && shd == 2 && refl == 3 && res == 100 && dd == 100) activePreset = 3;

        TextView[] btns = {btn_gfx_preset_low, btn_gfx_preset_med, btn_gfx_preset_high, btn_gfx_preset_ultra};
        int accentColor = LauncherThemeManager.getAccentColor(requireContext());
        int colorOnAccent = requireContext().getResources().getColor(R.color.launcher_on_accent);
        int colorText = requireContext().getResources().getColor(R.color.launcher_text);

        for (int i = 0; i < btns.length; i++) {
            boolean selected = (i == activePreset);
            btns[i].setBackgroundResource(selected ? R.drawable.launcher_btn_primary : R.drawable.launcher_btn_outline);
            btns[i].setBackgroundTintList(selected ? ColorStateList.valueOf(accentColor) : null);
            btns[i].setTextColor(selected ? colorOnAccent : colorText);
        }
    }

    private void updateHudEditorButtonUI() {
        if (btn_open_hud_editor == null || !isAdded()) return;
        boolean scheduled = settings.getBoolean("open_hud_edit_on_start");
        int accentColor = LauncherThemeManager.getAccentColor(requireContext());
        btn_open_hud_editor.setBackgroundTintList(ColorStateList.valueOf(accentColor));
        btn_open_hud_editor.setText(scheduled
                ? I18n.tr("✓ 已预约：下次进入游戏将自动打开按键调整")
                : I18n.tr("进入游戏时自动打开按键位置调整"));
    }

    private void setupSliders() {
        bindSlider(font_size_slider, font_size_value, "font_size", true, "", false);
        bindSlider(chatposx_slider, chatposx_value, "chat_posx", false, "", false);
        bindSlider(chatposy_slider, chatposy_value, "chat_posy", false, "", false);
        bindSlider(chatsizex_slider, chatsizex_value, "chat_sizex", false, "", false);
        bindSlider(chatsizey_slider, chatsizey_value, "chat_sizey", false, "", false);

        bindSlider(slider_resolution, tv_resolution_val, "gta_resolution", false, "%", true);
        bindSlider(slider_draw_distance, tv_draw_distance_val, "gta_draw_distance", false, "%", true);
        bindSlider(slider_brightness, tv_brightness_val, "gta_brightness", false, "%", false);

        bindSlider(slider_cam_sens_x, tv_cam_sens_x_val, "cam_sens_x", false, "%", false);
        bindSlider(slider_cam_sens_y, tv_cam_sens_y_val, "cam_sens_y", false, "%", false);
        bindSlider(slider_cam_sens_foot, tv_cam_sens_foot_val, "cam_sens_foot", false, "%", false);
        bindSlider(slider_cam_sens_veh, tv_cam_sens_veh_val, "cam_sens_veh", false, "%", false);
        bindSlider(slider_cam_sens_aim, tv_cam_sens_aim_val, "cam_sens_aim", false, "%", false);

        bindSlider(slider_joystick_size, tv_joystick_size_val, "joystick_size", false, "%", false);
        bindSlider(slider_shoot_threshold, tv_shoot_threshold_val, "shoot_threshold", false, "%", false);
        bindSlider(slider_analog_sens, tv_analog_sens_val, "analog_sensitivity", false, "%", false);
        bindSlider(slider_flick_sens, tv_flick_sens_val, "flick_sensitivity", false, "%", false);

        bindSlider(slider_sfx_volume, tv_sfx_volume_val, "sfx_volume", false, "%", false);
        bindSlider(slider_music_volume, tv_music_volume_val, "music_volume", false, "%", false);
        bindSlider(slider_voice_volume, tv_voice_volume_val, "voice_volume", false, "%", false);
        bindSlider(slider_voice_mic_volume, tv_voice_mic_volume_val, "voice_mic_volume", false, "%", false);
        bindSlider(slider_voice_btn_size, tv_voice_btn_size_val, "voice_btn_size", false, "%", false);

        bindSlider(slider_nametag_size, tv_nametag_size_val, "nametag_size", false, "%", false);
        bindSlider(slider_textlabel_size, tv_textlabel_size_val, "textlabel_size", false, "%", false);
    }

    private void bindSlider(GtaSaSliderView slider, TextView valueView, String key,
                            boolean asFloat, String suffix, boolean syncPreset) {
        if (slider == null || valueView == null) return;
        slider.clearOnChangeListeners();
        slider.addOnChangeListener((s, value, fromUser) -> {
            valueView.setText(Math.round(value) + suffix);
            if (fromUser && !updatingWidgets) {
                if (asFloat) savePreference(key, value);
                else savePreference(key, Math.round(value));
                if (syncPreset) {
                    updateGraphicsPresetUI();
                }
            }
        });
        syncSlider(slider, valueView, key, asFloat, suffix);
    }

    private void syncSlider(GtaSaSliderView slider, TextView valueView, String key, boolean asFloat, String suffix) {
        if (slider == null || valueView == null) return;
        float current = asFloat ? settings.getFloat(key) : settings.getInt(key);
        float clamped = Math.min(Math.max(current, slider.getValueFrom()), slider.getValueTo());
        float step = slider.getStepSize();
        if (step > 0) {
            clamped = slider.getValueFrom() + Math.round((clamped - slider.getValueFrom()) / step) * step;
            clamped = Math.min(Math.max(clamped, slider.getValueFrom()), slider.getValueTo());
        }
        slider.setValue(clamped);
        valueView.setText(Math.round(clamped) + suffix);
    }

    private Map<CompoundButton, String> buildSwitchMap() {
        Map<CompoundButton, String> map = new HashMap<>();
        map.put(systemkeyboard, "system_keyboard");
        map.put(timestamp, "timestamp");
        map.put(fullscreen, "fullscreen");
        map.put(displayfps, "display_fps");
        map.put(voice, "voice_chat");
        map.put(fastconnect, "fast_connect");
        map.put(headmove_switch, "head_move");
        map.put(dlmodels_switch, "dl_custom_models");
        map.put(switch_invert_look, "invert_look");
        map.put(switch_auto_climb, "auto_climb");
        map.put(switch_frame_limiter, "frame_limiter");
        map.put(switch_free_cam, "free_cam");
        map.put(switch_first_person_look, "first_person_look");
        map.put(switch_bike_steering, "bike_steering");
        map.put(switch_vibration, "vibration");
        map.put(switch_radio_autotune, "radio_autotune");
        map.put(switch_km_enabled, "km_enabled");
        map.put(switch_joypad_invert_x, "joypad_invert_x");
        map.put(switch_joypad_invert_y, "joypad_invert_y");
        map.put(switch_voice_mic_enable, "voice_mic_enable");
        map.put(switch_voice_speaker_list, "voice_speaker_list");
        map.put(switch_voice_balancer, "voice_balancer");
        map.put(switch_voice_filter, "voice_filter");
        map.put(switch_show_gta_hud, "show_gta_hud");
        map.put(switch_show_radar, "show_radar");
        map.put(switch_radar_tap_map, "radar_tap_map");
        map.put(switch_show_nametag_weapon, "show_nametag_weapon");
        map.put(switch_show_death_window, "show_death_window");
        map.put(switch_show_nametags, "show_nametags");
        map.put(switch_show_button_panel, "show_button_panel");
        map.put(switch_show_screenshot_btn, "show_screenshot_btn");
        map.put(switch_gta_subtitles, "gta_subtitles");
        map.put(switch_chat_shadow, "chat_shadow");
        return map;
    }

    private void setupSwitchListeners() {
        for (Map.Entry<CompoundButton, String> entry : buildSwitchMap().entrySet()) {
            setupSwitch(entry.getKey(), entry.getValue());
        }
        setupSwitchWithWarning(modify_files);
    }

    private void setupSwitch(CompoundButton switchButton, String key) {
        if (switchButton == null) return;
        switchButton.setOnCheckedChangeListener(null);
        switchButton.setChecked(settings.getBoolean(key));
        switchButton.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (!updatingWidgets) {
                savePreference(key, isChecked);
            }
        });
    }

    private void setupSwitchWithWarning(CompoundButton switchButton) {
        if (switchButton == null) return;
        switchButton.setOnCheckedChangeListener(null);
        switchButton.setChecked(settings.getBoolean("modify_files"));

        switchButton.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (updatingWidgets) return;
            if (isChecked && buttonView.isPressed()) {
                showAlertDialog(I18n.tr("开启此选项后，启动器将跳过原始文件完整性检查。若因手工替换数据包导致游戏异常，请关闭此选项。"),
                        (dialog, which) -> dialog.dismiss(), (dialog, which) -> {
                            switchButton.setChecked(false);
                            dialog.dismiss();
                        });
            }
            savePreference("modify_files", isChecked);
        });
    }

    private void setupRestoreButton() {
        restore_button.setOnClickListener(v -> showAlertDialog(I18n.tr("确定要将所有联机、画面、镜头、操作、音频与界面设置恢复为默认值吗？"), (dialog, which) -> {
            settings = ClientSettings.resetDefaults(requireContext());
            LauncherThemeManager.setHue(requireContext(), LauncherThemeManager.DEFAULT_HUE);
            setWidgetsForSettings();
            Toast.makeText(requireContext(), I18n.tr("已恢复全部默认设置"), Toast.LENGTH_SHORT).show();
            dialog.dismiss();
        }, (dialog, which) -> dialog.dismiss()));
    }

    private void setupChatButtons() {
        chat_minus.setOnClickListener(v -> adjustChatStrings(-1));
        chat_plus.setOnClickListener(v -> adjustChatStrings(1));
    }

    private void adjustChatStrings(int delta) {
        int currentValue = settings.getInt("chat_strings");
        int newValue = currentValue + delta;
        if (newValue < 5 || newValue > 20) return;

        chat_strings.setText(String.valueOf(newValue));
        savePreference("chat_strings", newValue);
    }

    private void setWidgetsForSettings() {
        updatingWidgets = true;
        try {
            et_settings_nickname.setText(settings.getString("nick_name"));

            for (Map.Entry<CompoundButton, String> entry : buildSwitchMap().entrySet()) {
                if (entry.getKey() != null) {
                    entry.getKey().setChecked(settings.getBoolean(entry.getValue()));
                }
            }
            modify_files.setChecked(settings.getBoolean("modify_files"));
            chat_strings.setText(String.valueOf(settings.getInt("chat_strings")));

            int hue = LauncherThemeManager.getHue(requireContext());
            if (slider_theme_hue != null) slider_theme_hue.setValue(hue);
            if (tv_theme_hue_val != null) tv_theme_hue_val.setText(hue + "°");

            samp_version.setText(ClientSettings.labelForStringOption(ClientSettings.SAMP_VERSIONS, settings.getString("samp_version")), false);
            charset.setText(ClientSettings.labelForStringOption(ClientSettings.CHARSETS, settings.getString("charset")), false);
            fps_limit.setText(ClientSettings.labelForIntOption(ClientSettings.FPS_LIMITS, settings.getInt("fps_limit")), false);

            setStringDropdownText(dropdown_aspect_ratio, ClientSettings.ASPECT_RATIOS, "aspect_ratio");
            setIntDropdownText(dropdown_visual_fx, ClientSettings.VISUAL_FX_OPTIONS, "gta_visual_fx");
            setIntDropdownText(dropdown_shadows, ClientSettings.SHADOWS_OPTIONS, "gta_shadows");
            setIntDropdownText(dropdown_car_reflections, ClientSettings.CAR_REFL_OPTIONS, "gta_car_reflections");
            setIntDropdownText(dropdown_traffic_mode, ClientSettings.TRAFFIC_OPTIONS, "gta_traffic_mode");
            setIntDropdownText(dropdown_ped_cam_zoom, ClientSettings.PED_ZOOM_OPTIONS, "ped_cam_zoom");
            setIntDropdownText(dropdown_veh_cam_height, ClientSettings.VEH_CAM_OPTIONS, "veh_cam_height");
            setIntDropdownText(dropdown_targeting_mode, ClientSettings.TARGETING_OPTIONS, "targeting_mode");
            setIntDropdownText(dropdown_touch_steering, ClientSettings.STEERING_OPTIONS, "touch_steering");
            setIntDropdownText(dropdown_touch_layout, ClientSettings.LAYOUT_OPTIONS, "touch_layout");
            setIntDropdownText(dropdown_accelerometer, ClientSettings.ACCEL_OPTIONS, "accelerometer");
            setIntDropdownText(dropdown_radio_station, ClientSettings.RADIO_STATION_OPTIONS, "radio_station");
            setStringDropdownText(dropdown_voice_mic_mode, ClientSettings.VOICE_MIC_MODES, "voice_mic_mode");
            setStringDropdownText(dropdown_crosshair_style, ClientSettings.CROSSHAIR_STYLES, "crosshair_style");
            setStringDropdownText(dropdown_font_family, ClientSettings.FONT_FAMILIES, "font_family");

            syncSlider(font_size_slider, font_size_value, "font_size", true, "");
            syncSlider(chatposx_slider, chatposx_value, "chat_posx", false, "");
            syncSlider(chatposy_slider, chatposy_value, "chat_posy", false, "");
            syncSlider(chatsizex_slider, chatsizex_value, "chat_sizex", false, "");
            syncSlider(chatsizey_slider, chatsizey_value, "chat_sizey", false, "");

            syncSlider(slider_resolution, tv_resolution_val, "gta_resolution", false, "%");
            syncSlider(slider_draw_distance, tv_draw_distance_val, "gta_draw_distance", false, "%");
            syncSlider(slider_brightness, tv_brightness_val, "gta_brightness", false, "%");

            syncSlider(slider_cam_sens_x, tv_cam_sens_x_val, "cam_sens_x", false, "%");
            syncSlider(slider_cam_sens_y, tv_cam_sens_y_val, "cam_sens_y", false, "%");
            syncSlider(slider_cam_sens_foot, tv_cam_sens_foot_val, "cam_sens_foot", false, "%");
            syncSlider(slider_cam_sens_veh, tv_cam_sens_veh_val, "cam_sens_veh", false, "%");
            syncSlider(slider_cam_sens_aim, tv_cam_sens_aim_val, "cam_sens_aim", false, "%");

            syncSlider(slider_joystick_size, tv_joystick_size_val, "joystick_size", false, "%");
            syncSlider(slider_shoot_threshold, tv_shoot_threshold_val, "shoot_threshold", false, "%");
            syncSlider(slider_analog_sens, tv_analog_sens_val, "analog_sensitivity", false, "%");
            syncSlider(slider_flick_sens, tv_flick_sens_val, "flick_sensitivity", false, "%");

            syncSlider(slider_sfx_volume, tv_sfx_volume_val, "sfx_volume", false, "%");
            syncSlider(slider_music_volume, tv_music_volume_val, "music_volume", false, "%");
            syncSlider(slider_voice_volume, tv_voice_volume_val, "voice_volume", false, "%");
            syncSlider(slider_voice_mic_volume, tv_voice_mic_volume_val, "voice_mic_volume", false, "%");
            syncSlider(slider_voice_btn_size, tv_voice_btn_size_val, "voice_btn_size", false, "%");

            syncSlider(slider_nametag_size, tv_nametag_size_val, "nametag_size", false, "%");
            syncSlider(slider_textlabel_size, tv_textlabel_size_val, "textlabel_size", false, "%");

            updateGraphicsPresetUI();
            updateScreenshotModeUI();
            updateHudEditorButtonUI();
        } finally {
            updatingWidgets = false;
        }
    }

    private void setStringDropdownText(GtaOptionView view, ClientSettings.StringOption[] options, String key) {
        if (view == null) return;
        view.setText(ClientSettings.labelForStringOption(options, settings.getString(key)), false);
    }

    private void setIntDropdownText(GtaOptionView view, ClientSettings.IntOption[] options, String key) {
        if (view == null) return;
        view.setText(ClientSettings.labelForIntOption(options, settings.getInt(key)), false);
    }

    private void updateScreenshotModeUI() {
        if (btn_ss_mode_hud == null || btn_ss_mode_game == null || !isAdded()) return;
        String mode = settings.getString("screenshot_mode");
        int accentColor = LauncherThemeManager.getAccentColor(requireContext());
        int colorOnAccent = requireContext().getResources().getColor(R.color.launcher_on_accent);
        int colorText = requireContext().getResources().getColor(R.color.launcher_text);

        boolean isHud = !ClientSettings.SCREENSHOT_MODE_GAME.equalsIgnoreCase(mode);
        btn_ss_mode_hud.setBackgroundResource(isHud ? R.drawable.launcher_btn_primary : R.drawable.launcher_btn_outline);
        btn_ss_mode_hud.setBackgroundTintList(isHud ? ColorStateList.valueOf(accentColor) : null);
        btn_ss_mode_hud.setTextColor(isHud ? colorOnAccent : colorText);

        btn_ss_mode_game.setBackgroundResource(!isHud ? R.drawable.launcher_btn_primary : R.drawable.launcher_btn_outline);
        btn_ss_mode_game.setBackgroundTintList(!isHud ? ColorStateList.valueOf(accentColor) : null);
        btn_ss_mode_game.setTextColor(!isHud ? colorOnAccent : colorText);
    }

    // ════════════════════════════════════════════════════════════════════════
    //  帮助与关于
    // ════════════════════════════════════════════════════════════════════════

    private void populateHelpAndAbout() {
        tv_help_mods_body.setText(
                I18n.tr("1. 在哪里开启和管理模组？\n")
                + I18n.tr("请返回启动器主界面，点击最左侧导航栏的「模组」板块。您可以在模组板块中统一开启或关闭各类加载器，并单独管理每一个模组包与脚本。\n\n")
                + I18n.tr("2. 如何安装车包、人物皮肤、武器与贴图音效模组？\n")
                + I18n.tr("客户端支持免打包文件替换。只需将下载好的模组文件夹（内含 .dff 模型、.txd 贴图或音效文件）放入游戏数据目录的 modloader/ 文件夹下，随后在主界面「模组」板块开启资源加载器即可自动生效，不会破坏原始数据包。\n\n")
                + I18n.tr("3. 如何使用 Lua 辅助脚本与原生扩展插件？\n")
                + I18n.tr("• Lua 脚本：将 .lua 脚本文件放入 monetloader/ 目录，并在主界面「模组」板块中启用脚本加载器；\n")
                + I18n.tr("• 原生插件与画质补丁：在主界面「模组」板块中可一键开启扩展插件与引擎修复补丁，点击「画质补丁配置」还可按需开关各项渲染优化。\n\n")
                + I18n.tr("4. 安装新模组后进游戏闪退怎么办？\n")
                + I18n.tr("不同模组之间可能存在模型 ID 冲突或旧版脚本不兼容。只需在主界面「模组」板块中暂时关闭最近新增的模组开关，即可立即恢复正常进服；也可在本页「问题反馈」中一键复制故障信息交由开发者排查。"));

        tv_help_faq_body.setText(
                I18n.tr("1. 如何使用自由镜头、调整摇杆大小或自定义游戏按键位置？\n")
                + I18n.tr("• 自由镜头：在「镜头设置」中开启「自由镜头（镜头不自动回正）」，行走或驾车时镜头将始终停留在您旋转到的方向；\n")
                + I18n.tr("• 分段灵敏度：在「镜头设置」中可分别调节步行、载具与开镜瞄准时的独立镜头灵敏度；\n")
                + I18n.tr("• 自定义按键位置：在「操作设置」中点击「进入游戏时自动打开按键位置调整」（或在游戏内展开底部快捷栏点击 HUD 键 / 输入 /hudedit），即可像单机版一样自由拖拽和缩放屏幕按键。\n\n")
                + I18n.tr("2. 步战时如何切换视角远近、开镜瞄准、格挡或乘坐副驾驶？\n")
                + I18n.tr("• 切换视角：点击游戏画面右上角的镜头图标即可在近距、标准、远距视角间切换，也可在「镜头设置」中设定默认距离；\n")
                + I18n.tr("• 瞄准与射击：持枪时点击右侧独立准星按钮进入瞄准状态，轻点开火键单发点射、按住连续射击；\n")
                + I18n.tr("• 近战格挡：徒手或持近战武器时按住格挡键即可保持防御姿态；\n")
                + I18n.tr("• 乘坐副驾驶：靠近载具时点击屏幕右侧出现的 G 键图标，或展开底部功能键盘点击 G。\n\n")
                + I18n.tr("3. 进入服务器后中文显示为乱码怎么解决？\n")
                + I18n.tr("在「联机设置」中修改「服务器文字编码」：国内大多数服务器使用 GBK 编码，部分新版服务器使用 UTF-8，港台服务器常使用 BIG5。\n\n")
                + I18n.tr("4. 如何拍摄不含聊天框和界面按钮的纯净游戏截图？\n")
                + I18n.tr("在「截图库」中将截图范围设为「仅游戏画面」，或在游戏聊天框直接输入 /ss game，即可拍摄隐藏所有界面与按键的纯净游戏画面。"));

        tv_help_commands.setText(
                I18n.tr("/ss 或 /screenshot      立即截图（也可点击底部快捷键盘 F8）\n")
                + I18n.tr("/ss game               强制拍摄一张不含界面与聊天的纯净画面\n")
                + I18n.tr("/hudedit               打开游戏内触控按键位置与大小编辑器\n")
                + I18n.tr("/freecam               实时开关自由镜头（镜头不自动回正）\n")
                + I18n.tr("/headmove              开关角色头部随视角方向转动\n")
                + I18n.tr("/pagesize [5-20]       修改聊天窗口同时显示的消息行数\n")
                + I18n.tr("/timestamp             开关聊天消息前方的接收时间显示\n")
                + I18n.tr("/clearmychat           清空当前聊天窗口的历史消息\n")
                + I18n.tr("/save [备注]           记录当前人物或车辆坐标\n")
                + I18n.tr("/dl                    开关周围载具编号与距离坐标显示\n")
                + I18n.tr("/dw                    开关右侧击杀通报列表\n")
                + I18n.tr("/q 或 /quit            断开服务器连接并退出游戏"));

        tv_about_version_line.setText(String.format(Locale.US,
                I18n.tr("SA-MP Mobile 移动端联机客户端  ·  版本 v%s"),
                BuildConfig.VERSION_NAME));

        tv_about_summary.setText(
                I18n.tr("专为移动端打造的圣安地列斯多人联机启动器与游戏客户端，全面支持 0.3.7 与 0.3.DL 服务器协议，集成原生画质调节、纯净截图库与模块化扩展支持。\n\n")
                + I18n.tr("开源致谢：基于 Alyn SA-MP Mobile 二次开发，致谢 AML / MonetLoader / CLEO 等社区开源项目。"));
    }

    // ════════════════════════════════════════════════════════════════════════
    //  通用工具方法
    // ════════════════════════════════════════════════════════════════════════

    void shareFile(File file, String mimeType, String chooserTitle) {
        if (file == null || !file.exists()) {
            Toast.makeText(requireContext(), I18n.tr("文件不存在"), Toast.LENGTH_SHORT).show();
            return;
        }
        try {
            Uri uri = FileProvider.getUriForFile(requireContext(),
                    requireContext().getPackageName() + ".provider", file);
            Intent intent = new Intent(Intent.ACTION_SEND);
            intent.setType(mimeType);
            intent.putExtra(Intent.EXTRA_STREAM, uri);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(Intent.createChooser(intent, chooserTitle));
        } catch (Exception e) {
            Toast.makeText(requireContext(), I18n.tr("分享失败: ") + e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }

    void copyTextToClipboard(String label, String text) {
        ClipboardManager cm = (ClipboardManager) requireContext().getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) {
            cm.setPrimaryClip(ClipData.newPlainText(label, text));
        }
    }

    private void savePreference(String key, Object value) {
        settings.put(key, value);
        ClientSettings.setAsync(requireContext(), key, value);
    }

    void showAlertDialog(String message, DialogInterface.OnClickListener positiveListener, DialogInterface.OnClickListener negativeListener) {
        new AlertDialog.Builder(requireActivity())
                .setTitle(I18n.tr("提示"))
                .setMessage(message)
                .setPositiveButton(I18n.tr("确定"), positiveListener)
                .setNegativeButton(I18n.tr("取消"), negativeListener)
                .show();
    }
}
