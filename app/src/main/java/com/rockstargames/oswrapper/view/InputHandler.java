package com.rockstargames.oswrapper.view;

import android.graphics.PointF;
import android.os.Build;
import android.os.SystemClock;
import android.util.Log;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.View;

import com.gta.game.input.KeyboardMouseBridge;
import com.gta.game.input.KeyboardMouseSettings;
import com.rockstargames.oswrapper.GameActivityBase;
import com.rockstargames.oswrapper.GameNative;
import com.rockstargames.oswrapper.GameThread;
import com.rockstargames.oswrapper.UtilsKt;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Iterator;
import java.util.List;

import kotlin.collections.CollectionsKt;
import kotlin.jvm.internal.Intrinsics;

public class InputHandler implements GameViewHandler {

    public static InputHandler INSTANCE = new InputHandler();

    private static final String TAG = "GameView.InputHandler";
    private static final long CHAT_OPEN_GRACE_MS = 750L;

    // Xbox 360 button mappings
    private static final int OSX360_A         = 0;
    private static final int OSX360_B         = 1;
    private static final int OSX360_X         = 2;
    private static final int OSX360_Y         = 3;
    private static final int OSX360_START     = 4;
    private static final int OSX360_BACK      = 5;
    private static final int OSX360_L1        = 6;
    private static final int OSX360_R1        = 7;
    private static final int OSX360_DPADUP    = 8;
    private static final int OSX360_DPADDOWN  = 9;
    private static final int OSX360_DPADLEFT  = 10;
    private static final int OSX360_DPADRIGHT = 11;
    private static final int OSX360_L3        = 12;
    private static final int OSX360_R3        = 13;

    // OSX Platform buttons
    private static final int OSXP_BACK    = 14;
    private static final int OSXP_GP_MENU = 15;
    private static final int OSXP_MENU    = 12;
    private static final int OSXP_SEARCH  = 13;

    private static final int[] TRACKED_MOUSE_BUTTONS = {
            MotionEvent.BUTTON_PRIMARY,
            MotionEvent.BUTTON_SECONDARY,
            MotionEvent.BUTTON_TERTIARY,
            MotionEvent.BUTTON_BACK,
            MotionEvent.BUTTON_FORWARD
    };

    private boolean dpadUpPressed;
    private boolean dpadDownPressed;
    private boolean dpadLeftPressed;
    private boolean dpadRightPressed;

    private final SparseArray<PointF> touches = new SparseArray<>();
    private View gameView;
    private boolean interfaceVisible;
    private boolean managedInterfaceVisible;
    private boolean manualInterfaceMode;
    private boolean nativeTextEntryMode;
    private boolean nativeTabMode;
    private long pendingTextEntryUntilMs;
    private int pendingDeadChar;
    private boolean pointerCaptured;
    private float lastMouseX;
    private float lastMouseY;
    private boolean hasLastMousePosition;
    private int pressedMouseButtons;
    private boolean mouseTouchActive;
    private boolean closeNativeInterfaceOnNextInput;
    private int mouseLogCounter;
    private int touchLogCounter;
    private int mouseEventCount;

    private InputHandler() {}

    // -----------------------------------------------------------------------
    // Controller count (replaces Kotlin Companion)
    // -----------------------------------------------------------------------

    public final int getControllerCount() {
        int[] deviceIds = InputDevice.getDeviceIds();
        Intrinsics.checkNotNullExpressionValue(deviceIds, "getDeviceIds(...)");
        ArrayList<InputDevice> arrayList = new ArrayList<>(deviceIds.length);
        for (int i : deviceIds) {
            arrayList.add(InputDevice.getDevice(i));
        }
        List<InputDevice> listFilterNotNull = CollectionsKt.filterNotNull(arrayList);
        if ((listFilterNotNull instanceof Collection) && listFilterNotNull.isEmpty()) {
            return 0;
        }
        Iterator<InputDevice> it = listFilterNotNull.iterator();
        int i2 = 0;
        while (it.hasNext()) {
            if (((it.next().getSources() & 16778257) != 0) && (i2 = i2 + 1) < 0) {
                CollectionsKt.throwCountOverflow();
            }
        }
        return i2;
    }

    // -----------------------------------------------------------------------
    // Key mapping
    // -----------------------------------------------------------------------

