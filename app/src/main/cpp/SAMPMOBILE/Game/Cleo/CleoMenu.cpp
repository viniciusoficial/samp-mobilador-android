#include "CleoInternal.h"
#include "L10n.h"

namespace cleo_internal {

bool isAnyMenuVisible()
{
	return g_scriptMenu.active || g_builtinMenu.open;
}

std::vector<int> collectInvokableScriptIndices()
{
	std::vector<int> out;
	for (int i = 0; i < static_cast<int>(g_scripts.size()); ++i) {
		if (g_scripts[i].invokable) out.push_back(i);
	}
	return out;
}

} // namespace cleo_internal

using namespace cleo_internal;

void CleoEngine::toggleMenu()
{
	if (!g_enabled || !g_symbolsReady) return;
	std::lock_guard<std::mutex> lock(g_touchMutex);
	if (g_scriptMenu.active) {
		g_scriptMenu.lastTouchedIndex = -2;
		g_scriptMenu.lastTouchedTime = nowMs();
		g_scriptMenu.active = false;
		g_menuRowRects.clear();
	} else {
		g_builtinMenu.open = !g_builtinMenu.open;
		g_builtinMenu.page = 0;
		if (!g_builtinMenu.open) g_menuRowRects.clear();
	}
}

bool CleoEngine::isMenuOpen()
{
	if (!g_enabled || !g_symbolsReady) return false;
	std::lock_guard<std::mutex> lock(g_touchMutex);
	return isAnyMenuVisible();
}

bool CleoEngine::dismissMenu()
{
	if (!g_enabled || !g_symbolsReady) return false;
	std::lock_guard<std::mutex> lock(g_touchMutex);
	if (g_scriptMenu.active) {
		g_scriptMenu.lastTouchedIndex = -2;
		g_scriptMenu.lastTouchedTime = nowMs();
		g_scriptMenu.active = false;
		g_menuRowRects.clear();
		return true;
	}
	if (g_builtinMenu.open) {
		g_builtinMenu.open = false;
		g_menuRowRects.clear();
		return true;
	}
	return false;
}

