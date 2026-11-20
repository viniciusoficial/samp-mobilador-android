package com.gta.game.input;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.util.Log;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import com.gta.game.R;
import com.rockstargames.oswrapper.GameThread;
import com.rockstargames.oswrapper.view.InputHandler;

import java.util.HashSet;
import java.util.Set;

public final class KeyboardMouseSettings {
    private static final String TAG = "KeyboardMouseSettings";
    public static final int MOVE_FORWARD = 0;
    public static final int MOVE_BACKWARD = 1;
    public static final int MOVE_LEFT = 2;
    public static final int MOVE_RIGHT = 3;
    public static final int SPRINT = 4;
    public static final int JUMP = 5;
    public static final int CROUCH = 6;
    public static final int FIRE = 7;
    public static final int AIM = 8;
    public static final int VEHICLE = 9;
    public static final int CONTROL_BACK = 10;
    public static final int YES = 11;
    public static final int NO = 12;
    public static final int WALK = 13;
    public static final int TAB = 14;
    public static final int ENTER = 15;
    public static final int CHAT = 16;
    public static final int WEAPON_NEXT = 17;
    public static final int WEAPON_PREVIOUS = 18;

    public static final int ACTION_COUNT = 19;
    private static final int KEY_MOUSE_LEFT = -1;
    private static final int KEY_MOUSE_RIGHT = -2;
    private static final int[] DEFAULT_KEYS = {
            KeyEvent.KEYCODE_W, KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_D,
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_C,
            KEY_MOUSE_LEFT, KEY_MOUSE_RIGHT, KeyEvent.KEYCODE_F, KeyEvent.KEYCODE_H,
            KeyEvent.KEYCODE_Y, KeyEvent.KEYCODE_N, KeyEvent.KEYCODE_ALT_LEFT,
            KeyEvent.KEYCODE_TAB, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_T,
            KeyEvent.KEYCODE_E, KeyEvent.KEYCODE_Q
    };
    private static final String[] ACTION_NAMES = {
            "Move forward", "Move backward", "Move left", "Move right", "Sprint",
            "Jump", "Crouch", "Fire", "Aim", "Enter / exit vehicle",
            "H action", "Yes", "No", "Walk", "Scoreboard", "Enter / confirm",
            "Open chat", "Next weapon", "Previous weapon"
    };

    private static final int[] ALTERNATE_KEYS = {
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
            KeyEvent.KEYCODE_PAGE_UP, KeyEvent.KEYCODE_PAGE_DOWN
    };
    private static final int[] ALTERNATE_ACTIONS = {
            MOVE_FORWARD, MOVE_BACKWARD, MOVE_LEFT, MOVE_RIGHT,
            WEAPON_NEXT, WEAPON_PREVIOUS
    };
    private static final int[] bindings = new int[ACTION_COUNT];
    private static final Set<Integer> pressedKeys = new HashSet<>();

    private static SettingsOverlay overlay;
    private static TextView stateView;
    private static Button[] bindingButtons;
    private static int pendingCapture = -1;
    private static boolean enabled = true;
    private static float sensitivityX = 1.0f;
    private static float sensitivityY = 1.0f;
    private static float maxMouseDelta = 120.0f;
    private static float mouseSmoothing;
    private static boolean invertX;
    private static boolean invertY;

    static {
        System.arraycopy(DEFAULT_KEYS, 0, bindings, 0, ACTION_COUNT);
    }

    private KeyboardMouseSettings() {
    }

