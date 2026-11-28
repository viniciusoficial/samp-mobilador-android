#include "CefResources.h"

#include "CefCrypto.h"
#include "CefJson.h"
#include "CefPak.h"

#include <android/log.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <thread>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define CEFR_LOG_TAG "CefResources"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  CEFR_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  CEFR_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CEFR_LOG_TAG, __VA_ARGS__)

namespace cefres
{
namespace
{

// resource_manager.cpp limits.
constexpr size_t kMaxResourceFileSize = 512ull * 1024 * 1024;
constexpr size_t kMinChunkBytes = 64;
constexpr uint64_t kStallWarnMs = 15000;
// How many times a single file may be re-requested after a failed integrity
// check or extraction before the client gives up and just logs it.
constexpr int kMaxRetries = 2;

uint64_t NowMs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool EnsureDir(const std::string& path)
{
    if (path.empty())
        return false;

    std::string current;
    current.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i)
    {
        current.push_back(path[i]);
        if (path[i] == '/' && current.size() > 1)
        {
            const std::string partial = current.substr(0, current.size() - 1);
            if (!partial.empty() && mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST)
                return false;
        }
    }
    if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
        return false;

    struct stat st{};
    return stat(current.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string ParentDir(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

bool WriteFileAtomic(const std::string& path, const uint8_t* data, size_t size)
{
    if (!EnsureDir(ParentDir(path)))
    {
        LOGE("[CefResources] cannot create parent of %s", path.c_str());
        return false;
    }

    const std::string tmp = path + ".tmp";
    FILE* file = std::fopen(tmp.c_str(), "wb");
    if (!file)
    {
        LOGE("[CefResources] cannot open %s (%s)", tmp.c_str(), std::strerror(errno));
        return false;
    }
    const size_t written = size ? std::fwrite(data, 1, size, file) : 0;
    std::fclose(file);

    if (written != size)
    {
        LOGE("[CefResources] short write to %s", tmp.c_str());
        unlink(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        LOGE("[CefResources] cannot rename %s (%s)", tmp.c_str(), std::strerror(errno));
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& data)
{
    return WriteFileAtomic(path, data.data(), data.size());
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out, size_t maxBytes)
{
    out.clear();
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return false;
    if (static_cast<uint64_t>(st.st_size) > maxBytes)
    {
        LOGE("[CefResources] %s is too large (%lld bytes)", path.c_str(), static_cast<long long>(st.st_size));
        return false;
    }

    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;
    out.resize(static_cast<size_t>(st.st_size));
    const size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    if (got != out.size())
    {
        out.clear();
        return false;
    }
    return true;
}

bool FileExists(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void RemoveTree(const std::string& path)
{
    DIR* dir = opendir(path.c_str());
    if (!dir)
    {
        unlink(path.c_str());
        return;
    }
    while (dirent* entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        const std::string child = path + "/" + name;
        struct stat st{};
        if (stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            RemoveTree(child);
        else
            unlink(child.c_str());
    }
    closedir(dir);
    rmdir(path.c_str());
}

std::string FileKey(const std::string& resourceName, const std::string& relativePath)
{
    return resourceName + "/" + relativePath;
}

bool IsSafeRelativePath(const std::string& path)
{
    // Same rules as the PC cache resolver, minus the ".pak" requirement: the
    // manifest paths are always paks, but the check is used for cache joins.
    if (path.empty() || path.size() > 200)
        return false;
    if (path.front() == '/' || path.front() == '\\')
        return false;

    size_t start = 0;
    while (true)
    {
        const size_t end = path.find_first_of("/\\", start);
        const std::string component = path.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (component.empty() || component == "." || component == "..")
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

bool EndsWithPak(const std::string& path)
{
    if (path.size() <= 4)
        return false;
    std::string tail = path.substr(path.size() - 4);
    for (auto& c : tail)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return tail == ".pak";
}

} // namespace

CefResources& CefResources::Get()
{
    static CefResources instance;
    return instance;
}

void CefResources::SetSender(Sender sender)
{
    sender_ = std::move(sender);
}

void CefResources::SetReadyHandler(ReadyHandler handler)
{
    readyHandler_ = std::move(handler);
}

void CefResources::SetStorageRoot(const std::string& root)
{
    if (root.empty())
        return;
    storageRoot_ = root;
    cacheDir_ = storageRoot_ + "/cache/" + serverId_;
    LOGI("[CefResources] storage root %s", storageRoot_.c_str());
}

std::string CefResources::ResourceRoot() const
{
    return storageRoot_.empty() ? std::string() : storageRoot_ + "/resources";
}

void CefResources::SetServerIdentity(const std::string& host, uint16_t port)
{
    const std::string id = host + "_" + std::to_string(port);
    if (id == serverId_)
        return;
    serverId_ = id;
    cacheDir_ = storageRoot_.empty() ? std::string() : storageRoot_ + "/cache/" + serverId_;
    LOGI("[CefResources] server bucket %s", serverId_.c_str());
}

const char* CefResources::StateName() const
{
    switch (state_)
    {
        case State::Idle:            return "idle";
        case State::AwaitingTrigger: return "awaiting-trigger";
        case State::VerifyingCache:  return "verifying-cache";
        case State::Downloading:     return "downloading";
        case State::Completed:       return "completed";
    }
    return "unknown";
}

void CefResources::OnJoinResponse(const std::string& manifestJson)
{
    manifestJson_ = manifestJson;
    if (manifestJson_.empty())
        manifestJson_ = "{}";
    LOGI("[CefResources] manifest %zu bytes", manifestJson_.size());
}

void CefResources::OnServerConfig(const std::vector<uint8_t>& masterKey)
{
    masterKey_ = masterKey;
    LOGI("[CefResources] master key %zu bytes", masterKey_.size());
    MarkReadyToDownload();
}

void CefResources::MarkReadyToDownload()
{
    if (state_ != State::Idle)
        return;
    state_ = State::AwaitingTrigger;
    LOGI("[CefResources] ready to download");
}

void CefResources::OnSessionLost()
{
    if (state_ != State::Idle)
        LOGI("[CefResources] session lost in state '%s' - resetting", StateName());
    ResetState();
}

void CefResources::ResetState()
{
    state_ = State::Idle;
    manifestJson_.clear();
    masterKey_.clear();
    requested_.clear();
    expected_.clear();
    assembling_.clear();
    needRequest_.clear();
    retries_.clear();
    ++generation_;
    downloadCompleteSent_ = false;
    completedFiles_ = 0;
    totalFiles_ = 0;
    pendingCacheChecks_ = 0;
    pendingExtractions_ = 0;

    // Drop anything still waiting in the worker queue: it belongs to the
    // session that just went away. A job already executing cannot be stopped,
    // but its result is discarded by the generation check in DrainResults().
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        workQueue_.clear();
    }
}

void CefResources::EnsureWorker()
{
    if (workerStarted_)
        return;
    workerStarted_ = true;
    worker_ = std::thread([this] { WorkerMain(); });
    LOGI("[CefResources] worker thread started");
}

void CefResources::Tick()
{
    // Always drain first: results may complete the last file and unblock the
    // ready handler, independently of which state we are in.
    if (workerStarted_)
        DrainResults();

    if (state_ == State::VerifyingCache)
    {
        // DrainResults() calls FinishCacheVerification() when the counter hits
        // zero; nothing else to do while cache hashes are still running.
        return;
    }

    if (state_ != State::AwaitingTrigger)
    {
        if (state_ == State::Downloading && lastActivityMs_ != 0)
        {
            const uint64_t now = NowMs();
            if (now - lastActivityMs_ > kStallWarnMs && now - lastStallLogMs_ > kStallWarnMs)
            {
                lastStallLogMs_ = now;
                LOGW("[CefResources] no FileData for %llums (%zu/%zu files done, state=%s)",
                     static_cast<unsigned long long>(now - lastActivityMs_),
                     completedFiles_, totalFiles_, StateName());
            }
        }
        return;
    }

    if (storageRoot_.empty() || masterKey_.empty() || !sender_)
        return;

    TriggerDownload();
}

void CefResources::TriggerDownload()
{
    EnsureWorker();
    state_ = State::VerifyingCache;

    cefjson::Value manifest;
    std::string error;
    if (!cefjson::Parse(manifestJson_, manifest, &error))
    {
        LOGE("[CefResources] manifest parse failed (%s) - treating as empty", error.c_str());
        manifest = cefjson::Value();
        manifest.type = cefjson::Value::Type::Object;
    }
    if (!manifest.IsObject())
    {
        LOGE("[CefResources] manifest is not a JSON object - treating as empty");
        manifest = cefjson::Value();
        manifest.type = cefjson::Value::Type::Object;
    }

    const std::string resourcesRoot = ResourceRoot();
    requested_.clear();
    expected_.clear();
    assembling_.clear();
    needRequest_.clear();
    retries_.clear();
    ++generation_;
    completedFiles_ = 0;
    totalFiles_ = 0;
    pendingCacheChecks_ = 0;
    pendingExtractions_ = 0;

    // Discard jobs left over from a previous trigger before queueing this one.
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        workQueue_.clear();
    }

    for (const auto& [resourceName, files] : manifest.object)
    {
        if (!files.IsArray())
        {
            LOGE("[CefResources] ignoring resource '%s': manifest entry is not an array",
                 resourceName.c_str());
            continue;
        }
        if (!cefpak::IsSafeEntryName(resourceName))
        {
            LOGE("[CefResources] refusing unsafe resource name '%s'", resourceName.c_str());
            continue;
        }

        for (const auto& entry : files.array)
        {
            const cefjson::Value* pathValue = entry.Find("path");
            const cefjson::Value* hashValue = entry.Find("hash");
            const cefjson::Value* sizeValue = entry.Find("size");
            if (!pathValue || !hashValue || !sizeValue ||
                !pathValue->IsString() || !hashValue->IsString() || !sizeValue->IsNumber())
            {
                LOGE("[CefResources] ignoring malformed file entry in '%s'", resourceName.c_str());
                continue;
            }

            const std::string path = pathValue->stringValue;
            const std::string hash = hashValue->stringValue;
            const uint64_t size = static_cast<uint64_t>(sizeValue->AsInt(0));

            if (!IsSafeRelativePath(path) || !EndsWithPak(path))
            {
                LOGE("[CefResources] refusing unsafe resource path '%s'", path.c_str());
                continue;
            }
            if (size == 0 || size > kMaxResourceFileSize)
            {
                LOGE("[CefResources] refusing '%s' with invalid size %llu",
                     path.c_str(), static_cast<unsigned long long>(size));
                continue;
            }

            ExpectedFile expected;
            expected.resourceName = resourceName;
            expected.relativePath = path;
            expected.hash = hash;
            expected.size = static_cast<size_t>(size);
            expected.savePath = cacheDir_ + "/" + path;
            expected.extractDir = resourcesRoot + "/" + resourceName;

            ++totalFiles_;

            const std::string key = FileKey(resourceName, path);
            expected_[key] = expected;

            // Cache check: a matching pak on disk means the decrypted tree can
            // be rebuilt locally without asking the server for anything. The
            // hash + unzip + decrypt work is heavy, so hand it to the worker
            // and finalise in DrainResults() once every result is back.
            if (!FileExists(expected.savePath))
            {
                needRequest_.insert(key);
                continue;
            }

            WorkItem item;
            item.key = key;
            item.generation = generation_;
            item.cacheVerify = true;
            item.pakPath = expected.savePath;
            item.hash = expected.hash;
            item.size = expected.size;
            item.savePath = expected.savePath;
            item.extractDir = expected.extractDir;
            item.masterKey = masterKey_;
            ++pendingCacheChecks_;
            EnqueueWork(std::move(item));
        }
    }

    if (pendingCacheChecks_ == 0)
        FinishCacheVerification();
    else
        LOGI("[CefResources] verifying %zu cached file(s) on the worker thread (%zu/%zu total)",
             pendingCacheChecks_, pendingCacheChecks_, totalFiles_);
}

// Runs on the game thread once every cache-verify job has reported back.
void CefResources::FinishCacheVerification()
{
    if (needRequest_.empty())
    {
        LOGI("[CefResources] all %zu file(s) already cached under %s",
             totalFiles_, ResourceRoot().c_str());
        state_ = State::Completed;
        SendDownloadComplete();
        if (readyHandler_)
            readyHandler_();
        return;
    }

    std::vector<std::string> keys(needRequest_.begin(), needRequest_.end());
    RequestFiles(keys);
}

void CefResources::RequestFiles(const std::vector<std::string>& keys)
{
    if (keys.empty())
    {
        CompleteIfDone();
        return;
    }

    cefproto::NetworkPacket packet;
    packet.type = cefproto::PacketType::RequestFiles;
    packet.requestFiles.files.reserve(keys.size());
    for (const auto& key : keys)
    {
        const auto it = expected_.find(key);
        if (it == expected_.end())
            continue;
        packet.requestFiles.files.emplace_back(it->second.resourceName, it->second.relativePath);
        requested_.push_back(it->second);
    }

    if (packet.requestFiles.files.empty())
    {
        CompleteIfDone();
        return;
    }

    if (!sender_(packet))
    {
        LOGE("[CefResources] cannot send RequestFiles - retrying on next tick");
        // Keep the keys so the next AwaitingTrigger pass retries them instead
        // of silently forgetting a file we know we need.
        for (const auto& key : keys)
            needRequest_.insert(key);
        requested_.clear();
        state_ = State::AwaitingTrigger;
        return;
    }

    if (state_ != State::Downloading)
        SendClientEvent(cefproto::ClientEvent::DownloadStart);

    state_ = State::Downloading;
    lastActivityMs_ = NowMs();
    lastStallLogMs_ = 0;
    LOGI("[CefResources] requested %zu of %zu file(s):", packet.requestFiles.files.size(), totalFiles_);
    for (const auto& file : requested_)
        LOGI("[CefResources]   %s/%s (%zu bytes)", file.resourceName.c_str(),
             file.relativePath.c_str(), file.size);
    requested_.clear();
}

void CefResources::OnFileData(const cefproto::FileDataPacket& packet)
{
    if (state_ != State::Downloading)
    {
        LOGW("[CefResources] FileData in state '%s' - dropped", StateName());
        return;
    }

    lastActivityMs_ = NowMs();

    const std::string key = FileKey(packet.resourceName, packet.relativePath);
    const auto expectedIt = expected_.find(key);
    if (expectedIt == expected_.end())
    {
        LOGE("[CefResources] dropping FileData for unrequested file '%s'", key.c_str());
        return;
    }
    const ExpectedFile& expected = expectedIt->second;

    const size_t maxChunks = expected.size / kMinChunkBytes + 1;
    if (packet.totalChunks == 0 || packet.totalChunks > maxChunks || packet.data.empty())
    {
        LOGE("[CefResources] invalid chunk layout for '%s' (totalChunks=%u, %zu bytes)",
             key.c_str(), packet.totalChunks, packet.data.size());
        return;
    }

    auto& assembly = assembling_[key];
    if (assembly.totalChunks == 0)
    {
        assembly.totalChunks = packet.totalChunks;
        assembly.chunks.resize(packet.totalChunks);
    }
    if (packet.totalChunks != assembly.totalChunks)
    {
        LOGE("[CefResources] chunk count mismatch for '%s': expected %u, got %u",
             key.c_str(), assembly.totalChunks, packet.totalChunks);
        return;
    }
    if (packet.chunkIndex >= assembly.totalChunks)
    {
        LOGE("[CefResources] invalid chunk index %u for '%s'", packet.chunkIndex, key.c_str());
        return;
    }

    if (assembly.chunks[packet.chunkIndex].empty())
    {
        if (packet.data.size() > expected.size - assembly.receivedBytes)
        {
            LOGE("[CefResources] '%s' exceeds its announced size (%zu bytes), aborting",
                 key.c_str(), expected.size);
            assembling_.erase(key);
            return;
        }
        assembly.chunks[packet.chunkIndex] = packet.data;
        ++assembly.receivedChunks;
        assembly.receivedBytes += packet.data.size();
    }

    if (assembly.receivedChunks != assembly.totalChunks)
        return;

    std::vector<uint8_t> complete;
    complete.reserve(assembly.receivedBytes);
    for (const auto& chunk : assembly.chunks)
        complete.insert(complete.end(), chunk.begin(), chunk.end());

    // `assembly` points into assembling_; capture what we still need for the
    // log line *before* the erase invalidates the reference.
    const size_t assembledBytes = assembly.receivedBytes;
    assembling_.erase(key);

    // Everything below is heavy (SHA-256 over the whole pak, atomic write,
    // unzip + AES decrypt of every entry). Do none of it on the game thread:
    // hand the assembled bytes to the worker and let DrainResults() account
    // for the outcome.
    WorkItem item;
    item.key = key;
    item.generation = generation_;
    item.cacheVerify = false;
    item.data = std::move(complete);
    item.hash = expected.hash;
    item.size = expected.size;
    item.savePath = expected.savePath;
    item.extractDir = expected.extractDir;
    item.masterKey = masterKey_;
    ++pendingExtractions_;
    EnqueueWork(std::move(item));

    LOGI("[CefResources] '%s' assembled (%zu bytes) - verifying on worker",
         key.c_str(), assembledBytes);
}

bool CefResources::ExtractPak(const ExpectedFile& file, const std::vector<uint8_t>& pak,
                             const std::vector<uint8_t>& masterKey)
{
    const std::string markerPath = file.extractDir + "/.cefhash";

    // Unchanged pak that was already unpacked in a previous session.
    std::vector<uint8_t> marker;
    if (ReadFile(markerPath, marker, 256))
    {
        const std::string markerHash(marker.begin(), marker.end());
        struct stat st{};
        const bool dirOk = stat(file.extractDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        if (markerHash == file.hash && dirOk)
        {
            LOGI("[CefResources] '%s' already extracted (hash unchanged)", file.resourceName.c_str());
            return true;
        }
    }

    std::vector<cefpak::Entry> entries;
    std::string error;
    if (!cefpak::ReadArchive(pak, entries, &error))
    {
        LOGE("[CefResources] cannot read pak for '%s': %s", file.resourceName.c_str(), error.c_str());
        return false;
    }

    // Swap the whole tree: extract next to it, then replace, so a partial
    // update can never be served to a page.
    const std::string stagingDir = file.extractDir + ".staging";
    RemoveTree(stagingDir);
    if (!EnsureDir(stagingDir))
    {
        LOGE("[CefResources] cannot create %s", stagingDir.c_str());
        return false;
    }

    size_t written = 0;
    size_t skipped = 0;
    for (const auto& entry : entries)
    {
        std::vector<uint8_t> plain;
        if (!cefcrypto::DecodeResourceEntry(entry.data, masterKey, plain))
        {
            LOGW("[CefResources] cannot decode '%s' in '%s' (key %zu bytes)",
                 entry.name.c_str(), file.resourceName.c_str(), masterKey.size());
            ++skipped;
            continue;
        }
        if (!WriteFileAtomic(stagingDir + "/" + entry.name, plain))
        {
            RemoveTree(stagingDir);
            return false;
        }
        ++written;
    }

    if (written == 0)
    {
        LOGE("[CefResources] '%s': no entry could be decoded (%zu skipped)",
             file.resourceName.c_str(), skipped);
        RemoveTree(stagingDir);
        return false;
    }

    if (!WriteFileAtomic(stagingDir + "/.cefhash", reinterpret_cast<const uint8_t*>(file.hash.data()),
                         file.hash.size()))
    {
        RemoveTree(stagingDir);
        return false;
    }

    // Swap the whole tree. The previous tree is parked first, so a failed
    // rename leaves the client with the old (working) files instead of none.
    const std::string previousDir = file.extractDir + ".old";
    RemoveTree(previousDir);

    struct stat existing{};
    const bool hadPrevious = stat(file.extractDir.c_str(), &existing) == 0 &&
                             S_ISDIR(existing.st_mode);
    if (hadPrevious && std::rename(file.extractDir.c_str(), previousDir.c_str()) != 0)
        LOGW("[CefResources] cannot park %s (%s) - replacing in place",
             file.extractDir.c_str(), std::strerror(errno));

    if (std::rename(stagingDir.c_str(), file.extractDir.c_str()) != 0)
    {
        LOGE("[CefResources] cannot move %s into place (%s)", stagingDir.c_str(), std::strerror(errno));
        RemoveTree(stagingDir);
        if (hadPrevious)
            std::rename(previousDir.c_str(), file.extractDir.c_str());
        return false;
    }
    RemoveTree(previousDir);

    LOGI("[CefResources] '%s' extracted: %zu file(s) written, %zu skipped -> %s",
         file.resourceName.c_str(), written, skipped, file.extractDir.c_str());
    return true;
}

void CefResources::CompleteIfDone()
{
    // A file only counts once the worker finished it. Anything still queued
    // (cache hashes or extract/decrypt jobs) means "not done yet".
    if (pendingCacheChecks_ > 0 || pendingExtractions_ > 0)
        return;

    if (completedFiles_ < totalFiles_)
    {
        LOGI("[CefResources] progress %zu/%zu file(s)", completedFiles_, totalFiles_);
        return;
    }

    if (state_ == State::Completed)
        return;

    state_ = State::Completed;
    LOGI("[CefResources] all %zu file(s) ready under %s", totalFiles_, ResourceRoot().c_str());
    SendClientEvent(cefproto::ClientEvent::DownloadFinish);
    SendDownloadComplete();
    if (readyHandler_)
        readyHandler_();
}

void CefResources::SendDownloadComplete()
{
    if (downloadCompleteSent_ || !sender_)
        return;
    downloadCompleteSent_ = true;

    cefproto::NetworkPacket packet;
    packet.type = cefproto::PacketType::DownloadComplete;
    if (sender_(packet))
        LOGI("[CefResources] DownloadComplete sent");
    else
        LOGE("[CefResources] DownloadComplete could not be sent");
}

// DownloadStart / DownloadFinish are the client events the server turns into
// Pawn's OnCefDownloadStart / OnCefDownloadFinish (plugin.cpp). The PC client
// emits them from DownloadDialog::Start/Finish, i.e. only when there is
// something to download and when that download finished - a cache hit stays
// silent, exactly like the PC dialog never opening.
void CefResources::SendClientEvent(const char* name)
{
    if (!sender_)
        return;

    cefproto::NetworkPacket packet;
    packet.type = cefproto::PacketType::ClientEmitEvent;
    packet.emitEvent.name = name;
    if (sender_(packet))
        LOGI("[CefResources] ClientEmitEvent '%s' sent", name);
    else
        LOGE("[CefResources] ClientEmitEvent '%s' could not be sent", name);
}

void CefResources::EnqueueWork(WorkItem item)
{
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        workQueue_.push_back(std::move(item));
    }
    workCv_.notify_one();
}

// Worker thread. Reads only fields carried by the WorkItem plus the pure
// helpers in the anonymous namespace above; it never touches member state, so
// there is nothing to lock beyond the queue itself.
void CefResources::WorkerMain()
{
    while (true)
    {
        WorkItem item;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workCv_.wait(lock, [this] { return workerStop_ || !workQueue_.empty(); });
            if (workerStop_)
                return;
            item = std::move(workQueue_.front());
            workQueue_.pop_front();
        }

        // Rebuild a local ExpectedFile from the WorkItem so ExtractPak needs no
        // access to expected_ (which the game thread owns).
        ExpectedFile file;
        file.hash = item.hash;
        file.size = item.size;
        file.savePath = item.savePath;
        file.extractDir = item.extractDir;
        // resourceName is only used for log lines; derive it from extractDir.
        const size_t slash = item.extractDir.find_last_of('/');
        file.resourceName = slash == std::string::npos ? item.extractDir
                                                       : item.extractDir.substr(slash + 1);

        bool ok = false;
        if (item.cacheVerify)
        {
            // Pak is already on disk: hash it, and only pay for the unzip when
            // it still matches the manifest.
            std::vector<uint8_t> pak;
            if (ReadFile(item.pakPath, pak, kMaxResourceFileSize))
            {
                if (cefcrypto::Sha256Hex(pak.data(), pak.size()) != item.hash)
                {
                    LOGW("[CefResources] cache-verify '%s': hash mismatch, will re-download",
                         item.key.c_str());
                }
                else
                {
                    ok = ExtractPak(file, pak, item.masterKey);
                }
            }
            else
            {
                LOGW("[CefResources] cache-verify '%s': cannot read %s",
                     item.key.c_str(), item.pakPath.c_str());
            }
        }
        else
        {
            // Fresh download assembled by OnFileData: verify + store the pak,
            // then extract. Nothing here ran on the game thread.
            if (item.data.size() == item.size &&
                cefcrypto::Sha256Hex(item.data.data(), item.data.size()) == item.hash)
            {
                if (WriteFileAtomic(item.savePath, item.data))
                {
                    LOGI("[CefResources] '%s' saved (%zu bytes, sha256 ok)",
                         item.key.c_str(), item.data.size());
                    ok = ExtractPak(file, item.data, item.masterKey);
                }
                else
                {
                    LOGE("[CefResources] '%s': cannot write %s",
                         item.key.c_str(), item.savePath.c_str());
                }
            }
            else
            {
                LOGE("[CefResources] '%s': integrity check failed (expected %s / %zu bytes, got %zu bytes)",
                     item.key.c_str(), item.hash.c_str(), item.size, item.data.size());
            }
        }

        // Free the (potentially multi-megabyte) payload before reporting back.
        item.data.clear();
        item.data.shrink_to_fit();
        item.masterKey.clear();
        item.masterKey.shrink_to_fit();

        WorkResult result;
        result.key = std::move(item.key);
        result.generation = item.generation;
        result.ok = ok;
        result.cacheVerify = item.cacheVerify;
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            resultQueue_.push_back(std::move(result));
        }
    }
}

void CefResources::DrainResults()
{
    std::vector<WorkResult> local;
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        local.swap(resultQueue_);
    }
    if (local.empty())
        return;

    for (auto& result : local)
    {
        if (result.generation != generation_)
        {
            // Job was enqueued before the last reset: its counters belong to a
            // session that no longer exists. Never let it touch this one.
            LOGI("[CefResources] dropping stale worker result '%s' (gen %u != %u)",
                 result.key.c_str(), result.generation, generation_);
            continue;
        }

        if (result.cacheVerify)
        {
            pendingCacheChecks_ = pendingCacheChecks_ > 0 ? pendingCacheChecks_ - 1 : 0;
            if (result.ok)
            {
                ++completedFiles_;
                LOGI("[CefResources] cache hit '%s' (%zu/%zu)",
                     result.key.c_str(), completedFiles_, totalFiles_);
            }
            else
            {
                // Corrupted or unreadable cached pak: ask the server for it.
                LOGW("[CefResources] cache miss '%s' - queuing re-download", result.key.c_str());
                needRequest_.insert(result.key);
            }
            continue;
        }

        pendingExtractions_ = pendingExtractions_ > 0 ? pendingExtractions_ - 1 : 0;
        if (result.ok)
        {
            ++completedFiles_;
            LOGI("[CefResources] '%s' ready (%zu/%zu)", result.key.c_str(),
                 completedFiles_, totalFiles_);
        }
        else
        {
            // Integrity or extraction failed. Re-request so a bad transfer does
            // not leave the client stuck waiting forever - but cap the retries,
            // a server that keeps sending a corrupt pak must not loop forever.
            int& tries = retries_[result.key];
            if (tries < kMaxRetries)
            {
                ++tries;
                LOGE("[CefResources] '%s' failed verification - re-requesting (%d/%d)",
                     result.key.c_str(), tries, kMaxRetries);
                needRequest_.insert(result.key);
            }
            else
            {
                LOGE("[CefResources] '%s' failed verification %d times - giving up",
                     result.key.c_str(), tries);
            }
        }
    }

    if (state_ == State::VerifyingCache && pendingCacheChecks_ == 0)
    {
        FinishCacheVerification();
        return;
    }

    // A failed download that was re-queued needs a fresh RequestFiles.
    if (state_ == State::Downloading && !needRequest_.empty() && assembling_.empty()
        && pendingExtractions_ == 0)
    {
        std::vector<std::string> keys(needRequest_.begin(), needRequest_.end());
        needRequest_.clear();
        LOGI("[CefResources] re-requesting %zu failed file(s)", keys.size());
        RequestFiles(keys);
        return;
    }

    if (state_ == State::Downloading)
        CompleteIfDone();
}

void CefResources::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        workerStop_ = true;
    }
    workCv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    workerStarted_ = false;
}

CefResources::~CefResources()
{
    Shutdown();
}

} // namespace cefres
