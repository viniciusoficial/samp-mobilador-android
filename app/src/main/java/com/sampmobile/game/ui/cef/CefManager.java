package com.sampmobile.game.ui.cef;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ApplicationInfo;
import android.graphics.Color;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.SparseArray;
import android.view.KeyEvent;
import android.view.View;
import android.webkit.WebSettings;
import android.webkit.WebView;

import java.io.File;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.List;

/**
 * omp-cef 浏览器管理器（Android 实现）。
 *
 * 对上层（原生 CefBrowser）暴露静态方法，全部可从任意线程调用 —— 原生侧在游戏
 * 线程收到服务端事件后直接调进来，内部统一切到主线程执行 View 操作。
 *
 * 与 PC 版 BrowserManager 的职责对应：
 *
 *   createBrowser        <-> CreateBrowser / CreateBrowserInternal
 *   setBrowserVisible    <-> SetBrowserVisible
 *   destroyBrowser       <-> DestroyBrowser
 *   loadUrl              <-> LoadUrl
 *   reloadBrowser        <-> ReloadBrowser
 *   focusBrowser         <-> FocusBrowser
 *   setBrowserLayer      <-> SetBrowserLayer
 *   nativeOnBrowserCreated -> SendBrowserCreateResult（回到服务端）
 *
 * 不支持的部分（阶段6/7）：World2D / WorldObject3D / 音频空间化 / 屏幕捕获。
 * 这些事件当前被 CefBrowser 记录并忽略，不会伪装成已实现。
 */
public final class CefManager {

    private static final String TAG = "CefManager";

    /** BrowserCreateStatus（omp-cef manager.hpp）。 */
    public static final int CREATE_OK = 0;
    public static final int CREATE_ERROR_GENERIC = 1;
    public static final int CREATE_ERROR_ID_IN_USE = 2;

    private static final Object LOCK = new Object();

    /** 预热只做一次；握手成功后才置 true。 */
    private static boolean sWarmUpStarted = false;
    private static final SparseArray<CefWebView> sBrowsers = new SparseArray<>();
    private static final List<Runnable> sPending = new ArrayList<>();

    private static WeakReference<Activity> sActivity = new WeakReference<>(null);
    private static Handler sMain;
    private static boolean sInstalled = false;

    /** 调试广播（仅 debuggable 包注册）：不依赖服务端即可验证浏览器层。 */
    private static final String ACTION_DEBUG = "com.sampmobile.CEFDEBUG";
    private static BroadcastReceiver sDebugReceiver;

    /**
     * 预热用的隐藏 WebView。
     *
     * 实测（vivo V2312A / Android 13）：首次 new WebView + loadUrl 到真正上屏
     * 需要 6~9 秒（Chromium 进程冷启动 + 首帧合成）。服务端 CreateBrowser 之后
     * 玩家要等这么久才看到界面是不可接受的，所以在 install() 阶段就先建一个
     * 不可见的 WebView 常驻，把引擎和首帧管线热起来。
     */
    private static WebView sWarmupView;

    private CefManager() {
    }

    // ---------------------------------------------------------------- 生命周期

    /** 由 SAMP.onCreate 调用一次（主线程）。 */
    public static void install(Activity activity) {
        if (activity == null) return;

        Handler main = new Handler(Looper.getMainLooper());
        List<Runnable> flush;
        synchronized (LOCK) {
            sActivity = new WeakReference<>(activity);
            sMain = main;
            sInstalled = true;
            flush = new ArrayList<>(sPending);
            sPending.clear();
        }
        Log.i(TAG, "installed, replaying " + flush.size() + " pending command(s)");
        for (Runnable r : flush) {
            main.post(r);
        }

        // 资源层（阶段5）把 .pak 解到 <filesDir>/cef/resources/，Java 侧
        // CefResourceProvider 就从这个目录给 http://cef/ 供文件，两边必须同根。
        File cefRoot = new File(activity.getFilesDir(), "cef");
        nativeSetStorageRoot(cefRoot.getAbsolutePath());

        registerDebugReceiver(activity, main);
        installImeWatcher(activity);
        // 不在这里预热：普通（非 CEF）服务器根本不需要 Chromium，
        // 无条件拉一个隐藏 WebView 会让所有玩家白付内存与启动时间。
        // 等握手成功、确认是 CEF 服后再预热，见 onTransportConnected()。
    }

    /**
     * 握手成功（确认对端是 CEF 服务器）后调用，此时才值得起 Chromium。
     *
     * 由 C++ 侧在 CefNetwork 进入 Connected 时回调。幂等：重复调用无副作用。
     */
    public static void onTransportConnected() {
        final Activity activity;
        final Handler main;
        synchronized (LOCK) {
            if (sWarmUpStarted) return;
            sWarmUpStarted = true;
            activity = sActivity.get();
            main = sMain;
        }
        if (activity == null || main == null) return;
        Log.i(TAG, "transport connected -> warming up WebView");
        warmUp(activity, main);
    }

    /**
     * 建立常驻的隐藏 WebView，把 Chromium 进程与首帧管线提前热起来。
     * 不影响布局（1x1、INVISIBLE、不接收触摸）。
     */
    private static void warmUp(final Activity activity, Handler main) {
        main.post(() -> {
            try {
                WebView view = new WebView(activity);
                view.setBackgroundColor(Color.TRANSPARENT);
                view.setLayerType(View.LAYER_TYPE_HARDWARE, null);
                WebSettings s = view.getSettings();
                s.setJavaScriptEnabled(true);
                s.setDomStorageEnabled(true);
                view.loadUrl("about:blank");

                android.view.ViewGroup decor =
                        (android.view.ViewGroup) activity.getWindow().getDecorView();
                decor.addView(view, new android.widget.FrameLayout.LayoutParams(1, 1));
                view.setVisibility(View.INVISIBLE);

                synchronized (LOCK) {
                    sWarmupView = view;
                }
                Log.i(TAG, "warmup WebView attached");
            } catch (Throwable t) {
                Log.w(TAG, "warmup failed", t);
            }
        });
    }

