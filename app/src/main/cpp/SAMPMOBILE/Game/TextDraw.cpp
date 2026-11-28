#include "TextDraw.h"
#include "Font.h"
#include "Game.h"
#include "RGBA.h"
#include "SnapShotHelper.h"
#include "Util.h"
#include "Artwork/ArtworkCore.h"
#include "Artwork/ArtworkManager.h"
#include "Engine/Symbols.h"
#include "../Client.h"
#include "../UI/UI.h"
#include "../UI/UISettings.h"
#include <encoding.h>
#include <SA.h>
#include <spdlog/spdlog.h>
#include <sstream>


extern RwTexture* TextDrawTexture[];

CTextDraw::CTextDraw(TEXT_DRAW_TRANSMIT* pTextDrawTransmit, const char* szText)
{
	memset(&m_TextDrawData, 0, sizeof(TEXT_DRAW_DATA));

	m_TextDrawData.fLetterWidth = pTextDrawTransmit->fLetterWidth;
	m_TextDrawData.fLetterHeight = pTextDrawTransmit->fLetterHeight;

	m_TextDrawData.dwLetterColor = pTextDrawTransmit->dwLetterColor;
	m_TextDrawData.byteUnk12 = 0;
	m_TextDrawData.byteCentered = pTextDrawTransmit->byteCenter;
	m_TextDrawData.byteBox = pTextDrawTransmit->byteBox;

	m_TextDrawData.fLineWidth = pTextDrawTransmit->fLineWidth;
	m_TextDrawData.fLineHeight = pTextDrawTransmit->fLineHeight;

	m_TextDrawData.dwBoxColor = pTextDrawTransmit->dwBoxColor;
	m_TextDrawData.byteProportional = pTextDrawTransmit->byteProportional;
	m_TextDrawData.dwBackgroundColor = pTextDrawTransmit->dwBackgroundColor;
	m_TextDrawData.byteShadow = pTextDrawTransmit->byteShadow;
	m_TextDrawData.byteOutline = pTextDrawTransmit->byteOutline;
	m_TextDrawData.byteAlignLeft = pTextDrawTransmit->byteLeft;
	m_TextDrawData.byteAlignRight = pTextDrawTransmit->byteRight;
	m_TextDrawData.dwStyle = pTextDrawTransmit->byteStyle;

	m_TextDrawData.fX = pTextDrawTransmit->fX;
	m_TextDrawData.fY = pTextDrawTransmit->fY;

	m_TextDrawData.dwParam1 = 0xFFFFFFFF;
	m_TextDrawData.dwParam2 = 0xFFFFFFFF;
	m_TextDrawData.byteSelectable = pTextDrawTransmit->byteSelectable;
	m_TextDrawData.wModelID = pTextDrawTransmit->wModelID;
	m_TextDrawData.vecRot.x = pTextDrawTransmit->vecRot.x;
	m_TextDrawData.vecRot.y = pTextDrawTransmit->vecRot.y;
	m_TextDrawData.vecRot.z = pTextDrawTransmit->vecRot.z;
	m_TextDrawData.fZoom = pTextDrawTransmit->fZoom;
	m_TextDrawData.wColor1 = pTextDrawTransmit->wColor1;
	m_TextDrawData.wColor2 = pTextDrawTransmit->wColor2;
	m_TextDrawData.bHasKeyCode = false;
	m_TextDrawData.iTextureSlot = -1;
	SetText(szText);

	if (m_TextDrawData.dwStyle == 4) {
		m_TextDrawData.iTextureSlot = GetFreeTextDrawTextureSlot();
		LoadTexture();
	}

	m_TextDrawData.bHasRectArea = false;
	m_rectArea.left = 0.0f;
	m_rectArea.right = 0.0f;
	m_rectArea.bottom = 0.0f;
	m_rectArea.top = 0.0f;
	m_bHovered = false;
	m_dwHoverColor = 0;
	m_bUsesCustomAsset = false;
	m_bPendingCustomAsset = false;
}

CTextDraw::~CTextDraw()
{
	DestroyTextDrawTexture(m_TextDrawData.iTextureSlot);
}

/**
 * @param szText
 */
void CTextDraw::SetText(const char* szText)
{
	memset(m_szText, 0, 800);
	strncpy(m_szText, szText, 800);
	m_szText[800] = 0;

	if (m_TextDrawData.dwStyle == 4 && m_TextDrawData.iTextureSlot != -1) {
		DestroyTextDrawTexture(m_TextDrawData.iTextureSlot);
		LoadTexture();
	}
}

