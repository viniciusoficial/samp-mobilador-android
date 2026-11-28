#include "HookDomains.h"
#include "../Hooks.h"
#include "../Engine/Symbols.h"
#include "../../UI/UI.h"
#include "../../UI/SAMPWidgets/Chat.h"
#include "../../Client.h"
#include "../../Settings.h"
#include <SA.h>
#include <spdlog/spdlog.h>
#include <GLES2/gl2.h>
#include <zlib.h>
#include <sys/stat.h>
#include <ctime>
#include <thread>
#include <vector>
#include "../../L10n.h"

static std::atomic<int> s_pendingScreenshotMode{-1};

void Hooks::requestScreenshot(int modeOverride)
{
	int mode = (modeOverride == 0 || modeOverride == 1) ? modeOverride : Settings::screenshotMode();
	s_pendingScreenshotMode.store(mode);
}

static void WritePngChunk(FILE* fp, const char type[4], const uint8_t* data, uint32_t len)
{
	uint8_t lenBE[4] = {
		(uint8_t)((len >> 24) & 0xFF),
		(uint8_t)((len >> 16) & 0xFF),
		(uint8_t)((len >> 8) & 0xFF),
		(uint8_t)(len & 0xFF)
	};
	fwrite(lenBE, 1, 4, fp);
	fwrite(type, 1, 4, fp);
	if (len > 0 && data) {
		fwrite(data, 1, len, fp);
	}
	uLong crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
	if (len > 0 && data) {
		crc = crc32(crc, reinterpret_cast<const Bytef*>(data), len);
	}
	uint8_t crcBE[4] = {
		(uint8_t)((crc >> 24) & 0xFF),
		(uint8_t)((crc >> 16) & 0xFF),
		(uint8_t)((crc >> 8) & 0xFF),
		(uint8_t)(crc & 0xFF)
	};
	fwrite(crcBE, 1, 4, fp);
}

void CaptureCurrentFramebufferToPng(int mode)
{
	if (!Client::gameDir()) return;

	int w = sa::RsGlobal ? sa::RsGlobal->screenWidth : 0;
	int h = sa::RsGlobal ? sa::RsGlobal->screenHeight : 0;
	if (w <= 0 || h <= 0) {
		GLint vp[4] = {0, 0, 0, 0};
		glGetIntegerv(GL_VIEWPORT, vp);
		w = vp[2];
		h = vp[3];
	}
	if (w <= 0 || h <= 0) return;

	std::vector<uint8_t> pixels((size_t)w * (size_t)h * 4);

	// GTA:SA Android v2.10 runs OpenGL ES on a dedicated GraphicsThread via
	// WarDrum's RenderQueue. Calling raw glReadPixels() on GameMain has no
	// bound EGL context and silently leaves pixels zeroed (pure black PNG).
	// emu_glReadPixels() flushes pending draw batches, enqueues
	// RQ_Command_rqReadPixels on GraphicsThread, and blocks on
	// RenderQueue::Flush() until pixels are populated.
	using EmuGlReadPixelsFn = void(*)(int, int, int, int, uint32_t, uint32_t, void*);
	static EmuGlReadPixelsFn s_emuReadPixels = nullptr;
	if (!s_emuReadPixels && g_saSym) {
		s_emuReadPixels = eng::as<EmuGlReadPixelsFn>(eng::Sym::emu_glReadPixels);
	}
	if (s_emuReadPixels) {
		s_emuReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	} else {
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	}

	char dir1[512], dir2[512];
	snprintf(dir1, sizeof(dir1), "%sSAMP", Client::gameDir());
	snprintf(dir2, sizeof(dir2), "%sSAMP/screens", Client::gameDir());
	mkdir(dir1, 0775);
	mkdir(dir2, 0775);

	time_t now = time(nullptr);
	struct tm tmNow{};
	localtime_r(&now, &tmNow);
	static int s_shotSeq = 0;
	char filename[128];
	snprintf(filename, sizeof(filename), "samp_%04d%02d%02d_%02d%02d%02d_%02d_%s.png",
		tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
		tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec,
		(++s_shotSeq) % 100,
		mode == 1 ? "game" : "hud");

	std::string fullPath = std::string(dir2) + "/" + filename;
	std::string fileLabel(filename);

	if (pUI && pUI->chat()) {
		pUI->chat()->addInfoMessage(L10N("-> Screenshot Taken (%s): %s", "-> 截图已保存（%s）：%s"),
			mode == 1 ? "Game Only" : "HUD+Chat", filename);
	}

	std::thread([pixels = std::move(pixels), w, h, fullPath, fileLabel]() {
		const size_t rowBytes = (size_t)w * 4;
		std::vector<uint8_t> raw((rowBytes + 1) * (size_t)h);
		for (int y = 0; y < h; ++y) {
			uint8_t* dst = &raw[(size_t)y * (rowBytes + 1)];
			const uint8_t* src = &pixels[(size_t)(h - 1 - y) * rowBytes];
			dst[0] = 0; // PNG filter type 0 (None)
			memcpy(dst + 1, src, rowBytes);
			for (int x = 0; x < w; ++x) {
				dst[1 + x * 4 + 3] = 0xFF; // force opaque alpha
			}
		}

		uLongf compBound = compressBound((uLong)raw.size());
		std::vector<uint8_t> compressed(compBound);
		if (compress2(compressed.data(), &compBound, raw.data(), (uLong)raw.size(), Z_BEST_SPEED) != Z_OK) {
			spdlog::error("Screenshot compress2 failed for {}", fullPath);
			return;
		}

		FILE* fp = fopen(fullPath.c_str(), "wb");
		if (!fp) {
			spdlog::error("Screenshot fopen failed for {}", fullPath);
			return;
		}
		static const uint8_t kPngSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
		fwrite(kPngSig, 1, 8, fp);

		uint8_t ihdr[13] = {
			(uint8_t)((w >> 24) & 0xFF), (uint8_t)((w >> 16) & 0xFF), (uint8_t)((w >> 8) & 0xFF), (uint8_t)(w & 0xFF),
			(uint8_t)((h >> 24) & 0xFF), (uint8_t)((h >> 16) & 0xFF), (uint8_t)((h >> 8) & 0xFF), (uint8_t)(h & 0xFF),
			8, // bit depth
			6, // color type RGBA
			0, 0, 0
		};
		WritePngChunk(fp, "IHDR", ihdr, 13);
		WritePngChunk(fp, "IDAT", compressed.data(), (uint32_t)compBound);
		WritePngChunk(fp, "IEND", nullptr, 0);
		fclose(fp);
		spdlog::info("Screenshot saved: {} ({}x{}, {} bytes)", fileLabel, w, h, (uint32_t)compBound);
	}).detach();
}

bool ConsumePendingScreenshot(int expectedMode)
{
	int cur = expectedMode;
	return s_pendingScreenshotMode.compare_exchange_strong(cur, -1);
}
