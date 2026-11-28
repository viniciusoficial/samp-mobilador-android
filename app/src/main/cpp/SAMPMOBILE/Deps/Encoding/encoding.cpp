#include "include/encoding.h"

#include <spdlog/spdlog.h>
#include <iconv.h>
#include <cstring>
#include <mutex>
#include <stdexcept>

// Server codepage used for decoding (cp2utf) and encoding (utf2cp).
// Default is GBK so that Chinese servers display correctly out of the box.
// Can be changed at runtime via Encoding::setCharset (e.g. "CP1251", "GBK", "BIG5").
static std::mutex g_charsetMutex;
static std::string g_charset = "GBK";

void Encoding::setCharset(const std::string& cs)
{
	if (!cs.empty()) {
		std::lock_guard<std::mutex> lock(g_charsetMutex);
		g_charset = cs;
	}
}

const std::string& Encoding::charset()
{
	std::lock_guard<std::mutex> lock(g_charsetMutex);
	return g_charset;
}

namespace {
struct CachedIconv {
	iconv_t cd = reinterpret_cast<iconv_t>(-1);
	std::string to;
	std::string from;

	~CachedIconv()
	{
		if (cd != reinterpret_cast<iconv_t>(-1)) {
			iconv_close(cd);
		}
	}

	iconv_t get(const char* wantTo, const char* wantFrom)
	{
		if (cd != reinterpret_cast<iconv_t>(-1) && to == wantTo && from == wantFrom) {
			// Reset multibyte shift state before reuse
			iconv(cd, nullptr, nullptr, nullptr, nullptr);
			return cd;
		}
		if (cd != reinterpret_cast<iconv_t>(-1)) {
			iconv_close(cd);
		}
		cd = iconv_open(wantTo, wantFrom);
		if (cd != reinterpret_cast<iconv_t>(-1)) {
			to = wantTo;
			from = wantFrom;
		}
		else {
			to.clear();
			from.clear();
		}
		return cd;
	}
};

bool is_pure_ascii(const std::string& s)
{
	for (unsigned char c : s) {
		if (c >= 0x80) return false;
	}
	return true;
}
} // namespace

static std::string iconv_convert(const std::string& s, const char* to, const char* from, CachedIconv& cache)
{
	iconv_t cd = cache.get(to, from);
	if (cd == reinterpret_cast<iconv_t>(-1)) {
		spdlog::error("iconv_open failed ({} -> {}): {}", from, to, strerror(errno));
		return s;
	}

	std::string out;
	out.resize(s.size() * 4 + 4);
	char* inbuf = const_cast<char*>(s.data());
	char* outbuf = out.data();
	size_t inbytesleft = s.size();
	size_t outbytesleft = out.size();
	size_t res = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
	if (res == static_cast<size_t>(-1)) {
		spdlog::error("iconv failed ({} -> {}): {}", from, to, strerror(errno));
		return s;
	}
	out.resize(out.size() - outbytesleft);
	return out;
}

// Strict UTF-8 validation: rejects overlong forms, surrogates and
// out-of-range codepoints. Some servers send UTF-8 directly regardless
// of the configured legacy charset, so valid UTF-8 is passed through
// untouched instead of being double-converted into mojibake.
static bool is_valid_utf8(const std::string& s)
{
	size_t i = 0, n = s.size();
	while (i < n) {
		unsigned char c = (unsigned char) s[i];
		size_t len;
		uint32_t cp;
		if (c < 0x80) { ++i; continue; }
		else if (c < 0xC2) return false;              // stray continuation / overlong
		else if (c < 0xE0) { len = 2; cp = c & 0x1F; }
		else if (c < 0xF0) { len = 3; cp = c & 0x0F; }
		else if (c < 0xF5) { len = 4; cp = c & 0x07; }
		else return false;
		if (i + len > n) return false;
		for (size_t j = 1; j < len; ++j) {
			unsigned char t = (unsigned char) s[i + j];
			if ((t & 0xC0) != 0x80) return false;
			cp = (cp << 6) | (t & 0x3F);
		}
		if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false; // overlong
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;        // invalid
		i += len;
	}
	return true;
}

std::string Encoding::cp2utf(const std::string& s)
{
	if (s.empty() || is_pure_ascii(s)) return s;
	if (is_valid_utf8(s)) return s;

	std::string cs;
	{
		std::lock_guard<std::mutex> lock(g_charsetMutex);
		cs = g_charset;
	}
	thread_local CachedIconv s_cp2utfCache;
	if (cs == "UTF-8" || cs == "utf8") {
		return iconv_convert(s, "UTF-8", "GBK", s_cp2utfCache);
	}
	return iconv_convert(s, "UTF-8", cs.c_str(), s_cp2utfCache);
}

std::string Encoding::utf2cp(const std::string& s)
{
	if (s.empty() || is_pure_ascii(s)) return s;

	std::string cs;
	{
		std::lock_guard<std::mutex> lock(g_charsetMutex);
		cs = g_charset;
	}
	if (cs == "UTF-8" || cs == "utf8") return s;
	thread_local CachedIconv s_utf2cpCache;
	return iconv_convert(s, cs.c_str(), "UTF-8", s_utf2cpCache);
}