    /** 由 SAMP.onDestroy 调用；拆除全部 WebView，避免跨越 Activity 生命周期泄漏。 */
    public static void shutdown() {
        Activity activity = sActivity.get();
        BroadcastReceiver receiver;
        synchronized (LOCK) {
            receiver = sDebugReceiver;
            sDebugReceiver = null;
        }
        if (receiver != null && activity != null) {
            try {
                activity.unregisterReceiver(receiver);
            } catch (Throwable ignored) {
            }
        }

        List<CefWebView> victims;
        WebView warmup;
        synchronized (LOCK) {
            sInstalled = false;
            victims = new ArrayList<>();
            for (int i = 0; i < sBrowsers.size(); i++) {
                victims.add(sBrowsers.valueAt(i));
            }
            sBrowsers.clear();
            sPending.clear();
            warmup = sWarmupView;
            sWarmupView = null;
            sActivity = new WeakReference<>(null);
        }
        Handler main = sMain;
        Runnable task = () -> {
            for (CefWebView view : victims) {
                try {
                    view.destroy();
                } catch (Throwable t) {
                    Log.w(TAG, "destroy failed", t);
                }
            }
            if (warmup != null) {
                try {
                    android.view.ViewGroup parent =
                            (android.view.ViewGroup) warmup.getParent();
                    if (parent != null) parent.removeView(warmup);
                    warmup.loadUrl("about:blank");
                    warmup.destroy();
                } catch (Throwable t) {
                    Log.w(TAG, "warmup destroy failed", t);
                }
            }
        };
        if (main != null) {
            main.post(task);
        } else {
            task.run();
        }
        Log.i(TAG, "shutdown, " + victims.size() + " browser(s) destroyed");
    }

    public static int browserCount() {
        synchronized (LOCK) {
            return sBrowsers.size();
        }
    }

    // ------------------------------------------------------- 原生侧调用（任意线程）

    public static void createBrowser(int id, String url, boolean focused, boolean controlsChat) {
        post(() -> doCreateBrowser(id, url, focused, controlsChat));
    }

    /**
     * World2D（阶段7）：服务端 `CreateWorld2DBrowser`。
     *
     * PC: `BrowserManager::CreateWorld2DBrowser` -> windowless 浏览器 + `View::Create(width, height)`，
     * 之后每帧把世界坐标投影到屏幕，用 `view.SetPosition(x, y)` 摆放（manager.cpp:1569）。
     * Android: WebView 的视口就是服务端给的 width×height 像素，位置由 native 每帧投影后
     * 经 {@link #setWorld2DScreen} 推过来。因此页面内容、尺寸、锚点语义与 PC 一致，
     * 差别只在"页面不是画进游戏纹理，而是叠在游戏画面上"（无世界遮挡）。
     */
    public static void createWorld2DBrowser(int id, String url,
                                            float worldX, float worldY, float worldZ,
                                            float width, float height,
                                            float offsetZ, float pivotX, float pivotY) {
        post(() -> doCreateWorld2D(id, url, worldX, worldY, worldZ,
                width, height, offsetZ, pivotX, pivotY));
    }

    /**
     * native 每帧投影结果（游戏线程）-> 主线程摆放。
     *
     * 相机一动就会每帧来一次，如果每次都 post 一个 Runnable，主线程队列会被
     * 位置更新灌满，离屏抓帧（WorldObject3D）根本挤不进去。这里只保留"最新
     * 一帧"：已经有一个 Runnable 在排队时只更新参数，不重复投递。语义等价，
     * 因为最终摆放的就是最后一次投影的结果。
     */
    public static void setWorld2DScreen(int id, int x, int y, boolean onScreen) {
        synchronized (sWorld2DPending) {
            sWorld2DPending.put(id, new int[]{x, y, onScreen ? 1 : 0});
            if (!sWorld2DQueued.add(id)) return;
        }
        post(() -> {
            final int[] p;
            synchronized (sWorld2DPending) {
                p = sWorld2DPending.remove(id);
                sWorld2DQueued.remove(id);
            }
            if (p == null) return;
            CefWebView view = find(id);
            if (view == null) return;
            view.setScreenPosition(p[0], p[1], p[2] != 0);
        });
    }

    /** World2D 待摆放的最新位置（见 {@link #setWorld2DScreen}）。 */
    private static final java.util.HashMap<Integer, int[]> sWorld2DPending =
            new java.util.HashMap<>();
    private static final java.util.HashSet<Integer> sWorld2DQueued = new java.util.HashSet<>();

    // ------------------------------------------------- WorldObject3D（阶段7 第二批）

    /** 最近一次抓到的页面帧，按 browserId 存放。游戏线程取走，主线程填入。 */
    private static final java.util.HashMap<Integer, java.nio.ByteBuffer> sWorldFrames =
            new java.util.HashMap<>();

    /**
     * 每个离屏浏览器的上一次光栅化耗时（ms），native 用来自适应抓帧间隔。
     * 独立于 sWorldFrames：即使这一次没有新帧，native 也要能拿到旧数据。
     */
    private static final java.util.HashMap<Integer, Integer> sWorldFrameCost =
            new java.util.HashMap<>();

    /**
     * 正在抓帧的 browserId。draw(Canvas) 偶尔会慢，这里保证同一时刻只有一个
     * 抓帧任务在主线程排队，避免每 100ms 的请求把它堆满。
     */
    private static final java.util.HashSet<Integer> sCaptureInFlight = new java.util.HashSet<>();

    /**
     * CreateBrowser 的离屏变体（native 调用，游戏线程）。
     *
     * 页面不显示给玩家，只用来抓帧；尺寸就是服务端给的纹理尺寸。
     */
    public static void createWorldObjectBrowser(int id, String url, int w, int h) {
        post(() -> doCreateWorldObject(id, url, w, h));
    }

    /**
     * 取上一帧并请求下一帧（native 按自适应间隔调，游戏线程）。
     *
     * 不能在调用线程直接 draw：Canvas 绘制必须在主线程，而这里拿到的是游戏
     * 线程。所以改成"取走缓存 + 投递下一次抓帧"，两边都不阻塞。
     *
     * 返回的是复用缓冲的引用，native 要立刻上传到纹理，不能长期持有。
     */
    public static java.nio.ByteBuffer captureWorldObject(int id) {
        java.nio.ByteBuffer ready;
        synchronized (sWorldFrames) {
            ready = sWorldFrames.remove(id);
        }
        synchronized (sCaptureInFlight) {
            if (!sCaptureInFlight.add(id)) {
                // 上一帧还没画完，直接把现有的（或 null）交回去。
                return ready;
            }
        }
        post(() -> {
            try {
                CefWebView view = find(id);
                if (view != null) {
                    java.nio.ByteBuffer px = view.captureFrame();
                    if (px != null) {
                        synchronized (sWorldFrames) {
                            sWorldFrames.put(id, px);
                        }
                        synchronized (sWorldFrameCost) {
                            sWorldFrameCost.put(id, view.lastCaptureMs());
                        }
                    }
                }
            } finally {
                synchronized (sCaptureInFlight) {
                    sCaptureInFlight.remove(id);
                }
            }
        });
        return ready;
    }