void CTextDraw::LoadTexture()
{
	char txdname[64 + 1];
	memset(txdname, 0, sizeof(txdname));
	char texturename[64 + 1];
	memset(texturename, 0, sizeof(texturename));

	char* szTexture = strchr(m_szText, ':');
	if (szTexture == nullptr) return;

	if (strlen(m_szText) < 64 && strchr(m_szText, '\\') == nullptr && strchr(m_szText, '/') == nullptr) {
		strncpy(txdname, m_szText, (size_t) (szTexture - m_szText));
		strcpy(texturename, ++szTexture);

		char rawTextureName[64 + 1];
		strncpy(rawTextureName, texturename, sizeof(rawTextureName) - 1);
		rawTextureName[sizeof(rawTextureName) - 1] = '\0';

		spdlog::info("LOADTEXTURE: txdname: {}, texture: {}", txdname, texturename);

		if (!strcasecmp(txdname, "ld_shtr") && !strcasecmp(texturename, "ship")) {
			strcpy(texturename, "ship_ld_shtr");
		}
		else if (!strcasecmp(txdname, "ld_spac") && !strcasecmp(texturename, "shoot")) {
			strcpy(texturename, "shoot_LD_SPAC");
		}
		else if (!strcasecmp(txdname, "ld_spac") && !strcasecmp(texturename, "white")) {
			strcpy(texturename, "white_LD_SPAC");
		}
		else {
			static char* texture_samp[] = {
					"smokeii_3", "back2", "back3", "back4", "back5",
					"back6", "back7", "back8", "back8_right", "back8_top",
					"intro1", "intro2", "intro3", "intro4", "duality", "health", "thrustg",
					"ric1", "ric2", "ric3", "ric4", "ric5", "load0uk", "loadsc0", "loadsc1",
					"loadsc10", "loadsc11", "loadsc12", "loadsc13", "loadsc14", "loadsc2",
					"loadsc3", "loadsc4", "loadsc5", "loadsc6", "loadsc7", "loadsc8",
					"loadsc9", "eax", "nvidia", "title_pc_eu", "title_pc_us", "loadscuk",
					"tyre64a", "wheel_alloy64", "wheel_classic64", "wheel_lighttruck64",
					"wheel_lightvan64", "wheel_offroad64", "wheel_saloon64", "wheel_smallcar64",
					"wheel_sport64", "wheel_truck64", "whee_rim64", "outro"};

			for (auto& i : texture_samp) {
				if (!strcasecmp(i, texturename)) {
					char buf[64];
					sprintf(buf, "%s_%s", texturename, txdname);
					strcpy(texturename, buf);
				}
			}
		}

		if (m_TextDrawData.iTextureSlot != -1) {
			RwTexture* tex = nullptr;
			m_bUsesCustomAsset = false;
			m_bPendingCustomAsset = false;
			// 0.3.DL custom TXD sprite syntax: "mdl<modelId>:<texname>" (e.g. "mdl-2001:sign")
			if (strncasecmp(txdname, "mdl", 3) == 0 && txdname[3] != '\0') {
				char* endPtr = nullptr;
				const long parsedId = strtol(txdname + 3, &endPtr, 10);
				if (endPtr && *endPtr == '\0') {
					const int32_t customId = static_cast<int32_t>(parsedId);
					if (artwork::isCustomObjectId(customId) || artwork::isCustomSkinId(customId)) {
						m_bUsesCustomAsset = true;
						tex = artwork::ArtworkManager::instance().findTextureInCustomModel(customId, rawTextureName);
						m_bPendingCustomAsset = (tex == nullptr);
					}
				}
			}
			if (!tex && !m_bUsesCustomAsset) {
				tex = LoadTextureFromTxd(txdname, texturename);
				if (!tex && strcasecmp(texturename, rawTextureName) != 0) {
					tex = LoadTextureFromTxd(txdname, rawTextureName);
				}
			}
			TextDrawTexture[m_TextDrawData.iTextureSlot] = tex;
		}
	}
}

