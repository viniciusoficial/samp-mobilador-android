package com.sampmobile.game.ui.widgets;

import android.app.Activity;
import android.graphics.Rect;
import android.view.View;
import android.view.ViewTreeObserver;

import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import com.joom.paranoid.Obfuscate;

@Obfuscate
public class HeightProvider implements ViewTreeObserver.OnGlobalLayoutListener {
    private final Activity mActivity;
    private final View decorView;
    private Listener listener;
    private int heightMaxHorizontal;
    private int heightMaxVertical;
    private int lastReportedHeight = -1;
    private boolean attached = false;

    public HeightProvider(final Activity activity) {
        this.mActivity = activity;
        this.decorView = activity.getWindow().getDecorView();
    }

    public HeightProvider init(View view) {
        if (!attached && decorView != null) {
            attached = true;
            decorView.getViewTreeObserver().addOnGlobalLayoutListener(this);
            ViewCompat.setOnApplyWindowInsetsListener(decorView, (v, insets) -> {
                Insets imeInsets = insets.getInsets(WindowInsetsCompat.Type.ime());
                int imeHeight = Math.max(0, imeInsets.bottom);
                reportHeight(imeHeight);
                return insets;
            });
        }
        return this;
    }

    public HeightProvider setHeightListener(Listener listener) {
        this.listener = listener;
        return this;
    }

    public void destroy() {
        if (attached && decorView != null) {
            ViewTreeObserver vto = decorView.getViewTreeObserver();
            if (vto != null && vto.isAlive()) {
                vto.removeOnGlobalLayoutListener(this);
            }
            ViewCompat.setOnApplyWindowInsetsListener(decorView, null);
            attached = false;
        }
        this.listener = null;
    }

    private void reportHeight(int keyboardHeight) {
        if (keyboardHeight < 0) {
            keyboardHeight = 0;
        }
        if (keyboardHeight != lastReportedHeight) {
            lastReportedHeight = keyboardHeight;
            if (listener != null) {
                listener.onHeightChanged(
                        mActivity.getResources().getConfiguration().orientation,
                        keyboardHeight);
            }
        }
    }

    @Override
    public void onGlobalLayout() {
        if (decorView == null) {
            return;
        }
        WindowInsetsCompat rootInsets = ViewCompat.getRootWindowInsets(decorView);
        if (rootInsets != null) {
            Insets imeInsets = rootInsets.getInsets(WindowInsetsCompat.Type.ime());
            if (imeInsets.bottom > 0 || rootInsets.isVisible(WindowInsetsCompat.Type.ime())) {
                reportHeight(imeInsets.bottom);
                return;
            }
        }

        Rect rect = new Rect();
        decorView.getWindowVisibleDisplayFrame(rect);
        int keyboardHeight;
        if (rect.bottom > rect.right) {
            if (rect.bottom > heightMaxVertical) {
                heightMaxVertical = rect.bottom;
            }
            keyboardHeight = heightMaxVertical - rect.bottom;
        } else {
            if (rect.bottom > heightMaxHorizontal) {
                heightMaxHorizontal = rect.bottom;
            }
            keyboardHeight = heightMaxHorizontal - rect.bottom;
        }
        // Ignore small status/navigation bar diffs (< 15% of screen height)
        int screenHeight = decorView.getHeight();
        if (screenHeight > 0 && keyboardHeight < screenHeight / 7) {
            keyboardHeight = 0;
        }
        reportHeight(keyboardHeight);
    }

    public interface Listener {
        void onHeightChanged(int orientation, int height);
    }
}
