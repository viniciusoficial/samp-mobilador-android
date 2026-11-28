#include <cstdlib>
#include "CefBrowser.h"

#include "CefJson.h"
#include "CefResources.h"
#include "CefSession.h"
#include "CefWorldMath.h"   // Place2D / MovedBy (compile-time asserted)
#include "../Client.h"

// World2D projection (stage 7): the shared game-symbol resolver + the same
// camera/matrix types the game itself uses.
#include "Camera.h"
#include "Memory.h"
#include "Core/Vector.h"
#include "Core/Matrix.h"
#include "Entity/Ped/Ped.h"
#include "../Game/Util.h"   // GamePool_FindPlayerPed()

// WorldObject3D: RenderWare texture upload + the SA-MP object pool.
#include "RenderWare/rw.h"
#include "../Game/Game.h"
#include "../Game/Object.h"
#include "../Net/NetGame.h"

extern NetGame* pNetGame;
extern Game* pGame;

#include <android/log.h>
#include <sys/system_properties.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#define CEFB_LOG_TAG "CefBrowser"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  CEFB_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  CEFB_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CEFB_LOG_TAG, __VA_ARGS__)

JavaVM* CefBrowser::javaVm_ = nullptr;
jclass CefBrowser::javaClass_ = nullptr;
jmethodID CefBrowser::midTransportConnected_ = nullptr;
jmethodID CefBrowser::midCreate_ = nullptr;
jmethodID CefBrowser::midDestroy_ = nullptr;
jmethodID CefBrowser::midSetVisible_ = nullptr;
jmethodID CefBrowser::midLoadUrl_ = nullptr;
jmethodID CefBrowser::midReload_ = nullptr;
jmethodID CefBrowser::midFocus_ = nullptr;
jmethodID CefBrowser::midSetLayer_ = nullptr;
jmethodID CefBrowser::midEmitToJs_ = nullptr;
jmethodID CefBrowser::midDestroyAll_ = nullptr;
jmethodID CefBrowser::midSetKeyCapture_ = nullptr;
jmethodID CefBrowser::midEnableKey_ = nullptr;
jmethodID CefBrowser::midSetChatInput_ = nullptr;
jmethodID CefBrowser::midSetInputFocus_ = nullptr;
jmethodID CefBrowser::midCreateWorld2D_ = nullptr;
jmethodID CefBrowser::midWorld2DScreen_ = nullptr;
jmethodID CefBrowser::midCreateWorldObject_ = nullptr;
jmethodID CefBrowser::midCaptureWorldObject_ = nullptr;
jmethodID CefBrowser::midWorldObjectCost_ = nullptr;

namespace
{

void ClearJniException(JNIEnv* env, const char* where)
{
    if (env && env->ExceptionCheck())
    {
        LOGE("[CefBrowser] JNI exception in %s", where);
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

// CSprite::CalcScreenCoors(const RwV3d&, RwV3d*, float*, float*, bool, bool)
//
// libGTASA.so exports the game's own projection (verified on the device build:
// `llvm-nm -D libGTASA.so` -> 0x6e9df8 "T" _ZN7CSprite15CalcScreenCoors...), so
// World2D can use exactly the call manager.cpp uses instead of re-deriving FOV
// and clip math that would drift from the game.
constexpr const char* kCalcScreenCoorsSym =
    "_ZN7CSprite15CalcScreenCoorsERK5RwV3dPS0_PfS4_bb";

using CalcScreenCoorsFn = bool (*)(const RwV3d&, RwV3d*, float*, float*, bool, bool);

CalcScreenCoorsFn ResolveCalcScreenCoors()
{
    static CalcScreenCoorsFn fn = []() -> CalcScreenCoorsFn {
        if (!g_saSym)
            return nullptr;
        const uintptr_t addr = g_saSym->GetSymbol<uintptr_t>(kCalcScreenCoorsSym);
        return addr ? reinterpret_cast<CalcScreenCoorsFn>(addr) : nullptr;
    }();
    return fn;
}

} // namespace

CefBrowser& CefBrowser::Get()
{
    static CefBrowser instance;
    return instance;
}

void CefBrowser::OnJniLoad(JNIEnv* env)
{
    if (!env)
        return;

    if (!javaVm_)
        env->GetJavaVM(&javaVm_);

    // CefManager lives in the app class loader; JNI_OnLoad runs on a thread
    // that has it, so this is the only safe place to resolve the class.
    jclass local = env->FindClass("com/sampmobile/game/ui/cef/CefManager");
    if (!local)
    {
        ClearJniException(env, "FindClass(CefManager)");
        LOGE("[CefBrowser] CefManager class not found - browser layer disabled");
        return;
    }

    javaClass_ = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (!javaClass_)
    {
        LOGE("[CefBrowser] cannot create global ref for CefManager");
        return;
    }

    midTransportConnected_ = env->GetStaticMethodID(javaClass_, "onTransportConnected", "()V");
    ClearJniException(env, "GetStaticMethodID(onTransportConnected)");
    midCreate_ = env->GetStaticMethodID(javaClass_, "createBrowser", "(ILjava/lang/String;ZZ)V");
    ClearJniException(env, "GetStaticMethodID(createBrowser)");
    midDestroy_ = env->GetStaticMethodID(javaClass_, "destroyBrowser", "(I)V");
    ClearJniException(env, "GetStaticMethodID(destroyBrowser)");
    midSetVisible_ = env->GetStaticMethodID(javaClass_, "setBrowserVisible", "(IZ)V");
    ClearJniException(env, "GetStaticMethodID(setBrowserVisible)");
    midLoadUrl_ = env->GetStaticMethodID(javaClass_, "loadUrl", "(ILjava/lang/String;)V");
    ClearJniException(env, "GetStaticMethodID(loadUrl)");
    midReload_ = env->GetStaticMethodID(javaClass_, "reloadBrowser", "(IZ)V");
    ClearJniException(env, "GetStaticMethodID(reloadBrowser)");
    midFocus_ = env->GetStaticMethodID(javaClass_, "focusBrowser", "(IZ)V");
    ClearJniException(env, "GetStaticMethodID(focusBrowser)");
    midSetLayer_ = env->GetStaticMethodID(javaClass_, "setBrowserLayer", "(II)V");
    ClearJniException(env, "GetStaticMethodID(setBrowserLayer)");
    midEmitToJs_ = env->GetStaticMethodID(javaClass_, "emitBrowserEvent",
                                          "(ILjava/lang/String;Ljava/lang/String;)V");
    ClearJniException(env, "GetStaticMethodID(emitBrowserEvent)");
    midDestroyAll_ = env->GetStaticMethodID(javaClass_, "destroyAllBrowsers", "()V");
    ClearJniException(env, "GetStaticMethodID(destroyAllBrowsers)");
    midSetKeyCapture_ = env->GetStaticMethodID(javaClass_, "setKeyCaptureEnabled", "(Z)V");
    ClearJniException(env, "GetStaticMethodID(setKeyCaptureEnabled)");
    midEnableKey_ = env->GetStaticMethodID(javaClass_, "enableKey", "(IZ)V");
    ClearJniException(env, "GetStaticMethodID(enableKey)");
    midSetChatInput_ = env->GetStaticMethodID(javaClass_, "setChatInputEnabled", "(Z)V");
    ClearJniException(env, "GetStaticMethodID(setChatInputEnabled)");
    midSetInputFocus_ = env->GetStaticMethodID(javaClass_, "setInputFocus", "(IZ)V");
    ClearJniException(env, "GetStaticMethodID(setInputFocus)");
    // World2D (stage 7): id, url, worldX/Y/Z, width, height, offsetZ, pivotX/Y.
    midCreateWorld2D_ = env->GetStaticMethodID(javaClass_, "createWorld2DBrowser",
                                               "(ILjava/lang/String;FFFFFFFF)V");
    ClearJniException(env, "GetStaticMethodID(createWorld2DBrowser)");
    midWorld2DScreen_ = env->GetStaticMethodID(javaClass_, "setWorld2DScreen", "(IIIZ)V");
    ClearJniException(env, "GetStaticMethodID(setWorld2DScreen)");
    // WorldObject3D (stage 7, second batch): offscreen page + frame capture.
    midCreateWorldObject_ = env->GetStaticMethodID(javaClass_, "createWorldObjectBrowser",
                                                   "(ILjava/lang/String;II)V");
    ClearJniException(env, "GetStaticMethodID(createWorldObjectBrowser)");
    midCaptureWorldObject_ = env->GetStaticMethodID(javaClass_, "captureWorldObject",
                                                    "(I)Ljava/nio/ByteBuffer;");
    ClearJniException(env, "GetStaticMethodID(captureWorldObject)");
    midWorldObjectCost_ = env->GetStaticMethodID(javaClass_, "worldObjectCaptureCost", "(I)I");
    ClearJniException(env, "GetStaticMethodID(worldObjectCaptureCost)");

    LOGI("[CefBrowser] JNI bridge ready (create=%p layer=%p emitToJs=%p)",
         reinterpret_cast<void*>(midCreate_), reinterpret_cast<void*>(midSetLayer_),
         reinterpret_cast<void*>(midEmitToJs_));
}

JNIEnv* CefBrowser::Env()
{
    JavaVM* vm = Client::javaVM();
    if (!vm)
        return nullptr;

    JNIEnv* env = nullptr;
    const jint status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED)
    {
        // The game thread is a native thread; attach once and stay attached.
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
        {
            LOGE("[CefBrowser] AttachCurrentThread failed");
            return nullptr;
        }
    }
    else if (status != JNI_OK)
    {
        LOGE("[CefBrowser] GetEnv failed (%d)", status);
        return nullptr;
    }
    return env;
}

jstring CefBrowser::NewJavaString(JNIEnv* env, const std::string& utf8)
{
    if (!env)
        return nullptr;
    if (utf8.empty())
        return env->NewStringUTF("");

    // Build via byte[] + UTF-8 charset so non-ASCII URLs survive; NewStringUTF
    // expects modified UTF-8 and mangles 4-byte sequences.
    jclass strClass = env->FindClass("java/lang/String");
    if (!strClass)
    {
        ClearJniException(env, "FindClass(String)");
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(strClass, "<init>", "([BLjava/lang/String;)V");
    if (!ctor)
    {
        env->DeleteLocalRef(strClass);
        ClearJniException(env, "GetMethodID(String ctor)");
        return nullptr;
    }

    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(utf8.size()));
    jstring charset = env->NewStringUTF("UTF-8");
    jstring result = nullptr;
    if (bytes && charset)
    {
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(utf8.size()),
                                reinterpret_cast<const jbyte*>(utf8.data()));
        result = static_cast<jstring>(env->NewObject(strClass, ctor, bytes, charset));
        ClearJniException(env, "NewObject(String)");
    }
    if (bytes) env->DeleteLocalRef(bytes);
    if (charset) env->DeleteLocalRef(charset);
    env->DeleteLocalRef(strClass);
    return result;
}

