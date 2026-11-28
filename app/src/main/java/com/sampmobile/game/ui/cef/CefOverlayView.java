package com.sampmobile.game.ui.cef;

import android.content.Context;
import android.os.SystemClock;
import android.util.Log;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.webkit.WebView;
import android.widget.FrameLayout;

/**
 * 承载一个 omp-cef WebView 的触摸闸门。
 *
 * 为什么需要它：
 *   WebView 一旦可见就吃掉所有触摸，游戏的虚拟摇杆、视角拖动全部失效。而 omp-cef
 *   的语义是"浏览器有焦点时游戏让出输入"（PC: FocusManager::Update 把鼠标锁进 CEF，
 *   IsAnyBrowserFocused 为假时又把输入还给游戏）。所以触摸是否交给页面，必须由焦点
 *   决定，不能由"有没有可见页面"决定。
 *
 * 实现方式：
 *   本类是 DecorView 的一个子 View，WebView 是它唯一的子 View。
 *   passThrough = true 时 dispatchTouchEvent 直接返回 false —— 没有子 View 消费这次
 *   ACTION_DOWN，ViewGroup 的派发循环会继续到下一个兄弟（游戏画面），事件原样落到
 *   Activity.onTouchEvent -> multiTouchEvent4Ex，与没有覆盖层时完全一样。
 *   注意不能在 onInterceptTouchEvent 里做这件事：拦截只影响"给不给子 View"，
 *   返回 false 仍然会把事件交给 WebView。
 *
 * 手势一致性：
 *   闸门在手指按下之后改变了方向（例如服务端在这期间 FocusBrowser），会让一侧收到
 *   "半截手势"。所以切换方向时对失去事件的一侧补 ACTION_CANCEL，见
 *   {@link #setPassThrough(boolean)} 与 {@link #cancelChildGesture()}。
 */
final class CefOverlayView extends FrameLayout {

    private static final String TAG = "CefOverlay";

    private final int browserId;
    private final WebView content;

    /** true = 放行给游戏；false = 页面自己消费。 */
    private boolean passThrough = false;

    CefOverlayView(Context context, int browserId, WebView content) {
        super(context);
        this.browserId = browserId;
        this.content = content;
        setClipChildren(false);
        setClipToPadding(false);
        addView(content, new LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    void setPassThrough(boolean passThrough) {
        if (this.passThrough == passThrough) return;
        this.passThrough = passThrough;
        if (passThrough) {
            // 页面正拿着这次手势：先取消，否则 WebView 会一直以为手指没抬起。
            cancelChildGesture();
        }
        Log.i(TAG, "browser " + browserId + " touch " + (passThrough ? "-> game" : "-> page"));
    }

    boolean isPassThrough() {
        return passThrough;
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent ev) {
        if (passThrough) {
            // 不消费：让 DecorView 继续派发给游戏画面。
            return false;
        }
        return super.dispatchTouchEvent(ev);
    }

    /** 给 WebView 补一个 ACTION_CANCEL，结束它正在处理的手势。 */
    void cancelChildGesture() {
        if (content == null) return;
        final long now = SystemClock.uptimeMillis();
        MotionEvent cancel = MotionEvent.obtain(now, now, MotionEvent.ACTION_CANCEL, 0f, 0f, 0);
        try {
            super.dispatchTouchEvent(cancel);
        } catch (Throwable t) {
            Log.w(TAG, "cancel gesture failed", t);
        } finally {
            cancel.recycle();
        }
    }
}