static RGBA DWORD2RGBA(uint32_t dwColor)
{
	RGBA tmp;

	tmp.R = dwColor & 0xFF;
	dwColor >>= 8;
	tmp.G = dwColor & 0xFF;
	dwColor >>= 8;
	tmp.B = dwColor & 0xFF;
	dwColor >>= 8;
	tmp.A = dwColor & 0xFF; /* dwColor >>= 8; */

	return tmp;
}

void CTextDraw::Draw()
{
	if (m_TextDrawData.iTextureSlot != -1 || m_TextDrawData.dwStyle == 4) {
		DrawTextured();
	}
	else if (NeedsImGuiRender()) {
		// The engine bitmap font cannot render this text; it is drawn later
		// by DrawImGui() during the UI pass. Keep the click area up to date.
		ComputeRectArea();
	}
	else {
		DrawDefault();
	}
}

bool CTextDraw::NeedsImGuiRender()
{
	if (m_TextDrawData.dwStyle == 4 || m_TextDrawData.dwStyle == 5) return false;
	return StringHasNonAscii(m_szText);
}

void CTextDraw::ComputeRectArea()
{
	int iScreenWidth = sa::RsGlobal->screenWidth;
	int iScreenHeight = sa::RsGlobal->screenHeight;
	float fHorizHudScale = 1.0f / 640.0f;
	float fVertHudScale = 1.0f / 448.0f;

	float fLineWidth = iScreenWidth * m_TextDrawData.fLineWidth * fHorizHudScale;
	float fLineHeight = iScreenWidth * m_TextDrawData.fLineHeight * fHorizHudScale;

	float fUseY = iScreenHeight - ((448.0 - m_TextDrawData.fY) * (iScreenHeight * fVertHudScale));
	float fUseX = iScreenWidth - ((640.0 - m_TextDrawData.fX) * (iScreenWidth * fHorizHudScale));

	if (m_TextDrawData.byteAlignRight) {
		m_rectArea.left = fUseX - (fLineWidth - fUseX);
		m_rectArea.right = fUseX;
		m_rectArea.bottom = fUseY;
		m_rectArea.top = fUseY + fLineHeight;
	}
	else if (m_TextDrawData.byteCentered) {
		m_rectArea.left = fUseX - (fLineHeight * 0.5f);
		m_rectArea.right = m_rectArea.left + fLineHeight;
		m_rectArea.bottom = fUseY;
		m_rectArea.top = fUseY + fLineWidth;
	}
	else {
		m_rectArea.left = fUseX;
		m_rectArea.right = fLineWidth - fUseX + fUseX;
		m_rectArea.bottom = fUseY;
		m_rectArea.top = fUseY + fLineHeight;
	}

	m_TextDrawData.bHasRectArea = true;
}