    private final int toGamepadButton(int keyCode) {
        if (keyCode == 4) return 14;
        if (keyCode == 82) return 15;
        if (keyCode == 110) return 4;
        if (keyCode == 96) return 0;
        if (keyCode == 97) return 1;
        if (keyCode == 99) return 2;
        if (keyCode == 100) return 3;
        if (keyCode == 102) return 6;
        if (keyCode == 103) return 7;
        switch (keyCode) {
            case 106:
                return 12;
            case 107:
                return 13;
            case 108:
                return 4;
            default:
                return -1;
        }
    }

    // -----------------------------------------------------------------------
    // Back button
    // -----------------------------------------------------------------------

    private void onBackPressed() {
        Log.i(TAG, "[!!] onBackPressed");
        GameThread.INSTANCE.onBackButtonPressed();
    }

    // -----------------------------------------------------------------------
    // Key events
    // -----------------------------------------------------------------------

    public boolean onKeyDown(int keyCode, KeyEvent event) {
        synchronizeNativeInterfaceState();
        if (KeyboardMouseSettings.captureKey(event)) {
            return true;
        }
        int source = event != null ? event.getSource() : 0;
        boolean isGamepad = (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
        if (!isGamepad) {
            KeyboardMouseSettings.setKeyState(keyCode, true);
        }
        if (!isGamepad && event != null && KeyboardMouseSettings.isEnabled()) {
            if (keyCode == KeyEvent.KEYCODE_ESCAPE) {
                pendingDeadChar = 0;
                pendingTextEntryUntilMs = 0L;
                if (managedInterfaceVisible) {
                    KeyboardMouseSettings.close();
                    return true;
                }
                if (KeyboardMouseBridge.nativeIsTextEntryActive() || nativeTextEntryMode) {
                    nativeTextEntryMode = false;
                    GameThread.INSTANCE.onPcTextInputKey(2, 0);
                    updateInterfaceVisibility();
                } else {
                    toggleInterfaceMode();
                }
                return true;
            }

            if (isTextEntryActiveOrPending()) {
                int textKey = textKeyFor(keyCode);
                if (textKey >= 0) {
                    pendingDeadChar = 0;
                    if (textKey == 1) { // Enter ends chat input
                        pendingTextEntryUntilMs = 0L;
                    }
                    GameThread.INSTANCE.onPcTextInputKey(textKey, 0);
                    return true;
                }
                int unicode = event.getUnicodeChar();
                if ((unicode & KeyCharacterMap.COMBINING_ACCENT) != 0) {
                    pendingDeadChar = unicode & KeyCharacterMap.COMBINING_ACCENT_MASK;
                    return true;
                }
                if (pendingDeadChar != 0 && unicode != 0) {
                    int combined = KeyEvent.getDeadChar(pendingDeadChar, unicode);
                    pendingDeadChar = 0;
                    if (combined != 0) {
                        unicode = combined;
                    }
                } else {
                    pendingDeadChar = 0;
                }
                if (unicode != 0) {
                    GameThread.INSTANCE.onPcTextInputKey(-1, unicode);
                    return true;
                }
                return true;
            }

            int mappedAction = KeyboardMouseSettings.actionForKey(keyCode);
            boolean nativeInterfaceActive = KeyboardMouseBridge.nativeIsInterfaceActive();
            boolean hardInterface = isHardInterfaceBlocking();
            if (hardInterface && mappedAction != KeyboardMouseSettings.TAB &&
                    (!nativeInterfaceActive || mappedAction != KeyboardMouseSettings.ENTER)) {
                return true;
            }

            if (mappedAction == KeyboardMouseSettings.CHAT &&
                    !KeyboardMouseBridge.nativeIsTextEntryActive()) {
                nativeTextEntryMode = true;
                pendingTextEntryUntilMs = SystemClock.uptimeMillis() + CHAT_OPEN_GRACE_MS;
                pendingDeadChar = 0;
                updateInterfaceVisibility();
                GameThread.INSTANCE.onPcOpenChat();
                return true;
            }

            // Direct PC weapon slot switching (1..9 -> slots 0..8) if unbound to another action
            if (mappedAction < 0 && !hardInterface && event.getRepeatCount() == 0 &&
                    KeyboardMouseSettings.isNumberSlotKeysEnabled() &&
                    keyCode >= KeyEvent.KEYCODE_1 && keyCode <= KeyEvent.KEYCODE_9) {
                int slot = keyCode - KeyEvent.KEYCODE_1;
                GameThread.INSTANCE.onPcSelectWeaponSlot(slot);
                return true;
            }

            if (mappedAction >= 0) {
                if (event.getRepeatCount() == 0) {
                    if (mappedAction == KeyboardMouseSettings.TAB) {
                        nativeTabMode = true;
                        updateInterfaceVisibility();
                    }
                    GameThread.INSTANCE.onPcInputAction(mappedAction, true);
                }
                return true;
            }
        }
        if (keyCode == KeyEvent.KEYCODE_BACK && !isGamepad) {
            return true;
        }
        int button = toGamepadButton(keyCode);
        if (button == -1) return false;
        GameThread.INSTANCE.onGamepadButtonDown(0, button);
        return true;
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {
        synchronizeNativeInterfaceState();
        int source = event != null ? event.getSource() : 0;
        boolean isGamepad = (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
        if (event != null && !isGamepad) {
            KeyboardMouseSettings.setKeyState(keyCode, false);
            int action = KeyboardMouseSettings.actionForKey(keyCode);
            if (action >= 0 && KeyboardMouseSettings.isEnabled()) {
                if (action == KeyboardMouseSettings.TAB) {
                    nativeTabMode = false;
                    updateInterfaceVisibility();
                }
                GameThread.INSTANCE.onPcInputAction(action, false);
                return true;
            }
            if (KeyboardMouseSettings.isEnabled() && isTextEntryActiveOrPending()) {
                return true;
            }
        }
        if (isHardInterfaceBlocking() && !isGamepad) {
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_BACK && !isGamepad) {
            onBackPressed();
            return true;
        }
        int button = toGamepadButton(keyCode);
        if (button == -1) return false;
        GameThread.INSTANCE.onGamepadButtonUp(0, button);
        return true;
    }

    // -----------------------------------------------------------------------
    // Gamepad axes / motion
    // -----------------------------------------------------------------------

    public final boolean onGenericMotionEvent(MotionEvent event) {
        if (event == null) {
            return false;
        }
        synchronizeNativeInterfaceState();
        if (isMouseSource(event.getSource())) {
            return onMouseMotionEvent(event, false);
        }
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == 0 &&
                (event.getSource() & InputDevice.SOURCE_GAMEPAD) == 0) {
            return false;
        }
        float axisValue = event.getAxisValue(0);
        float axisValue2 = event.getAxisValue(1);
        float axisValue3 = event.getAxisValue(11);
        float axisValue4 = event.getAxisValue(14);
        float axisValue5 = event.getAxisValue(15);
        float axisValue6 = event.getAxisValue(16);
        if (axisValue5 > 0.2f) {
            GameThread.INSTANCE.onGamepadButtonDown(0, 11);
            this.dpadRightPressed = true;
        } else if (axisValue5 < -0.2f) {
            GameThread.INSTANCE.onGamepadButtonDown(0, 10);
            this.dpadLeftPressed = true;
        } else {
            if (this.dpadLeftPressed) {
                GameThread.INSTANCE.onGamepadButtonUp(0, 10);
            }
            if (this.dpadRightPressed) {
                GameThread.INSTANCE.onGamepadButtonUp(0, 11);
            }
            this.dpadLeftPressed = false;
            this.dpadRightPressed = false;
        }
        if (axisValue6 < -0.2f) {
            GameThread.INSTANCE.onGamepadButtonDown(0, 8);
            this.dpadUpPressed = true;
        } else if (axisValue6 > 0.2f) {
            GameThread.INSTANCE.onGamepadButtonDown(0, 9);
            this.dpadDownPressed = true;
        } else {
            if (this.dpadUpPressed) {
                GameThread.INSTANCE.onGamepadButtonUp(0, 8);
            }
            if (this.dpadDownPressed) {
                GameThread.INSTANCE.onGamepadButtonUp(0, 9);
            }
            this.dpadUpPressed = false;
            this.dpadDownPressed = false;
        }
        float fMax3 = UtilsKt.max3(event.getAxisValue(18), event.getAxisValue(19), event.getAxisValue(22));
        GameThread.INSTANCE.onGamepadAxesChanged(
                0, axisValue, axisValue2, axisValue3, axisValue4,
                Math.max(event.getAxisValue(17), event.getAxisValue(23)), fMax3);
        return true;
    }

    // -----------------------------------------------------------------------
    // Touch events
    // -----------------------------------------------------------------------

    public final boolean onTouchEvent(MotionEvent event) {
        Intrinsics.checkNotNullParameter(event, "event");
        synchronizeNativeInterfaceState();
        if (isMouseSource(event.getSource())) {
            return onMouseMotionEvent(event, false);
        }
        if (KeyboardMouseSettings.isEnabled() && !isHardInterfaceBlocking()) {
            if (touchLogCounter++ % 60 == 0) {
                Log.d(TAG, "[TOUCH-MOUSE] blocked reason=PC gameplay mode");
            }
            releaseTouchPointers();
            return true;
        }
        int actionIndex = event.getActionIndex();
        int pointerId = event.getPointerId(actionIndex);
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                float x = event.getX(actionIndex);
                float y = event.getY(actionIndex);
                PointF pointF = new PointF(x, y);
                this.touches.put(pointerId, pointF);
                GameThread.INSTANCE.onTouchStart(pointerId, x, y);
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_OUTSIDE:
            case MotionEvent.ACTION_POINTER_UP: {
                PointF pointF2 = this.touches.get(pointerId);
                if (pointF2 != null) {
                    GameThread.INSTANCE.onTouchEnd(pointerId, pointF2.x, pointF2.y);
                }
                this.touches.remove(pointerId);
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < event.getPointerCount(); i++) {
                    float x2 = event.getX(i);
                    float y2 = event.getY(i);
                    PointF pointF3 = new PointF(x2, y2);
                    int pointerId2 = event.getPointerId(i);
                    this.touches.put(pointerId2, pointF3);
                    GameThread.INSTANCE.onTouchMove(pointerId2, x2, y2);
                }
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                for (int i = 0; i < this.touches.size(); i++) {
                    int iKeyAt = this.touches.keyAt(i);
                    PointF pointF4 = this.touches.get(iKeyAt);
                    if (pointF4 != null) {
                        GameThread.INSTANCE.onTouchEnd(iKeyAt, pointF4.x, pointF4.y);
                    }
                }
                this.touches.clear();
                return true;
            }
            default:
                return false;
        }
    }

