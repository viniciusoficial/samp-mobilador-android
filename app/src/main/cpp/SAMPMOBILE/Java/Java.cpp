//
// Created by ALYN on 2024/8/25.
//
#include "Java.h"
#include "../Client.h"
#include "../InputArbiter.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/ButtonPanel.h"
#include "../UI/SAMPWidgets/Chat.h"
#include "../Net/NetGame.h"
#include "../Game/Artwork/ArtworkManager.h"
#include "../Game/NativeCameraMouse.h"
#include "../Game/PcInput.h"

#include <spdlog/spdlog.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>


namespace {
struct PendingDialogResp {
	int dialogId;
	int buttonId;
	int listItem;
	std::string text;
};

struct PendingHardwareKey {
	int keyCode;
	bool isDown;
};

std::mutex g_jniInputMutex;
std::deque<std::string> g_pendingKeyboardSends;
std::deque<PendingDialogResp> g_pendingDialogResponses;
std::deque<PendingHardwareKey> g_pendingHardwareKeys;

inline void clearJniException(JNIEnv* env, const char* where)
{
	if (env && env->ExceptionCheck()) {
		spdlog::error("JNI exception in {}", where);
		env->ExceptionDescribe();
		env->ExceptionClear();
	}
}
}

void Java::processPendingInputs()
{
	InputArbiter::processPendingBackDismiss();

	std::deque<std::string> kbQueue;
	std::deque<PendingDialogResp> dlgQueue;
	std::deque<PendingHardwareKey> keyQueue;
	{
		std::lock_guard<std::mutex> lock(g_jniInputMutex);
		kbQueue.swap(g_pendingKeyboardSends);
		dlgQueue.swap(g_pendingDialogResponses);
		keyQueue.swap(g_pendingHardwareKeys);
	}

	if (pUI && pUI->chat()) {
		for (const auto& s : kbQueue) {
			pUI->chat()->keyboardEvent(s);
		}
	}

	if (pNetGame) {
		for (auto& d : dlgQueue) {
			pNetGame->SendDialogResponse(d.dialogId, d.buttonId, d.listItem, const_cast<char*>(d.text.c_str()));
		}
	}

	if (pUI && pUI->buttonpanel()) {
		for (const auto& k : keyQueue) {
			pUI->buttonpanel()->handleHardwareKey(k.keyCode, k.isDown);
		}
	}
}

