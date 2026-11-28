#pragma once

// Minimal ZIP (.pak) reader for omp-cef resources.
//
// The server packs each resource directory into `scriptfiles/cef/<name>.pak`
// with miniz and ships the whole archive over the FileData channel; the PC
// client opens it with miniz and feeds every entry through
// DecodeResourceEntry(). Android already links zlib, so this module parses the
// archive itself (central directory -> local header -> raw DEFLATE) instead of
// vendoring another zip implementation.
//
// Only the two methods miniz emits for a resource pak are accepted: 0 (stored)
// and 8 (deflate). Anything else is rejected with a reason; ZIP64 is rejected
// explicitly rather than silently mis-parsed.

#include <cstdint>
#include <string>
#include <vector>

namespace cefpak
{

struct Entry
{
    std::string name;            // archive-internal path, '/' separated
    std::vector<uint8_t> data;   // inflated bytes, still OCEF2-wrapped
};

// Hard limits mirroring the PC client (resource_manager.cpp).
inline constexpr size_t MAX_ENTRY_BYTES = 512ull * 1024 * 1024;
inline constexpr size_t MAX_ENTRY_COUNT = 4096;

// 累计解压体积上限。单条 512MB x 4096 条 = 2TB，只有单条限制等于没有限制：
// 一个几百 KB 的包解出 2TB 就能把设备内存/存储吃干（ZIP 炸弹）。
// 2GiB 对 CEF 资源包足够宽裕（实测 pak 在 100MiB 量级）。
inline constexpr uint64_t MAX_TOTAL_UNCOMPRESSED_BYTES = 2ull * 1024 * 1024 * 1024;
inline constexpr size_t MAX_PATH_BYTES   = 512;

// Parses the archive and inflates every entry. Returns false and fills `error`
// when the archive is malformed, uses an unsupported method, or contains an
// unsafe entry name (absolute, "..", backslash, control characters).
bool ReadArchive(const std::vector<uint8_t>& pak,
                 std::vector<Entry>& out,
                 std::string* error);

// True when `path` is a plain relative archive path safe to join under the
// resource root: no leading '/', no '\\', no empty/"."/".." component, no
// control bytes, length bounded.
bool IsSafeEntryName(const std::string& path);

} // namespace cefpak
