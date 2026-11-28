#include "MenuPool.h"
#include "NetGame.h"
#include "Proto/RpcOutbound.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Util.h"
#include "../UI/UI.h"
#include "../UI/UISettings.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <encoding.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>


std::string CMenuPool::formatMenuText(const std::string& raw, const char* resetHex)
{
	if (raw.empty()) {
		return {};
	}
	// SA-MP menu rules: max 31 usable chars per entry, max 8 ~x~ color codes per entry.
	// Convert to UTF-8 before truncating so multi-byte characters (GBK/UTF-8) are not split mid-sequence.
	constexpr size_t kMaxUsableMenuChars = 31;
	constexpr int kMaxMenuColorCodes = 8;
	std::string utf8 = Encoding::cp2utf(raw);
	size_t byteIdx = 0;
	size_t cpCount = 0;
	while (byteIdx < utf8.size() && cpCount < kMaxUsableMenuChars) {
		// Keep 3-char GXT color tags (~r~, ~h~, etc.) atomic so truncation never slices one in half
		if (utf8[byteIdx] == '~' && byteIdx + 2 < utf8.size() && utf8[byteIdx + 2] == '~') {
			if (cpCount + 3 > kMaxUsableMenuChars) break;
			byteIdx += 3;
			cpCount += 3;
			continue;
		}
		const unsigned char c = static_cast<unsigned char>(utf8[byteIdx]);
		size_t step = 1;
		if ((c & 0xE0) == 0xC0) step = 2;
		else if ((c & 0xF0) == 0xE0) step = 3;
		else if ((c & 0xF8) == 0xF0) step = 4;
		if (byteIdx + step > utf8.size()) break;
		byteIdx += step;
		++cpCount;
	}
	if (byteIdx < utf8.size()) {
		utf8.resize(byteIdx);
	}
	return GxtToImGuiColors(utf8, resetHex, kMaxMenuColorCodes);
}

void CMenuPool::Reset()
{
	hideActiveInternal();
	for (auto& m : m_menus) {
		m.reset();
	}
	m_currentMenu = INVALID_MENU_ID;
	m_selectedRow = -1;
	std::lock_guard<std::mutex> lock(m_touchMutex);
	m_pendingTouches.clear();
	m_layout = LayoutSnapshot{};
}

