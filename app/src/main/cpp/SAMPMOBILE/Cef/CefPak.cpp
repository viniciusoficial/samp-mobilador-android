#include "CefPak.h"

#include <zlib.h>

#include <cstring>

namespace cefpak
{
namespace
{

constexpr uint32_t kEocdSig     = 0x06054b50;
constexpr uint32_t kCentralSig  = 0x02014b50;
constexpr uint32_t kLocalSig    = 0x04034b50;

uint16_t ReadU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t ReadU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void Fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

bool InflateRaw(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out)
{
    z_stream stream{};
    // -MAX_WBITS: raw DEFLATE, no zlib/gzip wrapper (what ZIP method 8 stores).
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return false;

    stream.next_in = const_cast<Bytef*>(src);
    stream.avail_in = static_cast<uInt>(srcSize);
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());

    const int rc = inflate(&stream, Z_FINISH);
    const size_t produced = out.size() - stream.avail_out;
    inflateEnd(&stream);

    if (rc != Z_STREAM_END)
        return false;

    out.resize(produced);
    return true;
}

} // namespace

bool IsSafeEntryName(const std::string& path)
{
    if (path.empty() || path.size() > MAX_PATH_BYTES)
        return false;
    if (path.front() == '/' || path.find('\\') != std::string::npos)
        return false;

    size_t start = 0;
    while (true)
    {
        const size_t end = path.find('/', start);
        const size_t len = end == std::string::npos ? std::string::npos : end - start;

        if (len == 0) // "a//b" or trailing '/'
            return false;

        const std::string component = path.substr(start, len);
        if (component == "." || component == "..")
            return false;

        for (const char ch : component)
        {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 0x20 || c == 0x7F)
                return false;
            if (ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
                ch == '<' || ch == '>' || ch == '|')
                return false;
        }

        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    return true;
}

bool ReadArchive(const std::vector<uint8_t>& pak,
                 std::vector<Entry>& out,
                 std::string* error)
{
    out.clear();

    if (pak.size() < 22)
    {
        Fail(error, "archive too small");
        return false;
    }

    // End of central directory: scan backwards over the optional comment.
    size_t eocd = std::string::npos;
    const size_t lowest = pak.size() >= (22 + 65535) ? pak.size() - (22 + 65535) : 0;
    for (size_t i = pak.size() - 22 + 1; i-- > lowest;)
    {
        if (ReadU32(&pak[i]) == kEocdSig)
        {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos)
    {
        Fail(error, "end of central directory not found");
        return false;
    }

    const uint16_t entryCount = ReadU16(&pak[eocd + 10]);
    const uint32_t cdSize     = ReadU32(&pak[eocd + 12]);
    const uint32_t cdOffset   = ReadU32(&pak[eocd + 16]);

    if (entryCount == 0xFFFF || cdOffset == 0xFFFFFFFFu || cdSize == 0xFFFFFFFFu)
    {
        Fail(error, "ZIP64 archives are not supported");
        return false;
    }
    if (entryCount == 0)
    {
        Fail(error, "archive has no entries");
        return false;
    }
    uint64_t totalUncompressed = 0;
    if (entryCount > MAX_ENTRY_COUNT)
    {
        Fail(error, "too many entries");
        return false;
    }
    if (static_cast<uint64_t>(cdOffset) + cdSize > pak.size())
    {
        Fail(error, "central directory out of bounds");
        return false;
    }

    size_t cursor = cdOffset;
    out.reserve(entryCount);

    for (uint16_t i = 0; i < entryCount; ++i)
    {
        if (cursor + 46 > pak.size() || ReadU32(&pak[cursor]) != kCentralSig)
        {
            Fail(error, "bad central directory entry");
            return false;
        }

        const uint16_t flags       = ReadU16(&pak[cursor + 8]);
        const uint16_t method      = ReadU16(&pak[cursor + 10]);
        const uint32_t compSize    = ReadU32(&pak[cursor + 20]);
        const uint32_t uncompSize  = ReadU32(&pak[cursor + 24]);
        const uint16_t nameLen     = ReadU16(&pak[cursor + 28]);
        const uint16_t extraLen    = ReadU16(&pak[cursor + 30]);
        const uint16_t commentLen  = ReadU16(&pak[cursor + 32]);
        const uint32_t localOffset = ReadU32(&pak[cursor + 42]);

        if (cursor + 46 + nameLen > pak.size())
        {
            Fail(error, "truncated central directory entry");
            return false;
        }

        const std::string name(reinterpret_cast<const char*>(&pak[cursor + 46]), nameLen);

        if ((flags & 0x0001) != 0)
        {
            Fail(error, "encrypted zip entries are not supported: " + name);
            return false;
        }
        if (!IsSafeEntryName(name))
        {
            Fail(error, "unsafe entry name: " + name);
            return false;
        }
        if (uncompSize > MAX_ENTRY_BYTES)
        {
            Fail(error, "entry too large: " + name);
            return false;
        }
        if (totalUncompressed + uncompSize > MAX_TOTAL_UNCOMPRESSED_BYTES)
        {
            Fail(error, "archive expands beyond total limit (zip bomb?)");
            return false;
        }
        totalUncompressed += uncompSize;
        if (static_cast<uint64_t>(localOffset) + 30 > pak.size() ||
            ReadU32(&pak[localOffset]) != kLocalSig)
        {
            Fail(error, "bad local header for " + name);
            return false;
        }

        const uint16_t localNameLen  = ReadU16(&pak[localOffset + 26]);
        const uint16_t localExtraLen = ReadU16(&pak[localOffset + 28]);
        const uint64_t dataOffset = static_cast<uint64_t>(localOffset) + 30 +
                                    localNameLen + localExtraLen;

        // Central-directory sizes are authoritative; the local header may carry
        // zeros when a data descriptor is used (general purpose flag bit 3).
        if (dataOffset + compSize > pak.size())
        {
            Fail(error, "entry data out of bounds: " + name);
            return false;
        }

        Entry entry;
        entry.name = name;

        if (method == 0)
        {
            entry.data.assign(pak.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                              pak.begin() + static_cast<std::ptrdiff_t>(dataOffset + compSize));
            if (entry.data.size() != uncompSize)
            {
                Fail(error, "stored size mismatch: " + name);
                return false;
            }
        }
        else if (method == 8)
        {
            entry.data.resize(uncompSize);
            if (uncompSize == 0)
            {
                entry.data.clear();
            }
            else if (!InflateRaw(&pak[static_cast<size_t>(dataOffset)], compSize, entry.data))
            {
                Fail(error, "inflate failed: " + name);
                return false;
            }
        }
        else
        {
            Fail(error, "unsupported compression method " + std::to_string(method) + ": " + name);
            return false;
        }

        out.push_back(std::move(entry));
        cursor += 46u + nameLen + extraLen + commentLen;
    }

    return true;
}

} // namespace cefpak
