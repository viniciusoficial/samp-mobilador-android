#include "CefProtocol.h"

#include <algorithm>
#include <cstring>

namespace cefproto
{

namespace
{

constexpr size_t MAX_STRING_BYTES = 4096; // matches WriteString() truncation

class ByteWriter
{
public:
    void PutU8(uint8_t v) { buf_.push_back(v); }

    void PutRaw(const void* p, size_t n)
    {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }

    void PutU16(uint16_t v) { PutRaw(&v, sizeof(v)); }
    void PutU32(uint32_t v) { PutRaw(&v, sizeof(v)); }
    void PutI32(int32_t v)  { PutRaw(&v, sizeof(v)); }
    void PutF32(float v)    { PutRaw(&v, sizeof(v)); }

    void PutString(const std::string& s)
    {
        // 先按 size_t 截断再转 uint16_t：原来先 static_cast<uint16_t> 再比较，
        // 长度 65536 会被截成 0（写出"空串"），65537 会变成 1，全是错的线上数据。
        const size_t len = s.length() > MAX_STRING_BYTES ? MAX_STRING_BYTES : s.length();
        PutU16(static_cast<uint16_t>(len));
        if (len > 0)
            PutRaw(s.data(), len);
    }

    void PutBytes(const std::vector<uint8_t>& bytes)
    {
        PutU32(static_cast<uint32_t>(bytes.size()));
        if (!bytes.empty())
            PutRaw(bytes.data(), bytes.size());
    }