Java::Java(JNIEnv* env, jobject sampObj, jobject uiObj)
{
	spdlog::info("Initializing java..");

	if (!env || !sampObj || !uiObj) {
		spdlog::error("Invalid java objects");
		return;
	}

	m_sampActivity = env->NewGlobalRef(sampObj);
	m_uiActivity = env->NewGlobalRef(uiObj);

	jclass sampClass = env->GetObjectClass(sampObj);
	if (!sampClass) {
		clearJniException(env, "Java::Java(sampClass)");
		spdlog::error("SAMP class not found");
		return;
	}

	jclass uiClass = env->GetObjectClass(uiObj);
	if (!uiClass) {
		env->DeleteLocalRef(sampClass);
		clearJniException(env, "Java::Java(uiClass)");
		spdlog::error("UI class not found");
		return;
	}

	// Overlay de configuracoes de teclado/mouse (comando /mob), definido na
	// activity SAMP — mesmo papel do KeyboardMouseSettings do mobilador.
	m_showKbMouseSettings = env->GetMethodID(sampClass, "showKeyboardMouseSettings", "()V");
	clearJniException(env, "GetMethodID(showKeyboardMouseSettings)");

	m_setPauseState = env->GetMethodID(uiClass, "setPauseState", "(Z)V");
	clearJniException(env, "GetMethodID(setPauseState)");

	m_showLoadingScreen = env->GetMethodID(uiClass, "showLoadingScreen", "(Z)V");
	clearJniException(env, "GetMethodID(showLoadingScreen)");
	m_showKeyboard = env->GetMethodID(uiClass, "showKeyboard", "(Z)V");
	clearJniException(env, "GetMethodID(showKeyboard)");
	m_showDialog = env->GetMethodID(uiClass, "showDialog", "(IILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	clearJniException(env, "GetMethodID(showDialog)");
	m_hideDialog = env->GetMethodID(uiClass, "hideDialog", "()V");
	clearJniException(env, "GetMethodID(hideDialog)");
	m_showEditObject = env->GetMethodID(uiClass, "showEditObject", "(Z)V");
	clearJniException(env, "GetMethodID(showEditObject)");

	m_showWantedLevel = env->GetMethodID(uiClass, "showWantedLevel", "(Z)V");
	clearJniException(env, "GetMethodID(showWantedLevel)");
	m_setWantedLevel = env->GetMethodID(uiClass, "setWantedLevel", "(I)V");
	clearJniException(env, "GetMethodID(setWantedLevel)");

	m_exitGame = env->GetMethodID(uiClass, "exitGame", "()V");
	clearJniException(env, "GetMethodID(exitGame)");

	if (jclass dlClass = env->FindClass("com/sampmobile/game/ArtworkDownloader")) {
		m_artworkDownloaderClass = static_cast<jclass>(env->NewGlobalRef(dlClass));
		m_artworkEnqueue = env->GetStaticMethodID(m_artworkDownloaderClass, "enqueue",
			"(IIILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J)V");
		clearJniException(env, "GetStaticMethodID(ArtworkDownloader.enqueue)");
		m_artworkCancelAll = env->GetStaticMethodID(m_artworkDownloaderClass, "cancelAll", "(I)V");
		clearJniException(env, "GetStaticMethodID(ArtworkDownloader.cancelAll)");
		env->DeleteLocalRef(dlClass);
	} else {
		clearJniException(env, "FindClass(ArtworkDownloader)");
	}

	env->DeleteLocalRef(sampClass);
	env->DeleteLocalRef(uiClass);
}

JNIEnv* Java::getEnv()
{
	JavaVM* vm = Client::javaVM();
	if (!vm) {
		spdlog::error("No java vm");
		return nullptr;
	}

	JNIEnv* env = nullptr;
	jint status = vm->GetEnv((void**) &env, JNI_VERSION_1_6);
	if (status == JNI_EDETACHED) {
		// Native threads (e.g. the GTA game thread) are not initially attached to the
		// JVM — attach on first use and detach automatically if the thread exits.
		if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
			spdlog::error("Failed to attach thread to JVM");
			return nullptr;
		}
		struct JvmThreadDetacher {
			JavaVM* jvm = nullptr;
			~JvmThreadDetacher() {
				if (jvm) {
					jvm->DetachCurrentThread();
				}
			}
		};
		thread_local JvmThreadDetacher s_detacher;
		s_detacher.jvm = vm;
	}
	else if (status != JNI_OK) {
		spdlog::error("Failed to get env");
		return nullptr;
	}

	return env;
}

jstring createJString(JNIEnv* env, const char* text)
{
	if (!env) return nullptr;
	const char* safeText = text ? text : "";
	const size_t len = strlen(safeText);

	jclass strClass = env->FindClass("java/lang/String");
	if (!strClass) {
		clearJniException(env, "createJString(FindClass)");
		return nullptr;
	}
	jmethodID ctorID = env->GetMethodID(strClass, "<init>", "([BLjava/lang/String;)V");
	if (!ctorID) {
		env->DeleteLocalRef(strClass);
		clearJniException(env, "createJString(GetMethodID)");
		return nullptr;
	}
	jstring encoding = env->NewStringUTF("UTF-8");
	jbyteArray bytes = env->NewByteArray(static_cast<jsize>(len));
	jstring result = nullptr;
	if (encoding && bytes) {
		if (len > 0) {
			env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(len), reinterpret_cast<const jbyte*>(safeText));
		}
		result = reinterpret_cast<jstring>(env->NewObject(strClass, ctorID, bytes, encoding));
		clearJniException(env, "createJString(NewObject)");
	}
	if (bytes) env->DeleteLocalRef(bytes);
	if (encoding) env->DeleteLocalRef(encoding);
	env->DeleteLocalRef(strClass);
	return result;
}

void Java::setPauseState(bool pause)
{
	m_cachedWantedVisible = -1;
	JNIEnv* env = getEnv();
	if (!env || !m_uiActivity || !m_setPauseState) {
		return;
	}
	env->CallVoidMethod(m_uiActivity, m_setPauseState, static_cast<jboolean>(pause));
	clearJniException(env, "setPauseState");
}

void Java::showLoadingScreen(bool show)
{
	JNIEnv* env = getEnv();
	if (!env || !m_uiActivity || !m_showLoadingScreen) {
		return;
	}
	env->CallVoidMethod(m_uiActivity, m_showLoadingScreen, static_cast<jboolean>(show));
	clearJniException(env, "showLoadingScreen");
}

