package com.sampmobile.game.ui;

import android.util.Log;
import android.view.MotionEvent;

import com.google.firebase.crashlytics.FirebaseCrashlytics;
import com.joom.paranoid.Obfuscate;

import java.io.File;
import java.io.UnsupportedEncodingException;
import java.util.Arrays;

import com.sampmobile.game.SAMP;
import com.sampmobile.game.ui.cef.CefManager;
import com.sampmobile.game.ui.widgets.Dialog;
import com.sampmobile.game.ui.widgets.EditObject;
import com.sampmobile.game.ui.widgets.HeightProvider;
import com.sampmobile.game.ui.widgets.Keyboard;
import com.sampmobile.game.ui.widgets.LoadingScreen;
import com.sampmobile.game.ui.widgets.WantedLevel;
import com.sampmobile.launcher.utils.ClientSettings;

@Obfuscate
public class UI implements Dialog.Listener, Keyboard.Listener, HeightProvider.Listener {
    private static final String TAG = "UI";

    private final SAMP samp;
    private final String charset;

    private LoadingScreen loadingScreen;
    private Dialog dialog;
    private Keyboard keyboard;
    private WantedLevel wantedLevel;
    private EditObject editObject = null;
    private HeightProvider heightProvider = null;

    private native void keyboardSend(byte[] str);

    private native void sendDialogResponse(int dialog_id, int button_id, int list_item, byte[] input);

    private native void setJavaDialogVisible(boolean visible);

    private native void setJavaKeyboardVisible(boolean visible);

    private native boolean handleNativeBackPressed();

    public UI(SAMP samp, ClientSettings settings) {
        this.samp = samp;
        this.charset = settings != null ? settings.getCharset() : "GBK";
    }

    public void initializeUI() {
        heightProvider = new HeightProvider(samp).init(samp.GetMainView()).setHeightListener(this);
        loadingScreen = new LoadingScreen(samp);
        editObject = new EditObject(samp);
        dialog = new Dialog(samp, this, charset);
        keyboard = new Keyboard(samp, this);
        wantedLevel = new WantedLevel(samp);

        showLoadingScreen(true);
        showWantedLevel(true);
    }

    public boolean onBackPressed() {
        if (keyboard != null && keyboard.isVisible()) {
            showKeyboard(false);
            return true;
        }
        if (isDialogVisible()) {
            dialog.SendDialogResponse(0, dialog.mCurrentListitem, dialog.mCurrentInputtext);
            return true;
        }
        try {
            if (handleNativeBackPressed()) {
                return true;
            }
        } catch (UnsatisfiedLinkError ignored) {
        }
        return false;
    }

    public void destroy() {
        if (heightProvider != null) {
            heightProvider.destroy();
            heightProvider = null;
        }
        if (editObject != null) {
            editObject.show(false);
        }
        _onDialogVisibilityChanged(false);
        _onKeyboardVisibilityChanged(false);
    }

    public boolean isDialogVisible() {
        return dialog != null && dialog.isShow && dialog.mMainLayout != null && dialog.mMainLayout.getVisibility() == android.view.View.VISIBLE;
    }

