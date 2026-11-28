package com.sampmobile.game.input;

import android.view.InputDevice;

/**
 * Ponte JNI do sistema de teclado/mouse PC portado do mobilador
 * (Game/PcInput.cpp na 2.10). Todos os metodos nativos sao implementados em
 * SAMPMOBILE/Game/PcInput.cpp.
 */
public final class KeyboardMouseBridge {
    private KeyboardMouseBridge() {
    }

    /** Overlay de configuracoes aberto: o jogo nao deve consumir teclas/mouse. */
    private static volatile boolean interfaceVisible;

    public static void setInterfaceVisible(boolean visible) {
        interfaceVisible = visible;
    }

    public static boolean isInterfaceVisible() {
        return interfaceVisible;
    }

    public static boolean isMouseSource(int source) {
        if ((source & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN
                || (source & InputDevice.SOURCE_STYLUS) == InputDevice.SOURCE_STYLUS) {
            return false;
        }
        return (source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
                || (source & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE
                || (source & InputDevice.SOURCE_TOUCHPAD) == InputDevice.SOURCE_TOUCHPAD
                || (source & InputDevice.SOURCE_TRACKBALL) == InputDevice.SOURCE_TRACKBALL;
    }

    public static native void nativeSetEnabled(boolean enabled);

    public static native void nativeSetExternalInputPresent(boolean present);

    public static native void nativeSetAction(int action, boolean pressed);

    public static native void nativeSetMouseOptions(float maxDelta, float smoothing);

    public static native void nativeSetCombatConfig(
            float aimSensitivity,
            float strafeSpeed,
            boolean pcSprint,
            boolean fastWeaponSwitch,
            boolean vehicleAutoCenter);

    public static native void nativeSelectWeaponSlot(int slot);

    public static native void nativeMouseMove(float dx, float dy);

    public static native int nativeGetMouseFrameCount();

    public static native void nativeMouseButtonDown(int button, int action);

    public static native void nativeMouseButtonUp(int button);

    public static native void nativeSetCapture(boolean captured);

    public static native void nativeMouseEnter();

    public static native void nativeMouseLeave();

    public static native void nativeMouseWheel(int direction);

    public static native void nativeOpenChat();

    public static native void nativeTextInputKey(int key, int unicode);

    public static native void nativeHandleEscape();

    public static native boolean nativeIsTextEntryActive();

    public static native boolean nativeIsInterfaceActive();

    public static native boolean nativeIsTabListOnly();

    public static native void nativeSetAspectRatio(float aspectRatio);
}
