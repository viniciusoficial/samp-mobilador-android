package com.sampmobile.game;


import com.sampmobile.launcher.utils.I18n;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.Toast;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import com.joom.paranoid.Obfuscate;
import com.rockstargames.gtasa.GTASA;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

import com.sampmobile.game.input.KeyboardMouseBridge;
import com.sampmobile.game.input.KeyboardMouseSettings;
import com.sampmobile.game.ui.GameInputArbiter;
import com.sampmobile.game.ui.UI;
import com.sampmobile.game.ui.cef.CefManager;
import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.LocaleManager;
import com.sampmobile.launcher.utils.ModManager;

@Obfuscate
public class SAMP extends GTASA {
    private static final String TAG = "SAMP";
    private static boolean sAmlLoaded = false;
    private static boolean sMonetLoaded = false;
    private static boolean sJavaCrashHandlerInstalled = false;

    private void installJavaCrashHandler() {
        if (sJavaCrashHandlerInstalled) return;
        sJavaCrashHandlerInstalled = true;
        final File extDir = getExternalFilesDir(null);
        if (extDir == null) return;
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            try {
                File logcatDir = new File(extDir, "logcat");
                if (!logcatDir.exists()) logcatDir.mkdirs();
                long epoch = System.currentTimeMillis() / 1000L;
                int pid = android.os.Process.myPid();
                File out = new File(logcatDir, "crash_java_" + epoch + "_" + pid + ".log");
                StringWriter sw = new StringWriter();
                sw.append("========== SA-MP Mobile Java crash report ==========\n");
                sw.append("epoch: ").append(String.valueOf(epoch)).append("\n");
                sw.append("pid: ").append(String.valueOf(pid))
                        .append(" thread: ").append(thread != null ? thread.getName() : "unknown").append("\n\n");
                throwable.printStackTrace(new PrintWriter(sw));
                try (FileOutputStream fos = new FileOutputStream(out)) {
                    fos.write(sw.toString().getBytes(StandardCharsets.UTF_8));
                }
            } catch (Throwable ignored) {
            }
            if (prev != null) {
                prev.uncaughtException(thread, throwable);
            }
        });
    }

    public UI ui;

    private native void initializeSAMP(UI ui, String gameDir, boolean isOffline);

    private native boolean multiTouchEvent4Ex(int action, int count, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3);

    private native void onHardwareKeyEvent(int keyCode, boolean isDown);

    /* Physical mouse: relative deltas, buttons and wheel are resolved by the
     * native side (Game/PcInput.cpp + Game/NativeCameraMouse.cpp), which drives
     * the SA camera and the local player keys. */
    private native void onMouseDelta(float dx, float dy);
    private native void onMouseButton(int button, boolean pressed);
    private native void onMouseWheel(int steps);
    private native void onMouseReset();

    private void loadLibraries() {
        Log.i(TAG, "**** Loading libraries");

        // check if arch is arm32 and load ImmEmulatorJ
        // Process bitness is the right signal: on translated/emulated devices the
        // SUPPORTED_ABIS list reflects the host (e.g. x86_64), not the loaded ABI.
        boolean is_64 = android.os.Process.is64Bit();
        Log.i(TAG, "is64BitProcess: " + is_64);

        if (!is_64) {
            System.loadLibrary("ImmEmulatorJ");
        }

        System.loadLibrary("GTASA");
        System.loadLibrary("SAMPMOBILE");
    }

    @Override
    protected void attachBaseContext(Context newBase) {
        super.attachBaseContext(LocaleManager.wrap(newBase));
    }

    private void loadModLibraries(ClientSettings settings) {
        boolean amlEnabled = settings.isAmlEnabled();
        boolean jpatchEnabled = settings.isJPatchEnabled();
        boolean monetEnabled = settings.isMonetEnabled();

        ModManager.ensureDirectories(this);

        if (amlEnabled || jpatchEnabled) {
            // Sync JPatch .so enabled/disabled filename before AML scans mods/
            ModManager.syncJPatchBinaryState(this, jpatchEnabled);
            if (jpatchEnabled) {
                ModManager.enforceJPatchTouchSafety(this);
            }
            ModManager.prepareAmlConfig(this);

            if (!sAmlLoaded) {
                try {
                    // Preload libc++_shared so third-party AML plugins linked dynamically against it can dlopen cleanly
                    try {
                        System.loadLibrary("c++_shared");
                    } catch (Throwable ignored) {
                    }
                    Log.i(TAG, "Loading AndroidModLoader (AML)...");
                    System.loadLibrary("AML");
                    sAmlLoaded = true;
                    Log.i(TAG, "AndroidModLoader (AML) loaded");
                } catch (Throwable t) {
                    Log.e(TAG, "Failed to load AML: " + t.getMessage(), t);
                }
            }
        }

        if (monetEnabled && !sMonetLoaded) {
            try {
                ModManager.ensureMonetLibsInstalled(this, false);
                Log.i(TAG, "Loading MonetLoader (LuaJIT 5.1 + libmonetloader)...");
                System.loadLibrary("luajit-5.1");
                System.loadLibrary("monetloader");
                sMonetLoaded = true;
                Log.i(TAG, "MonetLoader loaded");
            } catch (Throwable t) {
                Log.e(TAG, "Failed to load MonetLoader: " + t.getMessage(), t);
            }
        }
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        Log.i(TAG, "**** onCreate");
        installJavaCrashHandler();

        android.content.Intent launchIntent = getIntent();
        if (launchIntent == null || !Objects.equals(launchIntent.getStringExtra("extra_check"), "sampmobile1337")) {
            Log.e(TAG, "Not joined from launcher!");
            supportPauseResume = false;
            super.onCreate(savedInstanceState);
            finish();
            return;
        }

        loadLibraries();

        super.onCreate(savedInstanceState);
        Toast.makeText(this, I18n.tr("SAMP 已启动"), Toast.LENGTH_LONG).show();

        File extDir = getExternalFilesDir(null);
        if (extDir == null) {
            Log.e(TAG, "External files directory is unavailable!");
            Toast.makeText(this, I18n.tr("游戏数据存储目录不可用"), Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        // delete MINFO.BIN (both root and models/MINFO.BIN) and CINFO.BIN if they exist
        File minfo = new File(extDir, "MINFO.BIN");
        File minfoModels = new File(extDir, "models/MINFO.BIN");
        File cinfo = new File(extDir, "CINFO.BIN");

        if (minfo.exists()) {
            Log.i(TAG, "Deleting MINFO.BIN");
            minfo.delete();
        }
        if (minfoModels.exists()) {
            Log.i(TAG, "Deleting models/MINFO.BIN");
            minfoModels.delete();
        }

        if (cinfo.exists()) {
            Log.i(TAG, "Deleting CINFO.BIN");
            cinfo.delete();
        }

        // Single cross-process source of truth: read SAMP/settings.json only (zero SharedPreferences in :game).
        ClientSettings settings = ClientSettings.readGameSnapshot(this);

        ui = new UI(this, settings);
        ui.initializeUI();

        // omp-cef 浏览器层：装上 DecorView，服务端 CreateBrowser 事件到达时可立即建窗
        CefManager.install(this);

        String gameDir = extDir.toString().concat("/");
        Log.i(TAG, "Game directory: " + gameDir);

        try {
            initializeSAMP(ui, gameDir, settings.isOfflineMode());
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, e.getMessage() != null ? e.getMessage() : "UnsatisfiedLinkError in initializeSAMP");
        }

        // Sistema de teclado/mouse PC (porte do mobilador): carrega binds/prefs e
        // detecta teclado/mouse externo para esconder os widgets de toque.
        initKeyboardMouse();

        // Consume one-shot HUD editor flag only after native Settings::initialize() has captured it.
        if (settings.isOpenHudEditOnStart()) {
            ClientSettings.consumeOpenHudEditOnStart(this);
        }

        // Load optional mod frameworks AFTER initializeSAMP (so our crash handler,
        // logger, hooks, and ModLoader are ready) and BEFORE onCreate returns
        // (so surfaceCreated / NVEventAppMain has not started yet).
        loadModLibraries(settings);

        // fullscreen
        if (settings.isFullscreen()) {
            if (Build.VERSION.SDK_INT >= 30) {
                getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            } else if (Build.VERSION.SDK_INT >= 28) {
                getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            }
        }

        applyPreferredRefreshRate(settings);
        installBackInvokedCallback();
    }

    private void installBackInvokedCallback() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return;
        try {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT,
                    () -> {
                        if (inputArbiter.onBackPressed()) {
                            return;
                        }
                        KeyEvent down = new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK);
                        KeyEvent up = new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK);
                        super.onKeyDown(KeyEvent.KEYCODE_BACK, down);
                        super.onKeyUp(KeyEvent.KEYCODE_BACK, up);
                    });
        } catch (Throwable ignored) {
        }
    }

    // Opt out of Android 15/16 default 60Hz game frame-rate intervention by selecting
    // the matching high-refresh Display.Mode and voting frame rate on the game Surface.
    private void applyPreferredRefreshRate(ClientSettings settings) {
        try {
            int fpsLimit = settings != null ? settings.getInt("fps_limit") : 0;
            Display curDisplay = Build.VERSION.SDK_INT >= 30
                    ? getDisplay()
                    : getWindowManager().getDefaultDisplay();
            if (curDisplay == null) return;

            Display.Mode currentMode = curDisplay.getMode();
            Display.Mode[] modes = curDisplay.getSupportedModes();
            float peakHz = currentMode != null ? currentMode.getRefreshRate() : 60.0f;
            int bestModeId = currentMode != null ? currentMode.getModeId() : 0;

            if (currentMode != null && modes != null) {
                for (Display.Mode m : modes) {
                    if (m.getPhysicalWidth() == currentMode.getPhysicalWidth()
                            && m.getPhysicalHeight() == currentMode.getPhysicalHeight()) {
                        float hz = m.getRefreshRate();
                        if (fpsLimit > 0 && fpsLimit <= 240) {
                            if (hz >= fpsLimit - 0.5f && (bestModeId == currentMode.getModeId() || hz < peakHz)) {
                                peakHz = hz;
                                bestModeId = m.getModeId();
                            }
                        } else if (hz > peakHz) {
                            peakHz = hz;
                            bestModeId = m.getModeId();
                        }
                    }
                }
                if (bestModeId != 0 && bestModeId != currentMode.getModeId()) {
                    WindowManager.LayoutParams lp = getWindow().getAttributes();
                    lp.preferredDisplayModeId = bestModeId;
                    getWindow().setAttributes(lp);
                }
            }

            final float targetHz = (fpsLimit >= 30 && fpsLimit <= 240) ? (float) fpsLimit : peakHz;
            if (Build.VERSION.SDK_INT >= 30 && targetHz > 0.0f) {
                View mainView = GetMainView();
                if (mainView instanceof SurfaceView) {
                    SurfaceHolder holder = ((SurfaceView) mainView).getHolder();
                    applySurfaceFrameRate(holder.getSurface(), targetHz);
                    holder.addCallback(new SurfaceHolder.Callback() {
                        @Override
                        public void surfaceCreated(SurfaceHolder h) {
                            applySurfaceFrameRate(h.getSurface(), targetHz);
                        }

                        @Override
                        public void surfaceChanged(SurfaceHolder h, int fmt, int w, int ht) {
                            applySurfaceFrameRate(h.getSurface(), targetHz);
                        }

                        @Override
                        public void surfaceDestroyed(SurfaceHolder h) {
                        }
                    });
                }
            }
        } catch (Throwable ignored) {
        }
    }

    private static void applySurfaceFrameRate(Surface surface, float fps) {
        if (surface == null || !surface.isValid() || fps <= 0.0f) return;
        try {
            if (Build.VERSION.SDK_INT >= 31) {
                surface.setFrameRate(fps, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                        Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
            } else if (Build.VERSION.SDK_INT >= 30) {
                surface.setFrameRate(fps, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
            }
        } catch (Throwable ignored) {
        }
    }

    private final GameInputArbiter inputArbiter = new GameInputArbiter(new GameInputArbiter.Host() {
        @Override
        public UI getUi() {
            return ui;
        }

        @Override
        public void cancelAllNativeTouches() {
            SAMP.this.cancelAllNativeTouches();
        }

        @Override
        public void dispatchHardwareKeyToNative(int keyCode, boolean isDown) {
            try {
                onHardwareKeyEvent(keyCode, isDown);
            } catch (UnsatisfiedLinkError ignored) {
            }
        }
    });

    /* ---------------------------------------------------------------------
     * Teclado/mouse PC (porte fiel do mobilador)
     *
     * Game/PcInput.cpp guarda o estado das acoes; aqui apenas traduzimos
     * KeyEvent/MotionEvent em PcInputAction, exatamente como o InputHandler
     * do mobilador fazia. O overlay de /mob recebe as teclas sozinho porque
     * e um FrameLayout focavel.
     * ------------------------------------------------------------------- */

    private static final int[] HARDWARE_EXTRA_KEYS = {
            KeyEvent.KEYCODE_F5,
            KeyEvent.KEYCODE_F6,
            KeyEvent.KEYCODE_F7,
            KeyEvent.KEYCODE_F8,
            KeyEvent.KEYCODE_F10
    };

    private android.hardware.input.InputManager mInputManager;
    private final android.hardware.input.InputManager.InputDeviceListener mInputDeviceListener =
            new android.hardware.input.InputManager.InputDeviceListener() {
                @Override
                public void onInputDeviceAdded(int deviceId) {
                    syncExternalInputPresence(true);
                }

                @Override
                public void onInputDeviceRemoved(int deviceId) {
                    syncExternalInputPresence(true);
                }

                @Override
                public void onInputDeviceChanged(int deviceId) {
                    syncExternalInputPresence(true);
                }
            };

    private void initKeyboardMouse() {
        try {
            KeyboardMouseSettings.load(this);
        } catch (Throwable t) {
            Log.e(TAG, "KeyboardMouseSettings.load failed: " + t.getMessage(), t);
        }
        try {
            mInputManager = (android.hardware.input.InputManager) getSystemService(Context.INPUT_SERVICE);
            if (mInputManager != null) {
                mInputManager.registerInputDeviceListener(mInputDeviceListener, null);
            }
        } catch (Throwable t) {
            Log.e(TAG, "InputManager unavailable: " + t.getMessage(), t);
        }
        syncExternalInputPresence(false);
    }

    private boolean hasExternalKeyboardOrMouse() {
        try {
            int[] ids = InputDevice.getDeviceIds();
            if (ids == null) return false;
            for (int id : ids) {
                InputDevice device = InputDevice.getDevice(id);
                if (device == null) continue;
                final int sources = device.getSources();
                // Teclado fisico de verdade: os gpio-keys internos do aparelho
                // respondem como KEYBOARD mas nao sao alfabeticos.
                if ((sources & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD
                        && device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC
                        && (sources & (InputDevice.SOURCE_GAMEPAD | InputDevice.SOURCE_JOYSTICK)) == 0) {
                    return true;
                }
                // Mouse/trackpad externo (nao o touchscreen virtual do sistema).
                if ((sources & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
                        && (sources & InputDevice.SOURCE_TOUCHSCREEN) == 0) {
                    return true;
                }
                if ((sources & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE
                        || (sources & InputDevice.SOURCE_TRACKBALL) == InputDevice.SOURCE_TRACKBALL) {
                    return true;
                }
            }
        } catch (Throwable ignored) {
        }
        return false;
    }

    private void syncExternalInputPresence(boolean notifyNative) {
        final boolean present = hasExternalKeyboardOrMouse();
        mExternalInputPresent = present;
        try {
            KeyboardMouseBridge.nativeSetExternalInputPresent(present);
            KeyboardMouseBridge.nativeSetEnabled(KeyboardMouseSettings.isEnabled() && present);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (notifyNative && present && !mExternalInputWasPresent) {
            runOnUiThread(() -> Toast.makeText(this,
                    "Teclado/mouse conectado — controles PC ativos (/mob)",
                    Toast.LENGTH_SHORT).show());
        }
        mExternalInputWasPresent = present;
    }

    private boolean mExternalInputPresent = false;
    private boolean mExternalInputWasPresent = false;

    /** Traduz uma tecla fisica em PcInputAction. Devolve true se consumiu. */
    private boolean handlePcKeyEvent(KeyEvent event) {
        if (event == null) return false;
        if (KeyboardMouseBridge.isInterfaceVisible()) {
            return false; // overlay de /mob trata as teclas
        }

        final int keyCode = event.getKeyCode();
        final boolean down = event.getAction() == KeyEvent.ACTION_DOWN;
        final boolean repeat = event.getRepeatCount() > 0;

        if (keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN
                || keyCode == KeyEvent.KEYCODE_VOLUME_MUTE) {
            return false;
        }

        // 1) Entrada de texto (chat nativo aberto)
        boolean textEntry;
        try {
            textEntry = KeyboardMouseBridge.nativeIsTextEntryActive();
        } catch (UnsatisfiedLinkError e) {
            textEntry = false;
        }
        if (textEntry) {
            if (!down) return true;
            int mapped = -1;
            int unicode = 0;
            if (keyCode == KeyEvent.KEYCODE_DEL) {
                mapped = 0;
            } else if (keyCode == KeyEvent.KEYCODE_ENTER || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER) {
                mapped = 1;
            } else if (keyCode == KeyEvent.KEYCODE_ESCAPE) {
                mapped = 2;
            } else if (keyCode == KeyEvent.KEYCODE_DPAD_UP) {
                mapped = 3;
            } else if (keyCode == KeyEvent.KEYCODE_DPAD_DOWN) {
                mapped = 4;
            } else if (keyCode == KeyEvent.KEYCODE_TAB) {
                mapped = 5;
            } else {
                unicode = event.getUnicodeChar(event.getMetaState());
                if (unicode == 0) {
                    return true;
                }
            }
            if (repeat && mapped != 0) {
                return true;
            }
            try {
                KeyboardMouseBridge.nativeTextInputKey(mapped, unicode);
            } catch (UnsatisfiedLinkError ignored) {
            }
            return true;
        }

        // 2) Acoes mapeadas em /mob
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (down && !repeat) {
                try {
                    KeyboardMouseBridge.nativeHandleEscape();
                } catch (UnsatisfiedLinkError ignored) {
                }
                return true;
            }
            return true;
        }

        if (!down) {
            final Integer held = ACTIONS_HELD.remove(keyCode);
            if (held != null) {
                try {
                    KeyboardMouseBridge.nativeSetAction(held, false);
                } catch (UnsatisfiedLinkError ignored) {
                }
                return true;
            }
        }

        if (keyCode >= KeyEvent.KEYCODE_1 && keyCode <= KeyEvent.KEYCODE_9
                && KeyboardMouseSettings.isNumberSlotKeysEnabled()) {
            if (down && !repeat) {
                try {
                    KeyboardMouseBridge.nativeSelectWeaponSlot(keyCode - KeyEvent.KEYCODE_1);
                } catch (UnsatisfiedLinkError ignored) {
                }
            }
            return true;
        }

        final int action = KeyboardMouseSettings.actionForKey(keyCode);
        if (action >= 0) {
            if (repeat) return true;
            if (down) {
                ACTIONS_HELD.put(keyCode, action);
            }
            try {
                KeyboardMouseBridge.nativeSetAction(action, down);
            } catch (UnsatisfiedLinkError ignored) {
            }
            return true;
        }

        if (keyCode == KeyEvent.KEYCODE_ESCAPE && down && !repeat) {
            try {
                KeyboardMouseBridge.nativeHandleEscape();
            } catch (UnsatisfiedLinkError ignored) {
            }
            return true;
        }

        for (int extra : HARDWARE_EXTRA_KEYS) {
            if (extra == keyCode) {
                if (!repeat) {
                    try {
                        onHardwareKeyEvent(keyCode, down);
                    } catch (UnsatisfiedLinkError ignored) {
                    }
                }
                return true;
            }
        }

        return false;
    }

    private final java.util.HashMap<Integer, Integer> ACTIONS_HELD = new java.util.HashMap<>();

    private void releasePcActions() {
        for (Integer action : ACTIONS_HELD.values()) {
            try {
                KeyboardMouseBridge.nativeSetAction(action, false);
            } catch (UnsatisfiedLinkError ignored) {
            }
        }
        ACTIONS_HELD.clear();
        try {
            KeyboardMouseBridge.nativeSetExternalInputPresent(mExternalInputPresent);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    /** Chamado pelo JNI a partir de /mob (Game/UI/SAMPWidgets/Chat.cpp). */
    public void showKeyboardMouseSettings() {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            // Solta a captura do ponteiro para os botoes do overlay receberem toque.
            updatePointerCapture(false);
            releaseMouseButtons();
            KeyboardMouseSettings.show(this);
        });
    }

    /** Devolve o foco ao jogo quando o overlay de /mob fecha. */
    public void restoreGameFocus() {
        KeyboardMouseBridge.setInterfaceVisible(false);
        try {
            View decor = getWindow() != null ? getWindow().getDecorView() : null;
            if (decor != null) {
                decor.requestFocus();
            }
            View mainView = GetMainView();
            if (mainView != null) {
                mainView.requestFocus();
            }
        } catch (Throwable ignored) {
        }
        releasePcActions();
    }

    @Override
    public void onBackPressed() {
        if (inputArbiter.onBackPressed()) {
            return;
        }
        super.onBackPressed();
    }

    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (handlePcKeyEvent(event)) {
            return true;
        }
        if (inputArbiter.onDispatchKeyEvent(event)) {
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        if (inputArbiter.onKeyDown(keyCode, event)) {
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, android.view.KeyEvent event) {
        if (inputArbiter.onKeyUp(keyCode, event)) {
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    /* ---------------------------------------------------------------------
     * Physical mouse: fiel ao handler do mobilador
     * (rockstargames/oswrapper/view/InputHandler.java).
     *
     * O Android entrega o mouse por dois caminhos: eventos de hover/motion
     * enquanto o cursor anda pela tela e, depois de requestPointerCapture(),
     * SOMENTE onCapturedPointerEvent() com eixos relativos. Os dois precisam
     * chegar na camera nativa, senao a camera trava assim que a captura liga.
     * ------------------------------------------------------------------- */

    private static final int[] TRACKED_MOUSE_BUTTONS = {
            MotionEvent.BUTTON_PRIMARY,
            MotionEvent.BUTTON_SECONDARY,
            MotionEvent.BUTTON_TERTIARY,
            MotionEvent.BUTTON_BACK,
            MotionEvent.BUTTON_FORWARD
    };

    private int mPressedMouseButtons = 0;
    private float mLastMouseX = Float.NaN;
    private float mLastMouseY = Float.NaN;
    private boolean mPointerCaptured = false;
    private boolean mCapturedPointerListenerInstalled = false;

    private static boolean isMouseSource(int source) {
        if ((source & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN
                || (source & InputDevice.SOURCE_STYLUS) == InputDevice.SOURCE_STYLUS) {
            return false;
        }
        return (source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
                || (source & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE
                || (source & InputDevice.SOURCE_TOUCHPAD) == InputDevice.SOURCE_TOUCHPAD
                || (source & InputDevice.SOURCE_TRACKBALL) == InputDevice.SOURCE_TRACKBALL;
    }

    private static boolean isCapturedMouseCandidate(int source) {
        if (isMouseSource(source)) {
            return true;
        }
        return (source & InputDevice.SOURCE_TOUCHSCREEN) == 0
                && (source & InputDevice.SOURCE_STYLUS) == 0
                && (source & InputDevice.SOURCE_GAMEPAD) == 0
                && (source & InputDevice.SOURCE_JOYSTICK) == 0;
    }

    private static int mouseButtonToNativeIndex(int buttonMask) {
        if (buttonMask == MotionEvent.BUTTON_PRIMARY) return 1;
        if (buttonMask == MotionEvent.BUTTON_SECONDARY) return 2;
        if (buttonMask == MotionEvent.BUTTON_TERTIARY) return 3;
        if (buttonMask == MotionEvent.BUTTON_BACK) return 4;
        if (buttonMask == MotionEvent.BUTTON_FORWARD) return 5;
        return 0;
    }

    private static boolean isMouseButtonAction(int action) {
        return action == MotionEvent.ACTION_BUTTON_PRESS
                || action == MotionEvent.ACTION_BUTTON_RELEASE
                || action == MotionEvent.ACTION_DOWN
                || action == MotionEvent.ACTION_UP
                || action == MotionEvent.ACTION_POINTER_DOWN
                || action == MotionEvent.ACTION_POINTER_UP;
    }

    private static boolean isMouseButtonDownAction(int action) {
        return action == MotionEvent.ACTION_BUTTON_PRESS
                || action == MotionEvent.ACTION_DOWN
                || action == MotionEvent.ACTION_POINTER_DOWN;
    }

    private int resolveMouseButton(MotionEvent event, int action) {
        int button = event.getActionButton();
        if (button != 0) {
            return button;
        }
        final int state = event.getButtonState();
        if (isMouseButtonDownAction(action)) {
            final int newlyPressed = state & ~mPressedMouseButtons;
            for (int mask : TRACKED_MOUSE_BUTTONS) {
                if ((newlyPressed & mask) != 0) return mask;
            }
            for (int mask : TRACKED_MOUSE_BUTTONS) {
                if ((state & mask) != 0) return mask;
            }
            return MotionEvent.BUTTON_PRIMARY;
        } else {
            final int releasedButton = mPressedMouseButtons & ~state;
            for (int mask : TRACKED_MOUSE_BUTTONS) {
                if ((releasedButton & mask) != 0) return mask;
            }
            for (int mask : TRACKED_MOUSE_BUTTONS) {
                if ((mPressedMouseButtons & mask) != 0) return mask;
            }
            return MotionEvent.BUTTON_PRIMARY;
        }
    }

    private boolean updatePressedMouseButtons(int buttonMask, boolean down) {
        if (mouseButtonToNativeIndex(buttonMask) < 1) {
            return false;
        }
        final boolean currentlyPressed = (mPressedMouseButtons & buttonMask) != 0;
        if (currentlyPressed == down) {
            return false;
        }
        if (down) {
            mPressedMouseButtons |= buttonMask;
        } else {
            mPressedMouseButtons &= ~buttonMask;
        }
        return true;
    }

    private void sendMouseButton(int buttonMask, boolean down) {
        if (!updatePressedMouseButtons(buttonMask, down)) {
            return;
        }
        final int button = mouseButtonToNativeIndex(buttonMask);
        try {
            if (down) {
                KeyboardMouseBridge.nativeMouseButtonDown(button,
                        KeyboardMouseSettings.actionForMouseButton(buttonMask));
            } else {
                KeyboardMouseBridge.nativeMouseButtonUp(button);
            }
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    private void releaseMouseButtons() {
        final int buttons = mPressedMouseButtons;
        mPressedMouseButtons = 0;
        for (int mask : TRACKED_MOUSE_BUTTONS) {
            if ((buttons & mask) != 0) {
                try {
                    KeyboardMouseBridge.nativeMouseButtonUp(mouseButtonToNativeIndex(mask));
                } catch (UnsatisfiedLinkError ignored) {
                }
            }
        }
    }

    private void updatePointerCapture(boolean wantCapture) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O || getWindow() == null) {
            return;
        }
        final View decor = getWindow().getDecorView();
        if (decor == null) {
            return;
        }
        try {
            installCapturedPointerListener(decor);
            if (wantCapture && !mPointerCaptured) {
                decor.requestPointerCapture();
                mPointerCaptured = true;
            } else if (!wantCapture && mPointerCaptured) {
                decor.releasePointerCapture();
                mPointerCaptured = false;
            }
            try {
                KeyboardMouseBridge.nativeSetCapture(mPointerCaptured);
            } catch (UnsatisfiedLinkError ignored) {
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                decor.setPointerIcon(android.view.PointerIcon.getSystemIcon(this,
                        mPointerCaptured ? android.view.PointerIcon.TYPE_NULL
                                : android.view.PointerIcon.TYPE_ARROW));
            }
        } catch (Throwable ignored) {
        }
        mLastMouseX = Float.NaN;
        mLastMouseY = Float.NaN;
    }

    @Override
    public void onPointerCaptureChanged(boolean hasCapture) {
        mPointerCaptured = hasCapture;
        mLastMouseX = Float.NaN;
        mLastMouseY = Float.NaN;
        try {
            KeyboardMouseBridge.nativeSetCapture(hasCapture);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (!hasCapture) {
            releaseMouseButtons();
        }
    }

    /**
     * While the pointer is captured Android stops sending hover/motion events to
     * the activity and delivers everything to the captured view, with relative
     * axes. Activity has no onCapturedPointerEvent callback, so the decor view
     * gets a View.OnCapturedPointerListener (API 26+). Without this the camera
     * never receives a single delta after the capture starts.
     */
    private void installCapturedPointerListener(View decor) {
        if (mCapturedPointerListenerInstalled || Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }
        decor.setOnCapturedPointerListener(new View.OnCapturedPointerListener() {
            @Override
            public boolean onCapturedPointer(View view, MotionEvent event) {
                if (event == null || !isCapturedMouseCandidate(event.getSource())) {
                    return false;
                }
                return handleMouseMotion(event, true);
            }
        });
        mCapturedPointerListenerInstalled = true;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        // Intercepta o mouse antes da view do jogo para o evento nunca ser engolido.
        if (event != null && isMouseSource(event.getSource())) {
            return handleMouseMotion(event, false);
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (event != null && isMouseSource(event.getSource())) {
            return handleMouseMotion(event, false);
        }
        return super.onGenericMotionEvent(event);
    }

    private boolean isPcInterfaceBusy() {
        if (KeyboardMouseBridge.isInterfaceVisible()) {
            return true;
        }
        try {
            return KeyboardMouseBridge.nativeIsInterfaceActive()
                    || KeyboardMouseBridge.nativeIsTextEntryActive();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    private boolean handleMouseMotion(MotionEvent event, boolean capturedEvent) {
        // Chat / IME / Java dialog owns the mouse: do not spin the camera.
        if (ui != null && ui.isInputFocused()) {
            return true;
        }
        // Overlay de /mob, chat aberto ou placar: o mouse pertence a interface.
        if (isPcInterfaceBusy()) {
            return true;
        }

        final int action = event.getActionMasked();
        final int source = event.getSource();

        if (action == MotionEvent.ACTION_HOVER_ENTER || action == MotionEvent.ACTION_HOVER_EXIT) {
            mLastMouseX = Float.NaN;
            mLastMouseY = Float.NaN;
            return true;
        }

        if (isMouseButtonAction(action)) {
            sendMouseButton(resolveMouseButton(event, action), isMouseButtonDownAction(action));
            if (!capturedEvent && !mPointerCaptured) {
                updatePointerCapture(true);
            }
            return true;
        }

        if (action == MotionEvent.ACTION_CANCEL) {
            releaseMouseButtons();
            mLastMouseX = Float.NaN;
            mLastMouseY = Float.NaN;
            return true;
        }

        if (action == MotionEvent.ACTION_SCROLL) {
            final float wheel = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
            if (wheel != 0.0f) {
                try {
                    KeyboardMouseBridge.nativeMouseWheel(wheel > 0.0f ? 1 : -1);
                } catch (UnsatisfiedLinkError ignored) {
                }
            }
            return true;
        }

        if (action == MotionEvent.ACTION_HOVER_MOVE || action == MotionEvent.ACTION_MOVE) {
            float dx = 0.0f;
            float dy = 0.0f;
            final boolean hasRelativeSource =
                    (source & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE;

            if (capturedEvent || hasRelativeSource) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    dx = event.getAxisValue(MotionEvent.AXIS_RELATIVE_X);
                    dy = event.getAxisValue(MotionEvent.AXIS_RELATIVE_Y);
                }
                if (dx == 0.0f && dy == 0.0f) {
                    final float rx = event.getX();
                    final float ry = event.getY();
                    if (!capturedEvent && !Float.isNaN(mLastMouseX)) {
                        dx = rx - mLastMouseX;
                        dy = ry - mLastMouseY;
                    }
                    mLastMouseX = rx;
                    mLastMouseY = ry;
                }
            } else {
                final float x = event.getX();
                final float y = event.getY();
                if (!Float.isNaN(mLastMouseX)) {
                    dx = x - mLastMouseX;
                    dy = y - mLastMouseY;
                }
                mLastMouseX = x;
                mLastMouseY = y;
            }

            if (!Float.isFinite(dx)) dx = 0.0f;
            if (!Float.isFinite(dy)) dy = 0.0f;

            if (dx != 0.0f || dy != 0.0f) {
                try {
                    KeyboardMouseBridge.nativeMouseMove(
                            KeyboardMouseSettings.scaleMouseX(dx),
                            KeyboardMouseSettings.scaleMouseY(dy));
                } catch (UnsatisfiedLinkError ignored) {
                }
                if (!capturedEvent && !mPointerCaptured) {
                    updatePointerCapture(true);
                }
            }
            return true;
        }

        return true;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (inputArbiter.onActivityTouchEvent(event)) {
            return true;
        }

        final int action = event.getActionMasked();
        final int numEvents = event.getPointerCount();
        final int actionIdx = event.getActionIndex();
        final int pointerId = (actionIdx >= 0 && actionIdx < numEvents) ? event.getPointerId(actionIdx) : -1;

        int x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, x4 = 0, y4 = 0;
        int presentMask = 0;

        for (int i = 0; i < numEvents; i++) {
            int pid = event.getPointerId(i);

            if (pid == 0) {
                x1 = (int) event.getX(i);
                y1 = (int) event.getY(i);
                presentMask |= (1 << 0);
            } else if (pid == 1) {
                x2 = (int) event.getX(i);
                y2 = (int) event.getY(i);
                presentMask |= (1 << 1);
            } else if (pid == 2) {
                x3 = (int) event.getX(i);
                y3 = (int) event.getY(i);
                presentMask |= (1 << 2);
            } else if (pid == 3) {
                x4 = (int) event.getX(i);
                y4 = (int) event.getY(i);
                presentMask |= (1 << 3);
            }
        }

        int packedPointer = (pointerId & 0xFF) | (presentMask << 8);

        try {
            multiTouchEvent4Ex(action, packedPointer, x1, y1, x2, y2, x3, y3, x4, y4);
        } catch (UnsatisfiedLinkError e) {
            e.printStackTrace();
        }
        // Do NOT chain to super.onTouchEvent(event): NvEventQueueActivity.onTouchEvent
        // pushes a duplicate legacy 2-finger multiTouchEvent() into libGTASA.so's native
        // NVEvent queue, which conflicts with our 4-finger pipeline and third-party AML
        // touch plugins (e.g. JPatch MultiTouch4).
        return true;
    }

    public void cancelAllNativeTouches() {
        try {
            multiTouchEvent4Ex(MotionEvent.ACTION_CANCEL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    @Override
    protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
    }

    @Override
    public void onStart() {
        Log.i(TAG, "**** onStart");
        super.onStart();
    }

    @Override
    public void onRestart() {
        Log.i(TAG, "**** onRestart");
        super.onRestart();
    }

    @Override
    public void onResume() {
        Log.i(TAG, "**** onResume");
        super.onResume();

        if (ui != null) {
            ui.onResumeEvent();
        }
        syncExternalInputPresence(false);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus) {
            inputArbiter.onWindowFocusLost();
            releaseMouseButtons();
            mLastMouseX = Float.NaN;
            mLastMouseY = Float.NaN;
        }
    }

    @Override
    public void onPause() {
        Log.i(TAG, "**** onPause");
        inputArbiter.onActivityPause();
        releaseMouseButtons();
        releasePcActions();
        try {
            onMouseReset();
        } catch (UnsatisfiedLinkError ignored) {
        }
        mLastMouseX = Float.NaN;
        mLastMouseY = Float.NaN;
        updatePointerCapture(false);
        super.onPause();
    }

    @Override
    public void onStop() {
        Log.i(TAG, "**** onStop");
        super.onStop();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "**** onDestroy");
        if (ui != null) {
            ui.destroy();
        }
        // 拆除全部 omp-cef WebView，避免跨 Activity 生命周期泄漏
        CefManager.shutdown();
        // Reaching onDestroy means an orderly teardown (intentional exit or
        // user swipe-away), so clear the session-alive marker before dying;
        // crash / LMK / vendor kill never get here and leave the marker behind.
        try {
            File extDir = getExternalFilesDir(null);
            if (extDir != null) {
                new File(extDir, "logcat/session_alive").delete();
            }
        } catch (Throwable ignored) {
        }
        // Kill the isolated :game process BEFORE NvEventQueueActivity.onDestroy() invokes
        // quitAndWait() (10s Main-Looper wait + OS_ApplicationEvent UAF) & systemCleanup().
        android.os.Process.killProcess(android.os.Process.myPid());
        super.onDestroy();
    }
}