int32_t CefBrowser::ArgInt(const cefproto::EmitEventPacket& e, size_t i)
{
    return i < e.args.size() ? e.args[i].intValue : 0;
}

bool CefBrowser::ArgBool(const cefproto::EmitEventPacket& e, size_t i)
{
    return i < e.args.size() ? e.args[i].boolValue : false;
}

std::string CefBrowser::ArgString(const cefproto::EmitEventPacket& e, size_t i)
{
    return i < e.args.size() ? e.args[i].stringValue : std::string();
}

float CefBrowser::ArgFloat(const cefproto::EmitEventPacket& e, size_t i)
{
    if (i >= e.args.size())
        return 0.0f;
    // app.cpp reads .floatValue directly, without a type check: an integer
    // argument for a float slot is a server bug, but the PC client would read
    // the union anyway, so keep the same permissive behaviour.
    return e.args[i].floatValue;
}

namespace
{

// renderer/main.cpp -> client.cpp argument mapping, in reverse:
//   CefValue::IsBool   -> ArgumentType::Bool
//   CefValue::IsInt    -> ArgumentType::Integer
//   CefValue::IsDouble -> ArgumentType::Float   (client.cpp casts to float)
//   CefValue::IsString -> ArgumentType::String
//   anything else      -> VTYPE_NULL, which client.cpp skips entirely
bool JsonToArgument(const cefjson::Value& v, cefproto::Argument& out)
{
    using T = cefjson::Value::Type;
    switch (v.type)
    {
        case T::Bool:
            out = cefproto::Argument::FromBool(v.boolValue);
            return true;
        case T::Int:
            // CefListValue::SetInt is 32-bit; V8 would have produced a double
            // for anything outside int32, so mirror that.
            if (v.intValue >= INT32_MIN && v.intValue <= INT32_MAX)
                out = cefproto::Argument::FromInt(static_cast<int32_t>(v.intValue));
            else
                out = cefproto::Argument::FromFloat(static_cast<float>(v.intValue));
            return true;
        case T::Double:
            out = cefproto::Argument::FromFloat(static_cast<float>(v.doubleValue));
            return true;
        case T::String:
            out = cefproto::Argument::FromString(v.stringValue);
            return true;
        default:
            return false; // null / array / object
    }
}

// Java String -> real UTF-8.
//
// GetStringUTFChars yields *modified* UTF-8 (CESU-8) which encodes supplementary
// code points (emoji, some CJK ext) as two 3-byte surrogates. That would put
// 6 bytes on the wire where the server expects 4-byte UTF-8 and CefString (PC)
// produces 4. Going through String.getBytes("UTF-8") matches CefString exactly.
std::string JavaUtf8(JNIEnv* env, jstring text)
{
    if (!text)
        return std::string();

    jclass strClass = env->FindClass("java/lang/String");
    if (!strClass)
    {
        ClearJniException(env, "FindClass(String)");
        return std::string();
    }
    jmethodID getBytes = env->GetMethodID(strClass, "getBytes", "(Ljava/lang/String;)[B");
    jstring charset = env->NewStringUTF("UTF-8");

    std::string out;
    if (getBytes && charset)
    {
        jbyteArray bytes = static_cast<jbyteArray>(
            env->CallObjectMethod(text, getBytes, charset));
        if (bytes)
        {
            const jsize len = env->GetArrayLength(bytes);
            if (len > 0)
            {
                out.resize(static_cast<size_t>(len));
                env->GetByteArrayRegion(bytes, 0, len,
                                        reinterpret_cast<jbyte*>(&out[0]));
            }
            env->DeleteLocalRef(bytes);
        }
    }
    if (charset) env->DeleteLocalRef(charset);
    env->DeleteLocalRef(strClass);
    ClearJniException(env, "String.getBytes(UTF-8)");
    return out;
}

} // namespace

std::string CefBrowser::BuildArgsJson(const std::vector<cefproto::Argument>& args){
    std::string json = "[";
    char num[64];
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i)
            json += ',';
        const auto& a = args[i];
        switch (a.type)
        {
            case cefproto::ArgumentType::Bool:
                json += a.boolValue ? "true" : "false";
                break;
            case cefproto::ArgumentType::Integer:
                std::snprintf(num, sizeof(num), "%d", a.intValue);
                json += num;
                break;
            case cefproto::ArgumentType::Float:
                // %.9g round-trips a float32 exactly and never emits "inf"/"nan"
                // for finite values, both of which JSON.parse rejects.
                std::snprintf(num, sizeof(num), "%.9g", static_cast<double>(a.floatValue));
                json += num;
                break;
            case cefproto::ArgumentType::String:
                json += cefjson::Escape(a.stringValue);
                break;
        }
    }
    json += ']';
    return json;
}

void CefBrowser::HandleBrowserEvent(const cefproto::EmitEventPacket& event){
    // EmitBrowserEvent carries the target browser id; the PC client looks the
    // instance up in its BrowserManager and silently drops the event when the
    // browser does not exist (app.cpp EmitBrowserEvent branch).
    JNIEnv* env = Env();
    if (!env || !javaClass_)
    {
        LOGW("[CefBrowser] drop browser event '%s' - JNI bridge unavailable",
             event.name.c_str());
        return;
    }

    const std::string argsJson = BuildArgsJson(event.args);
    jstring jname = NewJavaString(env, event.name);
    jstring jargs = NewJavaString(env, argsJson);
    if (jname && jargs)
    {
        env->CallStaticVoidMethod(javaClass_, midEmitToJs_,
                                  static_cast<jint>(event.browserId), jname, jargs);
        ClearJniException(env, "emitBrowserEvent");
        LOGI("[CefBrowser] EmitBrowserEvent '%s' -> browser %d (%zu args) %s",
             event.name.c_str(), event.browserId, event.args.size(), argsJson.c_str());
    }
    if (jname) env->DeleteLocalRef(jname);
    if (jargs) env->DeleteLocalRef(jargs);
}

void CefBrowser::OnJsEmit(int32_t browserId, const std::string& name, const std::string& argsJson)
{
    using namespace cefproto;

    // Carried over from client.cpp: these event names never reach the server.
    if (name == "omp:cef:internal:inputFocusChanged")
    {
        cefjson::Value parsed;
        bool focus = false;
        if (cefjson::Parse(argsJson, parsed) && parsed.IsArray() &&
            parsed.array.size() >= 1 && parsed.array[0].IsBool())
        {
            focus = parsed.array[0].boolValue;
        }
        JNIEnv* env = Env();
        if (env && javaClass_)
        {
            // PC: client.cpp -> FocusManager::SetInputFocus (NOT FocusBrowser).
            // 浏览器焦点与"页面输入框焦点"是两套状态，混用会让 ShouldBlockChat 判错。
            env->CallStaticVoidMethod(javaClass_, midSetInputFocus_,
                                      static_cast<jint>(browserId),
                                      focus ? JNI_TRUE : JNI_FALSE);
            ClearJniException(env, "setInputFocus(js)");
        }
        LOGI("[CefBrowser] js inputFocusChanged browser=%d focus=%d", browserId, focus ? 1 : 0);
        return;
    }

    // omp:cef:internal:chatInputState 是客户端 -> 页面的单向事件（见
    // ChatHook::SetChatInputState）。页面若主动 emit 同名事件，PC 端 client.cpp 并不
    // 拦截，会原样转发到服务端；这里保持一致，不做特殊处理。

    if (name == "game:data:pollPlayerStats")
    {
        // PC polls SA-MP memory for HUD stats. Needs the game-data hook that
        // arrives with the HUD/World2D work; do not fake numbers.
        LOGW("[CefBrowser] js pollPlayerStats ignored - game data polling is not wired yet");
        return;
    }

    cefjson::Value parsed;
    std::string error;
    if (!cefjson::Parse(argsJson, parsed, &error) || !parsed.IsArray())
    {
        LOGW("[CefBrowser] js emit '%s' with unparsable args (%s)", name.c_str(), error.c_str());
        return;
    }

    std::vector<Argument> args;
    args.reserve(parsed.array.size());
    for (const auto& v : parsed.array)
    {
        Argument a;
        if (JsonToArgument(v, a))
            args.push_back(a);
        // Dropped values match client.cpp's VTYPE_NULL handling: the argument
        // slot disappears rather than becoming a placeholder.
    }

    auto& network = CefSession::Get().Network();
    if (network.GetState() != CefNetwork::State::Connected)
    {
        LOGW("[CefBrowser] js emit '%s' dropped - transport not connected", name.c_str());
        return;
    }

    network.SendClientEmit(browserId, name, args);
    LOGI("[CefBrowser] js emit '%s' -> server (browser %d, %zu args) %s",
         name.c_str(), browserId, args.size(), argsJson.c_str());
}