    // -----------------------------------------------------------------------
    // GameViewHandler
    // -----------------------------------------------------------------------

    @Override
    public void setup(GameActivityBase activity) {
        KeyboardMouseSettings.load(activity);
        GameThread.INSTANCE.onPcInputEnabled(KeyboardMouseSettings.isEnabled());
        updatePointerCapture();
    }

    @Override
    public void onPause() {
        nativeTabMode = false;
        nativeTextEntryMode = false;
        pendingTextEntryUntilMs = 0L;
        pendingDeadChar = 0;
        releasePcKeys();
        releaseMouseButtons();
        releaseTouchPointers();
        updateInterfaceVisibility();
        if (pointerCaptured && gameView != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            gameView.releasePointerCapture();
            pointerCaptured = false;
        }
    }

    @Override
    public void onResume() {
        KeyboardMouseSettings.syncNativeConfig();
        updateInterfaceVisibility();
        updatePointerCapture();
    }

    public void setGameView(View view) {
        gameView = view;
        updatePointerCapture();
    }

    public void restoreGameFocus() {
        if (gameView == null) {
            return;
        }
        gameView.post(() -> {
            if (gameView != null) {
                gameView.requestFocus();
                synchronizeNativeInterfaceState();
                updatePointerCapture();
            }
        });
    }

