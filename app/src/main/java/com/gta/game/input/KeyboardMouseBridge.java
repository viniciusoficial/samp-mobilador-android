package com.gta.game.input;

public final class KeyboardMouseBridge {
    private KeyboardMouseBridge() {
    }

    public static native void nativeSetEnabled(boolean enabled);

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
}