void CefBrowser::SendPressKey(int vk, int scancode, int modifiers, bool down, bool repeat)
{
    auto& network = CefSession::Get().Network();
    if (network.GetState() != CefNetwork::State::Connected)
    {
        LOGW("[CefBrowser] PressKey dropped - transport not connected");
        return;
    }

    std::vector<cefproto::Argument> args;
    args.push_back(cefproto::Argument::FromInt(vk));
    args.push_back(cefproto::Argument::FromInt(scancode));
    args.push_back(cefproto::Argument::FromInt(modifiers));
    args.push_back(cefproto::Argument::FromBool(down));
    args.push_back(cefproto::Argument::FromBool(repeat));

    // PC sends PressKey through the plain ClientEmitEventPacket constructor, i.e.
    // browserId stays 0 (plugin.cpp ignores it for this event anyway).
    network.SendClientEmit(0, cefproto::ClientEvent::PressKey, args);
    LOGI("[CefBrowser] PressKey vk=%d scan=%d mod=%d down=%d repeat=%d -> server",
         vk, scancode, modifiers, down ? 1 : 0, repeat ? 1 : 0);
}

void CefBrowser::SendChatInputState(bool open)
{
    auto& network = CefSession::Get().Network();
    if (network.GetState() != CefNetwork::State::Connected)
        return;

    std::vector<cefproto::Argument> args;
    args.push_back(cefproto::Argument::FromBool(open));

    // ChatHook::SetChatInputState uses browserId = -1: the state belongs to the
    // player, not to one page.
    network.SendClientEmit(-1, cefproto::ClientEvent::ChatInputState, args);
    LOGI("[CefBrowser] ChatInputState open=%d -> server", open ? 1 : 0);
}

void CefBrowser::OnSessionLost()
{
    pendingCreates_.clear();
    world2d_.clear();

    // Tear down WorldObject textures before the Java side goes away: the
    // materials still point at them and would render garbage otherwise.
    while (!worldObjects_.empty())
        RemoveWorldObject(worldObjects_.front().browserId);

    JNIEnv* env = Env();
    if (!env || !javaClass_)
        return;

    env->CallStaticVoidMethod(javaClass_, midDestroyAll_);
    ClearJniException(env, "destroyAllBrowsers");
    LOGI("[CefBrowser] session lost - all browsers torn down");
}

void CefBrowser::FlushPendingCreates()
{
    if (pendingCreates_.empty())
        return;

    std::vector<cefproto::EmitEventPacket> queued;
    queued.swap(pendingCreates_);

    LOGI("[CefBrowser] resources ready - replaying %zu queued CreateBrowser event(s)",
         queued.size());
    for (const auto& event : queued)
        HandleServerEvent(event);
}