    @Override
    public void _onDialogVisibilityChanged(boolean visible) {
        try {
            setJavaDialogVisible(visible);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (visible && samp != null) {
            samp.cancelAllNativeTouches();
        }
    }

    @Override
    public void _onKeyboardVisibilityChanged(boolean visible) {
        try {
            setJavaKeyboardVisible(visible);
        } catch (UnsatisfiedLinkError ignored) {
        }
        if (visible && samp != null) {
            samp.cancelAllNativeTouches();
        }
        // 游戏聊天输入开/关 -> omp-cef：本地事件发给每个页面，同时上行
        // ClientEmitEvent(ChatInputState)，对应 PC ChatHook::SetChatInputState。
        CefManager.onGameChatStateChanged(visible);
    }

    public boolean isInputFocused() {
        if (keyboard != null && keyboard.isVisible()) {
            return true;
        }
        return isDialogVisible();
    }

    public boolean onTouchEvent(MotionEvent event) {
        if (keyboard != null) {
            return keyboard.onTouchEvent(event);
        }
        return false;
    }

    public void exitGame() {
        try {
            FirebaseCrashlytics.getInstance().setCrashlyticsCollectionEnabled(false);
        } catch (Throwable ignored) {
            // FirebaseInitProvider only runs in the main launcher process, not :game.
        }
        deleteSessionAliveMarker();
        Thread exitWatchdog = new Thread(() -> {
            try {
                Thread.sleep(150);
            } catch (InterruptedException ignored) {
            }
            android.os.Process.killProcess(android.os.Process.myPid());
        }, "samp-exit-watchdog");
        exitWatchdog.setDaemon(true);
        exitWatchdog.start();
        samp.runOnUiThread(samp::finish);
    }

    // Native writes logcat/session_alive at client init and only intentional
    // exits delete it; a leftover means the previous session died abnormally.
    private void deleteSessionAliveMarker() {
        try {
            File extDir = samp.getExternalFilesDir(null);
            if (extDir != null) {
                new File(extDir, "logcat/session_alive").delete();
            }
        } catch (Throwable ignored) {
        }
    }

    public void onResumeEvent() {
        if (heightProvider != null) {
            heightProvider.init(samp.GetMainView());
        }
    }

    private void showLoadingScreen(boolean z) {
        samp.runOnUiThread(() -> loadingScreen.show(z));
    }

    public void showEditObject(boolean z) {
        samp.runOnUiThread(() -> editObject.show(z));
    }

    public void setPauseState(boolean pause) {
        samp.runOnUiThread(() -> {
            if (pause) {
                keyboard.show(false);
                wantedLevel.show(false);
                dialog.showWithoutReset(false);
                editObject.showWithoutReset(false);
            } else {
                wantedLevel.show(true);
                dialog.showWithoutReset(true);
                editObject.showWithoutReset(true);
            }
        });
    }

    private void showDialog(int dialog_id, int dialog_style, String title, String message, String button1, String button2) {
        Log.i(TAG, "**** showDialog " + dialog_id + " " + title + " " + message + " " + button1 + " " + button2);
        samp.runOnUiThread(() -> dialog.show(dialog_id, dialog_style, title, message, button1, button2));
    }

    private void hideDialog() {
        Log.i(TAG, "**** hideDialog");
        samp.runOnUiThread(() -> {
            if (dialog != null && dialog.isShow) {
                dialog.Hide();
            }
        });
    }

    private void showWantedLevel(boolean z) {
        samp.runOnUiThread(() -> wantedLevel.show(z));
    }

    private void setWantedLevel(int level) {
        Log.i(TAG, "**** setWantedLevel " + level);
        samp.runOnUiThread(() -> wantedLevel.setLevel(level));
    }

    public void showKeyboard(boolean z) {
        Log.i(TAG, "**** showKeyboard " + z);
        // 页面输入框正拿着键盘且该浏览器 controlsChat=true 时，游戏聊天框让位
        // （PC ChatHook::Hook_OpenChatInput -> FocusManager::ShouldBlockChat）。
        if (z && CefManager.shouldBlockChat()) {
            Log.i(TAG, "**** showKeyboard blocked - focused CEF page owns chat input");
            return;
        }
        samp.runOnUiThread(() -> keyboard.show(z));
    }

    @Override
    public void _sendDialogResponse(int dialog_id, int button_id, int list_item, byte[] input) {
        Log.i(TAG, "**** sendDialogResponse " + dialog_id + " " + button_id + " " + list_item + " " + Arrays.toString(input));
        sendDialogResponse(dialog_id, button_id, list_item, input);
    }

    @Override
    public void _keyboardSend(String str) {
        if (str == null) str = "";
        byte[] ret;

        try {
            ret = str.getBytes(charset);
        } catch (UnsupportedEncodingException e) {
            ret = str.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        }

        try {
            keyboardSend(ret);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, e.getMessage() != null ? e.getMessage() : "UnsatisfiedLinkError in keyboardSend");
        }
    }

    @Override
    public void onHeightChanged(int orientation, int height) {
        if (keyboard != null) {
            keyboard.onHeightChanged(height);
        }

        if (dialog != null) {
            dialog.onHeightChanged(height);
        }
    }
}
