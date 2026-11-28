/// \file
///
/// This file is part of RakNet Copyright 2003 Kevin Jenkins.
///
/// Usage of RakNet is subject to the appropriate license agreement.
/// Creative Commons Licensees are subject to the
/// license found at
/// http://creativecommons.org/licenses/by-nc/2.5/
/// Single application licensees are subject to the license found at
/// http://www.rakkarsoft.com/SingleApplicationLicense.html
/// Custom license users are subject to the terms therein.
/// GPL license users are subject to the GNU General Public
/// License as published by the Free
/// Software Foundation; either version 2 of the License, or (at your
/// option) any later version.

#include "GetTime.h"
#include <cstdint>
#ifdef _COMPATIBILITY_1
#include "Compatibility1Includes.h"
#elif defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

namespace {
#if !defined(_WIN32) && !defined(_COMPATIBILITY_1)
uint64_t monotonicNowUs()
{
	struct timespec ts{};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec / 1000);
}

uint64_t monotonicBaseUs()
{
	static const uint64_t s_base = monotonicNowUs();
	return s_base;
}
#endif
} // namespace

RakNetTime RakNet::GetTime( void )
{
#ifdef _WIN32
	static LARGE_INTEGER yo = []() {
		LARGE_INTEGER f{};
		QueryPerformanceFrequency(&f);
		return f;
	}();
	LARGE_INTEGER PerfVal{};
	QueryPerformanceCounter(&PerfVal);
	return static_cast<RakNetTime>(PerfVal.QuadPart * 1000 / yo.QuadPart);
#elif !defined(_COMPATIBILITY_1)
	const uint64_t elapsedUs = monotonicNowUs() - monotonicBaseUs();
	return static_cast<RakNetTime>(elapsedUs / 1000ULL);
#else
	return 0;
#endif
}

RakNetTimeNS RakNet::GetTimeNS( void )
{
#ifdef _WIN32
	static LARGE_INTEGER yo = []() {
		LARGE_INTEGER f{};
		QueryPerformanceFrequency(&f);
		return f;
	}();
	LARGE_INTEGER PerfVal{};
	QueryPerformanceCounter(&PerfVal);
	__int64 quotient = ((PerfVal.QuadPart * 1000) / yo.QuadPart);
	__int64 remainder = ((PerfVal.QuadPart * 1000) % yo.QuadPart);
	return static_cast<RakNetTimeNS>(quotient * 1000 + (remainder * 1000 / yo.QuadPart));
#elif !defined(_COMPATIBILITY_1)
	return static_cast<RakNetTimeNS>(monotonicNowUs() - monotonicBaseUs());
#else
	return 0;
#endif
}