    public static synchronized void load(Context context) {
        android.content.SharedPreferences prefs =
                context.getSharedPreferences("keyboard_mouse", Context.MODE_PRIVATE);
        for (int action = 0; action < ACTION_COUNT; action++) {
            bindings[action] = prefs.getInt("binding_" + action, DEFAULT_KEYS[action]);
        }
        if (!prefs.contains("bindings_version")) {
            if (bindings[WEAPON_NEXT] == KeyEvent.KEYCODE_PAGE_UP) {
                bindings[WEAPON_NEXT] = KeyEvent.KEYCODE_E;
            }
            if (bindings[WEAPON_PREVIOUS] == KeyEvent.KEYCODE_PAGE_DOWN) {
                bindings[WEAPON_PREVIOUS] = KeyEvent.KEYCODE_Q;
            }
            prefs.edit().putInt("bindings_version", 2).apply();
        }
        enabled = prefs.getBoolean("enabled", true);
        sensitivityX = normalizeSensitivity(prefs.getFloat("sensitivity_x", 1.0f));
        sensitivityY = normalizeSensitivity(prefs.getFloat("sensitivity_y", 1.0f));
        maxMouseDelta = normalizeMaxMouseDelta(prefs.getFloat("mouse_max_delta", 120.0f));
        mouseSmoothing = normalizeMouseSmoothing(prefs.getFloat("mouse_smoothing", 0.0f));
        invertX = prefs.getBoolean("invert_x", false);
        invertY = prefs.getBoolean("invert_y", false);
        applyMouseOptions();
    }

    public static synchronized void save(Context context) {
        android.content.SharedPreferences.Editor editor =
                context.getSharedPreferences("keyboard_mouse", Context.MODE_PRIVATE).edit();
        for (int action = 0; action < ACTION_COUNT; action++) {
            editor.putInt("binding_" + action, bindings[action]);
        }
        editor.putInt("bindings_version", 2);
        editor.putBoolean("enabled", enabled);
        editor.putFloat("sensitivity_x", sensitivityX);
        editor.putFloat("sensitivity_y", sensitivityY);
        editor.putFloat("mouse_max_delta", maxMouseDelta);
        editor.putFloat("mouse_smoothing", mouseSmoothing);
        editor.putBoolean("invert_x", invertX);
        editor.putBoolean("invert_y", invertY);
        editor.apply();
    }

    public static synchronized int actionForKey(int keyCode) {
        for (int action = 0; action < ACTION_COUNT; action++) {
            if (bindings[action] == keyCode) {
                return action;
            }
        }
        for (int i = 0; i < ALTERNATE_KEYS.length; i++) {
            if (ALTERNATE_KEYS[i] == keyCode) {
                return ALTERNATE_ACTIONS[i];
            }
        }
        if (keyCode == KeyEvent.KEYCODE_CTRL_LEFT || keyCode == KeyEvent.KEYCODE_CTRL_RIGHT) {
            return CROUCH;
        }
        return -1;
    }

    public static synchronized int actionForMouseButton(int button) {
        int binding = button == MotionEvent.BUTTON_PRIMARY ? KEY_MOUSE_LEFT
                : button == MotionEvent.BUTTON_SECONDARY ? KEY_MOUSE_RIGHT : 0;
        for (int action = 0; action < ACTION_COUNT; action++) {
            if (bindings[action] == binding && binding != 0) {
                return action;
            }
        }
        return -1;
    }

    public static synchronized float scaleMouseX(float delta) {
        return delta * sensitivityX * (invertX ? -1.0f : 1.0f);
    }

    public static synchronized float scaleMouseY(float delta) {
        return delta * sensitivityY * (invertY ? -1.0f : 1.0f);
    }

    public static synchronized boolean isEnabled() {
        return enabled;
    }

    public static synchronized void setKeyState(int keyCode, boolean down) {
        if (down) {
            pressedKeys.add(keyCode);
        } else {
            pressedKeys.remove(keyCode);
        }
        updateStateView();
    }

    public static synchronized void setMouseState(int button, boolean down) {
        int stateCode = -100 - button;
        if (down) {
            pressedKeys.add(stateCode);
        } else {
            pressedKeys.remove(stateCode);
        }
        updateStateView();
    }

    public static synchronized boolean isCapturingBinding() {
        return pendingCapture >= 0;
    }