    public void onPointerCaptureChanged(boolean captured) {
        if (pointerCaptured == captured) {
            return;
        }
        pointerCaptured = captured;
        hasLastMousePosition = false;
        GameNative.implOnCaptureStatusChanged(captured);
        if (gameView != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            gameView.setPointerIcon(PointerIcon.getSystemIcon(gameView.getContext(),
                    shouldHideCursor() ? PointerIcon.TYPE_NULL : PointerIcon.TYPE_ARROW));
        }
        if (!captured) {
            releaseMouseButtons();
        }
    }

    public boolean onCapturedPointerEvent(MotionEvent event) {
        if (event == null || !isCapturedMouseCandidate(event.getSource())) {
            return false;
        }
        synchronizeNativeInterfaceState();
        return onMouseMotionEvent(event, true);
    }

    public void setInterfaceVisible(boolean visible) {
        managedInterfaceVisible = visible;
        updateInterfaceVisibility();
    }

    public void onPcInputEnabledChanged() {
        if (!KeyboardMouseSettings.isEnabled()) {
            releaseMouseButtons();
            releasePcKeys();
        } else {
            KeyboardMouseSettings.syncNativeConfig();
        }
        updatePointerCapture();
    }

    private boolean onMouseMotionEvent(MotionEvent event, boolean capturedEvent) {
        if (KeyboardMouseSettings.isCapturingBinding() &&
                KeyboardMouseSettings.captureMouse(event)) {
            return true;
        }
        int action = event.getActionMasked();
        int source = event.getSource();
        int deviceId = event.getDeviceId();
        InputDevice device = InputDevice.getDevice(deviceId);
        int deviceSources = device != null ? device.getSources() : 0;
        float x = event.getX();
        float y = event.getY();

        if (action == MotionEvent.ACTION_HOVER_ENTER) {
            hasLastMousePosition = false;
            logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            return true;
        }
        if (action == MotionEvent.ACTION_HOVER_EXIT) {
            // Do not release held mouse buttons on hover exit (pointer capture / OTG edges)
            hasLastMousePosition = false;
            logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            return true;
        }

        if (interfaceVisible && !isTabListOnlyMode()) {
            if (isMouseButtonAction(action)) {
                int button = resolveMouseButton(event, action);
                boolean down = isMouseButtonDownAction(action);
                if (!down) {
                    releaseMouseButtons();
                }
                updatePressedMouseButtons(button, down);
                if (button == MotionEvent.BUTTON_PRIMARY && down && !mouseTouchActive) {
                    mouseTouchActive = true;
                    lastMouseX = x;
                    lastMouseY = y;
                    GameThread.INSTANCE.onTouchStart(0, x, y);
                } else if (button == MotionEvent.BUTTON_PRIMARY && !down && mouseTouchActive) {
                    mouseTouchActive = false;
                    GameThread.INSTANCE.onTouchEnd(0, x, y);
                }
                logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            } else if ((action == MotionEvent.ACTION_HOVER_MOVE || action == MotionEvent.ACTION_MOVE) &&
                    mouseTouchActive) {
                lastMouseX = x;
                lastMouseY = y;
                GameThread.INSTANCE.onTouchMove(0, x, y);
                logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            } else if (action == MotionEvent.ACTION_HOVER_MOVE || action == MotionEvent.ACTION_MOVE) {
                logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            }
            return true;
        }

        if (isMouseButtonAction(action)) {
            int button = resolveMouseButton(event, action);
            boolean down = isMouseButtonDownAction(action);
            int nativeBtn = mouseButtonToNativeIndex(button);
            if (nativeBtn >= 1) {
                boolean changed = updatePressedMouseButtons(button, down);
                if (changed) {
                    KeyboardMouseSettings.setMouseState(button, down);
                    if (KeyboardMouseSettings.isEnabled()) {
                        int mappedAction = KeyboardMouseSettings.actionForMouseButton(button);
                        if (down) {
                            KeyboardMouseBridge.nativeMouseButtonDown(nativeBtn, mappedAction);
                        } else {
                            KeyboardMouseBridge.nativeMouseButtonUp(nativeBtn);
                        }
                    }
                }
                logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
                return true;
            }
        }

        if (action == MotionEvent.ACTION_CANCEL) {
            releaseMouseButtons();
            hasLastMousePosition = false;
            logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            return true;
        }

        if ((action == MotionEvent.ACTION_HOVER_MOVE || action == MotionEvent.ACTION_MOVE) &&
                KeyboardMouseSettings.isEnabled()) {
            float dx = 0.0f;
            float dy = 0.0f;
            boolean hasRelativeSource = (source & InputDevice.SOURCE_MOUSE_RELATIVE) ==
                    InputDevice.SOURCE_MOUSE_RELATIVE;
            if (capturedEvent || hasRelativeSource) {
                dx = event.getAxisValue(MotionEvent.AXIS_RELATIVE_X);
                dy = event.getAxisValue(MotionEvent.AXIS_RELATIVE_Y);
                if (dx == 0.0f && dy == 0.0f) {
                    float rx = event.getX();
                    float ry = event.getY();
                    if (!capturedEvent && hasLastMousePosition) {
                        dx = rx - lastMouseX;
                        dy = ry - lastMouseY;
                    }
                    lastMouseX = rx;
                    lastMouseY = ry;
                    hasLastMousePosition = true;
                }
            } else {
                dx = hasLastMousePosition ? (x - lastMouseX) : 0.0f;
                dy = hasLastMousePosition ? (y - lastMouseY) : 0.0f;
                lastMouseX = x;
                lastMouseY = y;
                hasLastMousePosition = true;
            }

            if (!Float.isFinite(dx)) dx = 0.0f;
            if (!Float.isFinite(dy)) dy = 0.0f;

            dx = KeyboardMouseSettings.scaleMouseX(dx);
            dy = KeyboardMouseSettings.scaleMouseY(dy);
            logMouseEvent(event, source, deviceSources, x, y, dx, dy, capturedEvent);
            if (dx != 0.0f || dy != 0.0f) {
                GameNative.implOnNativeMouseMove(dx, dy);
            }
            return true;
        }

        if (action == MotionEvent.ACTION_SCROLL && KeyboardMouseSettings.isEnabled()) {
            float wheel = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
            if (wheel != 0.0f) {
                GameThread.INSTANCE.onPcMouseWheel(wheel > 0.0f ? 1 : -1);
                return true;
            }
        }
        return false;
    }

