#include "ImGuiWrapper.h"
#include <chrono>
#include "UISettings.h"
#include "../Settings.h"
#include "gbk_charset.h"
#include "../Game/Game.h"
#include "../Net/NetGame.h"


// Glyph ranges requested for the UI font. Codepoints missing from a font
// file are skipped by the atlas builder, so it is safe to request ranges
// a given font does not have. Large CJK ideograph blocks are intentionally
// NOT listed here; common Chinese is added via
// GetGlyphRangesChineseSimplifiedCommon() in initialize() to keep the
// atlas within texture size limits.
static const ImWchar g_fontGlyphRanges[] =
		{
				0x0020, 0x00FF, // Basic Latin + Latin Supplement
				0x0100, 0x017F, // Latin Extended-A
				0x0370, 0x03FF, // Greek and Coptic
				0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
				0x0E00, 0x0E7F, // Thai
				0x2000, 0x206F, // General Punctuation
				0x20A0, 0x20CF, // Currency Symbols
				0x2100, 0x214F, // Letterlike Symbols
				0x2190, 0x21FF, // Arrows
				0x2200, 0x22FF, // Mathematical Operators
				0x2300, 0x23FF, // Miscellaneous Technical
				0x2460, 0x24FF, // Enclosed Alphanumerics
				0x2500, 0x257F, // Box Drawing
				0x2580, 0x259F, // Block Elements
				0x25A0, 0x25FF, // Geometric Shapes
				0x2600, 0x26FF, // Miscellaneous Symbols
				0x2700, 0x27BF, // Dingbats
				0x2DE0, 0x2DFF, // Cyrillic Extended-A
				0x3000, 0x303F, // CJK Symbols and Punctuation
				0x3040, 0x30FF, // Hiragana + Katakana
				0x3100, 0x312F, // Bopomofo
				0x3130, 0x318F, // Hangul Compatibility Jamo
				0x3200, 0x32FF, // Enclosed CJK Letters and Months
				0xA640, 0xA69F, // Cyrillic Extended-B
				0xF020, 0xF0FF, // Private use (weapon glyphs etc.)
				0xFE30, 0xFE4F, // CJK Compatibility Forms
				0xFF00, 0xFFEF, // Halfwidth and Fullwidth Forms
				0
		};

ImGuiWrapper::ImGuiWrapper(const ImVec2& display_size, const std::string& fonts_path)
{
	m_displaySize = display_size;
	m_renderer = nullptr;
	m_fontRaster = nullptr;
	m_fontsPath = fonts_path;

	m_font = nullptr;
	m_weapFont = nullptr;

	m_vertexBuffer = nullptr;
	m_vertexBufferSize = 10000;
}

ImGuiWrapper::~ImGuiWrapper()
{
	shutdown();
}

bool ImGuiWrapper::initialize()
{
	spdlog::info("ImGuiWrapper::initialize");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = m_displaySize;
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

	// Keep the atlas within the engine's texture size limit (2048) or the
	// raster gets rescaled non-uniformly and every glyph samples the wrong
	// texels, which shows up as distorted pseudo-CJK glyphs.
	io.Fonts->TexDesiredWidth = 2048;

	// The full CJK ideograph range (~21k glyphs) produces an atlas far too
	// tall for the engine's texture limits, which shows up as stretched
	// text. Instead request every double-byte GBK character: all of GB2312
	// plus the GBK in-grid extensions (traditional hanzi, rare chars and
	// symbol glyphs like arrows/box-drawing a server can actually send).
	static ImVector<ImWchar> s_glyphRanges;
	s_glyphRanges.clear();
	{
		ImFontGlyphRangesBuilder builder;
		builder.AddRanges(g_fontGlyphRanges);
		builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
		builder.AddText(kGbkGlyphSet);
		builder.BuildRanges(&s_glyphRanges);
	}
	const ImWchar* glyphRanges = s_glyphRanges.Data;

	int family = Settings::fontFamily();
	float targetFontSize = UISettings::fontSize() / 2.0f;
	// Keep the rasterized glyph size <= 18.0f so all ~7,500 GBK glyphs fit inside a
	// 2048x2048 atlas texture (RenderWare's RwImageFindRasterFormat hard-clamps textures
	// to 2048x2048; a 2048x4096 atlas gets vertically squashed 2:1, causing every CJK
	// character to sample the wrong row). We then set ImFont::Scale = targetFontSize / atlasFontSize
	// so rendered text scales to the full targetFontSize on 1080p/1440p screens.
	float atlasFontSize = std::min(targetFontSize, 18.0f);
	if (family == 2) {
		m_font = loadFont("custom.ttf", atlasFontSize, glyphRanges);
	}
	else if (family == 0) {
		m_font = loadFont("chinese.ttf", atlasFontSize, glyphRanges);
	}
	if (m_font == nullptr) {
		m_font = loadFont("arial_bold.ttf", atlasFontSize, glyphRanges);
	}
	if (m_font == nullptr) {
		m_font = loadFont("arial.ttf", atlasFontSize, glyphRanges);
	}
	if (m_font == nullptr) {
		m_font = loadFont("chinese.ttf", atlasFontSize, glyphRanges);
	}
	if (m_font == nullptr) {
		return false;
	}

	// Fill in glyphs missing from the base font (Chinese, symbols, ...)
	if (family != 0) {
		mergeFont(m_font, "chinese.ttf", atlasFontSize, glyphRanges);
	}

	m_weapFont = loadFont("gtaweap3.ttf", atlasFontSize, io.Fonts->GetGlyphRangesDefault());

	createFontTexture();

	m_font->Scale = targetFontSize / atlasFontSize;
	if (m_weapFont) {
		m_weapFont->Scale = targetFontSize / atlasFontSize;
	}

	m_renderer = new ImGuiRenderer(ImGui::GetBackgroundDrawList(), m_font);

	return true;
}