void Java::showKeyboard(bool show)
{
	JNIEnv* env = getEnv();
	if (!env || !m_uiActivity || !m_showKeyboard) {
		return;
	}
	env->CallVoidMethod(m_uiActivity, m_showKeyboard, static_cast<jboolean>(show));
	clearJniException(env, "showKeyboard");
}

void Java::showKeyboardMouseSettings()
{
	JNIEnv* env = getEnv();
	if (!env || !m_sampActivity || !m_showKbMouseSettings) {
		spdlog::warn("showKeyboardMouseSettings unavailable");
		return;
	}
	env->CallVoidMethod(m_sampActivity, m_showKbMouseSettings);
	clearJniException(env, "showKeyboardMouseSettings");
}

void Java::showDialog(int dialog_id, int dialog_style, char* title, char* message, char* button1, char* button2)
{
	JNIEnv* env = getEnv();
	if (!env || !m_uiActivity || !m_showDialog) {
		return;
	}

	jstring jtitle = createJString(env, title);
	jstring jmessage = createJString(env, message);
	jstring jbutton1 = createJString(env, button1);
	jstring jbutton2 = createJString(env, button2);

	env->CallVoidMethod(m_uiActivity, m_showDialog, dialog_id, dialog_style, jtitle, jmessage, jbutton1, jbutton2);
	clearJniException(env, "showDialog");

	if (jtitle) env->DeleteLocalRef(jtitle);
	if (jmessage) env->DeleteLocalRef(jmessage);
	if (jbutton1) env->DeleteLocalRef(jbutton1);
	if (jbutton2) env->DeleteLocalRef(jbutton2);
}

void Java::hideDialog()
{
	JNIEnv* env = getEnv();
	if (!env || !m_uiActivity || !m_hideDialog) {
		return;
	}
	env->CallVoidMethod(m_uiActivity, m_hideDialog);
	clearJniException(env, "hideDialog");
}

void Java::showEditObject(bool show)
{
	JNIEnv* env = getEnv();
	if (!env || !m_uiActivity || !m_showEditObject) {
		return;
	}
	env->CallVoidMethod(m_uiActivity, m_showEditObject, static_cast<jboolean>(show));
	clearJniException(env, "showEditObject");
}

void Java::showWantedLevel(bool show)
{
	const int nextState = show ? 1 : 0;
	if (m_cachedWantedVisible == nextState) {
		return;
	}
	JNIEnv* env = getEnv();
	if (!env || !m_uiActivity || !m_showWantedLevel) {
		return;
	}
	m_cachedWantedVisible = nextState;
	env->CallVoidMethod(m_uiActivity, m_showWantedLevel, static_cast<jboolean>(show));
	clearJniException(env, "showWantedLevel");
}

void Java::setWantedLevel(int level)
{
	if (m_cachedWantedLevel == level) {
		return;
	}
	JNIEnv* env = getEnv();
	if (!env || !m_uiActivity || !m_setWantedLevel) {
		return;
	}
	m_cachedWantedLevel = level;
	env->CallVoidMethod(m_uiActivity, m_setWantedLevel, level);
	clearJniException(env, "setWantedLevel");
}

void Java::exitGame()
{
	JNIEnv* env = getEnv();
	if (!env || !m_uiActivity || !m_exitGame) {
		return;
	}
	env->CallVoidMethod(m_uiActivity, m_exitGame);
	clearJniException(env, "exitGame");
}