    private static boolean isMouseButtonAction(int action) {
        return action == MotionEvent.ACTION_BUTTON_PRESS ||
                action == MotionEvent.ACTION_BUTTON_RELEASE ||
                action == MotionEvent.ACTION_DOWN ||
                action == MotionEvent.ACTION_UP ||
                action == MotionEvent.ACTION_POINTER_DOWN ||
                action == MotionEvent.ACTION_POINTER_UP;
    }

    private static boolean isMouseButtonDownAction(int action) {
        return action == MotionEvent.ACTION_BUTTON_PRESS ||
                action == MotionEvent.ACTION_DOWN ||
                action == MotionEvent.ACTION_POINTER_DOWN;
    }

    private int resolveMouseButton(MotionEvent event, int action) {
        int button = event.getActionButton();
        if (button != 0) {
            return button;
        }
        int state = event.getButtonState();
        if (isMouseButtonDownAction(action)) {
            int newlyPressed = state & ~pressedMouseButtons;
            for (int mask : TRACKED_MOUSE_BUTTONS) {
                if ((newlyPressed & mask) != 0) return mask;
            }
            for (int mask : TRACKED_MOUSE_BUTTONS) {
                if ((state & mask) != 0) return mask;
            }
            return MotionEvent.BUTTON_PRIMARY;
        } else {
            int releasedButton = pressedMouseButtons & ~state;
            for (int mask : TRACKED_MOUSE_BUTTONS) {
                if ((releasedButton & mask) != 0) return mask;
            }
            for (int mask : TRACKED_MOUSE_BUTTONS) {
                if ((pressedMouseButtons & mask) != 0) return mask;
            }
            return MotionEvent.BUTTON_PRIMARY;
        }
    }