int CMenuPool::findFirstEnabledRow(const MenuData& menu) const
{
	if (!menu.menuEnabled) {
		return -1;
	}
	const uint8_t count = menu.rowCount();
	for (uint8_t i = 0; i < count && i < proto::kMaxMenuItems; ++i) {
		if (menu.rowEnabled[i]) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

void CMenuPool::InitMenu(const proto::InitMenu& p)
{
	if (p.menuId >= MAX_MENUS) {
		return;
	}

	auto menu = std::make_unique<MenuData>();
	menu->title = formatMenuText(p.title, "{FFFFFF}");
	menu->posX = std::isfinite(p.position.x) ? p.position.x : 120.0f;
	menu->posY = std::isfinite(p.position.y) ? p.position.y : 120.0f;
	menu->columns = p.hasTwoColumns ? 2 : 1;
	menu->colWidth[0] = (std::isfinite(p.col1Width) && p.col1Width > 10.0f) ? p.col1Width : 150.0f;
	menu->colWidth[1] = (std::isfinite(p.col2Width) && p.col2Width > 10.0f) ? p.col2Width : 120.0f;
	menu->menuEnabled = p.menuEnabled;
	menu->rowEnabled = p.rowEnabled;

	menu->columnHeaders[0] = formatMenuText(p.columnHeaders[0], "{C8C8C8}");
	menu->columnItemCount[0] = std::min<uint8_t>(p.columnItemCount[0], static_cast<uint8_t>(proto::kMaxMenuItems));
	for (uint8_t i = 0; i < menu->columnItemCount[0]; ++i) {
		const char* resetCol = (p.menuEnabled && p.rowEnabled[i]) ? "{DCDCDC}" : "{6E6E6E}";
		menu->items[0][i] = formatMenuText(p.items[0][i], resetCol);
	}

	if (p.hasTwoColumns) {
		menu->columnHeaders[1] = formatMenuText(p.columnHeaders[1], "{C8C8C8}");
		menu->columnItemCount[1] = std::min<uint8_t>(p.columnItemCount[1], static_cast<uint8_t>(proto::kMaxMenuItems));
		for (uint8_t i = 0; i < menu->columnItemCount[1]; ++i) {
			const char* resetCol = (p.menuEnabled && p.rowEnabled[i]) ? "{DCDCDC}" : "{6E6E6E}";
			menu->items[1][i] = formatMenuText(p.items[1][i], resetCol);
		}
	}

	const uint8_t rowCount = menu->rowCount();
	m_menus[p.menuId] = std::move(menu);

	if (m_currentMenu == p.menuId && m_visible.load(std::memory_order_acquire)) {
		m_selectedRow = findFirstEnabledRow(*m_menus[p.menuId]);
	}

	spdlog::info("CMenuPool::InitMenu id={} cols={} rows={} enabled={}",
		p.menuId, m_menus[p.menuId]->columns, rowCount, m_menus[p.menuId]->menuEnabled ? 1 : 0);
}

void CMenuPool::ShowMenu(uint8_t menuId)
{
	if (menuId >= MAX_MENUS || !m_menus[menuId]) {
		spdlog::warn("CMenuPool::ShowMenu: invalid or uninitialized menu {}", menuId);
		return;
	}

	m_currentMenu = menuId;
	m_selectedRow = findFirstEnabledRow(*m_menus[menuId]);
	{
		std::lock_guard<std::mutex> lock(m_touchMutex);
		m_pendingTouches.clear();
	}
	m_visible.store(true, std::memory_order_release);
	spdlog::info("CMenuPool::ShowMenu id={} selectedRow={}", menuId, m_selectedRow);
}

void CMenuPool::HideMenu(uint8_t menuId)
{
	if (!m_visible.load(std::memory_order_acquire)) {
		return;
	}
	if (menuId != INVALID_MENU_ID && menuId >= MAX_MENUS) {
		return;
	}
	if (menuId == INVALID_MENU_ID || m_currentMenu == menuId) {
		hideActiveInternal();
		spdlog::info("CMenuPool::HideMenu id={}", menuId);
	}
}

void CMenuPool::hideActiveInternal()
{
	m_visible.store(false, std::memory_order_release);
	m_currentMenu = INVALID_MENU_ID;
	m_selectedRow = -1;
	std::lock_guard<std::mutex> lock(m_touchMutex);
	m_pendingTouches.clear();
	m_layout.valid = false;
}

void CMenuPool::MoveSelection(int delta)
{
	if (!m_visible.load(std::memory_order_acquire) || m_currentMenu >= MAX_MENUS || !m_menus[m_currentMenu]) {
		return;
	}
	const MenuData& menu = *m_menus[m_currentMenu];
	const int count = static_cast<int>(menu.rowCount());
	if (!menu.menuEnabled || count <= 0 || delta == 0) {
		return;
	}

	const int step = (delta > 0) ? 1 : -1;
	int cur = (m_selectedRow >= 0 && m_selectedRow < count) ? m_selectedRow : (step > 0 ? -1 : count);
	for (int i = 0; i < count; ++i) {
		cur = (cur + step + count) % count;
		if (menu.rowEnabled[cur]) {
			m_selectedRow = cur;
			return;
		}
	}
}

void CMenuPool::SelectCurrentRow()
{
	if (!m_visible.load(std::memory_order_acquire) || m_currentMenu >= MAX_MENUS || !m_menus[m_currentMenu]) {
		return;
	}
	const MenuData& menu = *m_menus[m_currentMenu];
	const int count = static_cast<int>(menu.rowCount());
	if (!menu.menuEnabled || m_selectedRow < 0 || m_selectedRow >= count || !menu.rowEnabled[m_selectedRow]) {
		return;
	}

	const uint8_t chosenRow = static_cast<uint8_t>(m_selectedRow);
	hideActiveInternal();
	sendMenuSelect(chosenRow);
}

void CMenuPool::ExitMenu()
{
	if (!m_visible.load(std::memory_order_acquire)) {
		return;
	}
	hideActiveInternal();
	sendMenuQuit();
}

void CMenuPool::sendMenuSelect(uint8_t row)
{
	if (!pNetGame || !pNetGame->GetRakClient()) return;

	RakNet::BitStream bs;
	proto::Writer writer(bs);
	proto::MenuSelectReq req{row};
	req.serialize(writer, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_MenuSelect, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	spdlog::info("CMenuPool: sent RPC_MenuSelect row={}", row);
}

void CMenuPool::sendMenuQuit()
{
	if (!pNetGame || !pNetGame->GetRakClient()) return;

	RakNet::BitStream bs;
	proto::Writer writer(bs);
	proto::MenuQuitReq req{};
	req.serialize(writer, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_MenuQuit, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	spdlog::info("CMenuPool: sent RPC_MenuQuit");
}

bool CMenuPool::enqueueTouchEvent(int type, int x, int y)
{
	if (!m_visible.load(std::memory_order_acquire)) {
		m_touchInsidePanel = false;
		return true;
	}

	bool inside = false;
	{
		std::lock_guard<std::mutex> lock(m_touchMutex);
		if (m_layout.valid) {
			inside = m_layout.panel.contains(static_cast<float>(x), static_cast<float>(y));
		}
	}

	if (type == 2) { // push
		m_touchInsidePanel = inside;
		if (inside) {
			std::lock_guard<std::mutex> lock(m_touchMutex);
			if (m_pendingTouches.size() >= 32) m_pendingTouches.pop_front();
			m_pendingTouches.push_back({type, x, y});
			return false;
		}
		return true;
	}
	if (type == 3) { // move
		return !m_touchInsidePanel;
	}
	if (type == 1) { // pop
		const bool consumed = m_touchInsidePanel;
		m_touchInsidePanel = false;
		if (consumed) {
			std::lock_guard<std::mutex> lock(m_touchMutex);
			if (m_pendingTouches.size() >= 32) m_pendingTouches.pop_front();
			m_pendingTouches.push_back({type, x, y});
			return false;
		}
		return true;
	}

	return true;
}

void CMenuPool::processPendingTouches()
{
	std::deque<PendingTouch> queue;
	{
		std::lock_guard<std::mutex> lock(m_touchMutex);
		if (m_pendingTouches.empty()) return;
		queue.swap(m_pendingTouches);
	}

	if (!m_visible.load(std::memory_order_acquire) || m_currentMenu >= MAX_MENUS || !m_menus[m_currentMenu]) {
		return;
	}

	const LayoutSnapshot layout = [&]() {
		std::lock_guard<std::mutex> lock(m_touchMutex);
		return m_layout;
	}();
	if (!layout.valid) return;

	const MenuData& menu = *m_menus[m_currentMenu];

	for (const auto& ev : queue) {
		if (!m_visible.load(std::memory_order_acquire)) break;
		if (ev.type != 1) continue; // act on finger release (pop)

		const float px = static_cast<float>(ev.x);
		const float py = static_cast<float>(ev.y);

		if (layout.exitBtn.contains(px, py)) {
			ExitMenu();
			break;
		}
		if (layout.selectBtn.contains(px, py)) {
			SelectCurrentRow();
			break;
		}

		for (uint8_t r = 0; r < layout.rowCount && r < proto::kMaxMenuItems; ++r) {
			if (layout.rowRects[r].contains(px, py)) {
				if (menu.menuEnabled && menu.rowEnabled[r]) {
					if (m_selectedRow == static_cast<int>(r)) {
						// Tapping an already-highlighted row confirms selection immediately
						SelectCurrentRow();
					}
					else {
						m_selectedRow = static_cast<int>(r);
					}
				}
				break;
			}
		}
	}
}

void CMenuPool::Render(ImGuiRenderer* renderer)
{
	if (!renderer || !m_visible.load(std::memory_order_acquire)) {
		return;
	}
	if (m_currentMenu >= MAX_MENUS || !m_menus[m_currentMenu]) {
		return;
	}

	const MenuData& menu = *m_menus[m_currentMenu];
	const ImVec2 disp = pUI ? pUI->size()
		: ImVec2(static_cast<float>(sa::RsGlobal->screenWidth), static_cast<float>(sa::RsGlobal->screenHeight));

	// PC GTA:SA CMenuSystem uses 640x448 virtual space (exact same scale as TextDraw).
	const float scaleX = disp.x / 640.0f;
	const float scaleY = disp.y / 448.0f;

	// Calibrated in 448-unit virtual space to match PC GTA:SA CMenuSystem proportions
	const float titleFont = std::max(16.0f, 26.0f * scaleY);
	const float headerFont = std::max(12.0f, 15.0f * scaleY);
	const float itemFont = std::max(12.0f, 15.0f * scaleY);
	const float btnFont = std::max(12.0f, 14.0f * scaleY);

	const float padX = 8.0f * scaleX;
	const float padY = 4.0f * scaleY;
	const bool hasTitle = !menu.title.empty();
	const float titleBandH = hasTitle ? (titleFont * 1.12f) : padY;
	const bool hasColHeader = !menu.columnHeaders[0].empty() || (menu.columns > 1 && !menu.columnHeaders[1].empty());
	const float headerH = hasColHeader ? (20.0f * scaleY) : 0.0f;
	const float rowH = 20.0f * scaleY;
	const float footerGap = 4.0f * scaleY;
	const float btnH = 22.0f * scaleY;

	const uint8_t rows = menu.rowCount();

	// Strictly follow server-specified virtual widths and column split (no artificial min/max warping)
	const float col1Px = std::max(40.0f * scaleX, menu.colWidth[0] * scaleX);
	const float col2Px = (menu.columns > 1) ? std::max(24.0f * scaleX, menu.colWidth[1] * scaleX) : 0.0f;
	const float panelW = std::min(disp.x, col1Px + col2Px);
	const float boxH = titleBandH + headerH + rowH * static_cast<float>(rows) + padY * 1.5f;
	const float totalH = boxH + footerGap + btnH;

	// Strictly follow server-specified virtual (posX, posY); only clamp if overflowing screen edges
	const float panelX = std::clamp(menu.posX * scaleX, 0.0f, std::max(0.0f, disp.x - panelW));
	const float panelY = std::clamp(menu.posY * scaleY, 0.0f, std::max(0.0f, disp.y - totalH));

	const float col2X = panelX + col1Px + padX * 0.5f;

	// Authentic GTA:SA CMenuSystem monochrome look:
	// Flat borderless semi-transparent black box (no colored chrome/borders).
	// Title straddles the top edge in crisp outlined white text (supports ~r~/~g~/etc.).
	const float boxTopY = hasTitle ? (panelY + titleFont * 0.42f) : panelY;
	const float boxBottomY = panelY + boxH;
	renderer->drawRect(ImVec2(panelX, boxTopY), ImVec2(panelX + panelW, boxBottomY), ImColor(0, 0, 0, 178), true);

	if (hasTitle) {
		renderer->drawText(ImVec2(panelX + padX * 0.8f, panelY), ImColor(255, 255, 255, 255), menu.title, true, titleFont);
	}

	float curY = panelY + titleBandH;

	// Column headers (neutral light gray, no colored bar)
	if (hasColHeader) {
		const ImColor hdrCol(200, 200, 200, 235);
		if (!menu.columnHeaders[0].empty()) {
			renderer->drawText(ImVec2(panelX + padX, curY + (headerH - headerFont) * 0.5f), hdrCol, menu.columnHeaders[0], true, headerFont);
		}
		if (menu.columns > 1 && !menu.columnHeaders[1].empty()) {
			renderer->drawText(ImVec2(col2X, curY + (headerH - headerFont) * 0.5f), hdrCol, menu.columnHeaders[1], true, headerFont);
		}
		renderer->drawLine(ImVec2(panelX + padX * 0.5f, curY + headerH - 1.0f), ImVec2(panelX + panelW - padX * 0.5f, curY + headerH - 1.0f), ImColor(255, 255, 255, 42), 1.0f);
		curY += headerH;
	}

	// Rows (neutral translucent highlight on active row; text may carry ~r~/~g~/etc. colors)
	LayoutSnapshot snap{};
	snap.valid = true;
	snap.panel = {panelX, panelY, panelX + panelW, panelY + totalH};
	snap.rowCount = rows;

	for (uint8_t r = 0; r < rows && r < proto::kMaxMenuItems; ++r) {
		const bool enabled = menu.menuEnabled && menu.rowEnabled[r];
		const bool selected = enabled && (m_selectedRow == static_cast<int>(r));

		const ImVec2 rMin(panelX, curY);
		const ImVec2 rMax(panelX + panelW, curY + rowH);
		snap.rowRects[r] = {rMin.x, rMin.y, rMax.x, rMax.y};

		if (selected) {
			renderer->drawRect(rMin, rMax, ImColor(255, 255, 255, 56), true);
		}

		const ImColor textCol = !enabled
			? ImColor(110, 110, 110, 200)
			: (selected ? ImColor(255, 255, 255, 255) : ImColor(220, 220, 220, 245));

		if (r < menu.columnItemCount[0] && !menu.items[0][r].empty()) {
			renderer->drawText(ImVec2(panelX + padX, curY + (rowH - itemFont) * 0.5f), textCol, menu.items[0][r], true, itemFont);
		}
		if (menu.columns > 1 && r < menu.columnItemCount[1] && !menu.items[1][r].empty()) {
			renderer->drawText(ImVec2(col2X, curY + (rowH - itemFont) * 0.5f), textCol, menu.items[1][r], true, itemFont);
		}

		curY += rowH;
	}

	// Minimal monochrome touch action strip below the menu box: [ Select ] [ Cancel ]
	const float btnGap = padX * 0.7f;
	const float btnW = (panelW - btnGap) * 0.5f;
	const float btnY = boxBottomY + footerGap;

	const bool canSelect = menu.menuEnabled && m_selectedRow >= 0 && m_selectedRow < rows && menu.rowEnabled[m_selectedRow];
	const float selX = panelX;
	const float exitX = selX + btnW + btnGap;

	snap.selectBtn = {selX, btnY, selX + btnW, btnY + btnH};
	snap.exitBtn = {exitX, btnY, exitX + btnW, btnY + btnH};

	// Select button (monochrome black + subtle gray edge)
	renderer->drawRect(ImVec2(selX, btnY), ImVec2(selX + btnW, btnY + btnH), ImColor(0, 0, 0, 178), true);
	renderer->drawRect(ImVec2(selX, btnY), ImVec2(selX + btnW, btnY + btnH),
		canSelect ? ImColor(255, 255, 255, 75) : ImColor(255, 255, 255, 30), false, 1.0f);
	const std::string selText = "Select";
	const ImVec2 selSz = renderer->calculateTextSize(selText, btnFont);
	renderer->drawText(ImVec2(selX + (btnW - selSz.x) * 0.5f, btnY + (btnH - selSz.y) * 0.5f),
		canSelect ? ImColor(240, 240, 240, 255) : ImColor(115, 115, 115, 190), selText, true, btnFont);

	// Cancel button (monochrome black + subtle gray edge)
	renderer->drawRect(ImVec2(exitX, btnY), ImVec2(exitX + btnW, btnY + btnH), ImColor(0, 0, 0, 178), true);
	renderer->drawRect(ImVec2(exitX, btnY), ImVec2(exitX + btnW, btnY + btnH), ImColor(255, 255, 255, 75), false, 1.0f);
	const std::string exitText = "Cancel";
	const ImVec2 exitSz = renderer->calculateTextSize(exitText, btnFont);
	renderer->drawText(ImVec2(exitX + (btnW - exitSz.x) * 0.5f, btnY + (btnH - exitSz.y) * 0.5f),
		ImColor(240, 240, 240, 255), exitText, true, btnFont);

	{
		std::lock_guard<std::mutex> lock(m_touchMutex);
		m_layout = snap;
	}
}
