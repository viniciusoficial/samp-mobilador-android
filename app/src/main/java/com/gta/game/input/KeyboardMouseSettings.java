package com.gta.game.input;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.Log;
import android.view.Gravity;
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
import android.widget.Toast;

import com.rockstargames.oswrapper.GameThread;
import com.rockstargames.oswrapper.view.InputHandler;

import java.util.HashSet;
import java.util.Locale;
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
    public static final int PASSENGER = 19;
    public static final int LOOK_BEHIND = 20;

    public static final int ACTION_COUNT = 21;

    private static final int KEY_MOUSE_LEFT = -1;
    private static final int KEY_MOUSE_RIGHT = -2;
    private static final int KEY_MOUSE_MIDDLE = -3;
    private static final int KEY_MOUSE_BACK = -4;
    private static final int KEY_MOUSE_FORWARD = -5;

    private static final int[] DEFAULT_KEYS = {
            KeyEvent.KEYCODE_W, KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_D,
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_C,
            KEY_MOUSE_LEFT, KEY_MOUSE_RIGHT, KeyEvent.KEYCODE_F, KeyEvent.KEYCODE_H,
            KeyEvent.KEYCODE_Y, KeyEvent.KEYCODE_N, KeyEvent.KEYCODE_ALT_LEFT,
            KeyEvent.KEYCODE_TAB, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_T,
            KeyEvent.KEYCODE_E, KeyEvent.KEYCODE_Q, KeyEvent.KEYCODE_G,
            KEY_MOUSE_MIDDLE
    };

    private static final String[] ACTION_NAMES = {
            "Andar para frente", "Andar para trás", "Streif esquerda (A)", "Streif direita (D)",
            "Correr (Sprint)", "Pular", "Agachar", "Atirar", "Mirar",
            "Entrar/sair motorista", "Ação / Buzina (H)", "Sim (Y)", "Não (N)",
            "Caminhar devagar", "Placar (TAB)", "Confirmar (Enter)", "Abrir Chat",
            "Próxima arma", "Arma anterior", "Entrar passageiro", "Olhar para trás"
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
    private static TextView mouseDiagnosticsView;
    private static String mouseDiagnostics =
            "Mouse: aguardando eventos | captura=false | L=false R=false | frames=0";
    private static boolean showMouseDiagnostics;
    private static Button[] bindingButtons;
    private static int pendingCapture = -1;

    // Configurable gameplay & camera parameters
    private static boolean enabled = true;
    private static float sensitivityX = 1.0f;
    private static float sensitivityY = 1.0f;
    private static float aimSensitivity = 0.85f;
    private static float strafeSpeed = 1.45f;
    private static float smoothing = 0.0f;
    private static boolean invertX;
    private static boolean invertY;
    private static boolean pcSprint = true;
    private static boolean fastWeaponSwitch = true;
    private static boolean numberSlotKeys = true;
    private static boolean vehicleAutoCenter = false;

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
        if (!prefs.contains("bindings_version") || prefs.getInt("bindings_version", 0) < 3) {
            if (bindings[WEAPON_NEXT] == KeyEvent.KEYCODE_PAGE_UP) {
                bindings[WEAPON_NEXT] = KeyEvent.KEYCODE_E;
            }
            if (bindings[WEAPON_PREVIOUS] == KeyEvent.KEYCODE_PAGE_DOWN) {
                bindings[WEAPON_PREVIOUS] = KeyEvent.KEYCODE_Q;
            }
            if (!prefs.contains("binding_" + PASSENGER)) {
                bindings[PASSENGER] = DEFAULT_KEYS[PASSENGER];
            }
            if (!prefs.contains("binding_" + LOOK_BEHIND)) {
                bindings[LOOK_BEHIND] = DEFAULT_KEYS[LOOK_BEHIND];
            }
            prefs.edit().putInt("bindings_version", 3).apply();
        }
        enabled = prefs.getBoolean("enabled", true);
        sensitivityX = normalizeRange(prefs.getFloat("sensitivity_x", 1.0f), 0.1f, 3.0f, 1.0f);
        sensitivityY = normalizeRange(prefs.getFloat("sensitivity_y", 1.0f), 0.1f, 3.0f, 1.0f);
        aimSensitivity = normalizeRange(prefs.getFloat("aim_sensitivity", 0.85f), 0.2f, 2.5f, 0.85f);
        strafeSpeed = normalizeRange(prefs.getFloat("strafe_speed", 1.45f), 1.0f, 2.2f, 1.45f);
        smoothing = normalizeRange(prefs.getFloat("smoothing", 0.0f), 0.0f, 0.75f, 0.0f);
        invertX = prefs.getBoolean("invert_x", false);
        invertY = prefs.getBoolean("invert_y", false);
        pcSprint = prefs.getBoolean("pc_sprint", true);
        fastWeaponSwitch = prefs.getBoolean("fast_weapon_switch", true);
        numberSlotKeys = prefs.getBoolean("number_slot_keys", true);
        vehicleAutoCenter = prefs.getBoolean("vehicle_auto_center", false);
        showMouseDiagnostics = prefs.getBoolean("show_diagnostics", false);
        syncNativeConfig();
    }

    public static synchronized void save(Context context) {
        if (context == null) {
            return;
        }
        android.content.SharedPreferences.Editor editor =
                context.getSharedPreferences("keyboard_mouse", Context.MODE_PRIVATE).edit();
        for (int action = 0; action < ACTION_COUNT; action++) {
            editor.putInt("binding_" + action, bindings[action]);
        }
        editor.putInt("bindings_version", 3);
        editor.putBoolean("enabled", enabled);
        editor.putFloat("sensitivity_x", sensitivityX);
        editor.putFloat("sensitivity_y", sensitivityY);
        editor.putFloat("aim_sensitivity", aimSensitivity);
        editor.putFloat("strafe_speed", strafeSpeed);
        editor.putFloat("smoothing", smoothing);
        editor.putBoolean("invert_x", invertX);
        editor.putBoolean("invert_y", invertY);
        editor.putBoolean("pc_sprint", pcSprint);
        editor.putBoolean("fast_weapon_switch", fastWeaponSwitch);
        editor.putBoolean("number_slot_keys", numberSlotKeys);
        editor.putBoolean("vehicle_auto_center", vehicleAutoCenter);
        editor.putBoolean("show_diagnostics", showMouseDiagnostics);
        editor.apply();
        syncNativeConfig();
    }

    public static synchronized void syncNativeConfig() {
        GameThread.INSTANCE.onPcInputEnabled(enabled);
        GameThread.INSTANCE.onPcMouseOptions(0.0f, smoothing);
        GameThread.INSTANCE.onPcCombatConfig(
                aimSensitivity, strafeSpeed, pcSprint, fastWeaponSwitch, vehicleAutoCenter);
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
        if (keyCode == KeyEvent.KEYCODE_SHIFT_RIGHT) {
            return bindings[SPRINT] == KeyEvent.KEYCODE_SHIFT_LEFT ? SPRINT : -1;
        }
        return -1;
    }

    public static synchronized int actionForMouseButton(int button) {
        int binding = mouseMaskToBinding(button);
        if (binding == 0) {
            return -1;
        }
        for (int action = 0; action < ACTION_COUNT; action++) {
            if (bindings[action] == binding) {
                return action;
            }
        }
        if (button == MotionEvent.BUTTON_PRIMARY) {
            return FIRE;
        }
        if (button == MotionEvent.BUTTON_SECONDARY) {
            return AIM;
        }
        return -1;
    }

    private static int mouseMaskToBinding(int button) {
        if (button == MotionEvent.BUTTON_PRIMARY) return KEY_MOUSE_LEFT;
        if (button == MotionEvent.BUTTON_SECONDARY) return KEY_MOUSE_RIGHT;
        if (button == MotionEvent.BUTTON_TERTIARY) return KEY_MOUSE_MIDDLE;
        if (button == MotionEvent.BUTTON_BACK) return KEY_MOUSE_BACK;
        if (button == MotionEvent.BUTTON_FORWARD) return KEY_MOUSE_FORWARD;
        return 0;
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

    public static synchronized boolean isNumberSlotKeysEnabled() {
        return numberSlotKeys;
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

    public static synchronized void clearPressedStates() {
        pressedKeys.clear();
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
        int code = event.getKeyCode();
        if (code == KeyEvent.KEYCODE_ESCAPE) {
            pendingCapture = -1;
            updateBindingButtons();
            return true;
        }
        if (code == KeyEvent.KEYCODE_BACK) {
            bindings[pendingCapture] = KeyEvent.KEYCODE_UNKNOWN;
            pendingCapture = -1;
            updateBindingButtons();
            return true;
        }
        for (int action = 0; action < ACTION_COUNT; action++) {
            if (action != pendingCapture && bindings[action] == code) {
                bindings[action] = KeyEvent.KEYCODE_UNKNOWN;
            }
        }
        bindings[pendingCapture] = code;
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
        if (pendingCapture < 0) {
            return false;
        }
        int actionMasked = event.getActionMasked();
        if (actionMasked != MotionEvent.ACTION_BUTTON_PRESS &&
                actionMasked != MotionEvent.ACTION_DOWN) {
            return false;
        }
        int button = event.getActionButton();
        if (button == 0 && actionMasked == MotionEvent.ACTION_DOWN) {
            button = MotionEvent.BUTTON_PRIMARY;
        }
        int binding = mouseMaskToBinding(button);
        if (binding == 0) {
            return false;
        }
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

        int pad = dp(owner, 16);
        SettingsOverlay root = new SettingsOverlay(owner);
        root.setBackgroundColor(0xC8070A10);
        root.setClickable(true);
        root.setFocusableInTouchMode(true);

        LinearLayout panel = new LinearLayout(owner);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(pad, pad, pad, pad);
        panel.setBackground(roundedBackground(0xFF12161F, dp(owner, 16), 0xFF283244));

        int screenW = owner.getResources().getDisplayMetrics().widthPixels;
        int screenH = owner.getResources().getDisplayMetrics().heightPixels;
        FrameLayout.LayoutParams panelParams = new FrameLayout.LayoutParams(
                Math.min((int) (screenW * 0.92f), dp(owner, 820)),
                Math.min((int) (screenH * 0.90f), dp(owner, 700)),
                Gravity.CENTER);
        root.addView(panel, panelParams);

        // Header
        LinearLayout heading = new LinearLayout(owner);
        heading.setGravity(Gravity.CENTER_VERTICAL);

        LinearLayout titleCol = new LinearLayout(owner);
        titleCol.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(owner);
        title.setText("Configuração Mobilador (/mob)");
        title.setTextColor(Color.WHITE);
        title.setTextSize(19);
        title.setTypeface(null, Typeface.BOLD);
        titleCol.addView(title);

        TextView subtitle = new TextView(owner);
        subtitle.setText("Controles estilo PC SA:MP — ajustes aplicados em tempo real.");
        subtitle.setTextColor(0xFF94A3B8);
        subtitle.setTextSize(12);
        titleCol.addView(subtitle);
        heading.addView(titleCol, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        Button closeHeader = makePillButton(owner, "Fechar", 0xFF232B3B, Color.WHITE);
        closeHeader.setOnClickListener(v -> {
            save(owner);
            close();
        });
        heading.addView(closeHeader);
        panel.addView(heading, verticalMargins(owner, 0, 10));

        // Scrollable Body
        ScrollView scrollView = new ScrollView(owner);
        scrollView.setFillViewport(false);
        LinearLayout content = new LinearLayout(owner);
        content.setOrientation(LinearLayout.VERTICAL);
        scrollView.addView(content);
        panel.addView(scrollView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        // Card 1: Geral + Presets
        LinearLayout generalCard = addSectionCard(owner, content, "MODO PC & PRESETS");
        CheckBox enableBox = addToggle(owner, generalCard,
                "Ativar suporte Teclado e Mouse", enabled);
        enableBox.setOnCheckedChangeListener((b, checked) -> {
            synchronized (KeyboardMouseSettings.class) {
                enabled = checked;
                save(owner);
                InputHandler.INSTANCE.onPcInputEnabledChanged();
            }
        });

        LinearLayout presetRow = new LinearLayout(owner);
        presetRow.setOrientation(LinearLayout.HORIZONTAL);
        Button pcCombatPreset = makePillButton(owner, "Preset Combate PC", 0xFF1D4ED8, Color.WHITE);
        Button rawZeroPreset = makePillButton(owner, "Restaurar Padrão", 0xFF283244, 0xFFE2E8F0);
        presetRow.addView(pcCombatPreset, weightedParams(owner, 0, 4));
        presetRow.addView(rawZeroPreset, weightedParams(owner, 4, 0));
        generalCard.addView(presetRow, verticalMargins(owner, 6, 2));

        // Card 2: Combate, Streif & Agilidade
        LinearLayout combatCard = addSectionCard(owner, content, "COMBATE, STREIF & AGILIDADE PC");

        TextView strafeLabel = makeValueLabel(owner);
        SeekBar strafeSeek = makeSlider(owner, 120, Math.round((strafeSpeed - 1.0f) * 100.0f));
        updateStrafeLabel(strafeLabel, strafeSpeed);
        combatCard.addView(strafeLabel);
        combatCard.addView(strafeSeek, verticalMargins(owner, 2, 8));

        strafeSeek.setOnSeekBarChangeListener(new SimpleSeekListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                synchronized (KeyboardMouseSettings.class) {
                    strafeSpeed = 1.0f + (progress / 100.0f);
                }
                updateStrafeLabel(strafeLabel, strafeSpeed);
                if (fromUser) syncNativeConfig();
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                save(owner);
            }
        });

        CheckBox pcSprintBox = addToggle(owner, combatCard,
                "Sprint estilo PC (segurar Shift para correr no máximo)", pcSprint);
        pcSprintBox.setOnCheckedChangeListener((b, checked) -> {
            pcSprint = checked;
            save(owner);
        });

        CheckBox fastSwitchBox = addToggle(owner, combatCard,
                "Troca rápida de armas sem delay (Q / E / Scroll)", fastWeaponSwitch);
        fastSwitchBox.setOnCheckedChangeListener((b, checked) -> {
            fastWeaponSwitch = checked;
            save(owner);
        });

        CheckBox slotKeysBox = addToggle(owner, combatCard,
                "Teclas numéricas 1–9 trocam slot de arma direto", numberSlotKeys);
        slotKeysBox.setOnCheckedChangeListener((b, checked) -> {
            numberSlotKeys = checked;
            save(owner);
        });

        // Card 3: Mouse & Câmera
        LinearLayout mouseCard = addSectionCard(owner, content, "MOUSE & CÂMERA (SEM RETORNO AO CENTRO)");

        TextView xLabel = makeValueLabel(owner);
        SeekBar xSeek = makeSlider(owner, 290, sensitivityToProgress(sensitivityX));
        updateSensitivityLabel(xLabel, sensitivityX, "Sensibilidade Horizontal (X)");
        mouseCard.addView(xLabel);
        mouseCard.addView(xSeek, verticalMargins(owner, 2, 6));

        TextView yLabel = makeValueLabel(owner);
        SeekBar ySeek = makeSlider(owner, 290, sensitivityToProgress(sensitivityY));
        updateSensitivityLabel(yLabel, sensitivityY, "Sensibilidade Vertical (Y)");
        mouseCard.addView(yLabel);
        mouseCard.addView(ySeek, verticalMargins(owner, 2, 6));

        TextView aimLabel = makeValueLabel(owner);
        SeekBar aimSeek = makeSlider(owner, 230, Math.round((aimSensitivity - 0.2f) * 100.0f));
        updateSensitivityLabel(aimLabel, aimSensitivity, "Sensibilidade ao Mirar (RMB / Sniper)");
        mouseCard.addView(aimLabel);
        mouseCard.addView(aimSeek, verticalMargins(owner, 2, 6));

        TextView smoothLabel = makeValueLabel(owner);
        SeekBar smoothSeek = makeSlider(owner, 75, Math.round(smoothing * 100.0f));
        updateSmoothingLabel(smoothLabel, smoothing);
        mouseCard.addView(smoothLabel);
        mouseCard.addView(smoothSeek, verticalMargins(owner, 2, 6));

        xSeek.setOnSeekBarChangeListener(new SimpleSeekListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                sensitivityX = progressToSensitivity(progress);
                updateSensitivityLabel(xLabel, sensitivityX, "Sensibilidade Horizontal (X)");
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                save(owner);
            }
        });
        ySeek.setOnSeekBarChangeListener(new SimpleSeekListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                sensitivityY = progressToSensitivity(progress);
                updateSensitivityLabel(yLabel, sensitivityY, "Sensibilidade Vertical (Y)");
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                save(owner);
            }
        });
        aimSeek.setOnSeekBarChangeListener(new SimpleSeekListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                aimSensitivity = 0.2f + (progress / 100.0f);
                updateSensitivityLabel(aimLabel, aimSensitivity, "Sensibilidade ao Mirar (RMB / Sniper)");
                if (fromUser) syncNativeConfig();
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                save(owner);
            }
        });
        smoothSeek.setOnSeekBarChangeListener(new SimpleSeekListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                smoothing = progress / 100.0f;
                updateSmoothingLabel(smoothLabel, smoothing);
                if (fromUser) syncNativeConfig();
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                save(owner);
            }
        });

        CheckBox invertXBox = addToggle(owner, mouseCard, "Inverter eixo X do mouse", invertX);
        invertXBox.setOnCheckedChangeListener((b, checked) -> {
            invertX = checked;
            save(owner);
        });
        CheckBox invertYBox = addToggle(owner, mouseCard, "Inverter eixo Y do mouse", invertY);
        invertYBox.setOnCheckedChangeListener((b, checked) -> {
            invertY = checked;
            save(owner);
        });
        CheckBox autoCenterBox = addToggle(owner, mouseCard,
                "Auto-centralizar câmera ao dirigir veículo", vehicleAutoCenter);
        autoCenterBox.setOnCheckedChangeListener((b, checked) -> {
            vehicleAutoCenter = checked;
            save(owner);
        });

        // Card 4: Mapeamentos
        bindingButtons = new Button[ACTION_COUNT];

        LinearLayout moveCard = addSectionCard(owner, content, "MOVIMENTAÇÃO & COMBATE");
        addBindingRows(owner, moveCard, new int[] {
                MOVE_FORWARD, MOVE_BACKWARD, MOVE_LEFT, MOVE_RIGHT,
                SPRINT, JUMP, CROUCH, WALK, FIRE, AIM,
                WEAPON_NEXT, WEAPON_PREVIOUS
        });

        LinearLayout worldCard = addSectionCard(owner, content, "VEÍCULOS, CHAT & INTERFACE");
        addBindingRows(owner, worldCard, new int[] {
                VEHICLE, PASSENGER, CONTROL_BACK, LOOK_BEHIND,
                CHAT, TAB, ENTER, YES, NO
        });

        // Card 5: Diagnóstico ao vivo
        LinearLayout diagCard = addSectionCard(owner, content, "TESTE & DIAGNÓSTICO AO VIVO");
        TextView receivedState = new TextView(owner);
        receivedState.setTextColor(0xFF93C5FD);
        receivedState.setTextSize(12);
        receivedState.setText("Entrada detectada: nenhuma");
        diagCard.addView(receivedState, verticalMargins(owner, 0, 4));
        stateView = receivedState;

        CheckBox diagnosticsBox = addToggle(owner, diagCard,
                "Exibir telemetria detalhada do mouse", showMouseDiagnostics);
        TextView diagnostics = new TextView(owner);
        diagnostics.setTextColor(0xFF94A3B8);
        diagnostics.setTextSize(11);
        diagnostics.setText(mouseDiagnostics);
        diagnostics.setVisibility(showMouseDiagnostics ? View.VISIBLE : View.GONE);
        diagCard.addView(diagnostics);
        mouseDiagnosticsView = diagnostics;
        diagnosticsBox.setOnCheckedChangeListener((b, checked) -> {
            showMouseDiagnostics = checked;
            diagnostics.setVisibility(checked ? View.VISIBLE : View.GONE);
            save(owner);
        });

        updateBindingButtons();

        // Preset listeners
        pcCombatPreset.setOnClickListener(v -> {
            synchronized (KeyboardMouseSettings.class) {
                enabled = true;
                strafeSpeed = 1.65f;
                aimSensitivity = 0.85f;
                smoothing = 0.0f;
                pcSprint = true;
                fastWeaponSwitch = true;
                numberSlotKeys = true;
                vehicleAutoCenter = false;
                enableBox.setChecked(true);
                strafeSeek.setProgress(Math.round((strafeSpeed - 1.0f) * 100.0f));
                aimSeek.setProgress(Math.round((aimSensitivity - 0.2f) * 100.0f));
                smoothSeek.setProgress(0);
                pcSprintBox.setChecked(true);
                fastSwitchBox.setChecked(true);
                slotKeysBox.setChecked(true);
                autoCenterBox.setChecked(false);
                save(owner);
                InputHandler.INSTANCE.onPcInputEnabledChanged();
            }
            Toast.makeText(owner, "Preset Combate PC aplicado!", Toast.LENGTH_SHORT).show();
        });

        rawZeroPreset.setOnClickListener(v -> {
            synchronized (KeyboardMouseSettings.class) {
                System.arraycopy(DEFAULT_KEYS, 0, bindings, 0, ACTION_COUNT);
                enabled = true;
                sensitivityX = 1.0f;
                sensitivityY = 1.0f;
                aimSensitivity = 0.85f;
                strafeSpeed = 1.45f;
                smoothing = 0.0f;
                invertX = false;
                invertY = false;
                pcSprint = true;
                fastWeaponSwitch = true;
                numberSlotKeys = true;
                vehicleAutoCenter = false;

                enableBox.setChecked(true);
                xSeek.setProgress(sensitivityToProgress(sensitivityX));
                ySeek.setProgress(sensitivityToProgress(sensitivityY));
                aimSeek.setProgress(Math.round((aimSensitivity - 0.2f) * 100.0f));
                strafeSeek.setProgress(Math.round((strafeSpeed - 1.0f) * 100.0f));
                smoothSeek.setProgress(0);
                invertXBox.setChecked(false);
                invertYBox.setChecked(false);
                pcSprintBox.setChecked(true);
                fastSwitchBox.setChecked(true);
                slotKeysBox.setChecked(true);
                autoCenterBox.setChecked(false);

                save(owner);
                InputHandler.INSTANCE.onPcInputEnabledChanged();
                updateBindingButtons();
            }
            Toast.makeText(owner, "Padrões restaurados!", Toast.LENGTH_SHORT).show();
        });

        // Footer
        LinearLayout footer = new LinearLayout(owner);
        footer.setOrientation(LinearLayout.HORIZONTAL);
        Button saveCloseButton = makePillButton(owner, "Salvar e Voltar ao Jogo", 0xFF16A34A, Color.WHITE);
        saveCloseButton.setOnClickListener(v -> {
            save(owner);
            close();
        });
        footer.addView(saveCloseButton, weightedParams(owner, 0, 0));
        panel.addView(footer, verticalMargins(owner, 10, 0));

        ViewGroup parent = owner.findViewById(android.R.id.content);
        if (parent == null) {
            throw new IllegalStateException("Activity content view is unavailable");
        }
        parent.addView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        overlay = root;
        root.requestFocus();
    }

    private static LinearLayout addSectionCard(Activity owner, LinearLayout parent, String title) {
        LinearLayout card = new LinearLayout(owner);
        card.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(owner, 12);
        card.setPadding(pad, pad, pad, pad);
        card.setBackground(roundedBackground(0xFF1A202E, dp(owner, 12), 0xFF2B3548));

        TextView header = new TextView(owner);
        header.setText(title);
        header.setTextColor(0xFF60A5FA);
        header.setTextSize(12);
        header.setTypeface(null, Typeface.BOLD);
        card.addView(header, verticalMargins(owner, 0, 6));

        parent.addView(card, verticalMargins(owner, 0, 10));
        return card;
    }

    private static CheckBox addToggle(Activity owner, LinearLayout parent, String label, boolean checked) {
        CheckBox box = new CheckBox(owner);
        box.setTextColor(0xFFE2E8F0);
        box.setTextSize(13);
        box.setText(label);
        box.setChecked(checked);
        parent.addView(box, verticalMargins(owner, 2, 2));
        return box;
    }

    private static TextView makeValueLabel(Activity owner) {
        TextView label = new TextView(owner);
        label.setTextColor(0xFFE2E8F0);
        label.setTextSize(13);
        return label;
    }

    private static SeekBar makeSlider(Activity owner, int max, int progress) {
        SeekBar bar = new SeekBar(owner);
        bar.setMax(max);
        bar.setProgress(Math.max(0, Math.min(max, progress)));
        return bar;
    }

    private static void addBindingRows(Activity owner, LinearLayout parent, int[] actions) {
        for (int action : actions) {
            LinearLayout row = new LinearLayout(owner);
            row.setGravity(Gravity.CENTER_VERTICAL);
            TextView label = new TextView(owner);
            label.setText(ACTION_NAMES[action]);
            label.setTextColor(0xFFE2E8F0);
            label.setTextSize(13);
            row.addView(label, new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1));

            Button button = makePillButton(owner, keyLabel(bindings[action]), 0xFF252E42, Color.WHITE);
            button.setMinWidth(dp(owner, 130));
            button.setOnClickListener(v -> {
                synchronized (KeyboardMouseSettings.class) {
                    pendingCapture = action;
                    updateBindingButtons();
                }
            });
            bindingButtons[action] = button;
            row.addView(button);
            parent.addView(row, verticalMargins(owner, 2, 4));
        }
    }

    private static Button makePillButton(Activity owner, String text, int bgColor, int textColor) {
        Button button = new Button(owner);
        button.setAllCaps(false);
        button.setText(text);
        button.setTextColor(textColor);
        button.setTextSize(12);
        button.setPadding(dp(owner, 12), dp(owner, 6), dp(owner, 12), dp(owner, 6));
        button.setBackground(roundedBackground(bgColor, dp(owner, 8), 0xFF3B475E));
        return button;
    }

    private static LinearLayout.LayoutParams weightedParams(Activity owner, int leftDp, int rightDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        p.leftMargin = dp(owner, leftDp);
        p.rightMargin = dp(owner, rightDp);
        return p;
    }

    private static LinearLayout.LayoutParams verticalMargins(Activity owner, int topDp, int bottomDp) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(owner, topDp);
        params.bottomMargin = dp(owner, bottomDp);
        return params;
    }

    private static int dp(Activity owner, int value) {
        return Math.round(value * owner.getResources().getDisplayMetrics().density);
    }

    private static GradientDrawable roundedBackground(int color, int radius, int strokeColor) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(color);
        background.setCornerRadius(radius);
        background.setStroke(1, strokeColor);
        return background;
    }

    private static final class SettingsOverlay extends FrameLayout {
        private final Activity ownerActivity;

        SettingsOverlay(Activity owner) {
            super(owner);
            this.ownerActivity = owner;
        }

        @Override
        public boolean dispatchKeyEvent(KeyEvent event) {
            if (event.getKeyCode() == KeyEvent.KEYCODE_ESCAPE &&
                    event.getAction() == KeyEvent.ACTION_DOWN) {
                if (captureKey(event)) {
                    return true;
                }
                save(ownerActivity);
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
            if (InputHandler.isMouseSource(event.getSource()) &&
                    isCapturingBinding() && captureMouse(event)) {
                return true;
            }
            return super.dispatchTouchEvent(event);
        }

        @Override
        public boolean dispatchGenericMotionEvent(MotionEvent event) {
            if (InputHandler.isMouseSource(event.getSource()) &&
                    isCapturingBinding() && captureMouse(event)) {
                return true;
            }
            return super.dispatchGenericMotionEvent(event);
        }

        void dismiss() {
            save(ownerActivity);
            ViewGroup parent = (ViewGroup) getParent();
            if (parent != null) {
                parent.removeView(this);
            }
            synchronized (KeyboardMouseSettings.class) {
                pendingCapture = -1;
                stateView = null;
                mouseDiagnosticsView = null;
                bindingButtons = null;
                overlay = null;
            }
            InputHandler.INSTANCE.setInterfaceVisible(false);
            InputHandler.INSTANCE.restoreGameFocus();
        }
    }

    private abstract static class SimpleSeekListener implements SeekBar.OnSeekBarChangeListener {
        @Override
        public void onStartTrackingTouch(SeekBar seekBar) {
        }
        @Override
        public void onStopTrackingTouch(SeekBar seekBar) {
        }
    }

    private static int sensitivityToProgress(float value) {
        return Math.max(0, Math.min(290, Math.round((value - 0.1f) * 100.0f)));
    }

    private static float progressToSensitivity(int progress) {
        return 0.1f + progress / 100.0f;
    }

    private static float normalizeRange(float value, float min, float max, float fallback) {
        if (!Float.isFinite(value)) {
            return fallback;
        }
        return Math.max(min, Math.min(max, value));
    }

    private static void updateSensitivityLabel(TextView label, float value, String title) {
        label.setText(title + ": " + String.format(Locale.ROOT, "%.2fx", value));
    }

    private static void updateStrafeLabel(TextView label, float value) {
        label.setText("Velocidade do Streif (A/D mirando): " + String.format(Locale.ROOT, "%.2fx", value));
    }

    private static void updateSmoothingLabel(TextView label, float value) {
        if (value <= 0.01f) {
            label.setText("Suavização do mouse: Desativada (Raw 1:1)");
        } else {
            label.setText("Suavização do mouse: " + Math.round(value * 100.0f) + "%");
        }
    }

    private static void updateBindingButtons() {
        if (bindingButtons == null) {
            return;
        }
        for (int action = 0; action < ACTION_COUNT; action++) {
            if (bindingButtons[action] == null) continue;
            if (action == pendingCapture) {
                bindingButtons[action].setText("Pressione tecla/botão...");
                bindingButtons[action].setBackground(roundedBackground(0xFF1D4ED8, 12, 0xFF93C5FD));
            } else {
                bindingButtons[action].setText(keyLabel(bindings[action]));
                bindingButtons[action].setBackground(roundedBackground(0xFF252E42, 12, 0xFF3B475E));
            }
        }
    }

    private static String keyLabel(int keyCode) {
        if (keyCode == KEY_MOUSE_LEFT) return "Mouse Esq";
        if (keyCode == KEY_MOUSE_RIGHT) return "Mouse Dir";
        if (keyCode == KEY_MOUSE_MIDDLE) return "Mouse Meio";
        if (keyCode == KEY_MOUSE_BACK) return "Mouse Voltar";
        if (keyCode == KEY_MOUSE_FORWARD) return "Mouse Avançar";
        if (keyCode == KeyEvent.KEYCODE_UNKNOWN) return "Sem tecla";
        return KeyEvent.keyCodeToString(keyCode).replace("KEYCODE_", "");
    }

    private static synchronized void updateStateView() {
        if (stateView == null) {
            return;
        }
        if (pressedKeys.isEmpty()) {
            stateView.setText("Entrada detectada: nenhuma");
            return;
        }
        StringBuilder state = new StringBuilder("Entrada detectada: ");
        boolean first = true;
        for (int code : pressedKeys) {
            if (!first) state.append(", ");
            first = false;
            if (code <= -100) {
                int mask = -code - 100;
                int b = mouseMaskToBinding(mask);
                state.append(b != 0 ? keyLabel(b) : ("Mouse #" + mask));
            } else {
                state.append(keyLabel(code));
            }
        }
        stateView.setText(state.toString());
    }

    public static synchronized void updateMouseDiagnostics(String diagnostics) {
        mouseDiagnostics = diagnostics;
        if (mouseDiagnosticsView != null) {
            mouseDiagnosticsView.setText(diagnostics);
        }
    }
}