    private boolean updatePressedMouseButtons(int button, boolean down) {
        if (mouseButtonToNativeIndex(button) < 1) {
            return false;
        }
        boolean currentlyPressed = (pressedMouseButtons & button) != 0;
        if (currentlyPressed == down) {
            return false;
        }
        if (down) {
            pressedMouseButtons |= button;
        } else {
            pressedMouseButtons &= ~button;
        }
        return true;
    }

    private void releaseMouseButtons() {
        int buttons = pressedMouseButtons;
        pressedMouseButtons = 0;
        if (mouseTouchActive) {
            GameThread.INSTANCE.onTouchEnd(0, lastMouseX, lastMouseY);
        }
        mouseTouchActive = false;
        for (int button : TRACKED_MOUSE_BUTTONS) {
            if ((buttons & button) != 0) {
                KeyboardMouseSettings.setMouseState(button, false);
                int nativeBtn = mouseButtonToNativeIndex(button);
                if (nativeBtn >= 1) {
                    KeyboardMouseBridge.nativeMouseButtonUp(nativeBtn);
                }
            }
        }
    }

    private static int mouseButtonToNativeIndex(int buttonMask) {
        if (buttonMask == MotionEvent.BUTTON_PRIMARY) return 1;
        if (buttonMask == MotionEvent.BUTTON_SECONDARY) return 2;
        if (buttonMask == MotionEvent.BUTTON_TERTIARY) return 3;
        if (buttonMask == MotionEvent.BUTTON_BACK) return 4;
        if (buttonMask == MotionEvent.BUTTON_FORWARD) return 5;
        return 0;
    }

