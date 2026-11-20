package com.rockstargames.oswrapper.view;

import android.graphics.PointF;
import android.util.Log;
import android.util.SparseArray;
import android.os.Build;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.View;

import com.gta.game.input.KeyboardMouseBridge;
import com.gta.game.input.KeyboardMouseSettings;
import com.rockstargames.oswrapper.GameActivityBase;
import com.rockstargames.oswrapper.GameThread;
import com.rockstargames.oswrapper.UtilsKt;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Iterator;
import java.util.List;

import kotlin.collections.CollectionsKt;
import kotlin.jvm.internal.Intrinsics;

public class InputHandler implements GameViewHandler {

    public static  InputHandler INSTANCE = new InputHandler();

    private static   String TAG = "GameView.InputHandler";

    // Xbox 360 button mappings
    private static   int OSX360_A         = 0;
    private static   int OSX360_B         = 1;
    private static   int OSX360_X         = 2;
    private static   int OSX360_Y         = 3;
    private static   int OSX360_START     = 4;
    private static   int OSX360_BACK      = 5;
    private static   int OSX360_L1        = 6;
    private static   int OSX360_R1        = 7;
    private static   int OSX360_DPADUP    = 8;
    private static   int OSX360_DPADDOWN  = 9;
    private static   int OSX360_DPADLEFT  = 10;
    private static   int OSX360_DPADRIGHT = 11;
    private static   int OSX360_L3        = 12;
    private static   int OSX360_R3        = 13;

    // OSX Platform buttons
    private static   int OSXP_BACK    = 14;
    private static   int OSXP_GP_MENU = 15;
    private static   int OSXP_MENU    = 12;
    private static   int OSXP_SEARCH  = 13;

    private boolean dpadUpPressed;
    private boolean dpadDownPressed;
    private boolean dpadLeftPressed;
    private boolean dpadRightPressed;

