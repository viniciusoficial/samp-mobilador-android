#include "MaterialTextGenerator.h"
#include "Game.h"
#include "Object.h"
#include "Util.h"
#include "../UI/UI.h"
#include "../Client.h"

#include <imgui.h>
#include <encoding.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>


MaterialTextGenerator::MaterialTextGenerator()
{
	m_camera = nullptr;
	m_frame = nullptr;

	SetUpScene();
}

void MaterialTextGenerator::SetUpScene()
{
	m_camera = RwCameraCreate();
	m_frame = RwFrameCreate();
	if (!m_camera || !m_frame) {
		if (m_camera) {
			eng::fn::RwCameraDestroy( m_camera);
			m_camera = nullptr;
		}
		if (m_frame) {
			eng::fn::RwFrameDestroy( m_frame);
			m_frame = nullptr;
		}
		return;
	}

	RwObjectHasFrameSetFrame(m_camera, m_frame);

	RwCameraSetFarClipPlane(m_camera, 300.0f);
	RwCameraSetNearClipPlane(m_camera, 0.01f);

	RwV2d view = {0.5f, 0.5f};
	RwCameraSetViewWindow(m_camera, &view);
	RwCameraSetProjection(m_camera, rwPERSPECTIVE);

	rpWorldAddCamera(m_camera);
}

RwTexture* MaterialTextGenerator::Create(int iSizeX, int iSizeY, const char* szFontName, uint8_t byteFontSize, uint8_t byteFontBold, uint32_t dwFontColor, uint32_t dwBackgroundColor, uint8_t byteAlign, const char* szText)
{
	if (!m_camera) {
		spdlog::error("MaterialTextGenerator::Create: camera is null");
		return 0;
	}

	RwRaster* raster = RwRasterCreate(iSizeX, iSizeY, 32, rwRASTERFORMAT8888 | rwRASTERTYPECAMERATEXTURE);
	if (!raster) {
		spdlog::error("MaterialTextGenerator::Create: RwRasterCreate({}x{}) failed", iSizeX, iSizeY);
		return 0;
	}

	RwTexture* bufferTexture = RwTextureCreate(raster);
	if (!bufferTexture) {
		spdlog::error("MaterialTextGenerator::Create: RwTextureCreate failed");
		RwRasterDestroy(raster);
		return 0;
	}
	// Wrap addressing so UV-animated scrolling text objects (e.g. model 19604
	// marquee boards) loop seamlessly instead of clamping to edge pixels.
	RwTextureSetAddressingMacro(bufferTexture, rwTEXTUREADDRESSWRAP);

	// background color
	uint8_t b = (dwBackgroundColor) & 0xFF;
	uint8_t g = (dwBackgroundColor >> 8) & 0xFF;
	uint8_t r = (dwBackgroundColor >> 16) & 0xFF;
	uint8_t a = (dwBackgroundColor >> 24) & 0xFF;
	uint32_t dwBackground = (r | (g << 8) | (b << 16) | (a << 24));

	m_camera->frameBuffer = raster;
	SetRenderWareCamera(m_camera);

	RwCameraClear(m_camera, (RwRGBA*) &dwBackground, 3);
	RwCameraBeginUpdate(m_camera);

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*) true);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*) true);
	RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*) rwSHADEMODENASHADEMODE);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*) 0);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*) rwCULLMODENACULLMODE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*) false);
	DefinedState();

	Render(iSizeX, iSizeY, szFontName, byteFontSize, byteFontBold, dwFontColor, byteAlign, szText);

	RwCameraEndUpdate((RwCamera*) m_camera);
	m_camera->frameBuffer = nullptr;

	// RwTextureCreate initializes refCount to 1 — do not increment it again,
	// otherwise RwTextureDestroy in ~CObject only drops 2 -> 1 and leaks every
	// generated raster and GL texture permanently.
	return bufferTexture;
}

