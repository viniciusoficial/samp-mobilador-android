#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include "UISettings.h"
#include "../Settings.h"

/* scaling */
ImVec2 UISettings::m_baseSize = ImVec2(640.0f, 480.0f);
ImVec2 UISettings::m_scaleFactor = ImVec2(1.0f, 1.0f);

/* font */
float UISettings::m_fontSize = 26.0f;
float UISettings::m_outlineSize = 2.0f;

/* ////////////////// pos & size ////////////////// */

/* widgets */
float UISettings::m_padding = 15.0f;

/* keyboard */
ImVec2 UISettings::m_keyboardPos = ImVec2(0.0f, 220.0f);
ImVec2 UISettings::m_keyboardSize = ImVec2(640.0f, 260.0f);
float UISettings::m_keyboardRowHeight = 52.0f;

/* splashscreen */
ImVec2 UISettings::m_splashscreenLogoPos;
ImVec2 UISettings::m_splashscreenLogoSize = ImVec2(640.0f, 480.0f);
ImVec2 UISettings::m_splashScreenPBarPos = ImVec2(100.0f, 460.0f);
ImVec2 UISettings::m_splashScreenPBarSize = ImVec2(440.0f, 10.0f);

/* chat */
int UISettings::m_chatDispMesssages = 9;
int UISettings::m_chatMaxMessages = 30;
ImVec2 UISettings::m_chatPos = ImVec2(100.0f, 10.0f);
ImVec2 UISettings::m_chatSize = ImVec2(400.0f, 0.0f);
ImVec2 UISettings::m_chatItemSize = ImVec2(400.0f, 13.0f);

/* spawn */
ImVec2 UISettings::m_spawnPos = ImVec2(200.0f, 430.0f);
ImVec2 UISettings::m_spawnSize = ImVec2(240.0f, 45.0f);

/* nametag */
ImVec2 UISettings::m_nametagBarSize = ImVec2(25.0f, 6.0f);

/* voice button */
ImVec2 UISettings::m_buttonVoicePos = ImVec2(480.0f, 270.0f);
ImVec2 UISettings::m_buttonVoiceSize = ImVec2(45.0f, 60.0f);

/* scoreboard */
ImVec2 UISettings::m_scoreboardSize = ImVec2(300.0f, 280.0f);

/* ////////////////// colors ////////////////// */

/* button */
ImColor UISettings::m_buttonColor = ImColor(0.11f, 0.11f, 0.11f, 0.80f);
ImColor UISettings::m_buttonFocusedColor = ImColor(0x32, 0x91, 0xF5);

/* keyboard */
ImColor UISettings::m_keyboardBackgroundColor = ImColor(0, 0, 0, 150);

void UISettings::Initialize(const ImVec2& display_size)
{
	m_scaleFactor = display_size / m_baseSize;

	/* font */
	m_fontSize = Settings::fontsize();
	m_fontSize *= m_scaleFactor.y;

	/* keyboard */
	m_keyboardSize = m_keyboardSize * m_scaleFactor;
	m_keyboardPos = m_keyboardPos * m_scaleFactor;
	m_keyboardRowHeight *= m_scaleFactor.y;

	/* splashscreen */
	m_splashscreenLogoSize = m_splashscreenLogoSize * m_scaleFactor;
	m_splashscreenLogoPos = ImVec2(0.0f, 0.0f);
	m_splashScreenPBarPos = m_splashScreenPBarPos * m_scaleFactor;
	m_splashScreenPBarSize = m_splashScreenPBarSize * m_scaleFactor;

	/* chat */
	m_chatDispMesssages = Settings::chatstrings() + 1;
	m_chatPos = ImVec2(Settings::chatPosX(), Settings::chatPosY());
	m_chatItemSize = ImVec2(Settings::chatSizeX(), Settings::chatSizeY() / 10.0f);
	m_chatPos = m_chatPos * m_scaleFactor;
	m_chatSize.x = Settings::chatSizeX();
	m_chatSize.y = m_chatItemSize.y * m_chatDispMesssages;
	m_chatSize = m_chatSize * m_scaleFactor;
	m_chatItemSize = m_chatItemSize * m_scaleFactor;

	/* spawn */
	m_spawnPos = m_spawnPos * m_scaleFactor;
	m_spawnSize = m_spawnSize * m_scaleFactor;

	/* nametag */
	m_nametagBarSize = m_nametagBarSize * m_scaleFactor;

	/* button voice */
	float voiceScale = (float) Settings::voiceBtnSize() / 100.0f;
	m_buttonVoicePos = m_buttonVoicePos * m_scaleFactor;
	m_buttonVoiceSize = (m_buttonVoiceSize * m_scaleFactor) * voiceScale;

	/* scoreboard */
	m_scoreboardSize = m_scoreboardSize * m_scaleFactor;
}

void UISettings::setChatStrings(int lines)
{
	if (lines < 5) lines = 5;
	if (lines > 20) lines = 20;
	Settings::setChatstrings(lines);
	m_chatDispMesssages = lines + 1;
	m_chatSize.x = Settings::chatSizeX() * m_scaleFactor.x;
	m_chatSize.y = m_chatItemSize.y * m_chatDispMesssages;
}

void UISettings::setFontSizeOffset(int offset)
{
	if (offset < -3) offset = -3;
	if (offset > 5) offset = 5;
	Settings::setFontSizeOffset(offset);
	float baseFont = Settings::fontsize() + static_cast<float>(offset) * 2.5f;
	if (baseFont < 12.0f) baseFont = 12.0f;
	m_fontSize = baseFont * m_scaleFactor.y;
	float baseItemY = (Settings::chatSizeY() / 10.0f) * (baseFont / Settings::fontsize());
	m_chatItemSize = ImVec2(Settings::chatSizeX() * m_scaleFactor.x, baseItemY * m_scaleFactor.y);
	m_chatSize.x = Settings::chatSizeX() * m_scaleFactor.x;
	m_chatSize.y = m_chatItemSize.y * m_chatDispMesssages;
}