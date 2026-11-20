package com.gta.game.input;

import com.rockstargames.oswrapper.GameNative;

public final class KeyboardMouseBridge {
    static {
        if (GameNative.INSTANCE == null) {
            throw new ExceptionInInitializerError("GameNative did not initialize");
        }
    }

    private KeyboardMouseBridge() {
    }

    public static native void nativeSetEnabled(boolean enabled);
    public static native void nativeSetAction(int action, boolean pressed);
    public static native void nativeMouseDelta(float dx, float dy);
    public static native void nativeMouseWheel(int direction);
    public static native void nativeOpenChat();
    public static native void nativeTextInputKey(int key, int unicode);
    public static native void nativeHandleEscape();
    public static native boolean nativeIsTextEntryActive();
    public static native boolean nativeIsInterfaceActive();
}