void CefBrowser::NotifyTransportConnected()
{
    if (!javaVm_ || !javaClass_ || !midTransportConnected_)
        return;

    JNIEnv* env = nullptr;
    bool attached = false;
    if (javaVm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
    {
        if (javaVm_->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
            return;
        attached = true;
    }

    env->CallStaticVoidMethod(javaClass_, midTransportConnected_);
    ClearJniException(env, "onTransportConnected");

    if (attached)
        javaVm_->DetachCurrentThread();
}

void CefBrowser::Install()
{
    if (installed_)
        return;
    installed_ = true;

    CefSession::Get().SetServerEventHandler([](const cefproto::EmitEventPacket& event) {
        CefBrowser::Get().HandleServerEvent(event);
    });
    CefSession::Get().SetBrowserEventHandler([](const cefproto::EmitEventPacket& event) {
        CefBrowser::Get().HandleBrowserEvent(event);
    });
    CefSession::Get().SetSessionLostHandler([]() {
        CefBrowser::Get().OnSessionLost();
    });
    cefres::CefResources::Get().SetReadyHandler([]() {
        CefBrowser::Get().FlushPendingCreates();
    });

    LOGI("[CefBrowser] installed (Overlay2D + JS bridge), java bridge %s",
         javaClass_ ? "ready" : "NOT READY");
}

void CefBrowser::HandleServerEvent(const cefproto::EmitEventPacket& event)
{
    using namespace cefproto;

    JNIEnv* env = Env();
    if (!env || !javaClass_)
    {
        LOGW("[CefBrowser] drop event '%s' - JNI bridge unavailable", event.name.c_str());
        return;
    }

    if (event.name == ServerEvent::CreateBrowser)
    {
        // app.cpp: args = [id:int, url:string, focused:bool, controlsChat:bool]
        if (event.args.size() < 4)
        {
            LOGW("[CefBrowser] CreateBrowser with %zu args (need 4)", event.args.size());
            return;
        }

        // app.cpp QueueOrCreateOverlay: a page cannot be loaded before its
        // resource tree exists, so hold the command until the download (or the
        // cache check) finished.
        if (!cefres::CefResources::Get().IsReady())
        {
            pendingCreates_.push_back(event);
            LOGI("[CefBrowser] CreateBrowser id=%d queued - resources %s (%zu pending)",
                 ArgInt(event, 0), cefres::CefResources::Get().StateName(),
                 pendingCreates_.size());
            return;
        }

        const jint id = ArgInt(event, 0);
        const std::string url = ArgString(event, 1);
        const jboolean focused = ArgBool(event, 2) ? JNI_TRUE : JNI_FALSE;
        const jboolean controlsChat = ArgBool(event, 3) ? JNI_TRUE : JNI_FALSE;

        jstring jurl = NewJavaString(env, url);
        if (!jurl)
        {
            LOGE("[CefBrowser] CreateBrowser: cannot allocate url string");
            return;
        }
        env->CallStaticVoidMethod(javaClass_, midCreate_, id, jurl, focused, controlsChat);
        ClearJniException(env, "createBrowser");
        env->DeleteLocalRef(jurl);

        LOGI("[CefBrowser] CreateBrowser id=%d url=%s focused=%d controlsChat=%d",
             id, url.c_str(), ArgBool(event, 2) ? 1 : 0, ArgBool(event, 3) ? 1 : 0);
        return;
    }

    if (event.name == ServerEvent::SetBrowserVisible)
    {
        if (event.args.size() < 2) return;
        const jint id = ArgInt(event, 0);
        const jboolean visible = ArgBool(event, 1) ? JNI_TRUE : JNI_FALSE;
        env->CallStaticVoidMethod(javaClass_, midSetVisible_, id, visible);
        ClearJniException(env, "setBrowserVisible");
        return;
    }

    if (event.name == ServerEvent::DestroyBrowser)
    {
        if (event.args.size() < 1) return;
        const jint id = ArgInt(event, 0);
        RemoveWorld2D(id);
        RemoveWorldObject(id);
        env->CallStaticVoidMethod(javaClass_, midDestroy_, id);
        ClearJniException(env, "destroyBrowser");
        LOGI("[CefBrowser] DestroyBrowser id=%d", id);
        return;
    }

    if (event.name == ServerEvent::LoadUrl)
    {
        if (event.args.size() < 2) return;
        const jint id = ArgInt(event, 0);
        const std::string url = ArgString(event, 1);
        jstring jurl = NewJavaString(env, url);
        if (!jurl) return;
        env->CallStaticVoidMethod(javaClass_, midLoadUrl_, id, jurl);
        ClearJniException(env, "loadUrl");
        env->DeleteLocalRef(jurl);
        LOGI("[CefBrowser] LoadUrl id=%d url=%s", id, url.c_str());
        return;
    }

    if (event.name == ServerEvent::ReloadBrowser)
    {
        if (event.args.size() < 2) return;
        const jint id = ArgInt(event, 0);
        const jboolean ignoreCache = ArgBool(event, 1) ? JNI_TRUE : JNI_FALSE;
        env->CallStaticVoidMethod(javaClass_, midReload_, id, ignoreCache);
        ClearJniException(env, "reloadBrowser");
        LOGI("[CefBrowser] ReloadBrowser id=%d ignoreCache=%d", id, ArgBool(event, 1) ? 1 : 0);
        return;
    }

    if (event.name == ServerEvent::FocusBrowser)
    {
        if (event.args.size() < 2) return;
        const jint id = ArgInt(event, 0);
        // app.cpp names this "toggle" but BrowserManager treats it as the
        // target focus state (if (focus) ...). Follow BrowserManager.
        const jboolean focus = ArgBool(event, 1) ? JNI_TRUE : JNI_FALSE;
        env->CallStaticVoidMethod(javaClass_, midFocus_, id, focus);
        ClearJniException(env, "focusBrowser");
        LOGI("[CefBrowser] FocusBrowser id=%d focus=%d", id, ArgBool(event, 1) ? 1 : 0);
        return;
    }

    if (event.name == ServerEvent::SetBrowserLayer)
    {
        // app.cpp requires both args to be Integer.
        if (event.args.size() != 2 ||
            event.args[0].type != ArgumentType::Integer ||
            event.args[1].type != ArgumentType::Integer)
        {
            LOGW("[CefBrowser] SetBrowserLayer with unexpected args");
            return;
        }
        const jint id = ArgInt(event, 0);
        const jint layer = ArgInt(event, 1);
        env->CallStaticVoidMethod(javaClass_, midSetLayer_, id, layer);
        ClearJniException(env, "setBrowserLayer");
        return;
    }

    if (event.name == ServerEvent::SetKeyCapture)
    {
        if (event.args.size() < 1) return;
        const jboolean enabled = ArgBool(event, 0) ? JNI_TRUE : JNI_FALSE;
        env->CallStaticVoidMethod(javaClass_, midSetKeyCapture_, enabled);
        ClearJniException(env, "setKeyCaptureEnabled");
        LOGI("[CefBrowser] SetKeyCapture enabled=%d", ArgBool(event, 0) ? 1 : 0);
        return;
    }

    if (event.name == ServerEvent::EnableKey)
    {
        if (event.args.size() < 2) return;
        const jint key = ArgInt(event, 0);
        const jboolean enabled = ArgBool(event, 1) ? JNI_TRUE : JNI_FALSE;
        env->CallStaticVoidMethod(javaClass_, midEnableKey_, key, enabled);
        ClearJniException(env, "enableKey");
        LOGI("[CefBrowser] EnableKey vk=%d enabled=%d", key, ArgBool(event, 1) ? 1 : 0);
        return;
    }

    if (event.name == ServerEvent::ToggleChatInput)
    {
        if (event.args.size() < 1) return;
        const jboolean enabled = ArgBool(event, 0) ? JNI_TRUE : JNI_FALSE;
        env->CallStaticVoidMethod(javaClass_, midSetChatInput_, enabled);
        ClearJniException(env, "setChatInputEnabled");
        LOGI("[CefBrowser] ToggleChatInput enabled=%d", ArgBool(event, 0) ? 1 : 0);
        return;
    }

    if (event.name == ServerEvent::CreateWorld2DBrowser)
    {
        // app.cpp: [id, url, worldX, worldY, worldZ, width, height, offsetZ, pivotX, pivotY]
        if (event.args.size() < 10)
        {
            LOGW("[CefBrowser] CreateWorld2DBrowser with %zu args (need 10)", event.args.size());
            return;
        }
        if (!cefres::CefResources::Get().IsReady())
        {
            pendingCreates_.push_back(event);
            LOGI("[CefBrowser] CreateWorld2DBrowser id=%d queued - resources %s",
                 ArgInt(event, 0), cefres::CefResources::Get().StateName());
            return;
        }

        const jint id = ArgInt(event, 0);
        const std::string url = ArgString(event, 1);

        World2DEntry entry;
        entry.id = id;
        entry.x = ArgFloat(event, 2);
        entry.y = ArgFloat(event, 3);
        entry.z = ArgFloat(event, 4);
        entry.width = ArgFloat(event, 5);
        entry.height = ArgFloat(event, 6);
        entry.offsetZ = ArgFloat(event, 7);
        entry.pivotX = ArgFloat(event, 8);
        entry.pivotY = ArgFloat(event, 9);

        RemoveWorld2D(id);
        world2d_.push_back(entry);

        jstring jurl = NewJavaString(env, url);
        if (!jurl)
            return;
        env->CallStaticVoidMethod(javaClass_, midCreateWorld2D_, id, jurl,
                                  entry.x, entry.y, entry.z,
                                  entry.width, entry.height,
                                  entry.offsetZ, entry.pivotX, entry.pivotY);
        ClearJniException(env, "createWorld2DBrowser");
        env->DeleteLocalRef(jurl);

        LOGI("[CefBrowser] CreateWorld2DBrowser id=%d url=%s world=(%.1f,%.1f,%.1f) "
             "size=%.0fx%.0f offsetZ=%.2f pivot=(%.2f,%.2f)",
             id, url.c_str(), entry.x, entry.y, entry.z,
             entry.width, entry.height, entry.offsetZ, entry.pivotX, entry.pivotY);
        return;
    }

    if (event.name == ServerEvent::SetWorld2DBrowserPos)
    {
        // app.cpp: [id, worldX, worldY, worldZ]
        if (event.args.size() < 4)
            return;
        const jint id = ArgInt(event, 0);
        World2DEntry* entry = FindWorld2D(id);
        if (!entry)
        {
            LOGW("[CefBrowser] SetWorld2DBrowserPos: no World2D browser %d", id);
            return;
        }
        entry->x = ArgFloat(event, 1);
        entry->y = ArgFloat(event, 2);
        entry->z = ArgFloat(event, 3);
        LOGI("[CefBrowser] SetWorld2DBrowserPos id=%d -> (%.1f,%.1f,%.1f)",
             id, entry->x, entry->y, entry->z);
        return;
    }

    // Recognised but not implementable through an Activity WebView yet.
    if (event.name == ServerEvent::CreateWorldBrowser)
    {
        // app.cpp:416 - [id, url, textureName, width, height]; the PC client
        // reads width/height as floats and clamps to 1..1024 (manager.cpp:740).
        if (event.args.size() < 5)
            return;
        int32_t w = static_cast<int32_t>(ArgFloat(event, 3));
        int32_t h = static_cast<int32_t>(ArgFloat(event, 4));
        // A server that sends integers puts garbage into the float union slot;
        // fall back to the integer reading in that case.
        if (w <= 0 || w > 1024) w = ArgInt(event, 3);
        if (h <= 0 || h > 1024) h = ArgInt(event, 4);
        CreateWorldBrowser(ArgInt(event, 0), ArgString(event, 1), ArgString(event, 2), w, h);
        return;
    }

    if (event.name == ServerEvent::AttachBrowserToObject)
    {
        if (event.args.size() < 2)
            return;
        AttachBrowserToObject(ArgInt(event, 0), ArgInt(event, 1));
        return;
    }

    if (event.name == ServerEvent::DetachBrowserFromObject)
    {
        if (event.args.size() < 2)
            return;
        DetachBrowserFromObject(ArgInt(event, 0), ArgInt(event, 1));
        return;
    }

    LOGW("[CefBrowser] event '%s' not handled on Android (%zu args)",
         event.name.c_str(), event.args.size());
}

// ------------------------------------------------------------------ World2D

CefBrowser::World2DEntry* CefBrowser::FindWorld2D(int32_t id)
{
    for (auto& entry : world2d_)
    {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

void CefBrowser::RemoveWorld2D(int32_t id)
{
    for (auto it = world2d_.begin(); it != world2d_.end(); ++it)
    {
        if (it->id == id)
        {
            world2d_.erase(it);
            return;
        }
    }
}

void CefBrowser::TickWorld2D()
{
    if (world2d_.empty())
        return;

    JNIEnv* env = Env();
    if (!env || !javaClass_ || !midWorld2DScreen_)
        return;

    auto project = ResolveCalcScreenCoors();
    if (!project)
    {
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            LOGW("[CefBrowser] CSprite::CalcScreenCoors missing from libGTASA - World2D disabled");
        }
        return;
    }

    for (auto& entry : world2d_)
    {
        // manager.cpp: the projection target is (x, y, z + offsetZ).
        RwV3d world{ entry.x, entry.y, entry.z + entry.offsetZ };
        RwV3d screen{};
        float w = 0.0f;
        float h = 0.0f;
        const bool projected = project(world, &screen, &w, &h, true, true);
        // manager.cpp skips the draw when projection fails or the point is behind
        // the camera; here "skip" means the overlay stays hidden. The arithmetic
        // lives in CefWorldMath.h so it can be asserted at compile time.
        const cefworld::Placement placed = cefworld::Place2D(
            screen.x, screen.y, screen.z,
            entry.width, entry.height,
            entry.pivotX, entry.pivotY, projected);
        const bool onScreen = placed.onScreen;
        const float px = placed.x;
        const float py = placed.y;

        // Debug probe: report where the client thinks the page is. Enabled per
        // entry by the debug broadcast, or globally by SetWorld2DProbeAll() so
        // the end-to-end test can observe pages created by the real plugin.
        // The device logcat on this ROM is unreliable, so the host script cannot
        // parse the projection from the log; these packets land in the test
        // server's log, which is a plain local file. Throttled to ~2.5Hz: the
        // camera moves continuously and the script only needs the latest value.
        if (entry.debugProbe || world2dProbeAll_.load(std::memory_order_relaxed))
        {
            const uint64_t nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (nowMs - entry.lastProbeMs >= 400)
            {
                entry.lastProbeMs = nowMs;
                auto& probeNet = CefSession::Get().Network();
                if (probeNet.GetState() == CefNetwork::State::Connected)
                {
                    std::vector<cefproto::Argument> probeArgs;
                    probeArgs.push_back(cefproto::Argument::FromInt(entry.id));
                    probeArgs.push_back(cefproto::Argument::FromInt(static_cast<int32_t>(px)));
                    probeArgs.push_back(cefproto::Argument::FromInt(static_cast<int32_t>(py)));
                    probeArgs.push_back(cefproto::Argument::FromInt(static_cast<int32_t>(entry.width)));
                    probeArgs.push_back(cefproto::Argument::FromInt(static_cast<int32_t>(entry.height)));
                    // onScreen 走 int 而不是 bool：原版插件把 bool 参数交给 Pawn 时
                    // 会少传一个槽（实测 6 个参数到 Pawn 只剩 5 个，后一个整体左移），
                    // 这条只是验收诊断，没必要为它跟组件的类型推断较劲。
                    probeArgs.push_back(cefproto::Argument::FromInt(onScreen ? 1 : 0));
                    probeNet.SendClientEmit(entry.id, "debug:world2d", probeArgs);
                }
            }
        }

        const bool moved = !entry.hasScreen
            || onScreen != entry.onScreen
            || cefworld::MovedBy(px, entry.screenX)
            || cefworld::MovedBy(py, entry.screenY);
        if (!moved)
            continue;

        entry.hasScreen = true;
        entry.onScreen = onScreen;
        entry.screenX = px;
        entry.screenY = py;

        env->CallStaticVoidMethod(javaClass_, midWorld2DScreen_,
                                  static_cast<jint>(entry.id),
                                  static_cast<jint>(px), static_cast<jint>(py),
                                  onScreen ? JNI_TRUE : JNI_FALSE);
        ClearJniException(env, "setWorld2DScreen");

        // Log the transition, not every frame (the camera moves continuously).
        static thread_local int32_t lastLoggedId = -1;
        if (onScreen && lastLoggedId != entry.id)
        {
            lastLoggedId = entry.id;
            LOGI("[CefBrowser] world2d id=%d world=(%.1f,%.1f,%.1f) -> screen=(%.1f,%.1f) "
                 "pos=(%.0f,%.0f) size=%.0fx%.0f",
                 entry.id, entry.x, entry.y, entry.z, screen.x, screen.y,
                 px, py, entry.width, entry.height);
        }
        else if (!onScreen)
        {
            lastLoggedId = -1;
        }
    }
}

void CefBrowser::SetWorld2DProbeAll(bool on)
{
    world2dProbeAll_.store(on, std::memory_order_relaxed);
    LOGI("[CefBrowser] world2d probe-all=%d (%zu World2D browsers)", on ? 1 : 0, world2d_.size());
}

void CefBrowser::PostToGameThread(std::function<void()> job)
{
    if (!job)
        return;
    {
        std::lock_guard<std::mutex> lock(gameQueueMutex_);
        // Bounded: a stuck game loop must not let the main thread grow this
        // without limit. Dropping the oldest is fine for debug entry points.
        if (gameQueue_.size() >= 64)
        {
            LOGW("[CefBrowser] game-thread queue full - dropping oldest job");
            gameQueue_.pop_front();
        }
        gameQueue_.push_back(std::move(job));
    }
}

void CefBrowser::DrainGameThreadQueue()
{
    std::deque<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(gameQueueMutex_);
        local.swap(gameQueue_);
    }
    for (auto& job : local)
    {
        if (job)
            job();
    }
}

void CefBrowser::DebugCreateWorld2D(int32_t id, float heightAbovePed, float width, float height)
{
    sa::CPed* ped = GamePool_FindPlayerPed();
    if (!ped)
    {
        LOGW("[CefBrowser] debug world2d: no local player ped");
        return;
    }

    const sa::CVector pos = ped->GetMatrix().m_pos;
    World2DEntry entry;
    entry.id = id;
    entry.x = pos.x;
    entry.y = pos.y;
    entry.z = pos.z;
    entry.offsetZ = heightAbovePed > 0.05f ? heightAbovePed : 1.5f;
    entry.width = width > 1.0f ? width : 512.0f;
    entry.height = height > 1.0f ? height : 256.0f;
    entry.pivotX = 0.5f;
    entry.pivotY = 1.0f;   // PC default: anchored at the bottom centre
    entry.debugProbe = true;

    RemoveWorld2D(id);
    world2d_.push_back(entry);

    JNIEnv* env = Env();
    if (!env || !javaClass_ || !midCreateWorld2D_)
        return;

    const std::string url = "http://cef/test/index.html";
    jstring jurl = NewJavaString(env, url);
    if (!jurl)
        return;
    env->CallStaticVoidMethod(javaClass_, midCreateWorld2D_, static_cast<jint>(id), jurl,
                              entry.x, entry.y, entry.z,
                              entry.width, entry.height,
                              entry.offsetZ, entry.pivotX, entry.pivotY);
    ClearJniException(env, "debugCreateWorld2DBrowser");
    env->DeleteLocalRef(jurl);

    LOGI("[CefBrowser] debug world2d id=%d at ped (%.1f,%.1f,%.1f) +%.1fm size=%.0fx%.0f",
         id, entry.x, entry.y, entry.z, entry.offsetZ, entry.width, entry.height);
}

// ============================================================ WorldObject3D
//
// PC 语义（app.cpp:416 / manager.cpp:587,1093）：
//   CreateWorldBrowser  [id, url, textureName, w, h]  建离屏浏览器 + 纹理
//   AttachBrowserToObject [browserId, objectId]       把纹理贴到实体上
//   DetachBrowserFromObject [browserId, objectId]     恢复原材质
//
// Android 的差异：WebView 没有 GPU 纹理出口，所以页面帧由 Java 侧
// draw(Canvas) 抓成 RGBA，再 memcpy 进 RenderWare 的 raster。纹理替换本身
// 复用 SA-MP SetObjectMaterial 的现成机制（CObject::ChangeCustomMaterialObjectCB
// 直接把 m_Material[i].pMaterialTexture 赋给 materials[i]->texture）。

CefBrowser::WorldObjectEntry* CefBrowser::FindWorldObject(int32_t id)
{
    for (auto& e : worldObjects_)
        if (e.browserId == id)
            return &e;
    return nullptr;
}

bool CefBrowser::worldObjectDebugEnabled()
{
    // 默认关闭。这些是排查用的自检事件（stage 1..8 + 每 3 秒心跳），
    // 正式服上每 3 秒往服务端刷一条日志没有任何好处，还可能被反作弊
    // 当成异常行为。
    //
    // 打开方式：
    //   adb shell setprop debug.samp.cef.worldobj 1   <- 真机 / 自动化验收走这条
    //   进程环境变量 SAMP_CEF_DEBUG_WORLDOBJ=1        <- 桌面 / CI 走这条
    //
    // 不能用 static 缓存：adb setprop 是随时下发的，缓存了就得重启进程才生效，
    // 而 E2E 脚本恰恰是「装好 APK 之后再打开开关」。读 prop 是一次共享内存读，
    // 每条调试事件读一次的代价可以忽略。
    char prop[PROP_VALUE_MAX] = {0};
    if (__system_property_get("debug.samp.cef.worldobj", prop) > 0 && prop[0] == '1')
        return true;

    const char* env = getenv("SAMP_CEF_DEBUG_WORLDOBJ");
    return env && env[0] == '1';
}

void CefBrowser::SendWorldObjectDebug(int32_t browserId, int32_t objectId, int32_t stage)
{
    if (!worldObjectDebugEnabled())
        return;
    auto& network = CefSession::Get().Network();
    if (network.GetState() != CefNetwork::State::Connected)
        return;
    std::vector<cefproto::Argument> args;
    args.push_back(cefproto::Argument::FromInt(browserId));
    args.push_back(cefproto::Argument::FromInt(objectId));
    args.push_back(cefproto::Argument::FromInt(stage));
    network.SendClientEmit(browserId, "debug:worldobj", args);
}

void CefBrowser::SendWorldObjectPerf(int32_t browserId, int32_t framesPerWindow,
                                     int32_t costMs, int32_t windowMs)
{
    if (!worldObjectDebugEnabled())
        return;
    auto& network = CefSession::Get().Network();
    if (network.GetState() != CefNetwork::State::Connected)
        return;
    // [browserId, frames, costMs, windowMs] - the host derives the rate itself so
    // the numbers are not rounded twice.
    std::vector<cefproto::Argument> args;
    args.push_back(cefproto::Argument::FromInt(browserId));
    args.push_back(cefproto::Argument::FromInt(framesPerWindow));
    args.push_back(cefproto::Argument::FromInt(costMs));
    args.push_back(cefproto::Argument::FromInt(windowMs));
    network.SendClientEmit(browserId, "debug:worldobjperf", args);
}

namespace
{

/** Monotonic milliseconds, cheap enough to call several times per frame. */
uint64_t NowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

/** objectId -> CObject（SA-MP 物体池；单机模式下池是空的）。 */
CObject* FindPooledObject(int32_t objectId)
{
    if (!pNetGame || objectId < 0 || objectId >= MAX_OBJECTS)
        return nullptr;
    CObjectPool* pool = pNetGame->GetObjectPool();
    return pool ? pool->GetAt(static_cast<OBJECTID>(objectId)) : nullptr;
}

/** 用纯色填满 raster。纹理必须先有确定的像素内容，否则第一次被渲染采样时
 *  驱动可能读到未初始化的显存（实测会直接把游戏带崩）。 */
void FillSolid(RwRaster* raster, uint32_t rgba)
{
    if (!raster)
        return;
    RwUInt8* dst = RwRasterLock(raster, 0, rwRASTERLOCKWRITE);
    if (!dst)
        return;
    for (int32_t y = 0; y < raster->height; ++y)
    {
        uint32_t* row = reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * raster->stride);
        for (int32_t x = 0; x < raster->width; ++x)
            row[x] = rgba;
    }
    RwRasterUnlock(raster);
}

/** 建一块可写的 32 位纹理；解锁时 RenderWare 自己上传到 GL。 */
RwTexture* CreateFrameTexture(int32_t w, int32_t h, RwRaster** outRaster)
{
    RwRaster* raster = RwRasterCreate(w, h, 32, rwRASTERFORMAT8888 | rwRASTERTYPETEXTURE);
    if (!raster)
    {
        LOGE("[CefBrowser] RwRasterCreate(%dx%d) failed", w, h);
        return nullptr;
    }
    RwTexture* tex = RwTextureCreate(raster);
    if (!tex)
    {
        LOGE("[CefBrowser] RwTextureCreate failed");
        RwRasterDestroy(raster);
        return nullptr;
    }
    // 512x256 这类非 2 的幂尺寸在 GLES2 上不能用 WRAP 寻址（会触发
    // GL_INVALID_OPERATION，采样结果未定义）。纹理只用来贴物体的 UV [0,1]，
    // CLAMP 就够。
    RwTextureSetAddressingMacro(tex, rwTEXTUREADDRESSCLAMP);
    // 0xAABBGGRR (little endian): opaque teal placeholder.
    FillSolid(raster, 0xFF806040u);
    *outRaster = raster;
    return tex;
}

} // namespace

void CefBrowser::CreateWorldBrowser(int32_t id, const std::string& url,
                                    const std::string& textureName,
                                    int32_t width, int32_t height)
{
    std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
    // manager.cpp:740 clamps both dimensions to 1..1024.
    if (width < 1 || width > 1024) width = 512;
    if (height < 1 || height > 1024) height = 256;

    RemoveWorldObject(id);

    WorldObjectEntry entry;
    entry.browserId = id;
    entry.width = width;
    entry.height = height;
    entry.textureName = textureName;
    worldObjects_.push_back(entry);

    JNIEnv* env = Env();
    if (!env || !javaClass_ || !midCreateWorldObject_)
    {
        LOGW("[CefBrowser] world object %d: JNI bridge not ready", id);
        return;
    }
    jstring jurl = NewJavaString(env, url);
    env->CallStaticVoidMethod(javaClass_, midCreateWorldObject_,
                              static_cast<jint>(id), jurl,
                              static_cast<jint>(width), static_cast<jint>(height));
    env->DeleteLocalRef(jurl);
    ClearJniException(env, "createWorldObjectBrowser");

    LOGI("[CefBrowser] world object %d: %dx%d texture='%s' url='%s'",
         id, width, height, textureName.c_str(), url.c_str());
}

bool CefBrowser::UploadWorldObjectFrame(WorldObjectEntry& entry, const uint8_t* rgba,
                                        int32_t w, int32_t h)
{
    if (!entry.rwRaster || !rgba || w <= 0 || h <= 0)
        return false;

    auto* raster = static_cast<RwRaster*>(entry.rwRaster);
    if (w != raster->width || h != raster->height)
        return false;

    RwUInt8* dst = RwRasterLock(raster, 0, rwRASTERLOCKWRITE);
    if (!dst)
        return false;

    const int32_t srcStride = w * 4;
    const int32_t dstStride = raster->stride;
    if (dstStride == srcStride)
    {
        memcpy(dst, rgba, static_cast<size_t>(srcStride) * h);
    }
    else
    {
        // Raster rows are padded to the driver's alignment; copy row by row.
        for (int32_t y = 0; y < h; ++y)
            memcpy(dst + static_cast<size_t>(y) * dstStride,
                   rgba + static_cast<size_t>(y) * srcStride, srcStride);
    }
    RwRasterUnlock(raster);
    return true;
}

void CefBrowser::AttachBrowserToObject(int32_t browserId, int32_t objectId)
{
    std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
    WorldObjectEntry* entry = FindWorldObject(browserId);
    if (!entry)
    {
        LOGW("[CefBrowser] attach: browser %d is not a WorldObject page", browserId);
        return;
    }

    if (ApplyWorldObjectAttach(browserId, objectId))
        return;

    // 不在池里不代表失败：物体走 RakNet、attach 走 KCP，两者之间没有顺序保证。
    // 实测原版 open.mp 服务端同一 tick 里 CreateObject + Attach，attach 先到，
    // 这里以前直接 return，板子上永远是模型原纹理。改成排队重试。
    if (FindPooledObject(objectId))
        return;   // 池里有但别的原因失败（例如纹理创建失败），不重试

    pendingAttaches_.push_back(PendingAttach{ browserId, objectId, NowMs() + 8000 });
    LOGI("[CefBrowser] attach: object %d not in the pool yet, queued (browser %d)",
         objectId, browserId);
}

bool CefBrowser::ApplyWorldObjectAttach(int32_t browserId, int32_t objectId)
{
    std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
    WorldObjectEntry* entry = FindWorldObject(browserId);
    if (!entry)
        return false;

    CObject* obj = FindPooledObject(objectId);
    if (!obj)
        return false;

    if (!entry->rwTexture)
    {
        RwRaster* raster = nullptr;
        entry->rwTexture = CreateFrameTexture(entry->width, entry->height, &raster);
        if (!entry->rwTexture)
            return false;
        entry->rwRaster = raster;
    }

    // 复用 SA-MP 的自定义材质通道：ChangeCustomMaterialObjectCB 会把这个
    // 纹理赋给 atomic 的第 i 个 material，render 结束后再恢复。
    //
    // 引用计数是这里的关键：CObject::~CObject() 对每个非空材质槽都会
    // RwTextureDestroy(pMaterialTexture)。如果不抬 refCount，服务端 DestroyObject
    // 或流送卸载一旦先销毁物体，我们的 rwTexture/rwRaster 就被连带释放，
    // 下一帧上传会锁已释放的 raster（堆破坏），Detach 时还会二次销毁。
    // 抬高之后物体析构只把计数减回 1，由我们自己在 RemoveWorldObject 里销毁。
    auto* texture = static_cast<RwTexture*>(entry->rwTexture);
    if (texture->refCount < 2)
        texture->refCount = 2;
    obj->m_Material[0].pMaterialTexture = texture;
    obj->m_bHasCustomObjectMaterial = true;

    entry->target = obj;
    entry->objectId = objectId;
    entry->attached = true;

    LOGI("[CefBrowser] world object %d attached to object %d (entity %p)",
         browserId, objectId, static_cast<void*>(obj->m_entity));
    SendWorldObjectDebug(browserId, objectId, 1);
    return true;
}

void CefBrowser::DetachBrowserFromObject(int32_t browserId, int32_t objectId)
{
    std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
    WorldObjectEntry* entry = FindWorldObject(browserId);
    if (!entry || !entry->attached)
        return;

    auto* obj = static_cast<CObject*>(entry->target);
    // 只有目标物体仍在池里、且确实是同一个对象时才去动它的材质。
    // 物体可能已经被 DestroyObject 回收，此时 obj 是悬空指针，碰一下就崩。
    if (obj && FindPooledObject(objectId) == obj)
    {
        // 顺序很重要：ChangeToOriginalObjectMaterial 只在标志为真时才恢复，
        // 所以先恢复、再摘掉我们的纹理、最后清标志。
        obj->ChangeToOriginalObjectMaterial();
        if (obj->m_Material[0].pMaterialTexture == entry->rwTexture)
        {
            obj->m_Material[0].pMaterialTexture = nullptr;
            // 把挂载时抬高的引用还回去，之后由 RemoveWorldObject 统一销毁。
            auto* texture = static_cast<RwTexture*>(entry->rwTexture);
            if (texture && texture->refCount > 1)
                texture->refCount = 1;
        }
        obj->m_bHasCustomObjectMaterial = false;
    }

    entry->target = nullptr;
    entry->attached = false;
    LOGI("[CefBrowser] world object %d detached from object %d", browserId, objectId);
}

void CefBrowser::RemoveWorldObject(int32_t browserId)
{
    std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
    // 还没解析出物体的 attach 请求也要一起丢掉，否则会在物体出现后去贴一张
    // 已经被销毁的纹理。
    for (auto it = pendingAttaches_.begin(); it != pendingAttaches_.end();)
        it = (it->browserId == browserId) ? pendingAttaches_.erase(it) : ++it;

    for (auto it = worldObjects_.begin(); it != worldObjects_.end(); ++it)
    {
        if (it->browserId != browserId)
            continue;

        if (it->attached)
            DetachBrowserFromObject(browserId, it->objectId);

        if (it->rwTexture)
        {
            // RwTextureCreate 初始化 refCount=1，这里销毁即可同时释放 raster。
            RwTextureDestroy(static_cast<RwTexture*>(it->rwTexture));
            it->rwTexture = nullptr;
            it->rwRaster = nullptr;
        }
        worldObjects_.erase(it);
        // Stage 6: the entry (texture + capture schedule) is really gone. Sent so
        // the host can tell "released" apart from "leaked after a Java-side
        // destroy", which is invisible over the wire otherwise.
        SendWorldObjectDebug(browserId, -1, 6);
        return;
    }
}

void CefBrowser::TickWorldObject3D()
{
    // Removals first: they come from the Java thread and free RenderWare objects,
    // which is only legal here.
    {
        std::vector<int32_t> gone;
        {
            std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
            if (!pendingWorldObjectRemovals_.empty())
                std::swap(gone, pendingWorldObjectRemovals_);
        }
        for (int32_t id : gone)
            RemoveWorldObject(id);
    }

    // Retry attaches whose object only showed up in the pool after the request
    // (attach travels over KCP, CreateObject over RakNet - no ordering).
    {
        std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
        if (!pendingAttaches_.empty())
        {
            const uint64_t now = NowMs();
            for (auto it = pendingAttaches_.begin(); it != pendingAttaches_.end();)
            {
                if (ApplyWorldObjectAttach(it->browserId, it->objectId))
                {
                    LOGI("[CefBrowser] attach: object %d resolved on retry", it->objectId);
                    it = pendingAttaches_.erase(it);
                    continue;
                }
                if (now > it->deadlineMs)
                {
                    LOGW("[CefBrowser] attach: object %d never appeared in the pool (browser %d)",
                         it->objectId, it->browserId);
                    // stage 8 = attach gave up, sent so the host can see it on the wire.
                    SendWorldObjectDebug(it->browserId, it->objectId, 8);
                    it = pendingAttaches_.erase(it);
                    continue;
                }
                ++it;
            }
        }
    }

    // Debug requests next: they are queued from the Java thread and must run
    // here, on the game thread.
    {
        std::vector<PendingWorldObject> todo;
        {
            std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
            if (!pendingWorldObjects_.empty())
                std::swap(todo, pendingWorldObjects_);
        }
        for (auto& p : todo)
            CreateWorldObjectNow(p.id, p.model, p.distance);
    }

    if (worldObjects_.empty())
        return;

    // Stage 8: the capture schedule used to be a flat 4Hz. The real bound is the
    // Java-side rasterisation cost of *this* page, which varies with page
    // complexity, so each entry carries its own deadline and backs off when the
    // measured cost grows (see the reschedule step below).
    const uint64_t nowMs = NowMs();

    // Heartbeat first: proves the game thread still reaches this point even if
    // every capture fails (stage 4). Without it a silent stall is
    // indistinguishable from "no frames yet".
    static thread_local uint64_t lastHeartbeatMs = 0;
    static thread_local int32_t heartbeat = 0;
    if (nowMs - lastHeartbeatMs >= 3000)
    {
        lastHeartbeatMs = nowMs;
        SendWorldObjectDebug(static_cast<int32_t>(worldObjects_.size()), ++heartbeat, 4);
    }

    JNIEnv* env = Env();
    if (!env || !javaClass_ || !midCaptureWorldObject_)
        return;

    // Entries whose deadline passed, oldest first. The per-tick cap matters when
    // several pages are alive: one tick must never block the game thread for
    // N x draw cost, and the ones we skip stay at the front of the queue.
    constexpr size_t kMaxCapturesPerTick = 2;
    std::vector<std::pair<uint64_t, int32_t>> due;
    {
        std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
        for (auto& e : worldObjects_)
        {
            if (!e.attached || !e.rwTexture || e.nextDueMs > nowMs)
                continue;

            // 目标物体可能已被服务端 DestroyObject、流送卸载或因断线回收。
            // 池里已经不是同一个指针了，就说明我们挂的那个对象没了：
            // 此时 entry->rwRaster 可能已失效，绝不能再 RwRasterLock 它。
            // 直接摘掉挂载，等 Java 侧下一轮重新发起 attach。
            if (e.target && FindPooledObject(e.objectId) != e.target)
            {
                e.target = nullptr;
                e.attached = false;
                LOGW("[CefBrowser] world object %d: target object %d gone, detached",
                     e.browserId, e.objectId);
                continue;
            }

            due.emplace_back(e.nextDueMs, e.browserId);
        }
    }
    if (due.empty())
        return;
    std::sort(due.begin(), due.end());
    if (due.size() > kMaxCapturesPerTick)
        due.resize(kMaxCapturesPerTick);

    static thread_local int32_t nullFrames = 0;
    static thread_local uint64_t lastStatMs = 0;

    struct PerfLine
    {
        int32_t browserId;
        int32_t frames;
        int32_t costMs;
        int32_t windowMs;
    };
    std::vector<PerfLine> perf;

    for (const auto& item : due)
    {
        const int32_t id = item.second;
        jobject buffer = env->CallStaticObjectMethod(javaClass_, midCaptureWorldObject_,
                                                    static_cast<jint>(id));
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            ++nullFrames;
            continue;
        }

        // Cost of the *previous* rasterisation, published by the Java side. It is
        // the only number the game thread can use to pace the next request.
        int32_t costMs = 0;
        if (midWorldObjectCost_)
        {
            costMs = env->CallStaticIntMethod(javaClass_, midWorldObjectCost_,
                                             static_cast<jint>(id));
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
                costMs = 0;
            }
        }

        bool uploaded = false;
        if (buffer)
        {
            // Direct ByteBuffer: the pixels are read straight out of its memory,
            // so there is no GetByteArrayElements copy (was ~0.5MB per frame on
            // a 512x256 page) and no per-frame byte[] allocation on the Java
            // side either.
            auto* base = static_cast<const uint8_t*>(env->GetDirectBufferAddress(buffer));
            const jlong cap = env->GetDirectBufferCapacity(buffer);
            if (base)
            {
                std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
                WorldObjectEntry* entry = FindWorldObject(id);
                if (entry && cap >= static_cast<jlong>(entry->width) * entry->height * 4 &&
                    UploadWorldObjectFrame(*entry, base, entry->width, entry->height))
                {
                    uploaded = true;
                    if (entry->frames++ == 0)
                    {
                        LOGI("[CefBrowser] world object %d: first frame uploaded (%dx%d)",
                             entry->browserId, entry->width, entry->height);
                        SendWorldObjectDebug(entry->browserId, entry->objectId, 2);
                    }
                }
            }
            env->DeleteLocalRef(buffer);
        }
        else
        {
            // Not an error on the first ticks: the page is still laying out and
            // the Java side returns null until a frame is ready.
            ++nullFrames;
        }

        // Reschedule. 3x the measured cost keeps the rasteriser under about a
        // third of the main thread, which is what leaves room for touch and IME
        // handling; the clamp stops a very cheap page from spinning at 60Hz and
        // bounds the worst case if the platform reports a silly number.
        std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
        WorldObjectEntry* entry = FindWorldObject(id);
        if (!entry)
            continue;
        if (costMs > 0)
            entry->captureCostMs = costMs;
        int32_t interval = entry->captureCostMs > 0 ? entry->captureCostMs * 3 : 100;
        if (uploaded && interval < 66)
            interval = 66;
        if (!uploaded && interval < 50)
            interval = 50;
        if (interval > 400)
            interval = 400;
        entry->nextDueMs = nowMs + static_cast<uint64_t>(interval);

        if (entry->windowStartMs == 0)
            entry->windowStartMs = nowMs;
        if (uploaded)
            ++entry->windowFrames;
        if (nowMs - entry->windowStartMs >= 3000)
        {
            perf.push_back(PerfLine{ entry->browserId, entry->windowFrames,
                                     entry->captureCostMs,
                                     static_cast<int32_t>(nowMs - entry->windowStartMs) });
            entry->windowFrames = 0;
            entry->windowStartMs = nowMs;
        }
    }

    for (const auto& line : perf)
        SendWorldObjectPerf(line.browserId, line.frames, line.costMs, line.windowMs);

    // Diagnostics every 3s: how many capture attempts came back empty. Without
    // this the device log is the only place to look, and it is unreliable here.
    if (nowMs - lastStatMs >= 3000)
    {
        lastStatMs = nowMs;
        const int32_t first = due.front().second;
        SendWorldObjectDebug(first, nullFrames, 3);
    }
}