void CleoEngine::renderMenu(ImGuiRenderer* renderer, float displayW, float displayH)
{
	if (!renderer || !g_enabled || !g_symbolsReady) return;
	std::lock_guard<std::mutex> lock(g_touchMutex);
	if (!isAnyMenuVisible()) return;

	const ImVec2 displaySize(displayW, displayH);
	g_screenW = displaySize.x;
	g_screenH = displaySize.y;

	const float panelW = std::clamp(displaySize.x * 0.36f, 420.0f, 680.0f);
	const float rowH = std::clamp(displaySize.y * 0.056f, 44.0f, 64.0f);
	const float headerH = rowH * 1.15f;
	const float footerH = rowH * 1.05f;
	const float pad = 16.0f;

	std::string title;
	std::string closeText;
	std::vector<std::pair<int, std::string>> rows;
	int page = 0;
	int totalItems = 0;
	int activeIdx = -1;

	if (g_scriptMenu.active) {
		title = g_scriptMenu.title.empty() ? "CLEO MENU" : g_scriptMenu.title;
		closeText = g_scriptMenu.closeLabel.empty() ? "CLOSE" : g_scriptMenu.closeLabel;
		page = g_scriptMenu.page;
		totalItems = static_cast<int>(g_scriptMenu.items.size());
		activeIdx = g_scriptMenu.activeIndex;
		int start = page * kMenuItemsPerPage;
		int end = std::min(totalItems, start + kMenuItemsPerPage);
		for (int i = start; i < end; ++i) {
			rows.emplace_back(i, g_scriptMenu.items[i]);
		}
	} else {
		auto inv = collectInvokableScriptIndices();
		size_t runningAuto = 0;
		for (const auto& s : g_scripts) {
			if (!s.invokable && s.thread.m_bActive) ++runningAuto;
		}
		char hdr[128];
		snprintf(hdr, sizeof(hdr), L10N("CLEO Menu  (%zu .csi | %zu .csa running)", "CLEO 菜单  (%zu .csi | %zu .csa 运行中)"), inv.size(), runningAuto);
		title = hdr;
		closeText = l10n::tr("Close", "关闭");
		page = g_builtinMenu.page;
		totalItems = static_cast<int>(inv.size());
		int start = page * kMenuItemsPerPage;
		int end = std::min(totalItems, start + kMenuItemsPerPage);
		for (int i = start; i < end; ++i) {
			const CleoScript& sc = g_scripts[inv[i]];
			std::string label = sc.displayName;
			if (sc.thread.m_bActive) label += l10n::tr("  [running]", "  [运行中]");
			rows.emplace_back(inv[i], label);
		}
	}

	const int visibleRowCount = std::max<int>(1, static_cast<int>(rows.size()));
	const float panelH = headerH + visibleRowCount * rowH + footerH + pad * 2.0f;
	const float x1 = (displaySize.x - panelW) * 0.5f;
	const float y1 = (displaySize.y - panelH) * 0.5f;
	const float x2 = x1 + panelW;
	const float y2 = y1 + panelH;
	g_menuPanelRect = RectF{x1, y1, x2, y2};

	const ImColor bgCol(18, 18, 22, 232);
	const ImColor borderCol(245, 183, 0, 230);
	const ImColor headerBg(34, 30, 18, 245);
	const ImColor goldText(245, 196, 40, 255);
	const ImColor rowBgA(32, 32, 38, 210);
	const ImColor rowBgB(26, 26, 32, 210);
	const ImColor rowActiveBg(90, 68, 12, 235);
	const ImColor whiteText(242, 242, 245, 255);
	const ImColor dimText(170, 170, 180, 255);
	const ImColor btnBg(50, 50, 60, 235);

	// Panel backdrop & gold border
	renderer->drawRect(ImVec2(x1, y1), ImVec2(x2, y2), bgCol, true);
	renderer->drawRect(ImVec2(x1, y1), ImVec2(x2, y2), borderCol, false, 2.5f);

	// Header
	renderer->drawRect(ImVec2(x1 + 2, y1 + 2), ImVec2(x2 - 2, y1 + headerH), headerBg, true);
	const float titleFont = rowH * 0.44f;
	renderer->drawText(ImVec2(x1 + pad, y1 + (headerH - titleFont) * 0.5f), goldText, title, true, titleFont);

	// Rows
	g_menuRowRects.clear();
	const float itemFont = rowH * 0.42f;
	float curY = y1 + headerH + pad * 0.5f;
	if (rows.empty()) {
		renderer->drawText(ImVec2(x1 + pad, curY + (rowH - itemFont) * 0.5f), dimText,
		                   l10n::tr("No interactive .csi scripts found (.csa scripts run automatically in the background)", "未发现 .csi 交互脚本（.csa 脚本自动在后台运行）"), true, itemFont * 0.9f);
		curY += rowH;
	} else {
		for (size_t i = 0; i < rows.size(); ++i) {
			RectF r{x1 + pad, curY + 2.0f, x2 - pad, curY + rowH - 2.0f};
			bool isSelected = (rows[i].first == activeIdx);
			ImColor fill = isSelected ? rowActiveBg : ((i & 1) ? rowBgB : rowBgA);
			renderer->drawRect(ImVec2(r.x1, r.y1), ImVec2(r.x2, r.y2), fill, true);
			if (isSelected) {
				renderer->drawRect(ImVec2(r.x1, r.y1), ImVec2(r.x2, r.y2), borderCol, false, 1.5f);
			}
			renderer->drawText(ImVec2(r.x1 + 14.0f, r.y1 + (rowH - 4.0f - itemFont) * 0.5f),
			                   isSelected ? goldText : whiteText, rows[i].second, true, itemFont);
			g_menuRowRects.emplace_back(rows[i].first, r);
			curY += rowH;
		}
	}

	// Footer buttons: [Prev] [Next] [Close]
	const float footY1 = y2 - footerH - pad * 0.4f;
	const float footY2 = y2 - pad * 0.4f;
	const float btnW = (panelW - pad * 4.0f) / 3.0f;
	const float btnFont = rowH * 0.40f;

	g_menuPrevRect = RectF{x1 + pad, footY1, x1 + pad + btnW, footY2};
	g_menuNextRect = RectF{x1 + pad * 2.0f + btnW, footY1, x1 + pad * 2.0f + btnW * 2.0f, footY2};
	g_menuCloseRect = RectF{x1 + pad * 3.0f + btnW * 2.0f, footY1, x2 - pad, footY2};

	auto drawBtn = [&](const RectF& rc, const std::string& label, const ImColor& fill, const ImColor& txt) {
		renderer->drawRect(ImVec2(rc.x1, rc.y1), ImVec2(rc.x2, rc.y2), fill, true);
		renderer->drawRect(ImVec2(rc.x1, rc.y1), ImVec2(rc.x2, rc.y2), ImColor(90, 90, 105, 220), false, 1.2f);
		ImVec2 ts = renderer->calculateTextSize(label, btnFont);
		float tx = rc.x1 + (rc.x2 - rc.x1 - ts.x) * 0.5f;
		float ty = rc.y1 + (rc.y2 - rc.y1 - ts.y) * 0.5f;
		renderer->drawText(ImVec2(tx, ty), txt, label, true, btnFont);
	};

	const int totalPages = std::max(1, (totalItems + kMenuItemsPerPage - 1) / kMenuItemsPerPage);
	drawBtn(g_menuPrevRect, l10n::tr("< Previous", "< 上一页"), btnBg, page > 0 ? whiteText : dimText);
	char nextLbl[48];
	snprintf(nextLbl, sizeof(nextLbl), L10N("Next (%d/%d) >", "下一页 (%d/%d) >"), page + 1, totalPages);
	drawBtn(g_menuNextRect, nextLbl, btnBg, (page + 1 < totalPages) ? whiteText : dimText);
	drawBtn(g_menuCloseRect, closeText, ImColor(135, 34, 34, 235), whiteText);
}
