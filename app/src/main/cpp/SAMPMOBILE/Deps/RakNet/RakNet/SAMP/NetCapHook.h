#pragma once

#include <atomic>

// dir: 0 = received, 1 = sent. kind: 0 = RPC (id = RPC number), 1 = raw packet (id = first byte).
// data is bit-packed exactly as it travels; bitLength is its length in bits.
typedef void (*RakNetCapHook)(int dir, int kind, int id, const unsigned char* data, unsigned int bitLength);

extern std::atomic<RakNetCapHook> g_rakNetCapHook;

inline void setRakNetCapHook(RakNetCapHook fn)
{
	g_rakNetCapHook.store(fn, std::memory_order_release);
}

inline void invokeRakNetCapHook(int dir, int kind, int id, const unsigned char* data, unsigned int bitLength)
{
	if (auto fn = g_rakNetCapHook.load(std::memory_order_acquire)) {
		fn(dir, kind, id, data, bitLength);
	}
}