    private   SparseArray<PointF> touches = new SparseArray<>();
    private View gameView;
    private boolean interfaceVisible;
    private boolean managedInterfaceVisible;
    private boolean manualInterfaceMode;
    private boolean nativeTextEntryMode;
    private boolean nativeTabMode;
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
        ArrayList arrayList = new ArrayList(deviceIds.length);
        for (int i : deviceIds) {
            arrayList.add(InputDevice.getDevice(i));
        }
        List listFilterNotNull = CollectionsKt.filterNotNull(arrayList);
        if ((listFilterNotNull instanceof Collection) && listFilterNotNull.isEmpty()) {
            return 0;
        }
        Iterator it = listFilterNotNull.iterator();
        int i2 = 0;
        while (it.hasNext()) {
            if (((((InputDevice) it.next()).getSources() & 16778257) != 0) && (i2 = i2 + 1) < 0) {
                CollectionsKt.throwCountOverflow();
            }
        }
        return i2;
    }


    // -----------------------------------------------------------------------
    // Key mapping
    // -----------------------------------------------------------------------

    private final int toGamepadButton(int keyCode) {
        if (keyCode == 4) {
            return 14;
        }
        if (keyCode == 82) {
            return 15;
        }
        if (keyCode == 110) {
            return 4;
        }
        if (keyCode == 96) {
            return 0;
        }
        if (keyCode == 97) {
            return 1;
        }
        if (keyCode == 99) {
            return 2;
        }
        if (keyCode == 100) {
            return 3;
        }
        if (keyCode == 102) {
            return 6;
        }
        if (keyCode == 103) {
            return 7;
        }
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
        boolean isGamepad = ((event != null ? event.getSource() : 0) & 16) != 0;
        if (!isGamepad) {
            KeyboardMouseSettings.setKeyState(keyCode, true);
        }
        if (!isGamepad && event != null && KeyboardMouseSettings.isEnabled()) {
            if (keyCode == KeyEvent.KEYCODE_ESCAPE) {
                Log.d(TAG, "PC key received: Escape");
                if (managedInterfaceVisible) {
                    KeyboardMouseSettings.close();
                    return true;
                }
                if (!KeyboardMouseBridge.nativeIsTextEntryActive()) {
                    toggleInterfaceMode();
                } else {
                    GameThread.INSTANCE.onPcTextInputKey(2, 0);
                }
                return true;
            }
            if (KeyboardMouseBridge.nativeIsTextEntryActive()) {
                int textKey = textKeyFor(keyCode);
                if (textKey >= 0) {
                    GameThread.INSTANCE.onPcTextInputKey(textKey, 0);
                    return true;
                }
                int unicode = event.getUnicodeChar();
                if (unicode != 0) {
                    GameThread.INSTANCE.onPcTextInputKey(-1, unicode);
                    return true;
                }
            }
            int mappedAction = KeyboardMouseSettings.actionForKey(keyCode);
            boolean nativeInterfaceActive = KeyboardMouseBridge.nativeIsInterfaceActive();
            if (interfaceVisible && mappedAction != KeyboardMouseSettings.TAB &&
                    (!nativeInterfaceActive || mappedAction != KeyboardMouseSettings.ENTER)) {
                return true;
            }
            int action = mappedAction;
            if (action == KeyboardMouseSettings.CHAT &&
                    !KeyboardMouseBridge.nativeIsTextEntryActive()) {
                Log.d(TAG, "PC key received: chat");
                nativeTextEntryMode = true;
                updateInterfaceVisibility();
                GameThread.INSTANCE.onPcOpenChat();
                return true;
            }
            if (action >= 0) {
                if (event.getRepeatCount() == 0) {
                    Log.d(TAG, "PC key received: code=" + keyCode + " action=" + action);
                }
                KeyboardMouseSettings.setKeyState(keyCode, true);
                if (event.getRepeatCount() == 0) {
                    if (action == KeyboardMouseSettings.TAB) {
                        nativeTabMode = true;
                        updateInterfaceVisibility();
                    }
                    GameThread.INSTANCE.onPcInputAction(action, true);
                }
                return true;
            }
        }
        if (keyCode == 4 && !isGamepad) {
            return true; // consume, handle on up
        }
        int button = toGamepadButton(keyCode);
        if (button == -1) return false;
        GameThread.INSTANCE.onGamepadButtonDown(0, button);
        return true;
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {
        synchronizeNativeInterfaceState();
        if (event != null && !((event.getSource() & 16) != 0)) {
            KeyboardMouseSettings.setKeyState(keyCode, false);
            int action = KeyboardMouseSettings.actionForKey(keyCode);
            if (action >= 0 && KeyboardMouseSettings.isEnabled()) {
                Log.d(TAG, "PC key released: code=" + keyCode + " action=" + action);
                if (action == KeyboardMouseSettings.TAB) {
                    nativeTabMode = false;
                    updateInterfaceVisibility();
                }
                GameThread.INSTANCE.onPcInputAction(action, false);
                return true;
            }
        }
        boolean isGamepad = ((event != null ? event.getSource() : 0) & 16) != 0;
        if (interfaceVisible && !isGamepad) {
            return true;
        }
        if (keyCode == 4 && !isGamepad) {
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
        if ((event.getSource() & 16) == 0) {
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
        GameThread.INSTANCE.onGamepadAxesChanged(0, axisValue, axisValue2, axisValue3, axisValue4, Math.max(event.getAxisValue(17), event.getAxisValue(23)), fMax3);
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
        if (KeyboardMouseSettings.isEnabled() && !interfaceVisible) {
            if (touchLogCounter++ % 60 == 0) {
                Log.d(TAG, "[TOUCH-MOUSE] blocked reason=PC gameplay mode");
            }
            releaseTouchPointers();
            return true;
        }
        if ((event.getActionMasked() == MotionEvent.ACTION_DOWN ||
                event.getActionMasked() == MotionEvent.ACTION_MOVE) &&
                touchLogCounter++ % 60 == 0) {
            Log.d(TAG, "[TOUCH-MOUSE] accepted reason=" +
                    (interfaceVisible ? "interface visible" : "PC mode disabled"));
        }
        int actionIndex = event.getActionIndex();
        int pointerId = event.getPointerId(actionIndex);
        int i = 0;
        switch (event.getActionMasked()) {
            case 0:
            case 5:
                float x = event.getX(actionIndex);
                float y = event.getY(actionIndex);
                PointF pointF = new PointF();
                pointF.x = x;
                pointF.y = y;
                this.touches.put(pointerId, pointF);
                GameThread.INSTANCE.onTouchStart(pointerId, x, y);
                return true;
            case 1:
            case 4:
            case 6:
                PointF pointF2 = this.touches.get(pointerId);
                if (pointF2 != null) {
                    GameThread.INSTANCE.onTouchEnd(pointerId, pointF2.x, pointF2.y);
                }
                this.touches.remove(pointerId);
                return true;
            case 2:
                while (i < event.getPointerCount()) {
                    float x2 = event.getX(i);
                    float y2 = event.getY(i);
                    PointF pointF3 = new PointF();
                    pointF3.x = x2;
                    pointF3.y = y2;
                    int pointerId2 = event.getPointerId(i);
                    this.touches.put(pointerId2, pointF3);
                    GameThread.INSTANCE.onTouchMove(pointerId2, x2, y2);
                    i++;
                }
                return true;
            case 3:
                while (i < this.touches.size()) {
                    int iKeyAt = this.touches.keyAt(i);
                    PointF pointF4 = this.touches.get(iKeyAt);
                    if (pointF4 != null) {
                        GameThread.INSTANCE.onTouchEnd(iKeyAt, pointF4.x, pointF4.y);
                    }
                    i++;
                }
                this.touches.clear();
                return true;
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
        updateInterfaceVisibility();
        updatePointerCapture();
    }

    public void setGameView(View view) {
        gameView = view;
        updatePointerCapture();
    }

    public void onPointerCaptureChanged(boolean captured) {
        pointerCaptured = captured;
        hasLastMousePosition = false;
        if (gameView != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            gameView.setPointerIcon(PointerIcon.getSystemIcon(gameView.getContext(),
                    shouldHideCursor() ? PointerIcon.TYPE_NULL : PointerIcon.TYPE_ARROW));
        }
        if (!captured) {
            releaseMouseButtons();
            KeyboardMouseBridge.nativeMouseLeave();
        } else {
            KeyboardMouseBridge.nativeMouseEnter();
        }
    }

    public boolean onCapturedPointerEvent(MotionEvent event) {
        if (event == null ||
                !isMouseSource(event.getSource())) {
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
            KeyboardMouseBridge.nativeMouseEnter();
            logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            return true;
        }
        if (action == MotionEvent.ACTION_HOVER_EXIT) {
            hasLastMousePosition = false;
            releaseMouseButtons();
            KeyboardMouseBridge.nativeMouseLeave();
            logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            return true;
        }

        if (interfaceVisible) {
            if (isMouseButtonAction(action)) {
                int button = resolveMouseButton(event, action);
                boolean down = action == MotionEvent.ACTION_BUTTON_PRESS ||
                        action == MotionEvent.ACTION_DOWN;
                updatePressedMouseButtons(button, down);
                if (button == MotionEvent.BUTTON_PRIMARY && down && !mouseTouchActive) {
                    mouseTouchActive = true;
                    lastMouseX = x;
                    lastMouseY = y;
                    GameThread.INSTANCE.onTouchStart(-1, x, y);
                } else if (button == MotionEvent.BUTTON_PRIMARY && !down && mouseTouchActive) {
                    mouseTouchActive = false;
                    GameThread.INSTANCE.onTouchEnd(-1, x, y);
                }
                logMouseEvent(
                        event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            } else if ((action == MotionEvent.ACTION_HOVER_MOVE || action == MotionEvent.ACTION_MOVE) &&
                    mouseTouchActive) {
                GameThread.INSTANCE.onTouchMove(-1, x, y);
                logMouseEvent(
                        event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            } else if (action == MotionEvent.ACTION_HOVER_MOVE ||
                    action == MotionEvent.ACTION_MOVE) {
                logMouseEvent(
                        event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            }
            return true;
        }

        if (isMouseButtonAction(action)) {
            int button = resolveMouseButton(event, action);
            boolean down = action == MotionEvent.ACTION_BUTTON_PRESS ||
                    action == MotionEvent.ACTION_DOWN;
            if (button == MotionEvent.BUTTON_PRIMARY || button == MotionEvent.BUTTON_SECONDARY) {
                updatePressedMouseButtons(button, down);
                int mapped = KeyboardMouseSettings.actionForMouseButton(button);
                KeyboardMouseSettings.setMouseState(button, down);
                if (KeyboardMouseSettings.isEnabled()) {
                    if (down && mapped >= 0) {
                        KeyboardMouseBridge.nativeMouseButtonDown(button, mapped);
                    } else if (!down) {
                        KeyboardMouseBridge.nativeMouseButtonUp(button);
                    }
                }
                logMouseEvent(
                        event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
                return true;
            }
        }

        if (action == MotionEvent.ACTION_CANCEL) {
            releaseMouseButtons();
            hasLastMousePosition = false;
            KeyboardMouseBridge.nativeMouseLeave();
            logMouseEvent(event, source, deviceSources, x, y, 0.0f, 0.0f, capturedEvent);
            return true;
        }

        if ((action == MotionEvent.ACTION_HOVER_MOVE || action == MotionEvent.ACTION_MOVE) &&
                KeyboardMouseSettings.isEnabled() && !interfaceVisible) {
            float dx;
            float dy;
            boolean hasRelativeSource = (source & InputDevice.SOURCE_MOUSE_RELATIVE) ==
                    InputDevice.SOURCE_MOUSE_RELATIVE;
            if ((capturedEvent || hasRelativeSource) &&
                    (action == MotionEvent.ACTION_MOVE ||
                            action == MotionEvent.ACTION_HOVER_MOVE)) {
                dx = event.getAxisValue(MotionEvent.AXIS_RELATIVE_X);
                dy = event.getAxisValue(MotionEvent.AXIS_RELATIVE_Y);
            } else {
                dx = hasLastMousePosition ? x - lastMouseX : 0;
                dy = hasLastMousePosition ? y - lastMouseY : 0;
                lastMouseX = x;
                lastMouseY = y;
                hasLastMousePosition = true;
            }
            dx = KeyboardMouseSettings.scaleMouseX(dx);
            dy = KeyboardMouseSettings.scaleMouseY(dy);
            logMouseEvent(
                    event, source, deviceSources, x, y, dx, dy, capturedEvent);
            if (dx != 0 || dy != 0) {
                KeyboardMouseBridge.nativeMouseMove(dx, dy);
            }
            return true;
        }
        if (action == MotionEvent.ACTION_SCROLL && KeyboardMouseSettings.isEnabled() &&
                !interfaceVisible) {
            float wheel = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
            if (wheel != 0) {
                GameThread.INSTANCE.onPcMouseWheel(wheel > 0 ? 1 : -1);
                return true;
            }
        }
        return false;
    }

    private static boolean isMouseButtonAction(int action) {
        return action == MotionEvent.ACTION_BUTTON_PRESS ||
                action == MotionEvent.ACTION_BUTTON_RELEASE ||
                action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_UP;
    }

    private int resolveMouseButton(MotionEvent event, int action) {
        int button = event.getActionButton();
        if (button == 0 && (action == MotionEvent.ACTION_DOWN ||
                action == MotionEvent.ACTION_BUTTON_PRESS)) {
            int state = event.getButtonState();
            if ((state & MotionEvent.BUTTON_PRIMARY) != 0) {
                return MotionEvent.BUTTON_PRIMARY;
            }
            if ((state & MotionEvent.BUTTON_SECONDARY) != 0) {
                return MotionEvent.BUTTON_SECONDARY;
            }
            return MotionEvent.BUTTON_PRIMARY;
        }
        if (button == 0 && (action == MotionEvent.ACTION_UP ||
                action == MotionEvent.ACTION_BUTTON_RELEASE)) {
            int releasedButton = pressedMouseButtons & ~event.getButtonState();
            if ((releasedButton & MotionEvent.BUTTON_PRIMARY) != 0) {
                return MotionEvent.BUTTON_PRIMARY;
            }
            if ((releasedButton & MotionEvent.BUTTON_SECONDARY) != 0) {
                return MotionEvent.BUTTON_SECONDARY;
            }
            if ((pressedMouseButtons & MotionEvent.BUTTON_PRIMARY) != 0) {
                return MotionEvent.BUTTON_PRIMARY;
            }
            if ((pressedMouseButtons & MotionEvent.BUTTON_SECONDARY) != 0) {
                return MotionEvent.BUTTON_SECONDARY;
            }
        }
        return button;
    }

    private void updatePressedMouseButtons(int button, boolean down) {
        if (button != MotionEvent.BUTTON_PRIMARY && button != MotionEvent.BUTTON_SECONDARY) {
            return;
        }
        if (down) {
            pressedMouseButtons |= button;
        } else {
            pressedMouseButtons &= ~button;
        }
    }

    private void releaseMouseButtons() {
        int buttons = pressedMouseButtons;
        pressedMouseButtons = 0;
        if (mouseTouchActive) {
            GameThread.INSTANCE.onTouchEnd(-1, lastMouseX, lastMouseY);
        }
        mouseTouchActive = false;
        for (int button : new int[] {
                MotionEvent.BUTTON_PRIMARY, MotionEvent.BUTTON_SECONDARY
        }) {
            if ((buttons & button) != 0) {
                KeyboardMouseSettings.setMouseState(button, false);
                KeyboardMouseBridge.nativeMouseButtonUp(button);
            }
        }
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
        KeyboardMouseSettings.updateMouseDiagnostics(
                "Detected: yes | source=0x" + Integer.toHexString(source) +
                        " deviceId=" + event.getDeviceId() +
                        " | delta=(" + dx + "," + dy + ")" +
                        " | events=" + mouseEventCount +
                        " | capture=" + pointerCaptured +
                        " | buttons L=" + leftPressed + " R=" + rightPressed +
                        " | frames consumed=" +
                        KeyboardMouseBridge.nativeGetMouseFrameCount());
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
        if (keyCode == KeyEvent.KEYCODE_DEL) {
            return 0;
        }
        if (keyCode == KeyEvent.KEYCODE_ENTER) {
            return 1;
        }
        return -1;
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
                        shouldCapture ? PointerIcon.TYPE_NULL : PointerIcon.TYPE_ARROW));
            }
            hasLastMousePosition = false;
        });
    }

    private boolean shouldCapturePointer() {
        return KeyboardMouseSettings.isEnabled() && !interfaceVisible &&
                !KeyboardMouseBridge.nativeIsInterfaceActive() && gameView.hasWindowFocus();
    }

    private boolean shouldHideCursor() {
        return KeyboardMouseSettings.isEnabled() && !interfaceVisible &&
                !KeyboardMouseBridge.nativeIsInterfaceActive();
    }

    public static boolean isMouseSource(int source) {
        return (source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE ||
                (source & InputDevice.SOURCE_MOUSE_RELATIVE) ==
                        InputDevice.SOURCE_MOUSE_RELATIVE;
    }

    private void synchronizeNativeInterfaceState() {
        boolean nativeInterfaceActive = KeyboardMouseBridge.nativeIsInterfaceActive();
        if (!KeyboardMouseBridge.nativeIsTextEntryActive() && !nativeInterfaceActive) {
            nativeTextEntryMode = false;
        }
        if (closeNativeInterfaceOnNextInput && !nativeInterfaceActive) {
            closeNativeInterfaceOnNextInput = false;
        }
        updateInterfaceVisibility();
    }

    private void updateInterfaceVisibility() {
        boolean visible = managedInterfaceVisible || manualInterfaceMode ||
                nativeTextEntryMode || nativeTabMode || KeyboardMouseBridge.nativeIsInterfaceActive();
        if (interfaceVisible != visible) {
            interfaceVisible = visible;
            if (visible) {
                releaseMouseButtons();
                releaseTouchPointers();
                releasePcKeys();
            }
            updatePointerCapture();
        }
    }

    private void releasePcKeys() {
        for (int action = 0; action < KeyboardMouseSettings.ACTION_COUNT; action++) {
            GameThread.INSTANCE.onPcInputAction(action, false);
        }
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    private static float max3(float a, float b, float c) {
        return Math.max(a, Math.max(b, c));
    }
}