void CefBrowser::DebugWorldObject(int32_t browserId, int32_t model, float distance)
{
    // Called on the Java main thread. Only queue the request: everything this
    // needs (ScriptCommand create_object, RwObject construction, material swap)
    // must run on the game thread.
    std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
    pendingWorldObjects_.push_back(PendingWorldObject{ browserId, model, distance });
    LOGI("[CefBrowser] debug worldobj %d queued (model %d, %.1fm)", browserId, model, distance);
}

void CefBrowser::ForgetWorldObject(int32_t browserId)
{
    // Called on the Java main thread; RwTextureDestroy must not run here.
    std::lock_guard<std::recursive_mutex> lock(worldObjectMutex_);
    pendingWorldObjectRemovals_.push_back(browserId);
}

void CefBrowser::CreateWorldObjectNow(int32_t browserId, int32_t model, float distance)
{
    // Single player has no SA-MP object pool, so the protocol path cannot be
    // exercised there. Spawn a real game object in front of the player and run
    // the exact same texture/material path, which is what actually needs
    // verifying (RwRaster upload + material swap).
    if (!pGame)
    {
        LOGW("[CefBrowser] debug worldobj: pGame is null");
        return;
    }
    if (distance < 1.0f || distance > 60.0f)
        distance = 6.0f;
    if (model <= 0 || model > 30000)
        model = 19377;   // large flat billboard-ish model

    WorldObjectEntry* entry = FindWorldObject(browserId);
    if (!entry)
    {
        // One-shot debug path: create the offscreen page too, so a single
        // broadcast is enough to exercise the whole chain.
        CreateWorldBrowser(browserId, "http://cef/test/index.html", "cef_debug", 512, 256);
        entry = FindWorldObject(browserId);
        if (!entry)
        {
            LOGW("[CefBrowser] debug worldobj: page %d could not be created", browserId);
            return;
        }
    }

    sa::CPed* ped = GamePool_FindPlayerPed();
    if (!ped)
    {
        LOGW("[CefBrowser] debug worldobj: no local player ped");
        return;
    }

    const sa::CMatrix& mat = ped->GetMatrix();
    const sa::CVector pos = mat.m_pos;
    // 用**相机朝向**而不是 ped 矩阵：GTA 的 ped m_up 是"上"方向，不是朝向，
    // 早先按它算会把物体放到玩家侧后方（截图里根本看不见）。
    float fx = 1.0f;
    float fy = 0.0f;
    {
        sa::CCamera& cam = sa::getTheCamera();
        fx = cam.m_fCamFrontXNorm;
        fy = cam.m_fCamFrontYNorm;
    }
    const float len = sqrtf(fx * fx + fy * fy);
    if (len > 0.01f)
    {
        fx /= len;
        fy /= len;
    }
    else
    {
        fx = 1.0f;
        fy = 0.0f;
    }

    const sa::CVector at{ pos.x + fx * distance, pos.y + fy * distance, pos.z + 0.3f };
    CObject* obj = pGame->NewObject(model, at, sa::CVector{ 0.0f, 0.0f, 0.0f }, 300.0f);
    if (!obj)
    {
        LOGW("[CefBrowser] debug worldobj: NewObject(%d) failed", model);
        return;
    }

    if (!entry->rwTexture)
    {
        RwRaster* raster = nullptr;
        entry->rwTexture = CreateFrameTexture(entry->width, entry->height, &raster);
        if (!entry->rwTexture)
            return;
        entry->rwRaster = raster;
    }

    obj->m_Material[0].pMaterialTexture = static_cast<RwTexture*>(entry->rwTexture);
    obj->m_bHasCustomObjectMaterial = true;
    entry->target = obj;
    entry->objectId = -1;
    entry->attached = true;

    LOGI("[CefBrowser] debug worldobj %d: model %d at (%.1f,%.1f,%.1f) entity=%p",
         browserId, model, at.x, at.y, at.z, static_cast<void*>(obj->m_entity));
    SendWorldObjectDebug(browserId, model, 1);
}