void CTextDraw::DrawImGui(ImGuiRenderer* renderer)
{
	if (!NeedsImGuiRender()) return;
	if (!strlen(m_szText)) return;

	int iScreenWidth = sa::RsGlobal->screenWidth;
	int iScreenHeight = sa::RsGlobal->screenHeight;
	float fHorizHudScale = 1.0f / 640.0f;
	float fVertHudScale = 1.0f / 448.0f;

	float fUseY = iScreenHeight - ((448.0 - m_TextDrawData.fY) * (iScreenHeight * fVertHudScale));
	float fUseX = iScreenWidth - ((640.0 - m_TextDrawData.fX) * (iScreenWidth * fHorizHudScale));

	uint32_t dwLetterColor = m_bHovered ? m_dwHoverColor : m_TextDrawData.dwLetterColor;
	RGBA letterColor = DWORD2RGBA(dwLetterColor);
	ImColor colLetter(letterColor.R, letterColor.G, letterColor.B, letterColor.A);

	char resetHex[9];
	snprintf(resetHex, sizeof(resetHex), "{%02X%02X%02X}", letterColor.R, letterColor.G, letterColor.B);

	std::string text = Encoding::cp2utf(ReplaceKey(m_szText));
	text = GxtToImGuiColors(text, resetHex);
	if (text.empty()) return;

	// The engine font sprite is roughly 30px tall at scale 1.0
	float fScaleY = (float) iScreenHeight * fVertHudScale * m_TextDrawData.fLetterHeight * 0.5f;
	float fontSize = fScaleY * 30.0f;

	ImVec2 textSize = renderer->calculateTextSize(text, fontSize);

	if (m_TextDrawData.byteBox) {
		RGBA boxColor = DWORD2RGBA(m_TextDrawData.dwBoxColor);
		float fLineWidth = iScreenWidth * m_TextDrawData.fLineWidth * fHorizHudScale;
		float fLineHeight = iScreenWidth * m_TextDrawData.fLineHeight * fHorizHudScale;
		float boxH = fLineHeight > 0.0f ? fLineHeight : textSize.y;
		if (textSize.y > boxH) boxH = textSize.y;

		float boxLeft = fUseX;
		float boxRight = fLineWidth > fUseX ? fLineWidth : (fUseX + textSize.x);
		if (m_TextDrawData.byteAlignRight) {
			boxLeft = fUseX - (fLineWidth - fUseX);
			boxRight = fUseX;
		}
		else if (m_TextDrawData.byteCentered) {
			float halfW = fLineHeight > 0.0f ? fLineHeight * 0.5f : textSize.x * 0.5f;
			boxLeft = fUseX - halfW;
			boxRight = fUseX + halfW;
		}

		renderer->drawRect(ImVec2(boxLeft, fUseY), ImVec2(boxRight, fUseY + boxH),
				ImColor(boxColor.R, boxColor.G, boxColor.B, boxColor.A), true);
	}

	// Render line-by-line so centered/right-aligned multi-line (~n~) Chinese
	// TextDraws align each line properly instead of left-aligning inside the block,
	// while carrying active {RRGGBB} color codes across newlines.
	std::stringstream ss(text);
	std::string line;
	float curY = fUseY;
	ImColor curColor = colLetter;
	while (std::getline(ss, line, '\n')) {
		if (!line.empty()) {
			ImVec2 lineSize = renderer->calculateTextSize(line, fontSize);
			float lineX = fUseX;
			if (m_TextDrawData.byteAlignRight) {
				lineX -= lineSize.x;
			}
			else if (m_TextDrawData.byteCentered) {
				lineX -= lineSize.x * 0.5f;
			}

			if (m_TextDrawData.byteShadow) {
				float off = (float) m_TextDrawData.byteShadow;
				renderer->drawText(ImVec2(lineX + off, curY + off),
						ImColor(0, 0, 0, letterColor.A), line, false, fontSize);
			}

			renderer->drawText(ImVec2(lineX, curY), curColor, line, m_TextDrawData.byteOutline != 0, fontSize);

			// Carry last inline {RRGGBB} color to the next line.
			for (size_t i = 0; i + 7 < line.size(); ++i) {
				if (line[i] == '{' && line[i + 7] == '}') {
					unsigned int hex = 0;
					char buf[7];
					memcpy(buf, &line[i + 1], 6);
					buf[6] = '\0';
					if (sscanf(buf, "%x", &hex) == 1) {
						curColor = ImColor(
								(int) ((hex >> 16) & 0xFF),
								(int) ((hex >> 8) & 0xFF),
								(int) (hex & 0xFF),
								(int) letterColor.A);
						i += 7;
					}
				}
			}
		}
		curY += fontSize;
	}
}

