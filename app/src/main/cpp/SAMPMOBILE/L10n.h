#pragma once

#include "Settings.h"

// Keeps both alternatives as string literals so printf format-security checking still works.
#define L10N(en, zhText) (l10n::zh() ? (zhText) : (en))

namespace l10n {

inline bool zh()
{
	return Settings::chineseLanguage();
}

inline const char* tr(const char* en, const char* zhText)
{
	return zh() ? zhText : en;
}

inline const char* onOff(bool enabled)
{
	return zh() ? (enabled ? "开启" : "关闭") : (enabled ? "ON" : "OFF");
}

inline const char* onOffTitle(bool enabled)
{
	return zh() ? (enabled ? "开启" : "关闭") : (enabled ? "On" : "Off");
}

} // namespace l10n
