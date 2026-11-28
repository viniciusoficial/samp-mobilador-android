package com.sampmobile.game.ui.cef;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.webkit.ConsoleMessage;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;

import java.nio.ByteBuffer;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.Locale;

/**
 * 一个 omp-cef browser id 对应的 Android WebView（Overlay2D 模式）。
 *
 * 与 PC 客户端的对应关系：
 *   PC: CefBrowserHost::CreateBrowser(windowless) + ViewTexture 上传 D3D9
 *   Android: WebView 挂在 Activity DecorView 上，由 SurfaceFlinger 合成
 *
 * Overlay2D 的视口语义（见 omp-cef manager.cpp CreateBrowserInternal）：网页
 * 视口就是游戏屏幕尺寸，位置由网页自身 CSS 决定。所以这里用 MATCH_PARENT，
 * 不额外做坐标换算。
 *
 * 线程：所有方法必须在主线程调用，由 CefManager 负责切换。
 */
final class CefWebView {

    private static final String TAG = "CefWebView";

    /** Overlay2D 的网页视口等于游戏画面，因此布局参数固定全屏。 */
    private static final FrameLayout.LayoutParams FULLSCREEN_LP = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT,
            Gravity.TOP | Gravity.START);

    private final Activity activity;
    private final int browserId;
    private final WebView web;
    private final CefOverlayView overlay;
    private final boolean controlsChatInput;

    /** World2D：视口是服务端给的像素尺寸，位置由世界坐标投影决定。 */
    private final boolean world2d;
    private final int pxWidth;
    private final int pxHeight;

    /**
     * WorldObject3D：页面不直接显示给玩家，而是被抓成像素后贴到游戏物体表面。
     *
     * WebView 没有 GPU 纹理出口，只能 draw(Canvas) 读回位图，所以它必须真的被
     * View 系统布局并绘制。为了不让玩家看见，这里给它一个极低的 alpha（不是
     * INVISIBLE —— 不可见的 View 会被跳过绘制，读回来就是空白）。
     */
    private final boolean offscreen;

    private boolean visible = true;
    private boolean focused = false;
    private boolean inputFocused = false;
    private int layer = 0;
    private boolean destroyed = false;

    // ---- WorldObject3D 抓帧复用缓冲（阶段8）------------------------------------
    // 每次抓帧都 createBitmap + allocateDirect + new byte[] 的话，512x256 每帧
    // 要分配 ~1MB，GC 抖动直接表现为帧率抖。这里按尺寸复用，只在页面尺寸变化
    // 时重建。frameBuf 是 direct 的：C++ 侧用 GetDirectBufferAddress 直接读，
    // 省掉一次 GetByteArrayElements 的 0.5MB 拷贝。
    private Bitmap frameBmp;
    private Canvas frameCanvas;
    private ByteBuffer frameBuf;

    /** 上一次 draw+copy 的耗时（ms），0 = 还没有测到。给 native 做自适应节流用。 */
    private int lastCaptureMs = 0;

    /** World2D：最近一次投影是否落在相机前方（false = 不画，等同 PC 跳过绘制）。 */
    private boolean onScreen = true;

    /** 软键盘当前占掉的高度（px），0 = 键盘没弹出。见 {@link #setImeInset(int)}。 */
    private int imeInset = 0;

    /**
     * 页面是否已经加载完成（onPageFinished）。
     *
     * EmitBrowserEvent 可能在 CreateBrowser 之后几百毫秒就到，而 WebView 冷启动
     * 要 1~3 秒才执行页面脚本。此期间 __cefDispatch 还不存在，直接 evaluate 会
     * 静默丢失事件，所以先排队，加载完成后再按序补发（PC 端 pending_emits_ 等价物）。
     */
    private boolean pageReady = false;
    private final Deque<String[]> pendingEvents = new ArrayDeque<>();

    private static final int MAX_PENDING_EVENTS = 128;

    @SuppressLint("SetJavaScriptEnabled")
    CefWebView(Activity activity, int browserId, String url, boolean focused, boolean controlsChat) {
        this(activity, browserId, url, focused, controlsChat, 0, 0);
    }

    /**
     * @param pxWidth  World2D 的视口宽度（像素，1..1024）；0 = 全屏 Overlay2D
     * @param pxHeight World2D 的视口高度（像素，1..1024）；0 = 全屏 Overlay2D
     */
    @SuppressLint("SetJavaScriptEnabled")
    CefWebView(Activity activity, int browserId, String url, boolean focused, boolean controlsChat,
               int pxWidth, int pxHeight) {
        this(activity, browserId, url, focused, controlsChat, pxWidth, pxHeight, false);
    }

    /**
     * @param pxWidth  World2D 的视口宽度（像素，1..1024）；0 = 全屏 Overlay2D
     * @param pxHeight World2D 的视口高度（像素，1..1024）；0 = 全屏 Overlay2D
     * @param offscreen WorldObject3D：只用来抓帧，不参与输入、几乎透明
     */
    @SuppressLint("SetJavaScriptEnabled")
    CefWebView(Activity activity, int browserId, String url, boolean focused, boolean controlsChat,
               int pxWidth, int pxHeight, boolean offscreen) {
        this.activity = activity;
        this.browserId = browserId;
        this.controlsChatInput = controlsChat;
        this.pxWidth = pxWidth;
        this.pxHeight = pxHeight;
        this.world2d = pxWidth > 0 && pxHeight > 0;
        this.offscreen = offscreen;

        this.web = new WebView(activity);
        this.web.setBackgroundColor(Color.TRANSPARENT);
        // 硬件层：透明背景 + 悬浮在游戏 Surface 之上时，软件层会出现黑底。
        //
        // 例外是 offscreen（WorldObject3D）：那个页面要 draw(Canvas) 读回像素，
        // 而硬件层下 Chromium 的内容在合成器里、不在 View 的 Canvas 上，draw()
        // 会一直等合成（实测把游戏主线程卡死，之后所有广播都无响应）。软件层
        // 让它走 CPU 光栅化，draw() 立刻返回真实内容。512x256 的尺寸下这点
        // 开销可以接受。
        this.web.setLayerType(offscreen ? View.LAYER_TYPE_SOFTWARE : View.LAYER_TYPE_HARDWARE, null);
        this.web.setFocusable(true);
        this.web.setFocusableInTouchMode(true);
        this.web.setVerticalScrollBarEnabled(false);
        this.web.setHorizontalScrollBarEnabled(false);
        this.web.setOverScrollMode(View.OVER_SCROLL_NEVER);

        configureSettings(web.getSettings());

        web.setWebViewClient(new CefClient());
        web.setWebChromeClient(new CefChromeClient());

        // 接口必须在 loadUrl 之前挂好，否则首个页面拿不到 window.cefNative。
        web.addJavascriptInterface(new CefJsBridge(browserId), CefJs.BRIDGE_NAME);

        // 触摸闸门：浏览器有焦点时页面消费触摸，否则原样放行给游戏。
        this.overlay = new CefOverlayView(activity, browserId, web);

        if (url != null && url.length() > 0) {
            web.loadUrl(url);
        }
        this.focused = focused;
        applyTouchMode();
    }

    private void configureSettings(WebSettings settings) {
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        // http://cef/<resource>/<path> 由 CefResourceProvider 处理，不走文件系统，
        // 因此关掉 file:// 访问能力，避免网页借 file:// 读设备文件。
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setSupportZoom(false);
        settings.setBuiltInZoomControls(false);
        settings.setDisplayZoomControls(false);
        settings.setLoadsImagesAutomatically(true);
        settings.setJavaScriptCanOpenWindowsAutomatically(true);
        // 游戏 UI 需要音效/语音提示，CEF 侧默认允许自动播放。
        settings.setMediaPlaybackRequiresUserGesture(false);
        // 资源来自本机 127.0.0.1 与 http://cef/，https 页面里嵌 http 资源需要放行。
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
        settings.setCacheMode(WebSettings.LOAD_DEFAULT);
        settings.setTextZoom(100);
    }

    /** 挂到 Activity 的 DecorView 上（与现有 WebOverlay 同一层级策略）。 */
    void attach() {
        ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();
        // 复制一份 LayoutParams：共享 static 实例会让多个 WebView 抢同一份参数。
        // World2D 用服务端给的像素尺寸（PC: View::Create(width, height)），
        // Overlay2D 仍旧全屏。
        final FrameLayout.LayoutParams lp = world2d
                ? new FrameLayout.LayoutParams(pxWidth, pxHeight, Gravity.TOP | Gravity.START)
                : new FrameLayout.LayoutParams(FULLSCREEN_LP);
        decor.addView(overlay, lp);
        applyLayer();
        if (offscreen) {
            // 抓帧要求 View 真的被绘制，所以不能 INVISIBLE；把 alpha 压到几乎为 0。
            overlay.setAlpha(0.02f);
        }

        // 先 INVISIBLE 再在下一帧转 VISIBLE：与 WebOverlay 的预热路径一致，
        // 避免首次 attach 时 SurfaceView 的 z-order 还没结算。
        overlay.setVisibility(View.INVISIBLE);
        overlay.post(() -> {
            overlay.setVisibility(visible ? View.VISIBLE : View.INVISIBLE);
            forceRedraw();
            Log.i(TAG, "attach id=" + browserId
                    + " vis=" + (overlay.getVisibility() == View.VISIBLE ? "V" : String.valueOf(overlay.getVisibility()))
                    + " shown=" + overlay.isShown()
                    + " size=" + web.getWidth() + "x" + web.getHeight()
                    + " parent=" + (overlay.getParent() != null)
                    + " z=" + overlay.getTranslationZ()
                    + " touch=" + (overlay.isPassThrough() ? "game" : "page"));
        });
    }

    /**
     * WebView 可见性从 INVISIBLE 切回来后，Chromium 侧不一定立刻提交新帧
     * （游戏 SurfaceView 在持续合成时尤其明显）。主动失效一次绘制。
     */
    private void forceRedraw() {
        web.requestLayout();
        web.invalidate();
        web.postInvalidateOnAnimation();
    }

    void setVisible(boolean visible) {
        this.visible = visible;
        if (destroyed) return;
        // 用 INVISIBLE 而不是 GONE：GONE 会让 WebView 失去视口尺寸，
        // 下次显示要重新布局，HUD 类页面会闪一下。
        applyVisibility();
        Log.i(TAG, "setVisible id=" + browserId + " want=" + visible
                + " now=" + (overlay.getVisibility() == View.VISIBLE ? "V" : String.valueOf(overlay.getVisibility()))
                + " shown=" + overlay.isShown()
                + " touch=" + (overlay.isPassThrough() ? "game" : "page"));
    }

    /**
     * 实际可见性 = 服务端要它可见 且（非 World2D 或 投影落在相机前方）。
     *
     * PC 端 World2D 在投影失败/点在相机背后时直接 `continue` 不画，语义等价于
     * 这里隐藏；服务端的 SetBrowserVisible 仍然有效，两者取与。
     */
    private void applyVisibility() {
        if (destroyed) return;
        final boolean show = visible && onScreen;
        overlay.setVisibility(show ? View.VISIBLE : View.INVISIBLE);
        if (show) {
            forceRedraw();
        } else {
            web.clearFocus();
            hideIme();
        }
        applyTouchMode();
    }

    /**
     * World2D：投影出来的屏幕左上角（像素）。PC 对应 `view.SetPosition(x, y)`。
     *
     * 用 translation 而不是改 LayoutParams：视口尺寸必须保持不变（页面按
     * width x height 布局），移动的只是它在屏幕上的位置。
     */
    void setScreenPosition(int x, int y, boolean onScreen) {
        if (destroyed || !world2d) return;
        final boolean changed = this.onScreen != onScreen;
        this.onScreen = onScreen;
        if (onScreen) {
            overlay.setX(x);
            overlay.setY(y);
        }
        if (changed) {
            applyVisibility();
        }
    }

    boolean isWorld2D() {
        return world2d;
    }

    /**
     * 浏览器焦点（服务端 FocusBrowser / 页面 cef.set_focus）。
     *
     * 与 PC BrowserManager::FocusBrowser 一致：获得焦点的浏览器独占输入，旧焦点被抢走
     * （抢焦点的逻辑在 CefManager，保证多浏览器时只有一个是焦点）。
     */
    void setFocus(boolean focused) {
        this.focused = focused;
        if (destroyed) return;
        if (focused) {
            web.requestFocus();
        } else {
            web.clearFocus();
            hideIme();
        }
        applyTouchMode();
        Log.i(TAG, "setFocus id=" + browserId + " focus=" + focused
                + " touch=" + (overlay.isPassThrough() ? "game" : "page"));
    }

    /**
     * 触摸归属：只有"可见 + 有焦点"的浏览器才消费触摸，其余一律放行给游戏。
     *
     * 这样全屏 HUD（CreateBrowser(focused=false)）不会压死虚拟摇杆，而服务端/页面显式
     * 要焦点时又能整屏接管——与 PC FocusManager 把鼠标锁进 CEF 的时机完全对应。
     */
    private void applyTouchMode() {
        if (destroyed) return;
        // offscreen（WorldObject3D）的页面只用来抓帧，永远不接收触摸。
        final boolean consume = visible && focused && !offscreen;
        overlay.setPassThrough(!consume);
    }

    /**
     * 把当前 WebView 内容读成 RGBA8888 像素（必须在主线程调用）。
     *
     * WebView 没有 GPU 纹理出口，这是把页面送进游戏渲染器的唯一通道：
     * draw(Canvas) 走的是 View 的软件绘制路径，能拿到 Chromium 合成后的内容。
     *
     * 返回的是**复用**的 direct ByteBuffer，不要保存引用：下一次抓帧会覆盖它。
     * native 侧读完立刻上传到纹理，之后才允许下一次抓帧（由 CefManager 的
     * in-flight 标记保证），所以复用是安全的。
     *
     * @return w*h*4 字节，行序自上而下；null = 还没布局完成或读失败
     */
    ByteBuffer captureFrame() {
        if (destroyed) return null;
        final int w = web.getWidth();
        final int h = web.getHeight();
        if (w <= 0 || h <= 0) return null;

        try {
            if (frameBmp == null || frameBmp.getWidth() != w || frameBmp.getHeight() != h) {
                if (frameBmp != null) frameBmp.recycle();
                frameBmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
                frameCanvas = new Canvas(frameBmp);
                frameBuf = ByteBuffer.allocateDirect(w * h * 4);
            }

            final long t0 = System.nanoTime();

            // 页面背景可能是透明的，先铺底色，避免读回未初始化的显存。
            frameCanvas.drawColor(Color.rgb(16, 16, 16));
            web.draw(frameCanvas);

            frameBuf.clear();
            frameBmp.copyPixelsToBuffer(frameBuf);
            frameBuf.position(0);

            lastCaptureMs = (int) Math.max(0, (System.nanoTime() - t0) / 1000000L);
            return frameBuf;
        } catch (Throwable t) {
            Log.w(TAG, "captureFrame id=" + browserId + " failed", t);
            return null;
        }
    }

    /** 上次抓帧耗时（ms）。0 = 还没测到。 */
    int lastCaptureMs() {
        return lastCaptureMs;
    }

    /** 键盘（IME）收起：交还焦点时必须做，否则输入法会卡在游戏画面上方。 */
    private void hideIme() {
        try {
            InputMethodManager imm = (InputMethodManager)
                    activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.hideSoftInputFromWindow(web.getWindowToken(), 0);
            }
        } catch (Throwable ignored) {
        }
    }

    /** 页面内文本输入框拿/失焦点（omp:cef:internal:inputFocusChanged，PC SetInputFocus）。 */
    void setInputFocus(boolean hasFocus) {
        this.inputFocused = hasFocus;
        Log.i(TAG, "setInputFocus id=" + browserId + " hasFocus=" + hasFocus);
    }

    boolean hasInputFocus() {
        return inputFocused;
    }

    /**
     * 软键盘占位：把覆盖层底部顶起 {@code px}，让 WebView 的*视口*真的变小。
     *
     * 为什么必须是缩视口，而不是整窗平移：
     *   adjustPan 只是把整个窗口往上推，页面视口（innerHeight）不变，Chromium 因此
     *   认为"什么都不用做"，靠页面下半部分的 input/按钮照样落在键盘下面 —— 实测
     *   V2312A 上 1b-ime 截图里输入框正好压在键盘下沿。视口一旦变矮，Chromium 会把
     *   聚焦元素滚进可视区，页面 CSS 的 100vh 也跟着正确折算，这才是浏览器语义。
     *
     * 实现用的是 FrameLayout 的 bottomMargin 而不是 setPadding：两者对 MATCH_PARENT
     * 子 View 的测量效果一样，但 margin 不改变 overlay 自身的内容坐标，后面做
     * World2D 坐标换算时不用再减 padding。
     */
    void setImeInset(int px) {
        if (destroyed) return;
        if (px < 0) px = 0;
        if (px == imeInset) return;
        imeInset = px;

        ViewGroup.LayoutParams params = overlay.getLayoutParams();
        if (params instanceof FrameLayout.LayoutParams) {
            FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) params;
            lp.bottomMargin = px;
            overlay.setLayoutParams(lp);
        }
        // 视口变化后 Chromium 需要一次新的布局才会重新滚动，主动催一帧。
        web.requestLayout();
        Log.i(TAG, "setImeInset id=" + browserId + " px=" + px
                + " viewport=" + web.getWidth() + "x" + web.getHeight());
    }

    int imeInset() {
        return imeInset;
    }

    /** PC 端 layer 是绘制顺序，Android 侧映射为 Z 轴高度。 */
    void setLayer(int layer) {
        this.layer = layer;
        applyLayer();
    }

    private void applyLayer() {
        if (destroyed) return;
        // 只有服务端显式指定了层级才抬 Z；setTranslationZ(0) 在部分 ROM 上
        // 会把 View 推进独立 RenderNode，反而被游戏 Surface 盖住。
        if (layer != 0) {
            overlay.setTranslationZ(layer);
        }
    }

    void loadUrl(String url) {
        if (destroyed || url == null || url.length() == 0) return;
        web.loadUrl(url);
    }

    void reload(boolean ignoreCache) {
        if (destroyed) return;
        if (ignoreCache) {
            web.clearCache(true);
        }
        web.reload();
    }

    // ------------------------------------------------------------ 页面事件通道

    /**
     * EmitBrowserEvent 的最后一跳：服务端事件 -> window.__cefDispatch(name, args)。
     *
     * 页面还没加载完就排队（WebView 冷启动实测 1~3 秒），加载完成后按到达顺序
     * 补发；超出上限丢最旧的并告警，绝不无限增长。
     */
    void dispatchEvent(String name, String argsJson) {
        if (destroyed || name == null || name.length() == 0) return;

        if (!pageReady) {
            if (pendingEvents.size() >= MAX_PENDING_EVENTS) {
                String[] dropped = pendingEvents.pollFirst();
                Log.w(TAG, "pending queue full, dropping '" + (dropped != null ? dropped[0] : "?") + "'");
            }
            pendingEvents.addLast(new String[]{name, argsJson != null ? argsJson : "[]"});
            Log.i(TAG, "queue event '" + name + "' for browser " + browserId
                    + " (" + pendingEvents.size() + " pending, page not ready)");
            return;
        }
        evaluateDispatch(name, argsJson);
    }

    private void evaluateDispatch(String name, String argsJson) {
        web.evaluateJavascript(CefJs.dispatchScript(name, argsJson), null);
        Log.i(TAG, "dispatch '" + name + "' -> browser " + browserId
                + " " + (argsJson != null ? argsJson : "[]"));
    }

    /**
     * 客户端内部事件（PC ChatHook::SetChatInputState 的本地分支）：只发给页面，
     * 不上行。走同一条排队路径，页面没加载完时不会丢。
     */
    void dispatchInternalEvent(String name, String argsJson) {
        dispatchEvent(name, argsJson);
    }

    /** 远程 URL 无法改字节流时用 evaluateJavascript 补注入（本地 http://cef/ 页面已内联）。 */
    private void injectBootstrap(String why) {
        web.evaluateJavascript(CefJs.BOOTSTRAP, null);
        Log.i(TAG, "bootstrap injected (" + why + ") browser " + browserId);
    }

    private void flushPendingEvents() {
        if (pendingEvents.isEmpty()) return;
        Log.i(TAG, "flushing " + pendingEvents.size() + " queued event(s) for browser " + browserId);
        while (!pendingEvents.isEmpty()) {
            String[] ev = pendingEvents.pollFirst();
            if (ev != null && ev.length >= 2) {
                evaluateDispatch(ev[0], ev[1]);
            }
        }
    }

    /** 与 CefResourceProvider 的路由判断保持一致：http://cef/ 或 cef://。 */
    private static boolean isCefUrl(String url) {
        if (url == null) return false;
        String lower = url.toLowerCase(Locale.US);
        return lower.startsWith("http://cef/") || lower.startsWith("https://cef/")
                || lower.startsWith("cef://");
    }

    boolean isDestroyed() {
        return destroyed;
    }

    int browserId() {
        return browserId;
    }

    boolean isVisible() {
        return visible;
    }

    boolean isFocused() {
        return focused;
    }

    boolean controlsChatInput() {
        return controlsChatInput;
    }

    void destroy() {
        if (destroyed) return;
        destroyed = true;
        pageReady = false;
        pendingEvents.clear();
        hideIme();
        ViewGroup parent = (ViewGroup) overlay.getParent();
        if (parent != null) {
            parent.removeView(overlay);
        }
        web.stopLoading();
        web.loadUrl("about:blank");
        web.removeAllViews();
        web.removeJavascriptInterface(CefJs.BRIDGE_NAME);
        web.destroy();
        // 抓帧复用缓冲跟着一起放掉，否则离屏页面 destroy 后还占着 0.5MB 位图。
        if (frameBmp != null) {
            frameBmp.recycle();
            frameBmp = null;
        }
        frameCanvas = null;
        frameBuf = null;
        Log.i(TAG, "browser " + browserId + " destroyed");
    }

    private final class CefClient extends WebViewClient {

        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            // 保持页面内导航在同一个 WebView 里；外链一律不交给系统浏览器，
            // 游戏内弹系统浏览器会覆盖游戏画面，与原客户端行为不符。
            return false;
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            String url = request.getUrl() != null ? request.getUrl().toString() : "";
            return CefResourceProvider.open(activity, url);
        }

        @Override
        public void onPageStarted(WebView view, String url, android.graphics.Bitmap favicon) {
            // 新文档：上一次的 __cefDispatch 随旧 JS context 一起消失。
            pageReady = false;
            if (!isCefUrl(url)) {
                // 远程页面拿不到我们的字节流，只能在这里补注入。
                injectBootstrap("onPageStarted remote " + url);
            }
        }

        @Override
        public void onPageFinished(WebView view, String url) {
            pageReady = true;
            // 本地页面已由 CefResourceProvider 内联 bootstrap（无竞态）；这里再补一次
            // 是幂等的保护，覆盖 302 / 直接输入 data: 等没有走拦截器的路径。
            injectBootstrap("onPageFinished");
            flushPendingEvents();
            Log.i(TAG, "browser " + browserId + " onPageFinished " + url);
        }

        @Override
        public void onReceivedError(WebView view, WebResourceRequest request,
                                    android.webkit.WebResourceError error) {
            Log.w(TAG, "browser " + browserId + " onReceivedError "
                    + request.getUrl() + " : " + error.getErrorCode()
                    + " " + error.getDescription());
        }

        @Override
        public void onReceivedHttpError(WebView view, WebResourceRequest request,
                                        WebResourceResponse errorResponse) {
            Log.w(TAG, "browser " + browserId + " onReceivedHttpError "
                    + request.getUrl() + " : " + errorResponse.getStatusCode()
                    + " " + errorResponse.getReasonPhrase());
        }
    }

    private final class CefChromeClient extends WebChromeClient {

        @Override
        public boolean onConsoleMessage(ConsoleMessage message) {
            Log.i(TAG, "console[" + browserId + "] " + message.message()
                    + " @" + message.sourceId() + ":" + message.lineNumber());
            return true;
        }
    }
}