void CTextDraw::DrawDefault()
{
	if (m_szText[0] == '\0') return;

	int iScreenWidth = sa::RsGlobal->screenWidth;
	int iScreenHeight = sa::RsGlobal->screenHeight;
	float fHorizHudScale = 1.0f / 640.0f;
	float fVertHudScale = 1.0f / 448.0f;

	float fScaleY = (float) iScreenHeight * fVertHudScale * m_TextDrawData.fLetterHeight * 0.5f;
	float fScaleX = (float) iScreenWidth * fHorizHudScale * m_TextDrawData.fLetterWidth;

	CFont::SetScale(fScaleX, fScaleY);

	if (m_bHovered) {
		//uint32_t color = __builtin_bswap32(m_dwHoverColor | (0x000000FF));
		RGBA color = DWORD2RGBA(m_dwHoverColor);
		CFont::SetColor(&color);
	}
	else {
		RGBA letterColor = DWORD2RGBA(m_TextDrawData.dwLetterColor);
		CFont::SetColor(&letterColor);
	}

	CFont::SetJustify(0);

	if (m_TextDrawData.byteAlignRight) {
		CFont::SetOrientation(2);
	}
	else if (m_TextDrawData.byteCentered) {
		CFont::SetOrientation(0);
	}
	else {
		CFont::SetOrientation(1);
	}

	float fLineWidth = iScreenWidth * m_TextDrawData.fLineWidth * fHorizHudScale;
	CFont::SetWrapX(fLineWidth);

	float fLineHeight = iScreenWidth * m_TextDrawData.fLineHeight * fHorizHudScale;
	CFont::SetCentreSize(fLineHeight);

	CFont::SetBackground(m_TextDrawData.byteBox, 0);

	RGBA boxColor = DWORD2RGBA(m_TextDrawData.dwBoxColor);
	CFont::SetBackgroundColor(&boxColor);

	CFont::SetProportional(m_TextDrawData.byteProportional);

	RGBA backgroundColor = DWORD2RGBA(m_TextDrawData.dwBackgroundColor);
	CFont::SetDropColor(&backgroundColor);

	if (m_TextDrawData.byteOutline) {
		CFont::SetEdge(m_TextDrawData.byteOutline);
	}
	else {
		CFont::SetDropShadowPosition(m_TextDrawData.byteShadow);
	}

	CFont::SetFontStyle(m_TextDrawData.dwStyle);

	float fUseY = iScreenHeight - ((448.0 - m_TextDrawData.fY) * (iScreenHeight * fVertHudScale));
	float fUseX = iScreenWidth - ((640.0 - m_TextDrawData.fX) * (iScreenWidth * fHorizHudScale));

	std::string text = ReplaceKey(m_szText);
	CFont::PrintString(fUseX, fUseY, text.c_str());

	CFont::SetEdge(0);

	ComputeRectArea();
}

void CTextDraw::DrawTextured()
{
	float scaleX = sa::RsGlobal->screenWidth * (1.0f / 640.0f);
	float scaleY = sa::RsGlobal->screenHeight * (1.0f / 448.0f);

	m_rectArea.left = m_TextDrawData.fX * scaleX;
	m_rectArea.bottom = m_TextDrawData.fY * scaleY;
	m_rectArea.right = (m_TextDrawData.fX + m_TextDrawData.fLineWidth) * scaleX;
	m_rectArea.top = (m_TextDrawData.fY + m_TextDrawData.fLineHeight) * scaleY;

	static float uv_reflected[8] = {
			0.0f, 1.0f,
			1.0f, 1.0f,
			0.0f, 0.0f,
			1.0f, 0.0f};

	static float uv_normal[8] = {
			0.0f, 0.0f,
			1.0f, 0.0f,
			0.0f, 1.0f,
			1.0f, 1.0f};

	m_TextDrawData.bHasRectArea = true;

	if (m_TextDrawData.iTextureSlot < 0 || m_TextDrawData.iTextureSlot >= 200) {
		return;
	}
	RwTexture* tex = TextDrawTexture[m_TextDrawData.iTextureSlot];
	if (!tex) {
		return;
	}

	if (m_bHovered) {
		DrawTextureUV(tex, &m_rectArea, m_dwHoverColor | 0xFF000000u, m_TextDrawData.dwStyle == 5 ? uv_reflected : uv_normal);
	}
	else {
		DrawTextureUV(tex, &m_rectArea, m_TextDrawData.dwLetterColor, m_TextDrawData.dwStyle == 5 ? uv_reflected : uv_normal);
	}
}

