#pragma once

#include <cstddef>
#include <cstdint>
#include "RGBA.h"

class CFont {
public:
	static void StringToAscii(const char* str, uint16_t* ascii, size_t maxOut = 800);
	static void AsciiToGxtChar(const char* ascii, uint16_t* gxt);
	static void AsciiToGxtCharU8(uint16_t* ascii, uint16_t* gxt, size_t maxOut = 800);
	static void SetScale(float x, float y);
	static void SetColor(RGBA* dwColor);
	static void SetJustify(uint8_t just);
	static void SetOrientation(uint8_t orientation);
	static void SetWrapX(float wrap);
	static void SetCentreSize(float size);
	static void SetBackground(uint8_t bBackground, uint8_t bOnlyText);
	static void SetBackgroundColor(RGBA* dwColor);
	static void SetProportional(uint8_t prop);
	static void SetDropColor(RGBA* dwColor);
	static void SetDropShadowPosition(uint8_t pos);
	static void PrintString(float fX, float fY, const char* szText);
	static void SetFontStyle(uint8_t style);
	static void SetEdge(uint8_t edge);
};
