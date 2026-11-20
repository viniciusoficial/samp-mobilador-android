package com.gta.game.input;

import android.app.Activity;
import android.content.Context;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import androidx.appcompat.app.AlertDialog;

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
            KeyEvent.KEYCODE_PAGE_UP, KeyEvent.KEYCODE_PAGE_DOWN
    };
    private static final String[] ACTION_NAMES = {
            "Move forward", "Move backward", "Move left", "Move right", "Sprint",
            "Jump", "Crouch", "Fire", "Aim", "Enter / exit vehicle",
            "H action", "Yes", "No", "Walk", "Scoreboard", "Enter / confirm",
            "Open chat", "Next weapon", "Previous weapon"
    };

    private static final int[] ALTERNATE_KEYS = {
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT
    };
    private static final int[] ALTERNATE_ACTIONS = {
            MOVE_FORWARD, MOVE_BACKWARD, MOVE_LEFT, MOVE_RIGHT
    };
    private static final int[] bindings = new int[ACTION_COUNT];
    private static final Set<Integer> pressedKeys = new HashSet<>();

    private static AlertDialog dialog;
    private static TextView stateView;
    private static Button[] bindingButtons;
    private static int pendingCapture = -1;
    private static boolean enabled = true;
    private static float sensitivityX = 1.0f;
    private static float sensitivityY = 1.0f;
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
        enabled = prefs.getBoolean("enabled", true);
        sensitivityX = normalizeSensitivity(prefs.getFloat("sensitivity_x", 1.0f));
        sensitivityY = normalizeSensitivity(prefs.getFloat("sensitivity_y", 1.0f));
        invertX = prefs.getBoolean("invert_x", false);
        invertY = prefs.getBoolean("invert_y", false);
    }

    public static synchronized void save(Context context) {
        android.content.SharedPreferences.Editor editor =
                context.getSharedPreferences("keyboard_mouse", Context.MODE_PRIVATE).edit();
        for (int action = 0; action < ACTION_COUNT; action++) {
            editor.putInt("binding_" + action, bindings[action]);
        }
        editor.putBoolean("enabled", enabled);
        editor.putFloat("sensitivity_x", sensitivityX);
        editor.putFloat("sensitivity_y", sensitivityY);
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
        if (owner.isFinishing() || owner.isDestroyed() ||
                (dialog != null && dialog.isShowing())) {
            return;
        }
        load(owner);
        InputHandler.INSTANCE.setInterfaceVisible(true);

        LinearLayout content = new LinearLayout(owner);
        content.setOrientation(LinearLayout.VERTICAL);
        int padding = (int) (16 * owner.getResources().getDisplayMetrics().density);
        content.setPadding(padding, padding, padding, padding);

        CheckBox enableBox = new CheckBox(owner);
        enableBox.setText("Enable keyboard and mouse controls");
        enableBox.setChecked(enabled);
        content.addView(enableBox);
        enableBox.setOnCheckedChangeListener((button, checked) -> {
            synchronized (KeyboardMouseSettings.class) {
                enabled = checked;
                GameThread.INSTANCE.onPcInputEnabled(checked);
            }
        });

        TextView xLabel = new TextView(owner);
        TextView yLabel = new TextView(owner);
        content.addView(xLabel);
        SeekBar xSensitivity = new SeekBar(owner);
        xSensitivity.setMax(290);
        xSensitivity.setProgress(sensitivityToProgress(sensitivityX));
        content.addView(xSensitivity);
        content.addView(yLabel);
        SeekBar ySensitivity = new SeekBar(owner);
        ySensitivity.setMax(290);
        ySensitivity.setProgress(sensitivityToProgress(sensitivityY));
        content.addView(ySensitivity);
        xSensitivity.setOnSeekBarChangeListener(sensitivityListener(true, xLabel));
        ySensitivity.setOnSeekBarChangeListener(sensitivityListener(false, yLabel));

        CheckBox invertXBox = new CheckBox(owner);
        invertXBox.setText("Invert mouse X");
        invertXBox.setChecked(invertX);
        content.addView(invertXBox);
        invertXBox.setOnCheckedChangeListener((button, checked) -> invertX = checked);
        CheckBox invertYBox = new CheckBox(owner);
        invertYBox.setText("Invert mouse Y");
        invertYBox.setChecked(invertY);
        content.addView(invertYBox);
        invertYBox.setOnCheckedChangeListener((button, checked) -> invertY = checked);

        TextView receivedState = new TextView(owner);
        receivedState.setText("Received input: none");
        content.addView(receivedState);
        stateView = receivedState;

        bindingButtons = new Button[ACTION_COUNT];
        for (int action = 0; action < ACTION_COUNT; action++) {
            final int actionId = action;
            Button button = new Button(owner);
            button.setAllCaps(false);
            button.setOnClickListener(v -> {
                synchronized (KeyboardMouseSettings.class) {
                    pendingCapture = actionId;
                    updateBindingButtons();
                }
            });
            bindingButtons[action] = button;
            content.addView(button);
        }
        updateBindingButtons();
        updateSensitivityLabel(xLabel, sensitivityX, "Mouse X sensitivity");
        updateSensitivityLabel(yLabel, sensitivityY, "Mouse Y sensitivity");

        LinearLayout actions = new LinearLayout(owner);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        Button resetButton = new Button(owner);
        resetButton.setText("Restore defaults");
        resetButton.setOnClickListener(v -> {
            synchronized (KeyboardMouseSettings.class) {
                System.arraycopy(DEFAULT_KEYS, 0, bindings, 0, ACTION_COUNT);
                enabled = true;
                sensitivityX = 1.0f;
                sensitivityY = 1.0f;
                invertX = false;
                invertY = false;
                enableBox.setChecked(true);
                xSensitivity.setProgress(sensitivityToProgress(sensitivityX));
                ySensitivity.setProgress(sensitivityToProgress(sensitivityY));
                invertXBox.setChecked(false);
                invertYBox.setChecked(false);
                GameThread.INSTANCE.onPcInputEnabled(true);
                updateBindingButtons();
            }
        });
        Button loadButton = new Button(owner);
        loadButton.setText("Load saved");
        loadButton.setOnClickListener(v -> {
            load(owner);
            enableBox.setChecked(enabled);
            xSensitivity.setProgress(sensitivityToProgress(sensitivityX));
            ySensitivity.setProgress(sensitivityToProgress(sensitivityY));
            invertXBox.setChecked(invertX);
            invertYBox.setChecked(invertY);
            GameThread.INSTANCE.onPcInputEnabled(enabled);
            updateBindingButtons();
        });
        Button saveButton = new Button(owner);
        saveButton.setText("Save");
        saveButton.setOnClickListener(v -> save(owner));
        actions.addView(resetButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        actions.addView(loadButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        actions.addView(saveButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        content.addView(actions);

        ScrollView scrollView = new ScrollView(owner);
        scrollView.addView(content);
        dialog = new AlertDialog.Builder(owner)
                .setTitle(R.string.keyboard_mouse_settings)
                .setView(scrollView)
                .setNegativeButton(android.R.string.cancel, null)
                .create();
        dialog.setOnKeyListener((ignored, keyCode, event) -> captureKey(event));
        dialog.setOnShowListener(ignored -> {
            if (dialog.getWindow() != null) {
                dialog.getWindow().getDecorView().setOnGenericMotionListener(
                        (view, event) -> captureMouse(event));
            }
        });
        dialog.setOnDismissListener(ignored -> {
            pendingCapture = -1;
            stateView = null;
            bindingButtons = null;
            dialog = null;
            InputHandler.INSTANCE.setInterfaceVisible(false);
        });
        dialog.show();
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
