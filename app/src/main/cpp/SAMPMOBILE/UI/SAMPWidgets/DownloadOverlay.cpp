#include "DownloadOverlay.h"
#include "../ImGuiRenderer.h"
#include "../UISettings.h"
#include "../../Game/Artwork/ArtworkManager.h"
#include "../../Game/Util.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include "../../L10n.h"

namespace artwork {

// Classic PC SA-MP 0.3.DL CDownloadWindow style:
// Centered translucent dark DXUT dialog with caption bar
// "Updating models.. [completed/total] (XX.XX KB/s)", 3-column header
// ("Type" | "ID" | "Progress" in #A9C4E4), and auto-scrolling file list
// showing "Model"/"Texture", "0xCRC", and status ("0%", "45%", green "100%", red "Error: ...").
void DrawDownloadOverlay(ImGuiRenderer* renderer, float displayW, float displayH)
{
	if (!renderer) {
		return;
	}
	const DownloadProgressSnapshot snap = ArtworkManager::instance().progressSnapshot();
	if (!snap.active || snap.totalFiles == 0) {
		return;
	}

	static uint32_t s_lastRateMs = 0;
	static uint64_t s_lastBytes = 0;
	static double s_speedKBps = 0.0;

	const uint32_t now = GetTickCount();
	if (s_lastRateMs == 0 || snap.downloadedBytes < s_lastBytes) {
		s_lastRateMs = now;
		s_lastBytes = snap.downloadedBytes;
		s_speedKBps = 0.0;
	} else if (now - s_lastRateMs >= 500u) {
		const double dtSec = static_cast<double>(now - s_lastRateMs) / 1000.0;
		s_speedKBps = (static_cast<double>(snap.downloadedBytes - s_lastBytes) / 1024.0) / dtSec;
		s_lastRateMs = now;
		s_lastBytes = snap.downloadedBytes;
	}

	// Scale proportionally to mobile landscape display while preserving PC 0.3.DL 640x300 aspect
	const float fontSize = std::clamp(UISettings::fontSize() * 0.50f, 15.0f, 28.0f);
	const float headerFontSize = fontSize * 0.92f;
	const float rowFontSize = fontSize * 0.88f;

	const float winW = std::clamp(displayW * 0.56f, 440.0f, 880.0f);
	const float winH = std::clamp(displayH * 0.52f, 250.0f, 480.0f);
	const float x0 = (displayW - winW) * 0.5f;
	const float y0 = (displayH - winH) * 0.5f;

	const float captionH = fontSize * 1.55f;
	const float colHeaderH = headerFontSize * 1.45f;
	const float listPad = 6.0f;

	// Outer classic DXUT dialog frame (translucent dark slate)
	renderer->drawRect(ImVec2(x0, y0), ImVec2(x0 + winW, y0 + winH), ImColor(12, 16, 22, 224), true);
	renderer->drawRect(ImVec2(x0, y0), ImVec2(x0 + winW, y0 + winH), ImColor(78, 96, 122, 210), false);

	// Caption bar ("Updating models.. [done/total] (XX.XX KB/s)")
	renderer->drawRect(ImVec2(x0 + 1.0f, y0 + 1.0f), ImVec2(x0 + winW - 1.0f, y0 + captionH), ImColor(24, 32, 44, 238), true);
	renderer->drawRect(ImVec2(x0, y0 + captionH), ImVec2(x0 + winW, y0 + captionH + 1.0f), ImColor(68, 86, 112, 190), true);

	const uint32_t doneCount = snap.completedFiles + snap.failedFiles;
	char caption[160];
	std::snprintf(caption, sizeof(caption), L10N("Updating models.. [%u/%u] (%.2f KB/s)", "正在更新模型.. [%u/%u]（%.2f KB/s）"),
		doneCount, snap.totalFiles, s_speedKBps);
	renderer->drawText(
		ImVec2(x0 + 12.0f, y0 + (captionH - fontSize) * 0.5f),
		ImColor(235, 240, 248, 255),
		caption,
		true,
		fontSize);

	// 3-Column Header ("Type" | "ID" | "Progress") — authentic SA-MP #A9C4E4 tint
	const float colHeaderY = y0 + captionH + 3.0f;
	const float colTypeX = x0 + 16.0f;
	const float colIdX = x0 + winW * 0.28f;
	const float colProgX = x0 + winW * 0.60f;
	const ImColor headerCol(169, 196, 228, 255); // 0xFFA9C4E4

	renderer->drawText(ImVec2(colTypeX, colHeaderY), headerCol, "Type", true, headerFontSize);
	renderer->drawText(ImVec2(colIdX, colHeaderY), headerCol, "ID", true, headerFontSize);
	renderer->drawText(ImVec2(colProgX, colHeaderY), headerCol, "Progress", true, headerFontSize);

	// Inner list box frame
	const float listX0 = x0 + listPad;
	const float listY0 = colHeaderY + colHeaderH;
	const float listX1 = x0 + winW - listPad;
	const float listY1 = y0 + winH - listPad;
	renderer->drawRect(ImVec2(listX0, listY0), ImVec2(listX1, listY1), ImColor(8, 10, 14, 215), true);
	renderer->drawRect(ImVec2(listX0, listY0), ImVec2(listX1, listY1), ImColor(52, 64, 82, 190), false);

	const float rowH = rowFontSize * 1.42f;
	const float availH = std::max(0.0f, (listY1 - listY0) - 6.0f);
	const int maxVisibleRows = std::max(1, static_cast<int>(availH / rowH));

	const size_t totalRows = snap.items.size();
	const size_t startIdx = (totalRows > static_cast<size_t>(maxVisibleRows))
		? (totalRows - static_cast<size_t>(maxVisibleRows))
		: 0u;

	for (size_t i = startIdx; i < totalRows; ++i) {
		const DownloadListItem& item = snap.items[i];
		const int visIdx = static_cast<int>(i - startIdx);
		const float ry = listY0 + 3.0f + static_cast<float>(visIdx) * rowH;
		if (ry + rowH > listY1 - 1.0f) {
			break;
		}

		// Highlight the newest / currently downloading row like CDXUTListBoxDL::SelectItem
		const bool isHighlighted = (i + 1 == totalRows) || (item.state == ResourceState::Downloading);
		if (isHighlighted) {
			renderer->drawRect(
				ImVec2(listX0 + 2.0f, ry),
				ImVec2(listX1 - 2.0f, ry + rowH - 1.0f),
				ImColor(36, 54, 78, 155),
				true);
		} else if ((visIdx & 1) == 1) {
			renderer->drawRect(
				ImVec2(listX0 + 2.0f, ry),
				ImVec2(listX1 - 2.0f, ry + rowH - 1.0f),
				ImColor(255, 255, 255, 10),
				true);
		}

		const char* typeStr = (item.type == FileType::Dff) ? l10n::tr("Model", "模型") : l10n::tr("Texture", "贴图");
		char idStr[24];
		std::snprintf(idStr, sizeof(idStr), "0x%X", item.crc);

		char progStr[64];
		ImColor progCol(230, 236, 244, 255);
		if (item.state == ResourceState::UrlRequested || item.state == ResourceState::Missing) {
			std::snprintf(progStr, sizeof(progStr), "0%%");
			progCol = ImColor(195, 204, 218, 255);
		} else if (item.state == ResourceState::Downloading) {
			double pct = 0.0;
			if (item.totalBytes > 0) {
				pct = std::clamp(static_cast<double>(item.downloadedBytes) * 100.0 / static_cast<double>(item.totalBytes), 0.0, 100.0);
			}
			std::snprintf(progStr, sizeof(progStr), "%.0f%%", pct);
			progCol = ImColor(242, 246, 255, 255);
		} else if (item.state == ResourceState::Cached) {
			std::snprintf(progStr, sizeof(progStr), "100%%");
			progCol = ImColor(119, 170, 119, 255); // classic SA-MP {77AA77}
		} else {
			if (!item.error.empty()) {
				std::snprintf(progStr, sizeof(progStr), L10N("Error: %.20s", "错误：%.20s"), item.error.c_str());
			} else {
				std::snprintf(progStr, sizeof(progStr), "%s", l10n::tr("Error", "错误"));
			}
			progCol = ImColor(204, 136, 136, 255); // classic SA-MP {CC8888}
		}

		const float textY = ry + (rowH - rowFontSize) * 0.5f;
		const ImColor rowTextCol(228, 234, 242, 255);
		renderer->drawText(ImVec2(colTypeX, textY), rowTextCol, typeStr, true, rowFontSize);
		renderer->drawText(ImVec2(colIdX, textY), rowTextCol, idStr, true, rowFontSize);
		renderer->drawText(ImVec2(colProgX, textY), progCol, progStr, true, rowFontSize);
	}
}

} // namespace artwork