    /**
     * 上一次离屏抓帧的耗时（ms），0 = 还没有数据。
     *
     * native 用它决定下一次抓帧的间隔：页面越复杂 draw 越慢，就抓得越稀，
     * 而不是让请求堆在主线程队列里。
     */
    public static int worldObjectCaptureCost(int id) {
        synchronized (sWorldFrameCost) {
            Integer v = sWorldFrameCost.get(id);
            return v != null ? v : 0;
        }
    }

    private static void doCreateWorldObject(int id, String url, int w, int h) {
        Activity activity = sActivity.get();
        if (activity == null) {
            Log.e(TAG, "createWorldObject(" + id + "): activity gone");
            reportCreateResult(id, false, CREATE_ERROR_GENERIC, "Activity unavailable");
            return;
        }
        synchronized (LOCK) {
            if (sBrowsers.get(id) != null) {
                Log.e(TAG, "createWorldObject(" + id + "): id already in use");
                reportCreateResult(id, false, CREATE_ERROR_ID_IN_USE, "Browser id already in use");
                return;
            }
        }

        final int pw = clamp(w, 1, 1024);
        final int ph = clamp(h, 1, 1024);

        CefWebView view;
        try {
            // offscreen=true：几乎透明、不接收触摸，只负责出帧。
            view = new CefWebView(activity, id, url, false, false, pw, ph, true);
        } catch (Throwable t) {
            Log.e(TAG, "createWorldObject(" + id + "): WebView creation failed", t);
            reportCreateResult(id, false, CREATE_ERROR_GENERIC, String.valueOf(t.getMessage()));
            return;
        }

        try {
            view.attach();
        } catch (Throwable t) {
            Log.e(TAG, "createWorldObject(" + id + "): attach failed", t);
            view.destroy();
            reportCreateResult(id, false, CREATE_ERROR_GENERIC, String.valueOf(t.getMessage()));
            return;
        }

        synchronized (LOCK) {
            sBrowsers.put(id, view);
        }
        view.setVisible(true);
        Log.i(TAG, "createWorldObject " + id + " " + pw + "x" + ph + " url=" + url);
        reportCreateResult(id, true, 0, "");
    }

    public static void destroyBrowser(int id) {
        post(() -> {
            CefWebView view;
            synchronized (LOCK) {
                view = sBrowsers.get(id);
                sBrowsers.remove(id);
            }
            if (view == null) {
                Log.w(TAG, "destroyBrowser: no browser " + id);
                // Native may still hold a WorldObject3D entry for this id (only
                // the Java side was destroyed); drop it anyway.
                forgetWorldObject(id);
                return;
            }
            view.destroy();
            forgetWorldObject(id);
            synchronized (sWorldFrames) {
                sWorldFrames.remove(id);
            }
            synchronized (sWorldFrameCost) {
                sWorldFrameCost.remove(id);
            }
            synchronized (sWorld2DPending) {
                sWorld2DPending.remove(id);
                sWorld2DQueued.remove(id);
            }
            applyImeResize(isAnyBrowserFocused());
        });
    }

    /**
     * 告诉 native 丢掉这个 id 的 WorldObject3D 条目（纹理 + 抓帧调度）。
     *
     * 正常路径上服务端会发 DestroyBrowser，native 自己会清；这里兜的是"只有
     * Java 侧被销毁"的情况（调试广播、服务端没确认的销毁）。不清的话那张纹理
     * 会一直留着，而且每 3 秒还会有一轮注定拿不到帧的抓帧尝试——实测表现为
     * worldobjperf 连续报 frames=0 却始终不退场。
     */
    private static void forgetWorldObject(int id) {
        try {
            nativeForgetWorldObject(id);
        } catch (Throwable t) {
            // 只可能是符号没导出（忘了 extern "C"，名字被 C++ mangle 了）。
            // 之前这里的 catch 是空的，于是"改了但完全没生效"且没有任何痕迹；
            // 现在至少留一条日志，并且 verify_all.sh 会直接 nm 检查符号。
            Log.w(TAG, "nativeForgetWorldObject(" + id + ") unavailable", t);
        }
    }

    private static native void nativeForgetWorldObject(int id);

    public static void setBrowserVisible(int id, boolean visible) {
        post(() -> {
            CefWebView view = find(id);
            if (view == null) {
                Log.w(TAG, "setBrowserVisible: no browser " + id);
                return;
            }
            view.setVisible(visible);
            // 隐藏的浏览器不保留退让，否则再显示时会顶着一块空白。
            pushImeInset();
            Log.i(TAG, "browser " + id + " visible=" + visible);
        });
    }

    public static void loadUrl(int id, String url) {
        post(() -> {
            CefWebView view = find(id);
            if (view == null) {
                Log.w(TAG, "loadUrl: no browser " + id);
                return;
            }
            view.loadUrl(url);
            Log.i(TAG, "browser " + id + " loadUrl " + url);
        });
    }

    public static void reloadBrowser(int id, boolean ignoreCache) {
        post(() -> {
            CefWebView view = find(id);
            if (view == null) {
                Log.w(TAG, "reloadBrowser: no browser " + id);
                return;
            }
            view.reload(ignoreCache);
            Log.i(TAG, "browser " + id + " reload ignoreCache=" + ignoreCache);
        });
    }

