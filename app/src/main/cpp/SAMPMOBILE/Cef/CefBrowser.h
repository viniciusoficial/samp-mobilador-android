#pragma once

// Overlay2D browser layer + JS bridge for the omp-cef compatible Android client.
//
// Mirrors the event-dispatch half of omp-cef src/client/core/app.cpp
// (App::OnPacketReceived -> CefEvent::Server::*) and the renderer-side
// window.cef implementation in src/client/renderer/main.cpp:
//
//   EmitEvent(CreateBrowser)     -> CefManager.createBrowser
//   EmitEvent(SetBrowserVisible) -> CefManager.setBrowserVisible
//   EmitEvent(DestroyBrowser)    -> CefManager.destroyBrowser
//   EmitEvent(LoadUrl)           -> CefManager.loadUrl
//   EmitEvent(ReloadBrowser)     -> CefManager.reloadBrowser
//   EmitEvent(FocusBrowser)      -> CefManager.focusBrowser
//   EmitEvent(SetBrowserLayer)   -> CefManager.setBrowserLayer
//   EmitEvent(CreateWorld2DBrowser) -> CefManager.createWorld2DBrowser
//   EmitEvent(SetWorld2DBrowserPos) -> world2d_ entry (re-projected every tick)
//
// World2D (stage 7): the PC client creates a *small* windowless browser
// (width x height, clamped 1..1024) and, every render frame, projects its world
// position with CSprite::CalcScreenCoors and draws the view at
// (screenX - width*pivotX, screenY - height*pivotY). Android has no game
// texture path, but the projection is identical, so the same math positions the
// WebView's rect — see TickWorld2D(). Occlusion by world geometry is therefore
// NOT modelled (the page is drawn on top), same as the PC World2D path which
// draws into the 2D overlay pass.
//
//   EmitBrowserEvent(name,args)  -> CefManager.emitBrowserEvent

//                                   -> window.__cefDispatch(name, args)
//
//   window.cef.emit(name,...)    -> CefJsBridge.emit  (Java, WebView thread)
//                                   -> nativeEmitFromJs
//                                   -> ClientEmitEvent(name,args) -> server
//
//   CefManager callback          -> ClientEmitEvent(BrowserCreateResult)
//
// Argument typing follows renderer/main.cpp exactly: bool/int/double/string
// survive, everything else becomes null and is then dropped by the browser
// process (client.cpp skips VTYPE_NULL), so non-scalar JS values never reach
// the server.
//
// World2D / WorldObject3D / audio / HUD events are recognised and logged as
// unsupported instead of being silently dropped or faked.//
// Threading: HandleServerEvent/HandleBrowserEvent run on the game thread
// (CefSession dispatches there); OnJsEmit runs on the Android main thread
// (CefJsBridge). Both call into Java, which owns thread switching.

#include <jni.h>
#include <cstdint>
#include <string>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include "CefProtocol.h"

class CefBrowser
{
public:
    static CefBrowser& Get();

    // Called from JNI_OnLoad (app class loader attached). Caches the
    // CefManager jclass + method ids: FindClass is unreliable on threads that
    // were attached with AttachCurrentThread.
    static void OnJniLoad(JNIEnv* env);

    // Called from the network thread once the handshake succeeded: the peer is
    // a real CEF server, so it is now worth starting Chromium. No-op if JNI
    // was not initialised or the callback is missing.
    static void NotifyTransportConnected();

    /** 调试自检事件是否开启（环境变量 SAMP_CEF_DEBUG_WORLDOBJ=1）。 */
    static bool worldObjectDebugEnabled();

    // Idempotent: installs itself as CefSession's event handlers.
    void Install();

    // Game thread: EmitEvent (control channel).
    void HandleServerEvent(const cefproto::EmitEventPacket& event);

    // Game thread: EmitBrowserEvent (page channel, stage 4).
    void HandleBrowserEvent(const cefproto::EmitEventPacket& event);

    // Android main thread: window.cef.emit(...) coming back from the WebView.
    void OnJsEmit(int32_t browserId, const std::string& name, const std::string& argsJson);

