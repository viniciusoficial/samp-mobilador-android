package com.sampmobile.game.ui;

import android.view.KeyEvent;
import android.view.MotionEvent;

import com.sampmobile.game.ui.cef.CefManager;

/**
 * Single Android UI-thread arbiter for focus, touch interception, hardware key gating,
 * native multi-touch release, and Back-key priority across:
 *   1) CEF focused browser (treated as an external black-box via {@link CefManager})
 *   2) Java overlays (Keyboard / Dialog via {@link UI})
 *   3) Native overlays (ImGui / CLEO / Server Menu / EditObject / SelectObject / TextDraw via JNI)
 *   4) Underlying GTA:SA engine
 */
public final class GameInputArbiter {
    public interface Host {
        UI getUi();
        void cancelAllNativeTouches();
        void dispatchHardwareKeyToNative(int keyCode, boolean isDown);
    }

    private final Host host;
    private boolean backConsumedByCef = false;
    private boolean backConsumedByUi = false;

    public GameInputArbiter(Host host) {
        this.host = host;
    }

    /**
     * Runs before {@code Activity.dispatchKeyEvent} enters the view hierarchy.
     * Returns {@code true} if the event was fully consumed here.
     */
    public boolean onDispatchKeyEvent(KeyEvent event) {
        if (event == null) return false;

        // Always upstream PressKey before IME/WebView consumes the key.
        CefManager.handleKeyEvent(event);

        // Intercept BACK while a CEF browser holds focus across both DOWN and UP so
        // ACTION_DOWN never leaks into NvEventQueueActivity (pause menu) or underlying UI,
        // and Chromium cannot silently swallow ACTION_UP.
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            final int action = event.getAction();
            if (action == KeyEvent.ACTION_DOWN) {
                if (event.getRepeatCount() == 0) {
                    backConsumedByCef = CefManager.isAnyBrowserFocused();
                }
                if (backConsumedByCef) {
                    return true;
                }
            } else if (action == KeyEvent.ACTION_UP) {
                if (backConsumedByCef || CefManager.isAnyBrowserFocused()) {
                    backConsumedByCef = false;
                    if (!event.isCanceled()) {
                        CefManager.onBackPressed();
                    }
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Single strict Back-key order for direct {@code Activity.onBackPressed()} invocations:
     *   CEF focus -> Java Keyboard -> Java Dialog -> Native modal -> fall through to Activity/GTA.
     */
    public boolean onBackPressed() {
        if (CefManager.onBackPressed()) {
            return true;
        }
        UI ui = host.getUi();
        return ui != null && ui.onBackPressed();
    }

    /**
     * Handles {@code Activity.onKeyDown}. Returns {@code true} if consumed.
     */
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (backConsumedByCef) {
                return true;
            }
            UI ui = host.getUi();
            if (event.getRepeatCount() == 0 && ui != null && ui.onBackPressed()) {
                backConsumedByUi = true;
                return true;
            }
            if (backConsumedByUi) {
                return true;
            }
        }

        UI ui = host.getUi();
        if (ui != null && !ui.isInputFocused() && !CefManager.isAnyBrowserFocused()) {
            host.dispatchHardwareKeyToNative(keyCode, true);
        }
        return false;
    }

    /**
     * Handles {@code Activity.onKeyUp}. Returns {@code true} if consumed.
     */
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (backConsumedByCef) {
                backConsumedByCef = false;
                return true;
            }
            if (backConsumedByUi) {
                backConsumedByUi = false;
                return true;
            }
        }

        // Preserve anti-stuck-key guarantee: allow key-up releases even if Java input opened
        // while a key was held; only block while CEF browser owns input focus.
        UI ui = host.getUi();
        if (ui != null && !CefManager.isAnyBrowserFocused()) {
            host.dispatchHardwareKeyToNative(keyCode, false);
        }
        return false;
    }

    /**
     * Handles top-level {@code Activity.onTouchEvent} before forwarding to native multi-touch.
     * Returns {@code true} if Java overlays consumed/blocked the touch.
     */
    public boolean onActivityTouchEvent(MotionEvent event) {
        UI ui = host.getUi();
        if (ui == null) return false;

        if (ui.onTouchEvent(event)) {
            host.cancelAllNativeTouches();
            return true;
        }
        if (ui.isDialogVisible()) {
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                host.cancelAllNativeTouches();
            }
            return true;
        }
        return false;
    }

    public void onWindowFocusLost() {
        clearBackLatches();
        host.cancelAllNativeTouches();
    }

    public void onActivityPause() {
        clearBackLatches();
        CefManager.releaseAllFocus();
        host.cancelAllNativeTouches();
    }

    private void clearBackLatches() {
        backConsumedByCef = false;
        backConsumedByUi = false;
    }
}