    public static void focusBrowser(int id, boolean focus) {
        post(() -> {
            CefWebView view = find(id);
            if (view == null) {
                Log.w(TAG, "focusBrowser: no browser " + id);
                return;
            }
            if (focus) {
                // 单焦点语义与 PC 一致：新获得焦点的浏览器会抢占旧焦点。
                for (int i = 0; i < sBrowsers.size(); i++) {
                    CefWebView other = sBrowsers.valueAt(i);
                    if (other != view && other.isFocused()) {
                        other.setFocus(false);
                    }
                }
                // 页面要整屏接管输入：先把游戏侧没抬起的手指清掉，否则摇杆会卡住。
                cancelGameTouches();
            }
            view.setFocus(focus);
            applyImeResize(isAnyBrowserFocused());
            Log.i(TAG, "browser " + id + " focus=" + focus
                    + " (any focused: " + isAnyBrowserFocused() + ")");
        });
    }

    public static void setBrowserLayer(int id, int layer) {
        post(() -> {
            CefWebView view = find(id);
            if (view == null) {
                Log.w(TAG, "setBrowserLayer: no browser " + id);
                return;
            }
            view.setLayer(layer);
        });
    }

    /**
     * EmitBrowserEvent：服务端定向发给某个浏览器页面的自定义事件。
     *
     * PC: app.cpp SendEmitToBrowser -> renderer `emit_event` -> events_[name] 回调。
     * Android: CefWebView.dispatchEvent -> window.__cefDispatch(name, args)。
     * 页面尚未加载完成时事件在 view 内排队，onPageFinished 后按序补齐，与 PC
     * 端 pending_emits_ 的行为一致。
     */
    public static void emitBrowserEvent(int id, String name, String argsJson) {
        post(() -> {
            CefWebView view = find(id);
            if (view == null) {
                Log.w(TAG, "emitBrowserEvent '" + name + "': no browser " + id);
                return;
            }
            view.dispatchEvent(name, argsJson);
        });
    }

    /**
     * 页面 cef.emit(...) 上行。运行在 WebView 的 JavaBridge 线程，直接进 native
     * （CefNetwork 的发送路径是线程安全的），避免主线程排队带来的额外延迟。
     */
    static void onJsEmit(int browserId, String name, String argsJson) {
        try {
            nativeEmitFromJs(browserId, name, argsJson);
        } catch (Throwable t) {
            Log.w(TAG, "nativeEmitFromJs failed (" + name + ")", t);
        }
    }

    /** 会话断开：与 PC app.cpp 的断线清理等价，销毁全部页面，避免僵尸 UI。 */
    public static void destroyAllBrowsers() {
        post(() -> {
            List<CefWebView> victims;
            synchronized (LOCK) {
                victims = new ArrayList<>();
                for (int i = 0; i < sBrowsers.size(); i++) {
                    victims.add(sBrowsers.valueAt(i));
                }
                sBrowsers.clear();
            }
            for (CefWebView view : victims) {
                try {
                    view.destroy();
                } catch (Throwable t) {
                    Log.w(TAG, "destroyAll: destroy failed", t);
                }
            }
            if (!victims.isEmpty()) {
                Log.i(TAG, "destroyAllBrowsers: " + victims.size() + " closed");
            }
            applyImeResize(isAnyBrowserFocused());
        });
    }

    // ------------------------------------------------- 输入（阶段6：键鼠 / IME / 返回键）

    /** 服务端 SetKeyCapture：打开后才上行 PressKey。默认关闭（与 PC key_capture_enabled_ 一致）。 */
    private static volatile boolean sKeyCaptureEnabled = false;

    /** 服务端 EnableKey：白名单按 Windows VK 索引，默认全关（PC key_allowed_{} 全零）。 */
    private static final boolean[] sKeyAllowed = new boolean[256];

    /** 服务端 ToggleChatInput：false 时游戏聊天框不再打开（PC FocusManager::ShouldBlockChat）。 */
    private static volatile boolean sChatInputEnabled = true;

    /** 页面内文本输入框当前所在的浏览器，-1 = 无（PC input_focused_browser_id_）。 */
    private static volatile int sInputFocusedId = -1;

    /** 切 adjustResize 之前窗口的 softInputMode，-1 = 当前没切过。 */
    private static int sOriginalSoftInputMode = -1;

    /** 软键盘当前占掉的高度（px），0 = 键盘没弹出。由 {@link #sampleImeInset()} 维护。 */
    private static int sImeInset = 0;

    /** 输入焦点刚变化时连续采样几次，等输入法动画结束。 */
    private static final int[] IME_SAMPLE_DELAYS = {80, 200, 400, 700, 1200};

    public static void setKeyCaptureEnabled(boolean enabled) {
        sKeyCaptureEnabled = enabled;
        Log.i(TAG, "key capture " + (enabled ? "enabled" : "disabled"));
    }

    public static void enableKey(int key, boolean enabled) {
        if (key < 0 || key > 255) return;
        sKeyAllowed[key] = enabled;
        Log.i(TAG, "key VK " + key + " (0x" + Integer.toHexString(key) + ") "
                + (enabled ? "enabled" : "disabled"));
    }

    public static void setChatInputEnabled(boolean enabled) {
        sChatInputEnabled = enabled;
        Log.i(TAG, "game chat input " + (enabled ? "enabled" : "disabled"));
    }

    /**
     * 页面内输入框焦点（omp:cef:internal:inputFocusChanged）。
     *
     * PC 把它和"浏览器焦点"分开存：浏览器焦点决定鼠标/键盘给谁，输入框焦点只用于
     * 判断聊天框要不要让位（ShouldBlockChat）。Android 保持同样的两套状态。
     */
    public static void setInputFocus(int browserId, boolean hasFocus) {
        if (hasFocus) {
            sInputFocusedId = browserId;
        } else if (sInputFocusedId == browserId) {
            sInputFocusedId = -1;
        }
        CefWebView view = find(browserId);
        if (view != null) view.setInputFocus(hasFocus);
        Log.i(TAG, "input focus browser=" + browserId + " hasFocus=" + hasFocus
                + " (current=" + sInputFocusedId + ")");

        // 输入框拿到/失去焦点是"键盘即将弹出/收起"的唯一可靠信号，在这里补采几次：
        // 输入法窗口动画要几百毫秒，单次采样容易量到 0 或量到半截高度。
        if (hasFocus) {
            Handler main;
            synchronized (LOCK) {
                main = sMain;
            }
            if (main != null) {
                for (final int delay : IME_SAMPLE_DELAYS) {
                    main.postDelayed(new Runnable() {
                        @Override
                        public void run() {
                            sampleImeInset();
                        }
                    }, delay);
                }
            }
        }
    }