    // Android main thread: hardware key -> ClientEmitEvent(PressKey).
    // Args match the PC client: [vk, scancode, modifiers, down, repeat].
    void SendPressKey(int vk, int scancode, int modifiers, bool down, bool repeat);

    // Android main thread: SA-MP chat input opened/closed ->
    // ClientEmitEvent(ChatInputState, [open]) with browserId = -1, exactly like
    // ChatHook::SetChatInputState in the PC client.
    void SendChatInputState(bool open);

    // Java -> native, main thread: BrowserCreateResult back to the server.
    void OnBrowserCreated(int browserId, bool success, int code, const std::string& reason);

    // Transport went away (SA-MP disconnected / debug disconnect): close every
    // WebView so a stale page cannot keep running against a dead session.
    void OnSessionLost();

    // Called by the resource layer once <filesDir>/cef/resources is usable:
    // replays the CreateBrowser events that arrived while files were still
    // being downloaded (app.cpp PendingCreate queue).
    void FlushPendingCreates();

    // Game thread, once per frame (Client.cpp right after CefSession::Tick):
    // project every World2D browser's world position to screen space and push
    // the resulting top-left corner to Java. PC does this inside DrawBrowsers();
    // the math is CSprite::CalcScreenCoors + pivot, identical to manager.cpp.
    void TickWorld2D();

    // ------------------------------------------------------- game-thread queue
    //
    // The debug JNI entry points (nativeDebugConnect / nativeDebugDisconnect /
    // nativeSetStorageRoot / nativeDebugWorld2D) are called from the Android
    // main thread by CefManager's broadcast receiver. All of them touch state
    // that only the game thread may mutate - the CEF transport (Connect/
    // Disconnect), CefResources' storage paths, and the world2d_ vector while
    // TickWorld2D is iterating it. Running them inline was a straight-up data
    // race (and, for World2D, a read of the ped pool off-thread).
    //
    // They now post a closure here; DrainGameThreadQueue() runs them at the top
    // of the frame, on the game thread, before anything else touches that state.
    void PostToGameThread(std::function<void()> job);
    void DrainGameThreadQueue();

    // Debug entry (debuggable builds only): place a World2D browser in front of
    // the camera so the on-device test can verify projection + hit testing.
    void DebugCreateWorld2D(int32_t id, float distance, float width, float height);

    // 让**所有** World2D 条目上报投影结果（debug:world2d），不限于调试入口建的那条。
    //
    // 原版插件建的 World2D 走的是 HandleServerEvent -> createWorld2DBrowser，
    // 这条条目 debugProbe=false，所以真机验收时服务端看不到"页面到底被算到了
    // 屏幕哪里"。这条开关只影响上报，不影响绘制/输入。
    void SetWorld2DProbeAll(bool on);

    // WorldObject3D (stage 7, second batch): pull one frame per attached page
    // and upload it into the RenderWare texture; and the debug entry that spawns
    // a local object and attaches the page to it.
    void TickWorldObject3D();
    void DebugWorldObject(int32_t browserId, int32_t model, float distance);

    // Java-side destroy path (CefManager.destroyBrowser). The server normally
    // sends DestroyBrowser, but a destroy that only reaches the Java side would
    // leave the texture and the capture schedule behind. Queued, executed on the
    // game thread by TickWorldObject3D; no-op if the entry is already gone.
    void ForgetWorldObject(int32_t browserId);

    // Diagnostics.
    bool JavaReady() const { return javaClass_ != nullptr; }
    size_t PendingCreateCount() const { return pendingCreates_.size(); }
    size_t World2DCount() const { return world2d_.size(); }

private:
    CefBrowser() = default;

