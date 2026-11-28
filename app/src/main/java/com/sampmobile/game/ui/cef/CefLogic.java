package com.sampmobile.game.ui.cef;

/**
 * CEF 输入路径上"有判断、无副作用"的纯逻辑。
 *
 * 单独成类的唯一目的是**可在 PC 上离线验证**：这里不 import 任何 android 类，
 * 因此可以被 javac 编译后在桌面 JVM 上直接跑断言（见 tools/cef_kat/logic_tests）。
 *
 * 设备上 logcat 在这台 ROM 上极不可靠（logd 反复重启），端到端只能靠上行报文
 * 证明"某条路径被走到"，证明不了"每一条分支都对"。这两段语义恰恰是最容易在
 * 后续改动里被悄悄改坏的（键门控决定服务端能不能收到键、IME inset 决定页面
 * 会不会被键盘盖住），所以把它们抽成纯函数 + 离线断言。
 *
 * 公式与阈值全部照抄自原有的内联实现，行为不变。
 */
final class CefLogic {

    /** 软键盘至少占屏高的 1/4，低于此值的差额视为导航条/手势条。 */
    private static final int IME_MIN_RATIO_DIVISOR = 4;

    private CefLogic() {
    }

    /**
     * 软键盘占掉的高度（px）。0 = 键盘没弹出。
     *
     * @param decorHeight   窗口根 View 的高度
     * @param visibleBottom getWindowVisibleDisplayFrame() 的 bottom
     */
    static int imeInsetOf(int decorHeight, int visibleBottom) {
        if (decorHeight <= 0) return 0;
        int inset = decorHeight - visibleBottom;
        if (inset < 0) inset = 0;
        // 导航条/手势条这类常驻遮挡只有几十 px，键盘至少占屏高四分之一。
        // 用比例阈值区分，避免页面底部永远挂着一条空白。
        if (inset < decorHeight / IME_MIN_RATIO_DIVISOR) inset = 0;
        return inset;
    }

    /**
     * 应该转成 bottomMargin 的键盘高度（px）。
     *
     * {@link #imeInsetOf} 只回答"屏幕上被键盘占了多少"，但在 ADJUST_RESIZE 窗口下
     * 这部分空间**已经被窗口让出来了**，再转成 margin 就是二次退让 —— 页面会被压到
     * 只剩键盘上沿那一点高度，而且因为窗口本身变矮，下一帧算出的 inset 依旧是同一个
     * 值，形成自我维持的反馈回路（真机现象：键盘收起后 viewport 变成 2298x372）。
     *
     * 判据是窗口有没有被系统 resize：
     *   - decorHeight <= 0            -> 量不到，按 0 处理（等下一次布局）
     *   - decorHeight < screenHeight  -> 窗口已被 resize，空间已让出，返回 0
     *   - 否则                        -> 仍按 deltas 退让
     *
     * @param decorHeight    窗口根 View 的高度
     * @param visibleBottom  getWindowVisibleDisplayFrame() 的 bottom
     * @param screenHeight   整块屏幕的高度（WindowManager 当前显示尺寸）
     */
    static int imeMarginOf(int decorHeight, int visibleBottom, int screenHeight) {
        if (decorHeight <= 0 || screenHeight <= 0) return 0;
        // 窗口比屏幕矮：系统已经在替他让位（adjustResize 生效），不要再减一次。
        if (decorHeight < screenHeight) return 0;
        return imeInsetOf(decorHeight, visibleBottom);
    }

    /**
     * PressKey 是否上行。逐条对齐 PC manager.cpp OnWndProcMessage：
     *
     *   if (!key_capture_enabled_) return;
     *   if (!key_allowed_[key]) return;
     *   if (!(down && repeat)) SendClientEmit(PressKey, ...)
     *
     * 即：捕获关闭不发、白名单外不发、按下时的自动重复不发；抬起（down=false）
     * 即使带 repeat 也要发，否则服务端会收到"按下没有对应抬起"。
     *
     * @param captureEnabled 服务端 SetKeyCapture 的当前状态
     * @param allowed        EnableKey 白名单，按 Windows VK 索引
     * @param vk             Windows 虚拟键码（{@link CefKeys#vkOf} 的结果）
     */
    static boolean shouldSendKey(boolean captureEnabled, boolean[] allowed, int vk,
                                 boolean down, boolean repeat) {
        if (!captureEnabled) return false;
        if (vk <= 0 || vk > 255) return false;
        if (allowed == null || vk >= allowed.length || !allowed[vk]) return false;
        if (down && repeat) return false;
        return true;
    }

    /**
     * 游戏聊天框是否要让位给页面（PC FocusManager::ShouldBlockChat）。
     *
     * PC 原文：
     *   if (!chat_input_enabled_) return true;
     *   if (input_focused_browser_id_ == -1) return false;
     *   return browser(input_focused_browser_id_)->controls_chat_input;
     *
     * @param chatInputEnabled   服务端 ToggleChatInput 的状态，默认 true
     * @param inputFocusedId     页面内输入框所在浏览器，-1 = 没有
     * @param controlsChatInput  该浏览器创建时声明的 controls_chat_input
     */
    static boolean shouldBlockChat(boolean chatInputEnabled, int inputFocusedId,
                                   boolean controlsChatInput) {
        if (!chatInputEnabled) return true;
        if (inputFocusedId < 0) return false;
        return controlsChatInput;
    }
}