    /**
     * 游戏聊天框是否应该被页面接管（PC FocusManager::ShouldBlockChat 的等价实现）。
     *
     * true = 玩家在页面输入框里打字，游戏自己的聊天输入不该再抢键盘。
     */
    public static boolean shouldBlockChat() {
        final int id = sInputFocusedId;
        // 三级判定（chat 被关 / 无输入焦点 / 该页 controls_chat_input）在 CefLogic
        // 里，由离线单测覆盖；这里只负责取页面状态。
        CefWebView view = id < 0 ? null : find(id);
        return CefLogic.shouldBlockChat(sChatInputEnabled, id,
                view != null && view.controlsChatInput());
    }

    /**
     * 游戏聊天输入开/关（UI._onKeyboardVisibilityChanged -> 这里）。
     *
     * PC ChatHook::SetChatInputState 做两件事：给每个页面发本地事件
     * omp:cef:internal:chatInputState，再发 ClientEmitEvent(ChatInputState, browserId=-1)。
     * 两者这里都照做。
     */
    public static void onGameChatStateChanged(final boolean open) {
        post(() -> {
            final String argsJson = open ? "[true]" : "[false]";
            List<CefWebView> all;
            synchronized (LOCK) {
                all = new ArrayList<>();
                for (int i = 0; i < sBrowsers.size(); i++) {
                    all.add(sBrowsers.valueAt(i));
                }
            }
            for (CefWebView view : all) {
                view.dispatchInternalEvent("omp:cef:internal:chatInputState", argsJson);
            }
            try {
                nativeSendChatInputState(open);
            } catch (Throwable t) {
                Log.w(TAG, "nativeSendChatInputState failed", t);
            }
            Log.i(TAG, "chat input " + open + " -> " + all.size() + " page(s) + server");
        });
    }

    /**
     * 按键上报（SAMP.dispatchKeyEvent 调用，主线程）。
     *
     * 与 PC 逐条对齐：只有 key_capture_enabled 且该 VK 在白名单里才发；按下自动重复
     * 一律丢弃（PC: if (!(down && repeat))），所以 repeat 字段在线上恒为 false。
     * 不做任何 UI 操作，因此可以直接在当前线程把事件送进 KCP。
     */
    public static void handleKeyEvent(KeyEvent event) {
        if (event == null || !sKeyCaptureEnabled) return;

        final int action = event.getAction();
        final boolean down;
        if (action == KeyEvent.ACTION_DOWN) {
            down = true;
        } else if (action == KeyEvent.ACTION_UP) {
            down = false;
        } else {
            return;   // ACTION_MULTIPLE 等：PC 只处理 WM_KEYDOWN/WM_KEYUP
        }

        final boolean repeat = down && event.getRepeatCount() > 0;
        final int vk = CefKeys.vkOf(event.getKeyCode());
        // 门控条件（捕获开关 / 白名单 / 丢弃自动重复）集中在 CefLogic，离线单测覆盖。
        if (!CefLogic.shouldSendKey(sKeyCaptureEnabled, sKeyAllowed, vk, down, repeat)) return;

        final int scan = CefKeys.scanOf(event.getKeyCode());
        final int modifiers = CefKeys.modifiersOfMetaState(event.getMetaState());
        try {
            nativeSendPressKey(vk, scan, modifiers, down, repeat);
            Log.i(TAG, "PressKey vk=" + vk + " scan=" + scan + " mod=" + modifiers
                    + " down=" + down + " repeat=" + repeat);
        } catch (Throwable t) {
            Log.w(TAG, "nativeSendPressKey failed", t);
        }
    }

    public static boolean isAnyBrowserFocused() {
        synchronized (LOCK) {
            for (int i = 0; i < sBrowsers.size(); i++) {
                if (sBrowsers.valueAt(i).isFocused()) return true;
            }
        }
        return false;
    }

    /**
     * 返回键：有浏览器拿着焦点时，第一下只把焦点还给游戏，不销毁页面
     * （页面生命周期属于服务端，客户端不能替它 DestroyBrowser）。
     */
    public static boolean onBackPressed() {
        int focusedId = -1;
        synchronized (LOCK) {
            for (int i = 0; i < sBrowsers.size(); i++) {
                CefWebView view = sBrowsers.valueAt(i);
                if (view.isFocused()) {
                    focusedId = view.browserId();
                    break;
                }
            }
        }
        if (focusedId < 0) return false;
        focusBrowser(focusedId, false);
        Log.i(TAG, "back pressed -> focus released (browser " + focusedId + ")");
        return true;
    }

    /** 应用切到后台 / 重建时收起输入法并让出游戏输入。 */
    public static void releaseAllFocus() {
        List<CefWebView> all;
        synchronized (LOCK) {
            all = new ArrayList<>();
            for (int i = 0; i < sBrowsers.size(); i++) {
                all.add(sBrowsers.valueAt(i));
            }
        }
        for (CefWebView view : all) {
            if (view.isFocused()) {
                view.setFocus(false);
            }
        }
        applyImeResize(false);
    }

