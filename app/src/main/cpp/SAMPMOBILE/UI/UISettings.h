#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>

class UISettings {
public:
	static void Initialize(const ImVec2& display_size);

	/* font */
	static float fontSize() { return m_fontSize; }
	static float outlineSize() { return m_outlineSize; }
	static void setFontSizeOffset(int offset);

	/* ////////////////// sizes ////////////////// */

	/* widgets */
	static float padding() { return m_padding; }

	/* keyboard */
	static ImVec2 keyboardPos() { return m_keyboardPos; }
	static ImVec2 keyboardSize() { return m_keyboardSize; }
	static float keyboardRowHeight() { return m_keyboardRowHeight; }

	/* splashscreen */
	static ImVec2 splashscreenLogoPos() { return m_splashscreenLogoPos; }
	static ImVec2 splashscreenLogoSize() { return m_splashscreenLogoSize; }
	static ImVec2 splashscreenPBarPos() { return m_splashScreenPBarPos; }
	static ImVec2 splashscreenPBarSize() { return m_splashScreenPBarSize; }

	/* chat */
	static ImVec2 chatPos() { return m_chatPos; }
	static ImVec2 chatSize() { return m_chatSize; }
	static ImVec2 chatItemSize() { return m_chatItemSize; }
	static int chatMaxMessages() { return m_chatMaxMessages; }
	static void setChatStrings(int lines);

	/* spawn */
	static ImVec2 spawnPos() { return m_spawnPos; }
	static ImVec2 spawnSize() { return m_spawnSize; }

	/* nametag */
	static ImVec2 nametagBarSize() { return m_nametagBarSize; }

	/* voice button */
	static ImVec2 buttonVoicePos() { return m_buttonVoicePos; }
	static ImVec2 buttonVoiceSize() { return m_buttonVoiceSize; }

	/* scoreboard */
	static ImVec2 scoreboardSize() { return m_scoreboardSize; }

	/* ////////////////// colors ////////////////// */

	/* button */
	static ImColor buttonColor() { return m_buttonColor; }
	static ImColor buttonFocusedColor() { return m_buttonFocusedColor; }

	/* keyboard */
	static ImColor keyboardBackgroundColor() { return m_keyboardBackgroundColor; }

private:
	/* scaling */
	static ImVec2 m_baseSize;
	static ImVec2 m_scaleFactor;

	/* font */
	static float m_fontSize;
	static float m_outlineSize;

	/* ////////////////// pos & size ////////////////// */

	/* Widgets */
	static float m_padding;

	/* keyboard */
	static ImVec2 m_keyboardPos;
	static ImVec2 m_keyboardSize;
	static float m_keyboardRowHeight;

	/* splashscreen */
	static ImVec2 m_splashscreenLogoPos;
	static ImVec2 m_splashscreenLogoSize;
	static ImVec2 m_splashScreenPBarPos;
	static ImVec2 m_splashScreenPBarSize;

public:
	/* chat */
	static ImVec2 m_chatPos;
	static ImVec2 m_chatSize;
	static ImVec2 m_chatItemSize;
	static int m_chatDispMesssages;
	static int m_chatMaxMessages;

private:
	/* spawn */
	static ImVec2 m_spawnPos;
	static ImVec2 m_spawnSize;

	/* nametag */
	static ImVec2 m_nametagBarSize;

	/* voice button */
	static ImVec2 m_buttonVoicePos;
	static ImVec2 m_buttonVoiceSize;

	/* scoreboard */
	static ImVec2 m_scoreboardSize;


	/* ////////////////// colors ////////////////// */

	/* button */
	static ImColor m_buttonColor;
	static ImColor m_buttonFocusedColor;

	/* keyboard */
	static ImColor m_keyboardBackgroundColor;
};