    const std::vector<uint8_t>& Data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader
{
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool GetU8(uint8_t& v)
    {
        if (pos_ + 1 > size_) return false;
        v = data_[pos_++];
        return true;
    }

    bool GetRaw(void* out, size_t n)
    {
        if (pos_ + n > size_) return false;
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    bool GetU16(uint16_t& v) { return GetRaw(&v, sizeof(v)); }
    bool GetU32(uint32_t& v) { return GetRaw(&v, sizeof(v)); }
    bool GetI32(int32_t& v)  { return GetRaw(&v, sizeof(v)); }
    bool GetF32(float& v)    { return GetRaw(&v, sizeof(v)); }

    bool GetString(std::string& out)
    {
        uint16_t len = 0;
        if (!GetU16(len)) return false;
        if (len == 0) { out.clear(); return true; }
        // 用减法判断剩余量：pos_ + len 在 32 位目标上可能回绕，
        // 从而让一个超大 len 绕过边界检查（越界读）。
        if (len > size_ - pos_) return false;
        out.assign(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return true;
    }

    bool GetBytes(std::vector<uint8_t>& out)
    {
        uint32_t len = 0;
        if (!GetU32(len)) return false;
        if (len == 0) { out.clear(); return true; }
        // 同上：减法形式对 32 位安全
        if (len > size_ - pos_) return false;
        out.assign(data_ + pos_, data_ + pos_ + len);
        pos_ += len;
        return true;
    }

    size_t Remaining() const { return size_ - pos_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

void WriteArgumentList(ByteWriter& w, const std::vector<Argument>& args)
{
    w.PutU8(static_cast<uint8_t>(args.size()));
    for (const Argument& a : args)
    {
        w.PutU8(static_cast<uint8_t>(a.type));
        switch (a.type)
        {
            case ArgumentType::String:  w.PutString(a.stringValue); break;
            case ArgumentType::Integer: w.PutI32(a.intValue);       break;
            case ArgumentType::Float:   w.PutF32(a.floatValue);     break;
            case ArgumentType::Bool:    w.PutU8(a.boolValue ? 1 : 0); break;
        }
    }
}

bool ReadArgumentList(ByteReader& r, std::vector<Argument>& args)
{
    uint8_t count = 0;
    if (!r.GetU8(count)) return false;

    args.clear();
    args.reserve(count);

    for (uint8_t i = 0; i < count; ++i)
    {
        uint8_t type = 0;
        if (!r.GetU8(type)) return false;

        Argument a;
        a.type = static_cast<ArgumentType>(type);
        switch (a.type)
        {
            case ArgumentType::String:
                if (!r.GetString(a.stringValue)) return false;
                break;
            case ArgumentType::Integer:
                if (!r.GetI32(a.intValue)) return false;
                break;
            case ArgumentType::Float:
                if (!r.GetF32(a.floatValue)) return false;
                break;
            case ArgumentType::Bool: {
                uint8_t b = 0;
                if (!r.GetU8(b)) return false;
                a.boolValue = (b != 0);
                break;
            }
            default:
                // Unknown argument type: the PC deserializer would read
                // nothing for it and accept the packet; doing the same keeps
                // the stream position identical.
                break;
        }
        args.push_back(std::move(a));
    }
    return true;
}

void WriteEmitEvent(ByteWriter& w, const EmitEventPacket& e)
{
    w.PutI32(e.browserId);
    w.PutString(e.name);
    WriteArgumentList(w, e.args);
}

bool ReadEmitEvent(ByteReader& r, EmitEventPacket& e)
{
    if (!r.GetI32(e.browserId)) return false;
    if (!r.GetString(e.name)) return false;
    return ReadArgumentList(r, e.args);
}

} // namespace

bool SerializePacket(const NetworkPacket& packet, std::vector<uint8_t>& out)
{
    ByteWriter w;
    w.PutU8(static_cast<uint8_t>(packet.type));

    switch (packet.type)
    {
        case PacketType::RequestJoin:
            w.PutI32(packet.requestJoin.playerid);
            w.PutU32(packet.requestJoin.client_version);
            break;

        case PacketType::HandshakeChallenge:
            w.PutBytes(packet.handshakeChallenge.cookie);
            w.PutBytes(packet.handshakeChallenge.server_public_key);
            break;

        case PacketType::HandshakeFinalize:
            w.PutBytes(packet.handshakeFinalize.cookie);
            w.PutBytes(packet.handshakeFinalize.client_public_key);
            break;

        case PacketType::JoinResponse: {
            const JoinResponsePacket& p = packet.joinResponse;
            w.PutU8(p.accepted ? 1 : 0);
            w.PutU32(p.kcp_conv_id);
            w.PutString(p.manifest_json);
            w.PutU8(p.reject_reason);
            w.PutU32(p.server_version);
            w.PutU32(p.client_version);
            w.PutString(p.message);
            break;
        }

        case PacketType::ServerConfig:
            w.PutBytes(packet.serverConfig.master_resource_key);
            w.PutU8(packet.serverConfig.resources_loader_ui ? 1 : 0);
            break;

        case PacketType::RequestFiles: {
            const RequestFilesPacket& p = packet.requestFiles;
            // 先过滤再计数：原来先写 files.size() 再在循环里 continue 跳过空项，
            // 线上"声明 N 条、实际 N-k 条"，对端按 count 读会错位甚至读越界。
            std::vector<const std::pair<std::string, std::string>*> valid;
            valid.reserve(p.files.size());
            for (const auto& entry : p.files)
            {
                if (entry.first.empty() || entry.second.empty())
                    continue;
                valid.push_back(&entry);
            }
            w.PutU16(static_cast<uint16_t>(valid.size()));
            for (const auto* entry : valid)
            {
                w.PutString(entry->first);
                w.PutString(entry->second);
            }
            break;
        }

        case PacketType::FileData: {
            const FileDataPacket& p = packet.fileData;
            w.PutString(p.resourceName);
            w.PutString(p.relativePath);
            w.PutString(p.fileHash);
            w.PutU32(p.chunkIndex);
            w.PutU32(p.totalChunks);
            w.PutBytes(p.data);
            break;
        }

        case PacketType::DownloadComplete:
            // PC sends SendPacket(PacketType::DownloadComplete, {}): the
            // default std::variant payload is RequestJoinPacket{} and the
            // serializer emits its body. Reproduce those exact 8 bytes.
            w.PutI32(-1);
            w.PutU32(0);
            break;

        case PacketType::EmitEvent:
        case PacketType::EmitBrowserEvent:
        case PacketType::ClientEmitEvent:
            WriteEmitEvent(w, packet.emitEvent);
            break;

        case PacketType::PressKey:
            // PressKey travels as a ClientEmitEvent on the wire in omp-cef;
            // the bare packet type has no body serializer. Emit type byte only.
            break;
    }

    out = w.Data();
    return true;
}

bool DeserializePacket(const uint8_t* data, size_t size, NetworkPacket& out)
{
    if (!data || size == 0)
        return false;

    ByteReader r(data, size);

    uint8_t typeVal = 0;
    if (!r.GetU8(typeVal))
        return false;

    out.type = static_cast<PacketType>(typeVal);

    switch (out.type)
    {
        case PacketType::RequestJoin:
            if (!r.GetI32(out.requestJoin.playerid)) return false;
            out.requestJoin.client_version = 0;
            if (r.Remaining() >= sizeof(uint32_t))
            {
                if (!r.GetU32(out.requestJoin.client_version))
                    out.requestJoin.client_version = 0;
            }
            break;

        case PacketType::HandshakeChallenge:
            if (!r.GetBytes(out.handshakeChallenge.cookie)) return false;
            if (!r.GetBytes(out.handshakeChallenge.server_public_key)) return false;
            break;

        case PacketType::HandshakeFinalize:
            if (!r.GetBytes(out.handshakeFinalize.cookie)) return false;
            if (!r.GetBytes(out.handshakeFinalize.client_public_key)) return false;
            break;

        case PacketType::JoinResponse: {
            JoinResponsePacket& p = out.joinResponse;
            uint8_t accepted = 0;
            if (!r.GetU8(accepted)) return false;
            p.accepted = (accepted != 0);
            if (!r.GetU32(p.kcp_conv_id)) return false;
            if (!r.GetString(p.manifest_json)) return false;

            p.reject_reason = static_cast<uint8_t>(JoinRejectReason::None);
            p.server_version = 0;
            p.client_version = 0;
            p.message.clear();

            // Optional trailing fields (added in later protocol revisions);
            // mirror the PC deserializer's in_avail() checks.
            if (r.Remaining() > 0)
            {
                uint8_t reason = 0;
                if (r.GetU8(reason))
                {
                    p.reject_reason = reason;
                    if (r.Remaining() >= sizeof(uint32_t))
                        r.GetU32(p.server_version);
                    if (r.Remaining() >= sizeof(uint32_t))
                        r.GetU32(p.client_version);
                    if (r.Remaining() >= 2)
                        r.GetString(p.message);
                }
            }
            break;
        }

        case PacketType::ServerConfig:
            if (!r.GetBytes(out.serverConfig.master_resource_key)) return false;
            {
                uint8_t b = 0;
                if (!r.GetU8(b)) return false;
                out.serverConfig.resources_loader_ui = (b != 0);
            }
            break;

        case PacketType::RequestFiles: {
            uint16_t count = 0;
            if (!r.GetU16(count)) return false;
            out.requestFiles.files.clear();
            out.requestFiles.files.reserve(count);
            for (uint16_t i = 0; i < count; ++i)
            {
                std::string resourceName, relativePath;
                if (!r.GetString(resourceName)) return false;
                if (!r.GetString(relativePath)) return false;
                out.requestFiles.files.emplace_back(std::move(resourceName), std::move(relativePath));
            }
            break;
        }

        case PacketType::FileData: {
            FileDataPacket& p = out.fileData;
            if (!r.GetString(p.resourceName)) return false;
            if (!r.GetString(p.relativePath)) return false;
            if (!r.GetString(p.fileHash)) return false;
            if (!r.GetU32(p.chunkIndex)) return false;
            if (!r.GetU32(p.totalChunks)) return false;
            if (!r.GetBytes(p.data)) return false;
            break;
        }

        case PacketType::EmitEvent:
        case PacketType::EmitBrowserEvent:
        case PacketType::ClientEmitEvent:
            if (!ReadEmitEvent(r, out.emitEvent)) return false;
            break;

        case PacketType::DownloadComplete:
        case PacketType::PressKey:
            // No payload deserializer on the PC side either; the type byte is
            // the entire meaningful content.
            break;

        default:
            // 未知类型：原来会掉出 switch 直接 return true，上层拿到一个
            // 只有 type 被赋值、载荷全空的 packet，行为不可预期。明确拒绝。
            // 刻意不调 android_log_print：本文件必须保持"纯逻辑"，
            // 能被 tools/cef_kat 离线编译运行（与 CefLogic.java 同一约束）。
            return false;
    }

    return true;
}

} // namespace cefproto