    public static synchronized boolean captureKey(KeyEvent event) {
        if (event == null) {
            return false;
        }
        setKeyState(event.getKeyCode(), event.getAction() == KeyEvent.ACTION_DOWN);
        if (pendingCapture < 0) {
            return false;
        }
        if (event.getAction() != KeyEvent.ACTION_DOWN) {
            return true;
        }
        if (event.getKeyCode() == KeyEvent.KEYCODE_ESCAPE) {
            pendingCapture = -1;
            updateBindingButtons();
            return true;
        }
        for (int action = 0; action < ACTION_COUNT; action++) {
            if (action != pendingCapture && bindings[action] == event.getKeyCode()) {
                bindings[action] = KeyEvent.KEYCODE_UNKNOWN;
            }
        }
        bindings[pendingCapture] = event.getKeyCode();
        pendingCapture = -1;
        updateBindingButtons();
        return true;
    }

    public static synchronized boolean close() {
        if (overlay == null) {
            return false;
        }
        overlay.dismiss();
        return true;
    }

    public static synchronized boolean captureMouse(MotionEvent event) {
        if (pendingCapture < 0 ||
                (event.getActionMasked() != MotionEvent.ACTION_BUTTON_PRESS &&
                        event.getActionMasked() != MotionEvent.ACTION_DOWN)) {
            return false;
        }
        int button = event.getActionButton();
        if (button == 0 && event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            button = MotionEvent.BUTTON_PRIMARY;
        }
        if (button != MotionEvent.BUTTON_PRIMARY && button != MotionEvent.BUTTON_SECONDARY) {
            return false;
        }
        int binding = button == MotionEvent.BUTTON_PRIMARY ? KEY_MOUSE_LEFT : KEY_MOUSE_RIGHT;
        for (int action = 0; action < ACTION_COUNT; action++) {
            if (action != pendingCapture && bindings[action] == binding) {
                bindings[action] = KeyEvent.KEYCODE_UNKNOWN;
            }
        }
        bindings[pendingCapture] = binding;
        pendingCapture = -1;
        updateBindingButtons();
        return true;
    }