RwTexture* MaterialTextGenerator::CreateCustomCarPlate(const char* szPlate)
{
	if (!szPlate || !*szPlate) {
		szPlate = DEFAULT_PLATE_TEXT;
	}

	const std::string utf8 = Encoding::cp2utf(szPlate);
	const std::string formatted = GxtToImGuiColors(utf8, "{444470}");

	constexpr int kPlateW = 256;
	constexpr int kPlateH = 64;
	uint8_t fontSize = 40;
	if (pUI && pUI->renderer()) {
		const ImVec2 sz = pUI->renderer()->calculateTextSize(formatted, static_cast<float>(fontSize));
		if (sz.x > static_cast<float>(kPlateW - 16) && sz.x > 1.0f) {
			const float scale = static_cast<float>(kPlateW - 16) / sz.x;
			fontSize = static_cast<uint8_t>(std::clamp(static_cast<int>(fontSize * scale), 14, 40));
		}
	}

	return Create(kPlateW, kPlateH, nullptr, fontSize, 0,
		DEFAULT_PLATE_TEXT_COLOR, DEFAULT_PLATE_BG_COLOR, DEFAULT_PLATE_ALIGN, formatted.c_str());
}

void MaterialTextGenerator::Render(int iSizeX, int iSizeY, const char* szFontName, uint8_t byteFontSize, uint8_t byteFontBold, uint32_t dwFontColor, uint8_t byteAlign, const char* szText)
{
	if (!pUI) {
		spdlog::error("MaterialTextGenerator::Render: pUI is null, text not drawn");
		return;
	}
	if (pUI) {
		ImGuiIO& io = ImGui::GetIO();
		ImVec2 savedDisplaySize = io.DisplaySize;
		io.DisplaySize = ImVec2((float) iSizeX, (float) iSizeY);

		ImGui::NewFrame();

		ImFont* pFont = pUI->font();
		float fFontSize = byteFontSize > 0 ? (float) byteFontSize : 1.0f;

		// text color
		uint8_t b = (dwFontColor) & 0xFF;
		uint8_t g = (dwFontColor >> 8) & 0xFF;
		uint8_t r = (dwFontColor >> 16) & 0xFF;
		uint8_t a = (dwFontColor >> 24) & 0xFF;

		// Split into lines (strip trailing \r from \r\n)
		std::vector<std::string> lines;
		{
			std::string strText = szText ? szText : "";
			std::stringstream ss(strText);
			std::string line;
			while (std::getline(ss, line, '\n')) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				lines.push_back(std::move(line));
			}
		}

		// text align (matches PC SA-MP: 0=DT_LEFT, 1=DT_CENTER|DT_VCENTER, 2=DT_RIGHT)
		ImVec2 _veecPos = ImVec2(0.0f, 0.0f);
		switch (byteAlign) {
			case MATERIAL_ALIGN_LEFT: {
				for (const auto& line : lines) {
					if (!line.empty()) {
						pUI->renderer()->drawText(_veecPos, ImColor(r, g, b, a), line, false, fFontSize, pFont);
					}
					_veecPos.y += fFontSize;
				}
				break;
			}
			case MATERIAL_ALIGN_CENTER: {
				float totalHeight = (float) lines.size() * fFontSize;
				_veecPos.y = ((float) iSizeY - totalHeight) * 0.5f;
				if (_veecPos.y < 0.0f) {
					_veecPos.y = 0.0f;
				}

				for (const auto& line : lines) {
					if (!line.empty()) {
						ImVec2 lineSize = pUI->renderer()->calculateTextSize(line, fFontSize);
						_veecPos.x = ((float) iSizeX - lineSize.x) * 0.5f;
						pUI->renderer()->drawText(_veecPos, ImColor(r, g, b, a), line, false, fFontSize, pFont);
					}
					_veecPos.y += fFontSize;
				}
				break;
			}
			case MATERIAL_ALIGN_RIGHT: {
				for (const auto& line : lines) {
					if (!line.empty()) {
						ImVec2 lineSize = pUI->renderer()->calculateTextSize(line, fFontSize);
						_veecPos.x = (float) iSizeX - lineSize.x;
						pUI->renderer()->drawText(_veecPos, ImColor(r, g, b, a), line, false, fFontSize, pFont);
					}
					_veecPos.y += fFontSize;
				}
				break;
			}
		}

		ImGui::EndFrame();
		ImGui::Render();
		pUI->renderDrawData(ImGui::GetDrawData());

		io.DisplaySize = savedDisplaySize;
	}
}