void ImGuiWrapper::render()
{
	static auto s_lastFrameTime = std::chrono::steady_clock::now();
	const auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - s_lastFrameTime).count();
	s_lastFrameTime = now;
	if (!(dt > 0.0f)) dt = 1.0f / 60.0f;
	else if (dt > 0.25f) dt = 0.25f;
	ImGui::GetIO().DeltaTime = dt;

	ImGui::NewFrame();

	drawList();

	ImGui::EndFrame();
	ImGui::Render();
	renderDrawData(ImGui::GetDrawData());
}

void ImGuiWrapper::shutdown()
{
	spdlog::info("ImGuiWrapper::shutdown");
	destroyFontTexture();
	if (m_vertexBuffer) {
		delete[] m_vertexBuffer;
		m_vertexBuffer = nullptr;
		m_vertexBufferSize = 0;
	}
	if (m_renderer) {
		delete m_renderer;
		m_renderer = nullptr;
	}
	if (ImGui::GetCurrentContext()) {
		ImGui::DestroyContext();
	}
}

ImFont* ImGuiWrapper::loadFont(const std::string& font_name, float font_size, const ImWchar* glyph_ranges)
{
	spdlog::info("ImGuiWrapper::loadFont: {} from {}", font_name.c_str(), m_fontsPath.c_str());

	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig fontCfg;
	fontCfg.GlyphExtraSpacing.x = UISettings::outlineSize();
	// The default 3x horizontal oversampling triples every glyph's atlas
	// footprint, which pushes the atlas past the engine's 2048px texture
	// limit once CJK glyphs are included.
	fontCfg.OversampleH = 1;

	std::string fontPath = m_fontsPath + font_name;
	FILE* probe = fopen(fontPath.c_str(), "rb");
	if (probe == nullptr) {
		spdlog::warn("ImGuiWrapper::loadFont: font not found, skipping: {} (errno={})", fontPath.c_str(), errno);
		return nullptr;
	}
	fseek(probe, 0, SEEK_END);
	long fileSize = ftell(probe);
	fclose(probe);
	if (fileSize <= 0) {
		spdlog::warn("ImGuiWrapper::loadFont: empty font file, skipping: {}", fontPath.c_str());
		return nullptr;
	}

	ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), font_size, &fontCfg, glyph_ranges);
	if (font == nullptr) {
		spdlog::error("Failed to load font: {} (path={}, size={})", font_name.c_str(), fontPath.c_str(), fileSize);
		return nullptr;
	}

	return font;
}

// Merges another TTF into an already loaded font. Only glyph slots not
// provided by the base font are filled, so the base font always wins for
// codepoints it contains.
void ImGuiWrapper::mergeFont(ImFont* base, const std::string& font_name, float font_size, const ImWchar* glyph_ranges)
{
	if (base == nullptr) {
		return;
	}

	std::string fontPath = m_fontsPath + font_name;
	FILE* test = fopen(fontPath.c_str(), "rb");
	if (test == nullptr) {
		return;
	}
	fclose(test);

	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig fontCfg;
	fontCfg.MergeMode = true;
	fontCfg.GlyphExtraSpacing.x = UISettings::outlineSize();
	fontCfg.OversampleH = 1;

	spdlog::info("ImGuiWrapper::mergeFont: {} into base font", font_name.c_str());
	io.Fonts->AddFontFromFileTTF(fontPath.c_str(), font_size, &fontCfg, glyph_ranges);
}

void ImGuiWrapper::setupRenderState(ImDrawData* draw_data)
{
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*) 0);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*) 0);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*) 1);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*) rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*) rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*) 0);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*) rwCULLMODECULLNONE);
	RwRenderStateSet(rwRENDERSTATEBORDERCOLOR, (void*) 0);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*) rwALPHATESTFUNCTIONGREATER);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*) 2);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*) rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*) rwTEXTUREADDRESSCLAMP);
}