    public static void show(Activity owner) {
        if (owner.isFinishing() || owner.isDestroyed() || overlay != null) {
            return;
        }
        load(owner);
        InputHandler.INSTANCE.setInterfaceVisible(true);

        int padding = dp(owner, 18);
        SettingsOverlay root = new SettingsOverlay(owner);
        root.setBackgroundColor(0xB8000000);
        root.setClickable(true);
        root.setFocusableInTouchMode(true);

        LinearLayout panel = new LinearLayout(owner);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(padding, padding, padding, padding);
        panel.setBackground(roundedBackground(0xFF171B24, dp(owner, 18)));
        FrameLayout.LayoutParams panelParams = new FrameLayout.LayoutParams(
                Math.min((int) (owner.getResources().getDisplayMetrics().widthPixels * 0.9f),
                        dp(owner, 760)),
                Math.min((int) (owner.getResources().getDisplayMetrics().heightPixels * 0.88f),
                        dp(owner, 680)),
                Gravity.CENTER);
        root.addView(panel, panelParams);

        LinearLayout heading = new LinearLayout(owner);
        heading.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = new TextView(owner);
        title.setText(R.string.keyboard_mouse_settings);
        title.setTextColor(Color.WHITE);
        title.setTextSize(21);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        heading.addView(title, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        Button closeButton = new Button(owner);
        closeButton.setText("Close");
        closeButton.setAllCaps(false);
        closeButton.setOnClickListener(v -> close());
        heading.addView(closeButton);
        panel.addView(heading);

        TextView description = new TextView(owner);
        description.setText("Configure gameplay controls. Changes apply immediately.");
        description.setTextColor(0xFFADB6C5);
        description.setTextSize(13);
        LinearLayout.LayoutParams descriptionParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        descriptionParams.topMargin = dp(owner, 4);
        descriptionParams.bottomMargin = dp(owner, 10);
        panel.addView(description, descriptionParams);

        ScrollView scrollView = new ScrollView(owner);
        scrollView.setFillViewport(false);
        LinearLayout content = new LinearLayout(owner);
        content.setOrientation(LinearLayout.VERTICAL);
        scrollView.addView(content);
        panel.addView(scrollView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        CheckBox enableBox = new CheckBox(owner);
        enableBox.setTextColor(Color.WHITE);
        enableBox.setText("Enable keyboard and mouse");
        enableBox.setChecked(enabled);
        content.addView(enableBox, verticalMargins(owner, 0, 4));
        enableBox.setOnCheckedChangeListener((button, checked) -> {
            synchronized (KeyboardMouseSettings.class) {
                enabled = checked;
                GameThread.INSTANCE.onPcInputEnabled(checked);
                InputHandler.INSTANCE.onPcInputEnabledChanged();
            }
        });

        addSectionHeader(owner, content, "KEYBOARD");
        bindingButtons = new Button[ACTION_COUNT];
        addBindingRows(owner, content, new int[] {
                MOVE_FORWARD, MOVE_BACKWARD, MOVE_LEFT, MOVE_RIGHT, SPRINT, JUMP, CROUCH,
                VEHICLE, CONTROL_BACK, YES, NO, WALK, TAB, ENTER, CHAT,
                WEAPON_NEXT, WEAPON_PREVIOUS
        });

        addSectionHeader(owner, content, "MOUSE");
        addBindingRows(owner, content, new int[] { FIRE, AIM });

        addSectionHeader(owner, content, "SENSITIVITY");
        TextView xLabel = new TextView(owner);
        TextView yLabel = new TextView(owner);
        xLabel.setTextColor(0xFFE7EAF0);
        yLabel.setTextColor(0xFFE7EAF0);
        content.addView(xLabel);
        SeekBar xSensitivity = new SeekBar(owner);
        xSensitivity.setMax(290);
        xSensitivity.setProgress(sensitivityToProgress(sensitivityX));
        content.addView(xSensitivity, verticalMargins(owner, 0, 5));
        content.addView(yLabel);
        SeekBar ySensitivity = new SeekBar(owner);
        ySensitivity.setMax(290);
        ySensitivity.setProgress(sensitivityToProgress(sensitivityY));
        content.addView(ySensitivity, verticalMargins(owner, 0, 5));
        xSensitivity.setOnSeekBarChangeListener(sensitivityListener(true, xLabel));
        ySensitivity.setOnSeekBarChangeListener(sensitivityListener(false, yLabel));

        CheckBox invertXBox = new CheckBox(owner);
        invertXBox.setTextColor(Color.WHITE);
        invertXBox.setText("Invert mouse X");
        invertXBox.setChecked(invertX);
        content.addView(invertXBox);
        invertXBox.setOnCheckedChangeListener((button, checked) -> invertX = checked);
        CheckBox invertYBox = new CheckBox(owner);
        invertYBox.setTextColor(Color.WHITE);
        invertYBox.setText("Invert mouse Y");
        invertYBox.setChecked(invertY);
        content.addView(invertYBox);
        invertYBox.setOnCheckedChangeListener((button, checked) -> invertY = checked);

        addSectionHeader(owner, content, "MOUSE RESPONSE");
        TextView maxDeltaLabel = new TextView(owner);
        maxDeltaLabel.setTextColor(0xFFE7EAF0);
        content.addView(maxDeltaLabel);
        SeekBar maxDeltaSeekBar = new SeekBar(owner);
        maxDeltaSeekBar.setMax(499);
        maxDeltaSeekBar.setProgress(maxDeltaToProgress(maxMouseDelta));
        content.addView(maxDeltaSeekBar, verticalMargins(owner, 0, 5));
        maxDeltaSeekBar.setOnSeekBarChangeListener(mouseOptionListener(true, maxDeltaLabel));

        TextView smoothingLabel = new TextView(owner);
        smoothingLabel.setTextColor(0xFFE7EAF0);
        content.addView(smoothingLabel);
        SeekBar smoothingSeekBar = new SeekBar(owner);
        smoothingSeekBar.setMax(90);
        smoothingSeekBar.setProgress(Math.round(mouseSmoothing * 100.0f));
        content.addView(smoothingSeekBar, verticalMargins(owner, 0, 5));
        smoothingSeekBar.setOnSeekBarChangeListener(mouseOptionListener(false, smoothingLabel));

        TextView receivedState = new TextView(owner);
        receivedState.setTextColor(0xFFADB6C5);
        receivedState.setText("Received input: none");
        content.addView(receivedState);
        stateView = receivedState;

        updateBindingButtons();
        updateSensitivityLabel(xLabel, sensitivityX, "Mouse X sensitivity");
        updateSensitivityLabel(yLabel, sensitivityY, "Mouse Y sensitivity");
        updateMouseOptionLabel(maxDeltaLabel, maxMouseDelta, true);
        updateMouseOptionLabel(smoothingLabel, mouseSmoothing, false);

        LinearLayout actions = new LinearLayout(owner);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        Button resetButton = makeActionButton(owner, "Restore defaults");
        resetButton.setOnClickListener(v -> {
            synchronized (KeyboardMouseSettings.class) {
                System.arraycopy(DEFAULT_KEYS, 0, bindings, 0, ACTION_COUNT);
                enabled = true;
                sensitivityX = 1.0f;
                sensitivityY = 1.0f;
                maxMouseDelta = 120.0f;
                mouseSmoothing = 0.0f;
                invertX = false;
                invertY = false;
                enableBox.setChecked(true);
                xSensitivity.setProgress(sensitivityToProgress(sensitivityX));
                ySensitivity.setProgress(sensitivityToProgress(sensitivityY));
                maxDeltaSeekBar.setProgress(maxDeltaToProgress(maxMouseDelta));
                smoothingSeekBar.setProgress(Math.round(mouseSmoothing * 100.0f));
                invertXBox.setChecked(false);
                invertYBox.setChecked(false);
                GameThread.INSTANCE.onPcInputEnabled(true);
                InputHandler.INSTANCE.onPcInputEnabledChanged();
                applyMouseOptions();
                updateBindingButtons();
            }
        });
        Button loadButton = makeActionButton(owner, "Load saved");
        loadButton.setOnClickListener(v -> {
            load(owner);
            enableBox.setChecked(enabled);
            xSensitivity.setProgress(sensitivityToProgress(sensitivityX));
            ySensitivity.setProgress(sensitivityToProgress(sensitivityY));
            maxDeltaSeekBar.setProgress(maxDeltaToProgress(maxMouseDelta));
            smoothingSeekBar.setProgress(Math.round(mouseSmoothing * 100.0f));
            invertXBox.setChecked(invertX);
            invertYBox.setChecked(invertY);
            GameThread.INSTANCE.onPcInputEnabled(enabled);
            InputHandler.INSTANCE.onPcInputEnabledChanged();
            applyMouseOptions();
            updateBindingButtons();
        });
        Button saveButton = makeActionButton(owner, "Save");
        saveButton.setOnClickListener(v -> save(owner));
        actions.addView(resetButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        actions.addView(loadButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        actions.addView(saveButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        LinearLayout footer = new LinearLayout(owner);
        footer.setOrientation(LinearLayout.VERTICAL);
        footer.addView(actions);
        Button closeFooterButton = makeActionButton(owner, "Close settings");
        closeFooterButton.setOnClickListener(v -> close());
        footer.addView(closeFooterButton);
        panel.addView(footer);

        ViewGroup parent = owner.findViewById(android.R.id.content);
        if (parent == null) {
            throw new IllegalStateException("Activity content view is unavailable");
        }
        parent.addView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        overlay = root;
        root.requestFocus();
    }

    private static void addSectionHeader(Activity owner, LinearLayout parent, String title) {
        TextView header = new TextView(owner);
        header.setText(title);
        header.setTextColor(0xFF72B7FF);
        header.setTextSize(12);
        header.setTypeface(null, android.graphics.Typeface.BOLD);
        LinearLayout.LayoutParams params = verticalMargins(owner, 8, 4);
        parent.addView(header, params);
    }

    private static void addBindingRows(Activity owner, LinearLayout parent, int[] actions) {
        for (int action : actions) {
            LinearLayout row = new LinearLayout(owner);
            row.setGravity(Gravity.CENTER_VERTICAL);
            TextView label = new TextView(owner);
            label.setText(ACTION_NAMES[action]);
            label.setTextColor(0xFFE7EAF0);
            label.setTextSize(14);
            row.addView(label, new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1));

            Button button = makeActionButton(owner, keyLabel(bindings[action]));
            button.setMinWidth(dp(owner, 112));
            button.setOnClickListener(v -> {
                synchronized (KeyboardMouseSettings.class) {
                    pendingCapture = action;
                    updateBindingButtons();
                }
            });
            bindingButtons[action] = button;
            row.addView(button);
            parent.addView(row, verticalMargins(owner, 0, 2));
        }
    }

    private static Button makeActionButton(Activity owner, String text) {
        Button button = new Button(owner);
        button.setAllCaps(false);
        button.setText(text);
        return button;
    }

    private static LinearLayout.LayoutParams verticalMargins(
            Activity owner, int topDp, int bottomDp) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(owner, topDp);
        params.bottomMargin = dp(owner, bottomDp);
        return params;
    }

    private static int dp(Activity owner, int value) {
        return Math.round(value * owner.getResources().getDisplayMetrics().density);
    }

    private static GradientDrawable roundedBackground(int color, int radius) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(color);
        background.setCornerRadius(radius);
        background.setStroke(1, 0xFF3B4658);
        return background;
    }

    private static final class SettingsOverlay extends FrameLayout {
        SettingsOverlay(Activity owner) {
            super(owner);
        }

        @Override
        public boolean dispatchKeyEvent(KeyEvent event) {
            if (event.getKeyCode() == KeyEvent.KEYCODE_ESCAPE &&
                    event.getAction() == KeyEvent.ACTION_DOWN) {
                if (captureKey(event)) {
                    return true;
                }
                close();
                return true;
            }
            if (isCapturingBinding() && captureKey(event)) {
                return true;
            }
            return super.dispatchKeyEvent(event);
        }

        @Override
        public boolean dispatchTouchEvent(MotionEvent event) {
            if ((event.getSource() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE &&
                    isCapturingBinding() && captureMouse(event)) {
                return true;
            }
            return super.dispatchTouchEvent(event);
        }

        @Override
        public boolean dispatchGenericMotionEvent(MotionEvent event) {
            if ((event.getSource() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE &&
                    isCapturingBinding() && captureMouse(event)) {
                return true;
            }
            return super.dispatchGenericMotionEvent(event);
        }

        void dismiss() {
            ViewGroup parent = (ViewGroup) getParent();
            if (parent != null) {
                parent.removeView(this);
            }
            synchronized (KeyboardMouseSettings.class) {
                pendingCapture = -1;
                stateView = null;
                bindingButtons = null;
                overlay = null;
            }
            InputHandler.INSTANCE.setInterfaceVisible(false);
        }
    }

    private static SeekBar.OnSeekBarChangeListener sensitivityListener(
            boolean horizontal, TextView label) {
        return new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                float value = progressToSensitivity(progress);
                synchronized (KeyboardMouseSettings.class) {
                    if (horizontal) {
                        sensitivityX = value;
                    } else {
                        sensitivityY = value;
                    }
                }
                updateSensitivityLabel(label, value, horizontal
                        ? "Mouse X sensitivity" : "Mouse Y sensitivity");
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        };
    }

    private static int sensitivityToProgress(float value) {
        return Math.max(0, Math.min(290, Math.round((value - 0.1f) * 100.0f)));
    }

    private static float progressToSensitivity(int progress) {
        return 0.1f + progress / 100.0f;
    }

    private static SeekBar.OnSeekBarChangeListener mouseOptionListener(
            boolean limit, TextView label) {
        return new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                synchronized (KeyboardMouseSettings.class) {
                    if (limit) {
                        maxMouseDelta = progress + 1.0f;
                    } else {
                        mouseSmoothing = progress / 100.0f;
                    }
                    applyMouseOptions();
                    updateMouseOptionLabel(
                            label, limit ? maxMouseDelta : mouseSmoothing, limit);
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        };
    }

    private static synchronized void applyMouseOptions() {
        KeyboardMouseBridge.nativeSetMouseOptions(maxMouseDelta, mouseSmoothing);
    }

    private static int maxDeltaToProgress(float value) {
        return Math.max(0, Math.min(499, Math.round(value) - 1));
    }

    private static float normalizeMaxMouseDelta(float value) {
        if (!Float.isFinite(value)) {
            Log.w(TAG, "Ignoring non-finite persisted mouse delta limit");
            return 120.0f;
        }
        return Math.max(1.0f, Math.min(500.0f, value));
    }

    private static float normalizeMouseSmoothing(float value) {
        if (!Float.isFinite(value)) {
            Log.w(TAG, "Ignoring non-finite persisted mouse smoothing");
            return 0.0f;
        }
        return Math.max(0.0f, Math.min(0.9f, value));
    }

    private static float normalizeSensitivity(float value) {
        if (!Float.isFinite(value)) {
            Log.w(TAG, "Ignoring non-finite persisted mouse sensitivity");
            return 1.0f;
        }
        return Math.max(0.1f, Math.min(3.0f, value));
    }

    private static void updateSensitivityLabel(TextView label, float value, String title) {
        label.setText(title + ": " + String.format(java.util.Locale.ROOT, "%.1f", value));
    }

    private static void updateMouseOptionLabel(TextView label, float value, boolean limit) {
        if (limit) {
            label.setText("Max mouse delta per frame: " + Math.round(value));
        } else {
            label.setText("Mouse smoothing: " + Math.round(value * 100.0f) + "%");
        }
    }

    private static void updateBindingButtons() {
        if (bindingButtons == null) {
            return;
        }
        for (int action = 0; action < ACTION_COUNT; action++) {
            String binding = action == pendingCapture ? "Press a key or mouse button..."
                    : keyLabel(bindings[action]);
            bindingButtons[action].setText(ACTION_NAMES[action] + ": " + binding);
        }
    }

    private static String keyLabel(int keyCode) {
        if (keyCode == KEY_MOUSE_LEFT) {
            return "Mouse Left";
        }
        if (keyCode == KEY_MOUSE_RIGHT) {
            return "Mouse Right";
        }
        if (keyCode == KeyEvent.KEYCODE_UNKNOWN) {
            return "Unbound";
        }
        return KeyEvent.keyCodeToString(keyCode).replace("KEYCODE_", "");
    }

    private static synchronized void updateStateView() {
        if (stateView == null) {
            return;
        }
        if (pressedKeys.isEmpty()) {
            stateView.setText("Received input: none");
            return;
        }
        StringBuilder state = new StringBuilder("Received input: ");
        boolean first = true;
        for (int code : pressedKeys) {
            if (!first) {
                state.append(", ");
            }
            first = false;
            if (code <= -100) {
                state.append(code == -101 ? "Mouse Left" : "Mouse Right");
            } else {
                state.append(keyLabel(code));
            }
        }
        stateView.setText(state);
    }
}
