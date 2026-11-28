package com.sampmobile.game.ui.cef;

import android.view.KeyEvent;

/**
 * Android KeyEvent -> omp-cef PressKey 参数。
 *
 * omp-cef 的按键上行（见 PC manager.cpp OnWndProcMessage）发的是 Windows 语义：
 *
 *   args[0] key        Windows 虚拟键码 (VK)，例如 'A' = 0x41
 *   args[1] scancode   Set-1 硬件扫描码，例如 'A' = 0x1E
 *   args[2] modifiers  CEF EVENTFLAG_* 位掩码（见下）
 *   args[3] down       按下 / 抬起
 *   args[4] repeat     自动重复
 *
 * Android 的 keyCode 与 VK 完全不同（KEYCODE_A = 29），扫描码也不是 PC 的 Set-1，
 * 所以必须查表。未收录的键返回 0（= 不上报），绝不臆造一个近似的 VK 骗服务端。
 */
final class CefKeys {

    // CEF cef_event_flags_t
    static final int EVENTFLAG_CAPS_LOCK_ON = 1 << 0;
    static final int EVENTFLAG_SHIFT_DOWN = 1 << 1;
    static final int EVENTFLAG_CONTROL_DOWN = 1 << 2;
    static final int EVENTFLAG_ALT_DOWN = 1 << 3;

    private static final int MAX_KEYCODE = 300;

    private static final int[] VK = new int[MAX_KEYCODE];
    private static final int[] SCAN = new int[MAX_KEYCODE];

