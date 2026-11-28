#pragma once

#include <string>

class Encoding {
public:
	// Convert server codepage bytes -> UTF-8
	static std::string cp2utf(const std::string& s);
	// Convert UTF-8 -> server codepage bytes
	static std::string utf2cp(const std::string& s);

	static void setCharset(const std::string& cs);
	static const std::string& charset();
};