void CefBrowser::OnBrowserCreated(int browserId, bool success, int code, const std::string& reason)
{
    LOGI("[CefBrowser] browser %d create result: success=%d code=%d reason=%s",
         browserId, success ? 1 : 0, code, reason.c_str());

    auto& network = CefSession::Get().Network();
    if (network.GetState() != CefNetwork::State::Connected)
    {
        LOGW("[CefBrowser] transport not connected - create result dropped");
        return;
    }
    network.SendBrowserCreateResult(browserId, success, code, reason);
}

// ---------------------------------------------------------------------- JNI

extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeOnBrowserCreated(
    JNIEnv* env, jclass, jint browserId, jboolean success, jint code, jstring reason)
{
    std::string reasonStr;
    if (reason)
    {
        const char* chars = env->GetStringUTFChars(reason, nullptr);
        if (chars)
        {
            reasonStr = chars;
            env->ReleaseStringUTFChars(reason, chars);
        }
    }
    CefBrowser::Get().OnBrowserCreated(browserId, success == JNI_TRUE, code, reasonStr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeDebugConnect(
    JNIEnv* env, jclass, jstring host, jint port, jint playerId)
{
    if (!host || port <= 0 || port > 65535)
        return;
    const char* chars = env->GetStringUTFChars(host, nullptr);
    if (!chars)
        return;
    std::string hostStr(chars);
    env->ReleaseStringUTFChars(host, chars);

    // Android main thread -> game thread. DebugConnect tears down and re-arms
    // the transport, which Tick() also drives under debugMutex_; doing that
    // inline from another thread would race the connect path.
    const uint16_t portNum = static_cast<uint16_t>(port);
    CefBrowser::Get().PostToGameThread([hostStr, portNum, playerId]() {
        CefSession::Get().DebugConnect(hostStr, portNum, playerId);
    });
}

extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeDebugDisconnect(JNIEnv*, jclass)
{
    // Same reason as DebugConnect: debugActive_ / network_ / debugMutex_ are
    // game-thread state, and DebugDisconnect also fires the session-lost path
    // that walks CefBrowser's containers.
    CefBrowser::Get().PostToGameThread([]() {
        CefSession::Get().DebugDisconnect();
    });
}

// CefManager.install(): <filesDir>/cef, the root for cache/ and resources/.
extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeSetStorageRoot(JNIEnv* env, jclass, jstring root)
{
    if (!root)
        return;
    // storageRoot_ / cacheDir_ are read by Tick() and by the manifest walk on
    // the game thread; assigning them from the main thread was a torn-std::string
    // race. Write them on the game thread instead.
    const std::string rootStr = JavaUtf8(env, root);
    CefBrowser::Get().PostToGameThread([rootStr]() {
        cefres::CefResources::Get().SetStorageRoot(rootStr);
    });
}

// window.cef.emit(name, ...args) -> CefJsBridge -> here (Android main thread).
// argsJson is a JSON array produced by JSON.stringify on the page side.
extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeEmitFromJs(
    JNIEnv* env, jclass, jint browserId, jstring name, jstring argsJson)
{
    if (!name || !argsJson)
        return;

    const std::string nameStr = JavaUtf8(env, name);
    const std::string argsStr = JavaUtf8(env, argsJson);
    if (nameStr.empty())
        return;

    CefBrowser::Get().OnJsEmit(browserId, nameStr, argsStr);
}

// SAMP.dispatchKeyEvent -> here: ClientEmitEvent(PressKey, [vk, scan, mod, down, repeat]).
extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeSendPressKey(
    JNIEnv*, jclass, jint vk, jint scancode, jint modifiers, jboolean down, jboolean repeat)
{
    if (vk <= 0 || vk > 255)
        return;
    CefBrowser::Get().SendPressKey(vk, scancode, modifiers,
                                   down == JNI_TRUE, repeat == JNI_TRUE);
}

// UI keyboard open/close -> here: ClientEmitEvent(ChatInputState, [open]), browserId -1.
extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeSendChatInputState(
    JNIEnv*, jclass, jboolean open)
{
    CefBrowser::Get().SendChatInputState(open == JNI_TRUE);
}

// Debug-only (debuggable builds): place a World2D browser right above the local
// player so the on-device test can verify the projection and that a tap lands on
// the projected rect. Never reachable from the server protocol.
extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeDebugWorld2D(
    JNIEnv*, jclass, jint id, jfloat heightAbovePed, jfloat width, jfloat height)
{
    // DebugCreateWorld2D reads the local ped's matrix (GamePool_FindPlayerPed)
    // and pushes into world2d_, which TickWorld2D iterates on the game thread.
    // Both are illegal off the game thread - queue it.
    CefBrowser::Get().PostToGameThread([id, heightAbovePed, width, height]() {
        CefBrowser::Get().DebugCreateWorld2D(id, heightAbovePed, width, height);
    });
}

// Debug-only: make every World2D browser report its projected rect over the
// wire, so an end-to-end run against the unmodified plugin can assert where the
// client placed a page it created from a server event (see SetWorld2DProbeAll).
extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeSetWorld2DProbe(JNIEnv*, jclass, jboolean on)
{
    CefBrowser::Get().SetWorld2DProbeAll(on == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeDebugWorldObject(
    JNIEnv*, jclass, jint id, jint model, jfloat distance)
{
    CefBrowser::Get().DebugWorldObject(id, model, distance);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sampmobile_game_ui_cef_CefManager_nativeForgetWorldObject(JNIEnv*, jclass, jint id)
{
    CefBrowser::Get().ForgetWorldObject(id);
}