void Java::startArtworkDownload(int32_t generation, int32_t fileType, uint32_t crc,
	const std::string& url, const std::string& fallbackUrl,
	const std::string& tempPath, const std::string& finalPath, uint32_t expectedSize)
{
	JNIEnv* env = getEnv();
	if (!env || !m_artworkDownloaderClass || !m_artworkEnqueue) {
		artwork::ArtworkManager::instance().notifyWorkerFinished(
			generation, static_cast<uint8_t>(fileType), crc, false,
			artwork::DownloadFailureKind::Unsupported, "JNI downloader unavailable");
		return;
	}
	jstring jUrl = env->NewStringUTF(url.c_str());
	jstring jFallback = fallbackUrl.empty() ? nullptr : env->NewStringUTF(fallbackUrl.c_str());
	jstring jTmp = env->NewStringUTF(tempPath.c_str());
	jstring jDst = env->NewStringUTF(finalPath.c_str());
	if (!jUrl || (!fallbackUrl.empty() && !jFallback) || !jTmp || !jDst) {
		if (jUrl) env->DeleteLocalRef(jUrl);
		if (jFallback) env->DeleteLocalRef(jFallback);
		if (jTmp) env->DeleteLocalRef(jTmp);
		if (jDst) env->DeleteLocalRef(jDst);
		clearJniException(env, "startArtworkDownload(NewStringUTF)");
		artwork::ArtworkManager::instance().notifyWorkerFinished(
			generation, static_cast<uint8_t>(fileType), crc, false,
			artwork::DownloadFailureKind::TransientNet, "JNI string alloc failed");
		return;
	}
	env->CallStaticVoidMethod(m_artworkDownloaderClass, m_artworkEnqueue,
		static_cast<jint>(generation),
		static_cast<jint>(fileType),
		static_cast<jint>(crc),
		jUrl,
		jFallback,
		jTmp,
		jDst,
		static_cast<jlong>(expectedSize));
	clearJniException(env, "startArtworkDownload");
	env->DeleteLocalRef(jUrl);
	if (jFallback) env->DeleteLocalRef(jFallback);
	env->DeleteLocalRef(jTmp);
	env->DeleteLocalRef(jDst);
}