    private void releaseTouchPointers() {
        for (int i = 0; i < touches.size(); i++) {
            int pointerId = touches.keyAt(i);
            PointF point = touches.valueAt(i);
            if (point != null) {
                GameThread.INSTANCE.onTouchEnd(pointerId, point.x, point.y);
            }
        }
        touches.clear();
    }

    private void logMouseEvent(
            MotionEvent event, int source, int deviceSources, float x, float y,
            float dx, float dy, boolean capturedEvent) {
        int action = event.getActionMasked();
        mouseEventCount++;
        boolean leftPressed = (pressedMouseButtons & MotionEvent.BUTTON_PRIMARY) != 0;
        boolean rightPressed = (pressedMouseButtons & MotionEvent.BUTTON_SECONDARY) != 0;
        boolean midPressed = (pressedMouseButtons & MotionEvent.BUTTON_TERTIARY) != 0;
        KeyboardMouseSettings.updateMouseDiagnostics(
                "Mouse ativo | src=0x" + Integer.toHexString(source) +
                        " | delta=(" + String.format(java.util.Locale.ROOT, "%.1f,%.1f", dx, dy) + ")" +
                        " | captura=" + (pointerCaptured ? "SIM" : "NAO") +
                        " | L=" + (leftPressed ? 1 : 0) + " R=" + (rightPressed ? 1 : 0) + " M=" + (midPressed ? 1 : 0) +
                        " | frames=" + KeyboardMouseBridge.nativeGetMouseFrameCount());
        boolean importantEvent = isMouseButtonAction(action) ||
                action == MotionEvent.ACTION_CANCEL ||
                action == MotionEvent.ACTION_HOVER_ENTER ||
                action == MotionEvent.ACTION_HOVER_EXIT;
        if (importantEvent || ++mouseLogCounter % 60 == 1) {
            Log.d(TAG, "[JAVA-MOUSE] action=" + action +
                    " source=0x" + Integer.toHexString(source) +
                    " deviceSources=0x" + Integer.toHexString(deviceSources) +
                    " deviceId=" + event.getDeviceId() +
                    " x=" + x + " y=" + y +
                    " deltaX=" + dx + " deltaY=" + dy +
                    " captured=" + capturedEvent +
                    " buttons=0x" + Integer.toHexString(event.getButtonState()));
        }
    }

