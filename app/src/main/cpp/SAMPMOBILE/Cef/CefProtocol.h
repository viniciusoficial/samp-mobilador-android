#pragma once

// omp-cef wire protocol contract for the Android client.
// Mirrors aurora-mp/omp-cef src/shared/packet.hpp, events.hpp, version.hpp.
// All multi-byte scalars on the wire are little-endian (the PC implementation
// writes raw host-order memory on x86; ARM Android is also little-endian, but
// this module reads/writes explicitly so behaviour cannot drift).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cefproto
{

// src/shared/version.hpp -> MakeVersionU32(1,3,0,0)
inline constexpr uint8_t VERSION_MAJOR = 1;
inline constexpr uint8_t VERSION_MINOR = 3;
inline constexpr uint8_t VERSION_PATCH = 0;
inline constexpr uint8_t VERSION_BUILD = 0;

inline constexpr uint32_t MakeVersionU32(uint8_t maj, uint8_t min, uint8_t patch, uint8_t build)
{
    return (uint32_t(maj) << 24) | (uint32_t(min) << 16) | (uint32_t(patch) << 8) | uint32_t(build);
}

inline constexpr uint32_t PLUGIN_VERSION_U32 =
    MakeVersionU32(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD);

enum class ArgumentType : uint8_t
{
    String  = 0,
    Integer = 1,
    Float   = 2,
    Bool    = 3,
};

struct Argument
{
    ArgumentType type = ArgumentType::String;
    std::string stringValue;
    int32_t intValue = 0;
    float floatValue = 0.0f;
    bool boolValue = false;

    Argument() = default;
    static Argument FromString(const std::string& v) { Argument a; a.type = ArgumentType::String;  a.stringValue = v; return a; }
    static Argument FromInt(int32_t v)               { Argument a; a.type = ArgumentType::Integer; a.intValue = v;    return a; }
    static Argument FromFloat(float v)               { Argument a; a.type = ArgumentType::Float;   a.floatValue = v;  return a; }
    static Argument FromBool(bool v)                 { Argument a; a.type = ArgumentType::Bool;    a.boolValue = v;   return a; }
};

enum class PacketType : uint8_t
{
    RequestJoin = 0,
    HandshakeChallenge,
    HandshakeFinalize,
    JoinResponse,
    ServerConfig,

    RequestFiles,
    FileData,

    DownloadComplete,
    PressKey,

    EmitEvent,
    EmitBrowserEvent,
    ClientEmitEvent,
};

enum class JoinRejectReason : uint8_t
{
    None = 0,
    VersionMismatch = 1,
};

struct RequestJoinPacket
{
    int32_t playerid = -1;
    uint32_t client_version = 0;
};

struct HandshakeChallengePacket
{
    std::vector<uint8_t> cookie;
    std::vector<uint8_t> server_public_key;
};

struct HandshakeFinalizePacket
{
    std::vector<uint8_t> cookie;
    std::vector<uint8_t> client_public_key;
};

struct JoinResponsePacket
{
    bool accepted = false;
    uint32_t kcp_conv_id = 0;
    std::string manifest_json;

    uint8_t reject_reason = static_cast<uint8_t>(JoinRejectReason::None);
    uint32_t server_version = 0;
    uint32_t client_version = 0;
    std::string message;
};

struct ServerConfigPacket
{
    std::vector<uint8_t> master_resource_key;
    bool resources_loader_ui = true;
};

struct RequestFilesPacket
{
    std::vector<std::pair<std::string, std::string>> files;
};

struct FileDataPacket
{
    std::string resourceName;
    std::string relativePath;
    std::string fileHash;
    uint32_t chunkIndex = 0;
    uint32_t totalChunks = 0;
    std::vector<uint8_t> data;
};

struct EmitEventPacket
{
    int32_t browserId = 0;
    std::string name;
    std::vector<Argument> args;
};

using ClientEmitEventPacket = EmitEventPacket;

// Tagged payload. Exactly one member is meaningful per PacketType; kept as
// plain members (not std::variant) to stay POD-friendly and exception-free.
struct NetworkPacket
{
    PacketType type = PacketType::RequestJoin;

    RequestJoinPacket requestJoin;
    HandshakeChallengePacket handshakeChallenge;
    HandshakeFinalizePacket handshakeFinalize;
    JoinResponsePacket joinResponse;
    ServerConfigPacket serverConfig;
    RequestFilesPacket requestFiles;
    FileDataPacket fileData;
    EmitEventPacket emitEvent;
};

// Serialize/Deserialize. Return false on malformed input; serialization
// mirrors src/shared/packet-serializer.hpp byte for byte, including the
// 4096-byte string cap and the DownloadComplete default-payload quirk
// (serialized as a default RequestJoinPacket body: 8 zero/ff bytes).
bool SerializePacket(const NetworkPacket& packet, std::vector<uint8_t>& out);
bool DeserializePacket(const uint8_t* data, size_t size, NetworkPacket& out);

// Event name constants (src/shared/events.hpp)
namespace ServerEvent
{
    inline constexpr const char* CreateBrowser        = "CreateBrowser";
    inline constexpr const char* CreateWorldBrowser   = "CreateWorldBrowser";
    inline constexpr const char* CreateWorld2DBrowser = "CreateWorld2DBrowser";
    inline constexpr const char* SetWorld2DBrowserPos = "SetWorld2DBrowserPos";
    inline constexpr const char* SetBrowserVisible    = "SetBrowserVisible";
    inline constexpr const char* SetBrowserLayer      = "SetBrowserLayer";
    inline constexpr const char* DestroyBrowser       = "DestroyBrowser";
    inline constexpr const char* ReloadBrowser        = "ReloadBrowser";
    inline constexpr const char* FocusBrowser         = "FocusBrowser";
    inline constexpr const char* LoadUrl              = "LoadUrl";
    inline constexpr const char* EnableDevTools       = "EnableDevTools";
    inline constexpr const char* AttachBrowserToObject   = "AttachBrowserToObject";
    inline constexpr const char* DetachBrowserFromObject = "DetachBrowserFromObject";
    inline constexpr const char* MuteBrowser          = "MuteBrowser";
    inline constexpr const char* SetAudioMode         = "SetAudioMode";
    inline constexpr const char* SetAudioSettings     = "SetAudioSettings";
    inline constexpr const char* ToggleHudComponent   = "ToggleHudComponent";
    inline constexpr const char* ToggleSpawnScreen    = "ToggleSpawnScreen";
    inline constexpr const char* ClearChat            = "ClearChat";
    inline constexpr const char* ToggleChatInput      = "ToggleChatInput";
    inline constexpr const char* SetKeyCapture        = "SetKeyCapture";
    inline constexpr const char* EnableKey            = "EnableKey";
    inline constexpr const char* ExitGame             = "ExitGame";
    inline constexpr const char* SetEscapeMenuMode    = "SetEscapeMenuMode";
    inline constexpr const char* SetPlayerListMode    = "SetPlayerListMode";
}

namespace ClientEvent
{
    inline constexpr const char* BrowserCreateResult = "BrowserCreateResult";
    inline constexpr const char* DownloadStart       = "DownloadStart";
    inline constexpr const char* DownloadFinish      = "DownloadFinish";
    inline constexpr const char* PressKey            = "PressKey";
    inline constexpr const char* ChatInputState      = "ChatInputState";
}

} // namespace cefproto