void Java::cancelArtworkDownloads(int32_t newGeneration)
{
	JNIEnv* env = getEnv();
	if (!env || !m_artworkDownloaderClass || !m_artworkCancelAll) {
		return;
	}
	env->CallStaticVoidMethod(m_artworkDownloaderClass, m_artworkCancelAll, static_cast<jint>(newGeneration));
	clearJniException(env, "cancelArtworkDownloads");
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_SAMP_initializeSAMP(JNIEnv* env, jobject sampObj, jobject uiObj, jstring gameDir, jboolean isOffline)
{
	if (!env || !gameDir) return;
	const char* gameDirCStr = env->GetStringUTFChars(gameDir, nullptr);
	if (!gameDirCStr) return;
	std::string gameDirStr(gameDirCStr);
	env->ReleaseStringUTFChars(gameDir, gameDirCStr);

	Client::initialize(gameDirStr, isOffline == JNI_TRUE);
	g_java = new Java(env, sampObj, uiObj);
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_ui_UI_setJavaDialogVisible(JNIEnv* env, jobject obj, jboolean visible)
{
	(void) env;
	(void) obj;
	InputArbiter::setJavaDialogVisible(visible == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_ui_UI_setJavaKeyboardVisible(JNIEnv* env, jobject obj, jboolean visible)
{
	(void) env;
	(void) obj;
	InputArbiter::setJavaKeyboardVisible(visible == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_ui_UI_keyboardSend(JNIEnv* env, jobject obj, jbyteArray str)
{
	if (!env || !str) return;
	jboolean isCopy = JNI_FALSE;
	jbyte* msg = env->GetByteArrayElements(str, &isCopy);
	if (!msg) return;
	jsize length = env->GetArrayLength(str);

	std::string str_string(reinterpret_cast<const char*>(msg), length > 0 ? static_cast<size_t>(length) : 0u);
	env->ReleaseByteArrayElements(str, msg, JNI_ABORT);

	std::lock_guard<std::mutex> lock(g_jniInputMutex);
	if (g_pendingKeyboardSends.size() < 32) {
		g_pendingKeyboardSends.push_back(std::move(str_string));
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_ui_UI_sendDialogResponse(JNIEnv* env, jobject obj, jint dialog_id, jint button_id, jint list_item, jbyteArray str)
{
	spdlog::info("Java_com_sampmobile_game_ui_UI_sendDialogResponse");
	if (!env) return;

	std::string szStr;
	if (str) {
		jboolean isCopy = JNI_FALSE;
		jbyte* msg = env->GetByteArrayElements(str, &isCopy);
		if (msg) {
			jsize length = env->GetArrayLength(str);
			szStr.assign(reinterpret_cast<const char*>(msg), length > 0 ? static_cast<size_t>(length) : 0u);
			env->ReleaseByteArrayElements(str, msg, JNI_ABORT);
		}
	}

	std::lock_guard<std::mutex> lock(g_jniInputMutex);
	if (g_pendingDialogResponses.size() < 32) {
		g_pendingDialogResponses.push_back({dialog_id, button_id, list_item, std::move(szStr)});
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_SAMP_onHardwareKeyEvent(JNIEnv* env, jobject obj, jint keyCode, jboolean isDown)
{
	std::lock_guard<std::mutex> lock(g_jniInputMutex);
	if (g_pendingHardwareKeys.size() < 64) {
		g_pendingHardwareKeys.push_back({static_cast<int>(keyCode), isDown == JNI_TRUE});
	}
}

// ---- Physical mouse (relative deltas from Android generic motion events) ----
// Todo o caminho passa por Game/PcInput.cpp (porte do mobilador).

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_SAMP_onMouseDelta(JNIEnv* env, jobject obj, jfloat dx, jfloat dy)
{
	(void) env;
	(void) obj;
	PcInputAddMouseDelta(static_cast<float>(dx), static_cast<float>(dy));
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_SAMP_onMouseButton(JNIEnv* env, jobject obj, jint button, jboolean pressed)
{
	(void) env;
	(void) obj;
	PcInputSetMouseButton(static_cast<int>(button), -1, pressed == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_SAMP_onMouseWheel(JNIEnv* env, jobject obj, jint steps)
{
	(void) env;
	(void) obj;
	PcInputHandleWheel(static_cast<int>(steps));
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_SAMP_onMouseReset(JNIEnv* env, jobject obj)
{
	(void) env;
	(void) obj;
	PcInputReleaseMouseButtons();
	NativeCameraMouseReset();
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_sampmobile_game_ui_UI_handleNativeBackPressed(JNIEnv* env, jobject obj)
{
	(void) env;
	(void) obj;
	return InputArbiter::requestNativeBackDismiss() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_ArtworkDownloader_onDownloadProgress(
	JNIEnv* env, jclass cls, jint generation, jint fileType, jint crc, jlong downloaded, jlong total)
{
	(void) env;
	(void) cls;
	artwork::ArtworkManager::instance().notifyWorkerProgress(
		static_cast<int32_t>(generation),
		static_cast<uint8_t>(fileType),
		static_cast<uint32_t>(crc),
		downloaded > 0 ? static_cast<uint64_t>(downloaded) : 0u,
		total > 0 ? static_cast<uint64_t>(total) : 0u);
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_ArtworkDownloader_onDownloadFinished(
	JNIEnv* env, jclass cls, jint generation, jint fileType, jint crc, jboolean success, jint failureKind, jstring error)
{
	(void) cls;
	std::string errStr;
	if (env && error) {
		const char* cstr = env->GetStringUTFChars(error, nullptr);
		if (cstr) {
			errStr = cstr;
			env->ReleaseStringUTFChars(error, cstr);
		}
	}
	artwork::ArtworkManager::instance().notifyWorkerFinished(
		static_cast<int32_t>(generation),
		static_cast<uint8_t>(fileType),
		static_cast<uint32_t>(crc),
		success == JNI_TRUE,
		static_cast<artwork::DownloadFailureKind>(failureKind),
		errStr);
}

extern "C" JNIEXPORT jint JNICALL Java_com_sampmobile_game_ArtworkDownloader_validateAndCommitFile(
	JNIEnv* env, jclass cls, jint fileType, jstring tempPath, jstring finalPath)
{
	(void) cls;
	if (!env || !tempPath || !finalPath) {
		return static_cast<jint>(artwork::DownloadFailureKind::UnsafeUrl);
	}
	const char* tmpCStr = env->GetStringUTFChars(tempPath, nullptr);
	const char* dstCStr = env->GetStringUTFChars(finalPath, nullptr);
	if (!tmpCStr || !dstCStr) {
		if (tmpCStr) env->ReleaseStringUTFChars(tempPath, tmpCStr);
		if (dstCStr) env->ReleaseStringUTFChars(finalPath, dstCStr);
		return static_cast<jint>(artwork::DownloadFailureKind::TransientNet);
	}
	const std::string tmp(tmpCStr);
	const std::string dst(dstCStr);
	env->ReleaseStringUTFChars(tempPath, tmpCStr);
	env->ReleaseStringUTFChars(finalPath, dstCStr);

	const artwork::FileType ft = (fileType == 1) ? artwork::FileType::Dff
		: ((fileType == 2) ? artwork::FileType::Txd : artwork::FileType::None);
	return static_cast<jint>(artwork::ArtworkManager::validateAndCommitDownloadedFile(ft, tmp, dst));
}