    /**
     * 页面需要键盘时把窗口切成 adjustResize，同时把软键盘高度退让给页面视口。
     *
     * 为什么两步都要做：
     *
     *   1) 窗口模式。Activity 清单里是 adjustPan（游戏自己的聊天框靠平移就够），但
     *      adjustPan 只是把整窗往上推，Chromium 收不到"可视区域变小"的信号，页面里
     *      靠下的 input 会被键盘盖住且不会自动滚动 —— 实测（V2312A）输入框正好压在
     *      键盘下沿。切成 adjustResize 后窗口本身会缩。
     *
     *   2) 视口退让。本 ROM 上 setSoftInputMode(adjustResize) 只改了请求值，DecorView
     *      的测量并没有跟着变（dumpsys window 仍是 sim={adjust=pan}）。而 CEF 覆盖层
     *      是直接挂在 DecorView 上的 MATCH_PARENT 子 View，即使 android.R.id.content
     *      缩了它也不会缩。所以最终靠 {@link #sampleImeInset()} 量出键盘真实高度，把
     *      覆盖层底部顶起同样高度，WebView 的 innerHeight 才会真的变小、Chromium 才会
     *      把聚焦元素滚进可视区。
     *
     * 两步是自洽的：窗口若真被 resize，DecorView 高度就等于可见高度，量出来的 inset
     * 自然是 0，不会重复退让。只在"浏览器持有焦点"期间生效 —— 这时游戏输入本来就让
     * 出去了。
     */
    private static void applyImeResize(boolean enable) {
        Activity activity = sActivity.get();
        if (activity == null) return;
        try {
            android.view.Window window = activity.getWindow();
            final int current = window.getAttributes().softInputMode;
            if (enable) {
                if (sOriginalSoftInputMode < 0) {
                    sOriginalSoftInputMode = current;
                }
                final int state = current & android.view.WindowManager.LayoutParams.SOFT_INPUT_MASK_STATE;
                window.setSoftInputMode(state | android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
                Log.i(TAG, "softInputMode -> adjustResize (was 0x" + Integer.toHexString(current) + ")");
            } else if (sOriginalSoftInputMode >= 0) {
                window.setSoftInputMode(sOriginalSoftInputMode);
                Log.i(TAG, "softInputMode restored to 0x" + Integer.toHexString(sOriginalSoftInputMode));
                sOriginalSoftInputMode = -1;
            }
        } catch (Throwable t) {
            Log.w(TAG, "setSoftInputMode failed", t);
        }
        // 焦点/可见性变了：立刻按当前 inset 重新分配一次（不退让的浏览器清零）。
        pushImeInset();
        sampleImeInset();
    }

    /**
     * 装一个只在布局变化时跑一次的采样器（不轮询）。
     *
     * 键盘弹出在多数 ROM 上都会触发一次 traversal，因此全局布局回调足够。但
     * adjustPan 路径下窗口尺寸不变，个别 ROM 不会重新布局，所以页面报告
     * inputFocusChanged 时还会用 {@link #IME_SAMPLE_DELAYS} 补采几次，避免
     * "键盘明明弹了却没量到"。采样次数有上限，不会常驻耗电。
     */
    private static void installImeWatcher(final Activity activity) {
        try {
            View decor = activity.getWindow().getDecorView();
            decor.getViewTreeObserver().addOnGlobalLayoutListener(
                    new android.view.ViewTreeObserver.OnGlobalLayoutListener() {
                        @Override
                        public void onGlobalLayout() {
                            sampleImeInset();
                        }
                    });
        } catch (Throwable t) {
            Log.w(TAG, "installImeWatcher failed", t);
        }
    }

    /** 量一次"可见区域"底部与 DecorView 底部的差额，作为软键盘高度。 */
    private static void sampleImeInset() {
        Activity activity = sActivity.get();
        if (activity == null) return;
        final View decor = activity.getWindow().getDecorView();
        if (decor == null) return;
        final int height = decor.getHeight();
        if (height <= 0) return;
        try {
            final android.graphics.Rect frame = new android.graphics.Rect();
            decor.getWindowVisibleDisplayFrame(frame);
            // 用 imeMarginOf 而不是 imeInsetOf：adjustResize 生效时窗口已经变矮，
            // 再按"屏幕被键盘占了多少"去减 bottomMargin 就是二次退让 —— 页面会被
            // 永久压在键盘上沿（真机现象：键盘收起后 viewport 只剩 2298x372）。
            final android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
            try {
                activity.getWindowManager().getDefaultDisplay().getRealMetrics(dm);
            } catch (Throwable ignored) {
                dm.heightPixels = height;
            }
            final int inset = CefLogic.imeMarginOf(height, frame.bottom, dm.heightPixels);
            if (inset != sImeInset) {
                sImeInset = inset;
                Log.i(TAG, "ime margin=" + inset + "px (decor " + height
                        + ", visible bottom " + frame.bottom + ", screen " + dm.heightPixels + ")");
                pushImeInset();
            }
        } catch (Throwable t) {
            Log.w(TAG, "sampleImeInset failed", t);
        }
    }

    /** 把当前软键盘高度写给"可见且有焦点"的浏览器，其余清零。 */
    private static void pushImeInset() {
        for (int i = 0; i < sBrowsers.size(); i++) {
            CefWebView view = sBrowsers.valueAt(i);
            final int want = (view.isVisible() && view.isFocused()) ? sImeInset : 0;
            if (view.imeInset() != want) {
                view.setImeInset(want);
            }
        }
    }

    /** 游戏侧手势清零（页面开始接管触摸时调用，避免摇杆粘键）。 */
    private static void cancelGameTouches() {
        Activity activity = sActivity.get();
        if (activity instanceof com.sampmobile.game.SAMP) {
            try {
                ((com.sampmobile.game.SAMP) activity).cancelAllNativeTouches();
            } catch (Throwable t) {
                Log.w(TAG, "cancelAllNativeTouches failed", t);
            }
        }
    }

    // ------------------------------------------------------------- 主线程实现

    private static void doCreateBrowser(int id, String url, boolean focused, boolean controlsChat) {
        Activity activity = sActivity.get();
        if (activity == null) {
            Log.e(TAG, "createBrowser(" + id + "): activity gone");
            reportCreateResult(id, false, CREATE_ERROR_GENERIC, "Activity unavailable");
            return;
        }

        synchronized (LOCK) {
            if (sBrowsers.get(id) != null) {
                Log.e(TAG, "createBrowser(" + id + "): id already in use");
                reportCreateResult(id, false, CREATE_ERROR_ID_IN_USE, "Browser id already in use");
                return;
            }
        }

        // CreateBrowser(focused=true)：新页面直接接管输入，旧焦点让位（单焦点语义）。
        if (focused) {
            for (int i = 0; i < sBrowsers.size(); i++) {
                CefWebView other = sBrowsers.valueAt(i);
                if (other.isFocused()) {
                    other.setFocus(false);
                }
            }
            cancelGameTouches();
        }

        CefWebView view;
        try {
            view = new CefWebView(activity, id, url, focused, controlsChat);
        } catch (Throwable t) {
            Log.e(TAG, "createBrowser(" + id + "): WebView creation failed", t);
            reportCreateResult(id, false, CREATE_ERROR_GENERIC, String.valueOf(t.getMessage()));
            return;
        }

        try {
            view.attach();
        } catch (Throwable t) {
            Log.e(TAG, "createBrowser(" + id + "): attach failed", t);
            view.destroy();
            reportCreateResult(id, false, CREATE_ERROR_GENERIC, String.valueOf(t.getMessage()));
            return;
        }

        synchronized (LOCK) {
            sBrowsers.put(id, view);
        }

        // PC 版在 CefBrowser 对象创建完成后即回报成功（不等页面加载完成），
        // 这里 WebView 已挂载，语义等价。
        applyImeResize(isAnyBrowserFocused());
        Log.i(TAG, "browser " + id + " created url=" + url
                + " focused=" + focused + " controlsChat=" + controlsChat);
        reportCreateResult(id, true, CREATE_OK, "Successfully created");
    }

    /** World2D 的创建：固定像素视口、不抢焦点、不接管游戏聊天（PC 同样置 false）。 */
    private static void doCreateWorld2D(int id, String url,
                                        float worldX, float worldY, float worldZ,
                                        float width, float height,
                                        float offsetZ, float pivotX, float pivotY) {
        Activity activity = sActivity.get();
        if (activity == null) {
            Log.e(TAG, "createWorld2D(" + id + "): activity gone");
            reportCreateResult(id, false, CREATE_ERROR_GENERIC, "Activity unavailable");
            return;
        }

        synchronized (LOCK) {
            if (sBrowsers.get(id) != null) {
                Log.e(TAG, "createWorld2D(" + id + "): id already in use");
                reportCreateResult(id, false, CREATE_ERROR_ID_IN_USE, "Browser id already in use");
                return;
            }
        }

        // manager.cpp: int bw = clamp((int)width, 1, 1024); same for height.
        final int w = clamp((int) width, 1, 1024);
        final int h = clamp((int) height, 1, 1024);
        if (width < 1.0f || height < 1.0f) {
            // PC clamps 0 to a 1x1 view, which renders a page nobody can see.
            // Keep the same behaviour but say so loudly, it is almost always a
            // server-side mistake (the Pawn defaults are 0.0).
            Log.w(TAG, "createWorld2D(" + id + "): width/height " + width + "x" + height
                    + " clamped to " + w + "x" + h + " like the PC client");
        }

        CefWebView view;
        try {
            view = new CefWebView(activity, id, url, false, false, w, h);
        } catch (Throwable t) {
            Log.e(TAG, "createWorld2D(" + id + "): WebView creation failed", t);
            reportCreateResult(id, false, CREATE_ERROR_GENERIC, String.valueOf(t.getMessage()));
            return;
        }

        try {
            view.attach();
        } catch (Throwable t) {
            Log.e(TAG, "createWorld2D(" + id + "): attach failed", t);
            view.destroy();
            reportCreateResult(id, false, CREATE_ERROR_GENERIC, String.valueOf(t.getMessage()));
            return;
        }

        synchronized (LOCK) {
            sBrowsers.put(id, view);
        }

        Log.i(TAG, "world2d browser " + id + " created url=" + url
                + " viewport=" + w + "x" + h
                + " world=(" + worldX + "," + worldY + "," + worldZ + ")"
                + " offsetZ=" + offsetZ + " pivot=(" + pivotX + "," + pivotY + ")");
        // PC 的 OnBrowserCreated 对 World2D 也回报 BrowserCreateResult。
        reportCreateResult(id, true, CREATE_OK, "Successfully created");
    }

    private static int clamp(int value, int min, int max) {
        return value < min ? min : (value > max ? max : value);
    }

    private static void reportCreateResult(int id, boolean ok, int code, String reason) {        try {
            nativeOnBrowserCreated(id, ok, code, reason != null ? reason : "");
        } catch (Throwable t) {
            Log.w(TAG, "nativeOnBrowserCreated failed", t);
        }
    }

    private static CefWebView find(int id) {
        synchronized (LOCK) {
            return sBrowsers.get(id);
        }
    }

    private static void post(Runnable task) {
        Handler main;
        synchronized (LOCK) {
            main = sMain;
            if (!sInstalled || main == null) {
                // 管理器尚未安装（游戏线程早于 onCreate 收到事件）：
                // 先排队，install() 时按序重放，不丢事件。
                if (sPending.size() < 64) {
                    sPending.add(task);
                    Log.i(TAG, "queued command until install (" + sPending.size() + ")");
                } else {
                    Log.e(TAG, "pending queue overflow, dropping command");
                }
                return;
            }
        }
        main.post(task);
    }

    /** 由原生 CefBrowser 实现：把创建结果作为 BrowserCreateResult 事件回给服务端。 */
    private static native void nativeOnBrowserCreated(int id, boolean ok, int code, String reason);

    /** window.cef.emit 上行：name + JSON 标量数组，原生侧还原类型后发 ClientEmitEvent。 */
    private static native void nativeEmitFromJs(int browserId, String name, String argsJson);

    /** 按键上行：ClientEmitEvent(PressKey, [vk, scancode, modifiers, down, repeat])。 */
    private static native void nativeSendPressKey(int vk, int scancode, int modifiers,
                                                 boolean down, boolean repeat);

    /** 游戏聊天输入开/关：ClientEmitEvent(ChatInputState, [open])，browserId = -1。 */
    private static native void nativeSendChatInputState(boolean open);

    private static native void nativeDebugConnect(String host, int port, int playerId);

    /** 资源缓存根目录（<filesDir>/cef），DownloadComplete 前解出的页面都在其下。 */
    private static native void nativeSetStorageRoot(String root);

    private static native void nativeDebugDisconnect();

    /** 阶段7 调试入口：在本地玩家头顶放一个 World2D 页面（仅 debuggable 包）。 */
    private static native void nativeDebugWorld2D(int id, float heightAbovePed, float width, float height);

    /** 让所有 World2D（含服务端事件的）上报投影矩形，仅用于验收。 */
    private static native void nativeSetWorld2DProbe(boolean on);
    private static native void nativeDebugWorldObject(int id, int model, float distance);

    // ------------------------------------------------------------- 调试入口

    /**
     * 仅 debuggable 包注册。用途：手机未连游戏服务器时，也能验证
     * CreateBrowser / 可见性 / 销毁与协议回执链路。
     *
     *   adb shell am broadcast -a com.sampmobile.CEFDEBUG -p <pkg> \
     *       --es action connect --es host 127.0.0.1 --ei port 7900 --ei playerid 1
     *   adb shell am broadcast -a com.sampmobile.CEFDEBUG -p <pkg> \
     *       --es action create --ei id 1 --es url http://cef/test/index.html
     *   adb shell am broadcast -a com.sampmobile.CEFDEBUG -p <pkg> \
     *       --es action visible --ei id 1 --ei on 1
     *   adb shell am broadcast -a com.sampmobile.CEFDEBUG -p <pkg> \
     *       --es action destroy --ei id 1
     */
    private static void registerDebugReceiver(Activity activity, Handler main) {
        final boolean debuggable =
                (activity.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (!debuggable) {
            Log.i(TAG, "release build: debug receiver not registered");
            return;
        }

        BroadcastReceiver receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                String action = intent.getStringExtra("action");
                if (action == null) action = "";
                Log.i(TAG, "debug action=" + action);
                switch (action) {
                    case "connect": {
                        String host = intent.getStringExtra("host");
                        int port = intent.getIntExtra("port", 0);
                        int playerId = intent.getIntExtra("playerid", 1);
                        if (host == null || host.length() == 0 || port <= 0) {
                            Log.w(TAG, "debug connect: need host + port");
                            return;
                        }
                        nativeDebugConnect(host, port, playerId);
                        break;
                    }
                    case "disconnect":
                        nativeDebugDisconnect();
                        break;
                    case "create": {
                        int id = intent.getIntExtra("id", 1);
                        String url = intent.getStringExtra("url");
                        if (url == null) url = "";
                        createBrowser(id, url,
                                intent.getIntExtra("focused", 0) != 0,
                                intent.getIntExtra("controls", 0) != 0);
                        break;
                    }
                    case "visible": {
                        int id = intent.getIntExtra("id", 1);
                        setBrowserVisible(id, intent.getIntExtra("on", 1) != 0);
                        break;
                    }
                    case "destroy":
                        destroyBrowser(intent.getIntExtra("id", 1));
                        break;
                    case "loadurl": {
                        int id = intent.getIntExtra("id", 1);
                        loadUrl(id, intent.getStringExtra("url"));
                        break;
                    }
                    case "layer": {
                        int id = intent.getIntExtra("id", 1);
                        setBrowserLayer(id, intent.getIntExtra("layer", 0));
                        break;
                    }
                    case "emit": {
                        // Local injection of an EmitBrowserEvent: verifies the page
                        // channel without a server round trip.
                        int id = intent.getIntExtra("id", 1);
                        String name = intent.getStringExtra("name");
                        if (name == null) name = "";
                        String args = intent.getStringExtra("args");
                        emitBrowserEvent(id, name, args != null ? args : "[]");
                        break;
                    }
                    case "destroyall":
                        destroyAllBrowsers();
                        break;
                    case "keycapture": {
                        setKeyCaptureEnabled(intent.getIntExtra("on", 1) != 0);
                        break;
                    }
                    case "enablekey": {
                        enableKey(intent.getIntExtra("key", 0), intent.getIntExtra("on", 1) != 0);
                        break;
                    }
                    case "chatstate": {
                        // 走游戏自己的入口（UI.showKeyboard），与原生代码打开聊天框时
                        // 完全同一条链路，因此同时也验证了 shouldBlockChat 闸门。
                        boolean open = intent.getIntExtra("on", 1) != 0;
                        Activity activity = sActivity.get();
                        if (activity instanceof com.sampmobile.game.SAMP) {
                            com.sampmobile.game.SAMP samp = (com.sampmobile.game.SAMP) activity;
                            if (samp.ui != null) {
                                samp.ui.showKeyboard(open);
                                Log.i(TAG, "debug chatstate -> ui.showKeyboard(" + open + ")");
                            }
                        }
                        break;
                    }
                    case "focus": {
                        focusBrowser(intent.getIntExtra("id", 1), intent.getIntExtra("on", 1) != 0);
                        break;
                    }
                    case "world2d": {
                        // 阶段7 调试入口：把 World2D 页面放到玩家头顶，验证投影与命中。
                        // 世界坐标由 native 读本地 ped 位置算，这里只传尺寸/高度。
                        int id = intent.getIntExtra("id", 12);
                        float above = intent.getIntExtra("above", 150) / 100.0f;
                        float w = intent.getIntExtra("w", 512);
                        float h = intent.getIntExtra("h", 256);
                        nativeDebugWorld2D(id, above, w, h);
                        break;
                    }
                    case "world2dprobe": {
                        // 验收用：原版插件建的 World2D 不会带 debugProbe，打开这个
                        // 开关后客户端会和调试入口一样上报 debug:world2d。
                        nativeSetWorld2DProbe(intent.getIntExtra("on", 1) != 0);
                        break;
                    }
                    case "worldobj": {
                        // 阶段7 第二批调试入口：在玩家前方生成一个真实物体，把页面
                        // 贴到它表面。单机模式下没有 SA-MP 物体池，协议路径的
                        // objectId 无从解析，所以贴图链路只能这样验。
                        int id = intent.getIntExtra("id", 12);
                        int model = intent.getIntExtra("model", 19377);
                        float dist = intent.getIntExtra("dist", 6);
                        nativeDebugWorldObject(id, model, dist);
                        break;
                    }
                    case "worldobjsize": {
                        // 只建离屏页面（不生成物体），用来单独验证抓帧。
                        int id = intent.getIntExtra("id", 12);
                        int w = intent.getIntExtra("w", 512);
                        int h = intent.getIntExtra("h", 256);
                        String url = intent.getStringExtra("url");
                        if (url == null) url = "http://cef/test/index.html";
                        createWorldObjectBrowser(id, url, w, h);
                        break;
                    }
                    case "status":
                        Log.i(TAG, "status: browsers=" + browserCount()
                                + " focused=" + isAnyBrowserFocused()
                                + " keyCapture=" + sKeyCaptureEnabled
                                + " inputFocus=" + sInputFocusedId);
                        break;
                    default:
                        Log.w(TAG, "unknown debug action " + action);
                        break;
                }
            }
        };

        try {
            IntentFilter filter = new IntentFilter(ACTION_DEBUG);
            if (Build.VERSION.SDK_INT >= 33) {
                activity.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED);
            } else {
                activity.registerReceiver(receiver, filter);
            }
            synchronized (LOCK) {
                sDebugReceiver = receiver;
            }
            Log.i(TAG, "debug receiver ready: " + ACTION_DEBUG);
        } catch (Throwable t) {
            Log.w(TAG, "register debug receiver failed", t);
        }
    }
}