    private static int textKeyFor(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_DEL:
                return 0;
            case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_NUMPAD_ENTER:
                return 1;
            case KeyEvent.KEYCODE_DPAD_UP:
                return 3;
            case KeyEvent.KEYCODE_DPAD_DOWN:
                return 4;
            case KeyEvent.KEYCODE_TAB:
                return 5;
            default:
                return -1;
        }
    }

    private boolean isTextEntryActiveOrPending() {
        if (KeyboardMouseBridge.nativeIsTextEntryActive()) {
            pendingTextEntryUntilMs = 0L;
            return true;
        }
        return nativeTextEntryMode && SystemClock.uptimeMillis() < pendingTextEntryUntilMs;
    }

    private void toggleInterfaceMode() {
        boolean nativeInterfaceActive = KeyboardMouseBridge.nativeIsInterfaceActive();
        if (nativeInterfaceActive) {
            closeNativeInterfaceOnNextInput = true;
            GameThread.INSTANCE.onPcEscape();
            return;
        }
        manualInterfaceMode = !manualInterfaceMode;
        updateInterfaceVisibility();
    }

    private void updatePointerCapture() {
        if (gameView == null) {
            return;
        }
        gameView.post(() -> {
            if (gameView == null) {
                return;
            }
            boolean shouldCapture = shouldCapturePointer();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                if (shouldCapture && !pointerCaptured) {
                    gameView.requestPointerCapture();
                } else if (!shouldCapture && pointerCaptured) {
                    gameView.releasePointerCapture();
                }
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                gameView.setPointerIcon(PointerIcon.getSystemIcon(gameView.getContext(),
                        shouldHideCursor() ? PointerIcon.TYPE_NULL : PointerIcon.TYPE_ARROW));
            }
            hasLastMousePosition = false;
        });
    }

    private boolean shouldCapturePointer() {
        return KeyboardMouseSettings.isEnabled() && !isHardInterfaceBlocking() &&
                gameView != null && gameView.hasWindowFocus();
    }

    private boolean shouldHideCursor() {
        return KeyboardMouseSettings.isEnabled() && !isHardInterfaceBlocking();
    }

    private boolean isTabListOnlyMode() {
        return (nativeTabMode || KeyboardMouseBridge.nativeIsTabListOnly())
                && !managedInterfaceVisible && !manualInterfaceMode && !nativeTextEntryMode
                && !KeyboardMouseBridge.nativeIsTextEntryActive();
    }

    private boolean isHardInterfaceBlocking() {
        if (managedInterfaceVisible || manualInterfaceMode || isTextEntryActiveOrPending()) {
            return true;
        }
        return KeyboardMouseBridge.nativeIsInterfaceActive() && !KeyboardMouseBridge.nativeIsTabListOnly();
    }

    public static boolean isMouseSource(int source) {
        if ((source & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN ||
                (source & InputDevice.SOURCE_STYLUS) == InputDevice.SOURCE_STYLUS) {
            return false;
        }
        return (source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE ||
                (source & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE ||
                (source & InputDevice.SOURCE_TOUCHPAD) == InputDevice.SOURCE_TOUCHPAD ||
                (source & InputDevice.SOURCE_TRACKBALL) == InputDevice.SOURCE_TRACKBALL;
    }

    private static boolean isCapturedMouseCandidate(int source) {
        if (isMouseSource(source)) {
            return true;
        }
        return (source & InputDevice.SOURCE_TOUCHSCREEN) == 0 &&
                (source & InputDevice.SOURCE_STYLUS) == 0 &&
                (source & InputDevice.SOURCE_GAMEPAD) == 0 &&
                (source & InputDevice.SOURCE_JOYSTICK) == 0;
    }

    private void synchronizeNativeInterfaceState() {
        boolean nativeInterfaceActive = KeyboardMouseBridge.nativeIsInterfaceActive();
        boolean nativeText = KeyboardMouseBridge.nativeIsTextEntryActive();
        if (nativeText) {
            nativeTextEntryMode = true;
            pendingTextEntryUntilMs = 0L;
        } else if (!nativeInterfaceActive && SystemClock.uptimeMillis() >= pendingTextEntryUntilMs) {
            nativeTextEntryMode = false;
        }
        if (closeNativeInterfaceOnNextInput && !nativeInterfaceActive) {
            closeNativeInterfaceOnNextInput = false;
        }
        updateInterfaceVisibility();
    }

    private void updateInterfaceVisibility() {
        boolean visible = managedInterfaceVisible || manualInterfaceMode ||
                isTextEntryActiveOrPending() || nativeTabMode ||
                KeyboardMouseBridge.nativeIsInterfaceActive();
        boolean hardBlock = isHardInterfaceBlocking();
        if (interfaceVisible != visible) {
            interfaceVisible = visible;
            if (hardBlock) {
                releaseMouseButtons();
                releaseTouchPointers();
                releasePcKeys();
            }
            updatePointerCapture();
        } else if (hardBlock && (pressedMouseButtons != 0 || touches.size() > 0)) {
            releaseMouseButtons();
            releaseTouchPointers();
        }
    }

    private void releasePcKeys() {
        for (int action = 0; action < KeyboardMouseSettings.ACTION_COUNT; action++) {
            GameThread.INSTANCE.onPcInputAction(action, false);
        }
        KeyboardMouseSettings.clearPressedStates();
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    private static float max3(float a, float b, float c) {
        return Math.max(a, Math.max(b, c));
    }
}