void CTextDraw::SnapshotProcess()
{
	if (m_TextDrawData.dwStyle != 5 || m_TextDrawData.iTextureSlot != -1) {
		return;
	}

	RwTexture* snapshot = nullptr;

	// 0.3.DL custom model preview (16-bit wire ID: negative objects arrive as uint16 two's complement)
	const int32_t wireModel = artwork::decodeWireModelId16(m_TextDrawData.wModelID);
	if (artwork::isCustomSkinId(wireModel)) {
		m_bUsesCustomAsset = true;
		const int32_t slot = artwork::ArtworkManager::instance().ensureCustomSkinLoaded(wireModel);
		const bool ready = (slot >= 0 && IsValidPedModel(static_cast<uint>(slot)));
		m_bPendingCustomAsset = !ready;
		const int pedModel = ready ? slot : artwork::ArtworkManager::instance().resolveFallbackSkinModel(wireModel);
		snapshot = pSnapShotHelper->CreatePedSnapShot(
				pedModel,
				m_TextDrawData.dwBackgroundColor,
				&m_TextDrawData.vecRot,
				m_TextDrawData.fZoom);
	}
	else if (artwork::isCustomObjectId(wireModel)) {
		m_bUsesCustomAsset = true;
		const int32_t slot = artwork::ArtworkManager::instance().ensureCustomObjectLoaded(wireModel);
		const bool ready = (slot >= 0 && IsValidModel(slot));
		m_bPendingCustomAsset = !ready;
		const int objModel = ready ? slot : artwork::ArtworkManager::instance().resolveFallbackObjectModel(wireModel);
		snapshot = pSnapShotHelper->CreateObjectSnapShot(
				objModel,
				m_TextDrawData.dwBackgroundColor,
				&m_TextDrawData.vecRot,
				m_TextDrawData.fZoom);
	}
	// PED MODEL
	else if (IsValidPedModel(m_TextDrawData.wModelID)) {
		if (!IsExistInfoForModel(m_TextDrawData.wModelID)) {
			m_TextDrawData.wModelID = 0;
		}

		snapshot = pSnapShotHelper->CreatePedSnapShot(
				m_TextDrawData.wModelID,
				m_TextDrawData.dwBackgroundColor,
				&m_TextDrawData.vecRot,
				m_TextDrawData.fZoom);
	}
		// VEHICLE MODEL
	else if (m_TextDrawData.wModelID >= 400 && m_TextDrawData.wModelID <= 611) {
		snapshot = pSnapShotHelper->CreateVehicleSnapShot(
				m_TextDrawData.wModelID,
				m_TextDrawData.dwBackgroundColor,
				&m_TextDrawData.vecRot,
				m_TextDrawData.fZoom,
				m_TextDrawData.wColor1,
				m_TextDrawData.wColor2
		);
	}
		// OBJECT MODEL
	else if ((m_TextDrawData.wModelID <= 320 || m_TextDrawData.wModelID > 373) && m_TextDrawData.wModelID < 615) {
		if (m_TextDrawData.wModelID >= 0) return;
		if (!IsValidModel(m_TextDrawData.wModelID)) {
			m_TextDrawData.wModelID = 18631;
		}

		snapshot = pSnapShotHelper->CreateObjectSnapShot(
				m_TextDrawData.wModelID,
				m_TextDrawData.dwBackgroundColor,
				&m_TextDrawData.vecRot,
				m_TextDrawData.fZoom
		);
	}
	else {
		if (!IsExistInfoForModel(m_TextDrawData.wModelID)) {
			m_TextDrawData.wModelID = 18631;
		}

		snapshot = pSnapShotHelper->CreateObjectSnapShot(
				m_TextDrawData.wModelID,
				m_TextDrawData.dwBackgroundColor,
				&m_TextDrawData.vecRot,
				m_TextDrawData.fZoom
		);
	}

	if (snapshot) {
		const int slot = GetFreeTextDrawTextureSlot();
		if (slot >= 0 && slot < 200) {
			m_TextDrawData.iTextureSlot = slot;
			TextDrawTexture[slot] = snapshot;
		}
		else {
			eng::fn::RwTextureDestroy(snapshot);
		}
	}
}

void CTextDraw::RefreshCustomAsset()
{
	if (!m_bUsesCustomAsset || !m_bPendingCustomAsset) {
		return;
	}
	if (m_TextDrawData.dwStyle == 4) {
		if (m_TextDrawData.iTextureSlot != -1) {
			DestroyTextDrawTexture(m_TextDrawData.iTextureSlot);
			m_TextDrawData.iTextureSlot = -1;
		}
		m_TextDrawData.iTextureSlot = GetFreeTextDrawTextureSlot();
		if (m_TextDrawData.iTextureSlot != -1) {
			LoadTexture();
		}
	} else if (m_TextDrawData.dwStyle == 5) {
		if (m_TextDrawData.iTextureSlot != -1) {
			DestroyTextDrawTexture(m_TextDrawData.iTextureSlot);
			m_TextDrawData.iTextureSlot = -1;
		}
		// Next SnapshotProcess() tick will re-render the now-loaded custom model.
	}
}

void CTextDraw::ReleaseCustomAssetBeforeUnload()
{
	if (!m_bUsesCustomAsset) {
		return;
	}
	if (m_TextDrawData.iTextureSlot != -1) {
		DestroyTextDrawTexture(m_TextDrawData.iTextureSlot);
		m_TextDrawData.iTextureSlot = -1;
	}
	m_bPendingCustomAsset = true;
}