void ImGuiWrapper::checkVertexBuffer(ImDrawData* draw_data)
{
	if (m_vertexBuffer == nullptr || m_vertexBufferSize < draw_data->TotalVtxCount) {
		spdlog::info("ImGuiWrapper::checkVertexBuffer");

		if (m_vertexBuffer) {
			delete[] m_vertexBuffer;
			m_vertexBuffer = nullptr;
		}

		m_vertexBufferSize = draw_data->TotalVtxCount + 10000;
		m_vertexBuffer = new RwIm2DVertex[m_vertexBufferSize];
		if (!m_vertexBuffer) {
			spdlog::info("Buffer allocation error. Size {}", m_vertexBufferSize);
			return;
		}

		spdlog::info("Vertex buffer reallocated. Size {}", m_vertexBufferSize);
	}
}

void ImGuiWrapper::renderDrawData(ImDrawData* draw_data)
{
	const RwReal nearScreenZ = getNearScreenZ();
	const RwReal recipNearClip = getRecipNearClip();

	checkVertexBuffer(draw_data);
	setupRenderState(draw_data);

	RwIm2DVertex* vtx_dst = m_vertexBuffer;
	int vtx_offset = 0;

	for (int n = 0; n < draw_data->CmdListsCount; n++) {
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		const ImDrawVert* vtx_src = cmd_list->VtxBuffer.Data;
		const ImDrawIdx* idx_src = cmd_list->IdxBuffer.Data;

		for (int i = 0; i < cmd_list->VtxBuffer.Size; i++) {
			RwIm2DVertexSetScreenX(vtx_dst, vtx_src->pos.x);
			RwIm2DVertexSetScreenY(vtx_dst, vtx_src->pos.y);
			RwIm2DVertexSetScreenZ(vtx_dst, nearScreenZ);
			RwIm2DVertexSetRecipCameraZ(vtx_dst, recipNearClip);
			vtx_dst->emissiveColor = vtx_src->col;
			RwIm2DVertexSetU(vtx_dst, vtx_src->uv.x, recipCameraZ);
			RwIm2DVertexSetV(vtx_dst, vtx_src->uv.y, recipCameraZ);

			vtx_dst++;
			vtx_src++;
		}

		const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback) {
				pcmd->UserCallback(cmd_list, pcmd);
			}
			else {
				ImVec4 clip_rect;
				clip_rect.x = pcmd->ClipRect.x;
				clip_rect.y = pcmd->ClipRect.w;
				clip_rect.z = pcmd->ClipRect.z;
				clip_rect.w = pcmd->ClipRect.y;
				setScissorRect((void*) &clip_rect);

				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*) pcmd->TextureId);
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, &m_vertexBuffer[vtx_offset], (RwInt32) cmd_list->VtxBuffer.Size, (RwImVertexIndex*) idx_buffer, pcmd->ElemCount);
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*) 0);

				clip_rect = {0.0f, 0.0f, 0.0f, 0.0f};
				setScissorRect((void*) &clip_rect);
			}

			idx_buffer += pcmd->ElemCount;
		}
		vtx_offset += cmd_list->VtxBuffer.Size;
	}

}

bool ImGuiWrapper::createFontTexture()
{
	spdlog::info("ImGuiWrapper::createFontTexture");

	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pxs;
	int width, height, bytes_per_pixel;
	io.Fonts->GetTexDataAsRGBA32(&pxs, &width, &height, &bytes_per_pixel);

	RwImage* font_img = RwImageCreate(width, height, bytes_per_pixel * 8);
	if (font_img == nullptr) {
		spdlog::error("RwImageCreate failed ({}x{})", width, height);
		return false;
	}
	RwImageAllocatePixels(font_img);
	if (font_img->cpPixels == nullptr) {
		spdlog::error("RwImageAllocatePixels failed ({}x{})", width, height);
		RwImageDestroy(font_img);
		return false;
	}

	RwUInt8* pixels = font_img->cpPixels;
	for (int y = 0; y < font_img->height; y++) {
		memcpy((void*) pixels, pxs + font_img->stride * y, font_img->stride);
		pixels += font_img->stride;
	}

	RwInt32 w, h, d, flags;
	RwImageFindRasterFormat(font_img, rwRASTERTYPETEXTURE, &w, &h, &d, &flags);
	spdlog::info("font raster format: {}x{} depth {} flags {:X} (image {}x{})", w, h, d, flags, width, height);
	m_fontRaster = RwRasterCreate(w, h, d, flags);
	if (m_fontRaster == nullptr) {
		spdlog::error("font raster pointer 0x{:X}", reinterpret_cast<uintptr_t>(m_fontRaster));
		RwImageDestroy(font_img);
		return false;
	}

	m_fontRaster = RwRasterSetFromImage(m_fontRaster, font_img);
	io.Fonts->TexID = (ImTextureID*) m_fontRaster;

	RwImageDestroy(font_img);

	spdlog::info("Atlas width {}", width);
	spdlog::info("Atlas height {}", height);
	return true;
}

void ImGuiWrapper::destroyFontTexture()
{
	if (m_fontRaster) {
		ImGuiIO& io = ImGui::GetIO();
		RwRasterDestroy(m_fontRaster);
		m_fontRaster = nullptr;
		io.Fonts->TexID = (ImTextureID) 0;
	}
}