    /** Set-1 扫描码，A..Z（按字母顺序，不是按 keycode 顺序）。 */
    private static final int[] LETTER_SCAN = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
            0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
            0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
    };

    /** Set-1 扫描码，0..9。 */
    private static final int[] DIGIT_SCAN = {
            0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A
    };

    static {
        for (int i = 0; i < 26; i++) {
            put(KeyEvent.KEYCODE_A + i, 'A' + i, LETTER_SCAN[i]);
        }
        for (int i = 0; i < 10; i++) {
            put(KeyEvent.KEYCODE_0 + i, '0' + i, DIGIT_SCAN[i]);
        }

        put(KeyEvent.KEYCODE_ESCAPE, 0x1B, 0x01);
        put(KeyEvent.KEYCODE_DEL, 0x08, 0x0E);            // 退格
        put(KeyEvent.KEYCODE_TAB, 0x09, 0x0F);
        put(KeyEvent.KEYCODE_ENTER, 0x0D, 0x1C);
        put(KeyEvent.KEYCODE_SPACE, 0x20, 0x39);
        put(KeyEvent.KEYCODE_CAPS_LOCK, 0x14, 0x3A);

        put(KeyEvent.KEYCODE_SHIFT_LEFT, 0x10, 0x2A);
        put(KeyEvent.KEYCODE_SHIFT_RIGHT, 0x10, 0x36);
        put(KeyEvent.KEYCODE_CTRL_LEFT, 0x11, 0x1D);
        put(KeyEvent.KEYCODE_CTRL_RIGHT, 0x11, 0x1D);
        put(KeyEvent.KEYCODE_ALT_LEFT, 0x12, 0x38);
        put(KeyEvent.KEYCODE_ALT_RIGHT, 0x12, 0x38);

        put(KeyEvent.KEYCODE_DPAD_UP, 0x26, 0x48);
        put(KeyEvent.KEYCODE_DPAD_DOWN, 0x28, 0x50);
        put(KeyEvent.KEYCODE_DPAD_LEFT, 0x25, 0x4B);
        put(KeyEvent.KEYCODE_DPAD_RIGHT, 0x27, 0x4D);

        put(KeyEvent.KEYCODE_MOVE_HOME, 0x24, 0x47);
        put(KeyEvent.KEYCODE_MOVE_END, 0x23, 0x4F);
        put(KeyEvent.KEYCODE_PAGE_UP, 0x21, 0x49);
        put(KeyEvent.KEYCODE_PAGE_DOWN, 0x22, 0x51);
        put(KeyEvent.KEYCODE_INSERT, 0x2D, 0x52);
        put(KeyEvent.KEYCODE_FORWARD_DEL, 0x2E, 0x53);

        put(KeyEvent.KEYCODE_GRAVE, 0xC0, 0x29);
        put(KeyEvent.KEYCODE_MINUS, 0xBD, 0x0C);
        put(KeyEvent.KEYCODE_EQUALS, 0xBB, 0x0D);
        put(KeyEvent.KEYCODE_LEFT_BRACKET, 0xDB, 0x1A);
        put(KeyEvent.KEYCODE_RIGHT_BRACKET, 0xDD, 0x1B);
        put(KeyEvent.KEYCODE_BACKSLASH, 0xDC, 0x2B);
        put(KeyEvent.KEYCODE_SEMICOLON, 0xBA, 0x27);
        put(KeyEvent.KEYCODE_APOSTROPHE, 0xDE, 0x28);
        put(KeyEvent.KEYCODE_COMMA, 0xBC, 0x33);
        put(KeyEvent.KEYCODE_PERIOD, 0xBE, 0x34);
        put(KeyEvent.KEYCODE_SLASH, 0xBF, 0x35);

        for (int i = 0; i < 12; i++) {
            // F1..F12：VK 0x70..0x7B，扫描码 F1..F10 = 0x3B..0x44，F11 = 0x57，F12 = 0x58
            final int scan = i < 10 ? 0x3B + i : (i == 10 ? 0x57 : 0x58);
            put(KeyEvent.KEYCODE_F1 + i, 0x70 + i, scan);
        }

        // 小键盘：Set-1 与导航键共用扫描码，靠扩展位区分；这里与 PC 一样只发基础码。
        final int[] numpadScan = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};
        for (int i = 0; i < 10; i++) {
            put(KeyEvent.KEYCODE_NUMPAD_0 + i, 0x60 + i, numpadScan[i]);
        }
        put(KeyEvent.KEYCODE_NUMPAD_DIVIDE, 0x6F, 0x35);
        put(KeyEvent.KEYCODE_NUMPAD_MULTIPLY, 0x6A, 0x37);
        put(KeyEvent.KEYCODE_NUMPAD_SUBTRACT, 0x6D, 0x4A);
        put(KeyEvent.KEYCODE_NUMPAD_ADD, 0x6B, 0x4E);
        put(KeyEvent.KEYCODE_NUMPAD_DOT, 0x6E, 0x53);
        put(KeyEvent.KEYCODE_NUMPAD_ENTER, 0x0D, 0x1C);
    }

    private CefKeys() {
    }

    private static void put(int keyCode, int vk, int scan) {
        if (keyCode < 0 || keyCode >= MAX_KEYCODE) return;
        VK[keyCode] = vk;
        SCAN[keyCode] = scan;
    }

    /** Windows 虚拟键码，未收录返回 0。 */
    static int vkOf(int keyCode) {
        return keyCode >= 0 && keyCode < MAX_KEYCODE ? VK[keyCode] : 0;
    }

    /** Set-1 扫描码，未收录返回 0。 */
    static int scanOf(int keyCode) {
        return keyCode >= 0 && keyCode < MAX_KEYCODE ? SCAN[keyCode] : 0;
    }

    /** Android metaState -> CEF EVENTFLAG 位掩码。 */
    static int modifiersOf(KeyEvent event) {
        return modifiersOfMetaState(event.getMetaState());
    }

    /**
     * 同 {@link #modifiersOf}，但只吃 metaState。
     *
     * 抽成纯函数是为了能在 PC 上离线跑断言：本方法只用到 KeyEvent 的 static final
     * 常量（javac 会内联），运行时不需要 android.jar。
     */
    static int modifiersOfMetaState(int meta) {
        int flags = 0;
        if ((meta & KeyEvent.META_SHIFT_ON) != 0) flags |= EVENTFLAG_SHIFT_DOWN;
        if ((meta & KeyEvent.META_CTRL_ON) != 0) flags |= EVENTFLAG_CONTROL_DOWN;
        if ((meta & KeyEvent.META_ALT_ON) != 0) flags |= EVENTFLAG_ALT_DOWN;
        if ((meta & KeyEvent.META_CAPS_LOCK_ON) != 0) flags |= EVENTFLAG_CAPS_LOCK_ON;
        return flags;
    }
}