    // One World2D browser: the browser's own viewport is width x height pixels
    // (manager.cpp clamps both to 1..1024) and its world anchor is (x,y,z).
    struct World2DEntry
    {
        int32_t id = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float offsetZ = 0.0f;   // PC default is 1.0 (above the actor's head)
        float pivotX = 0.5f;
        float pivotY = 1.0f;
        bool onScreen = false;  // last projection result
        bool hasScreen = false; // any projection attempt yet
        float screenX = 0.0f;
        float screenY = 0.0f;
        // Created through the debug broadcast: also report the projected rect
        // over the wire (see TickWorld2D). The host test script cannot read the
        // client's log on this ROM, so the server log is the only reliable
        // place to observe where the client put the page.
        bool debugProbe = false;
        // 上报节流的时间戳，按条目存。之前是函数级 static，只要服务端同时开
        // 两条 World2D，第二条就永远轮不到上报 —— 验收脚本会误判"没投影"。
        uint64_t lastProbeMs = 0;
    };

    World2DEntry* FindWorld2D(int32_t id);
    void RemoveWorld2D(int32_t id);

    // ---------------------------------------------------------------- WorldObject3D
    //
    // PC (app.cpp:416 / manager.cpp:587,1093): CreateWorldBrowser(id, url,
    // textureName, w, h) creates an offscreen browser; AttachBrowserToObject
    // (browserId, objectId) pastes its texture onto a game entity; Detach
    // restores the original material.
    //
    // WebView has no GPU texture surface, so the page is rasterised on the Java
    // side and the pixels are pushed into a RenderWare texture, which then
    // replaces the object's material - the same mechanism SA-MP's
    // SetObjectMaterial uses (CObject::ChangeCustomMaterialObjectCB).
    struct WorldObjectEntry
    {
        int32_t browserId = 0;
        int32_t objectId = -1;
        int32_t width = 512;
        int32_t height = 256;
        std::string textureName;
        void* rwTexture = nullptr;      // RwTexture*
        void* rwRaster = nullptr;       // RwRaster* backing it
        void* target = nullptr;         // CObject* once attached
        bool attached = false;
        bool filled = false;            // placeholder drawn at least once
        int32_t frames = 0;             // successful captures

        // Stage 8: adaptive capture schedule. The page is rasterised with
        // View.draw() on the Java main thread, so the achievable rate is bounded
        // by that cost, not by how often the game thread asks. Each entry keeps
        // its own deadline, derived from the measured cost of the previous
        // capture, so a heavy page backs off instead of queueing up work.
        uint64_t nextDueMs = 0;
        int32_t captureCostMs = 0;      // last main-thread draw+copy cost
        int32_t windowFrames = 0;       // uploads since windowStartMs
        uint64_t windowStartMs = 0;
    };

    void CreateWorldBrowser(int32_t id, const std::string& url,
                            const std::string& textureName, int32_t width, int32_t height);
    void AttachBrowserToObject(int32_t browserId, int32_t objectId);
    void DetachBrowserFromObject(int32_t browserId, int32_t objectId);
    void RemoveWorldObject(int32_t browserId);
    WorldObjectEntry* FindWorldObject(int32_t browserId);

    // Wire-level evidence for the on-device test: stage 1 = material swapped,
    // stage 2 = first page frame uploaded into the texture.
    void SendWorldObjectDebug(int32_t browserId, int32_t objectId, int32_t stage);

    // Stage 8 throughput report (debug:worldobjperf). Sent once per 3s window so
    // the numbers come off the wire instead of the device log, which is
    // unreliable on this ROM.
    void SendWorldObjectPerf(int32_t browserId, int32_t framesPerWindow,
                             int32_t costMs, int32_t windowMs);

    // Upload raw RGBA (top-down, w*h*4) into the entry's raster.
    bool UploadWorldObjectFrame(WorldObjectEntry& entry, const uint8_t* rgba,
                                int32_t w, int32_t h);

    std::vector<WorldObjectEntry> worldObjects_;

    // DebugWorldObject is called from the Java thread (broadcast receiver).
    // Spawning an object runs ScriptCommand + RenderWare creation, which is only
    // legal on the game thread - doing it inline froze the game loop. Requests
    // are queued here and executed at the top of TickWorldObject3D.
    struct PendingWorldObject
    {
        int32_t id;
        int32_t model;
        float distance;
    };
    std::vector<PendingWorldObject> pendingWorldObjects_;

    // Removals requested from the Java thread; same rule as the creations above -
    // destroying a RwTexture must happen on the game thread.
    std::vector<int32_t> pendingWorldObjectRemovals_;

    // Attach requests whose object is not in the client's object pool *yet*.
    //
    // AttachBrowserToObject travels over the CEF channel (KCP) while
    // CreateObject travels over the game's RakNet channel, so the attach can
    // arrive before the object exists - measured on a real open.mp server:
    // `CEF_CreateWorldBrowser` + `CEF_AttachBrowserToObject` in the same server
    // tick, attach dropped with "object 1 not in the SA-MP pool". Retry on the
    // game thread until the pool has it (or the deadline passes).
    struct PendingAttach
    {
        int32_t browserId;
        int32_t objectId;
        uint64_t deadlineMs;
    };
    std::vector<PendingAttach> pendingAttaches_;

    // Material swap + stage packet; false when the object is not resolvable yet.
    bool ApplyWorldObjectAttach(int32_t browserId, int32_t objectId);

    void CreateWorldObjectNow(int32_t browserId, int32_t model, float distance);

    // DebugWorldObject runs on the Java thread (JNI call) while TickWorldObject3D
    // runs on the game thread: concurrent push_back reallocates the vector and
    // invalidates every reference Tick is holding. Everything below takes this.
    std::recursive_mutex worldObjectMutex_;
    uint64_t lastWorldObjectTick_ = 0;

    // Returns an attached JNIEnv or nullptr.
    static JNIEnv* Env();

    // Argument accessors mirroring app.cpp: the PC client reads the field that
    // matches the position without type checks (except SetBrowserLayer).
    static int32_t ArgInt(const cefproto::EmitEventPacket& e, size_t i);
    static bool ArgBool(const cefproto::EmitEventPacket& e, size_t i);
    static std::string ArgString(const cefproto::EmitEventPacket& e, size_t i);
    static float ArgFloat(const cefproto::EmitEventPacket& e, size_t i);

    // [1,"a",true] - the same shape renderer/main.cpp builds for
    // window.__cefDispatch.
    static std::string BuildArgsJson(const std::vector<cefproto::Argument>& args);

    static jstring NewJavaString(JNIEnv* env, const std::string& utf8);

private:
    static JavaVM* javaVm_;
    static jclass javaClass_;
    static jmethodID midTransportConnected_;
    static jmethodID midCreate_;
    static jmethodID midDestroy_;
    static jmethodID midSetVisible_;
    static jmethodID midLoadUrl_;
    static jmethodID midReload_;
    static jmethodID midFocus_;
    static jmethodID midSetLayer_;
    static jmethodID midEmitToJs_;
    static jmethodID midDestroyAll_;
    static jmethodID midSetKeyCapture_;
    static jmethodID midEnableKey_;
    static jmethodID midSetChatInput_;
    static jmethodID midSetInputFocus_;
    static jmethodID midCreateWorld2D_;    static jmethodID midWorld2DScreen_;
    static jmethodID midCreateWorldObject_;    // CefManager.createWorldObjectBrowser
    static jmethodID midCaptureWorldObject_;   // CefManager.captureWorldObject -> ByteBuffer
    static jmethodID midWorldObjectCost_;      // CefManager.worldObjectCaptureCost -> int

    bool installed_ = false;

    // CreateBrowser events held back until the resource layer is ready
    // (game thread only; same role as App::pending_creates_ on PC).
    std::vector<cefproto::EmitEventPacket> pendingCreates_;

    // World2D browsers, game thread only (HandleServerEvent owns the writes,
    // TickWorld2D the reads are in the same thread).
    std::vector<World2DEntry> world2d_;

    // 见 SetWorld2DProbeAll()。默认关：正常玩家不该为每条 World2D 付上报开销。
    // Java 线程（广播）写、游戏线程读，所以用原子量而不是裸 bool。
    std::atomic<bool> world2dProbeAll_{ false };

    // Java main thread -> game thread job queue (see PostToGameThread above).
    std::mutex gameQueueMutex_;
    std::deque<std::function<void()>> gameQueue_;
};
