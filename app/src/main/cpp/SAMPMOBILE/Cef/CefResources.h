#pragma once

// Resource channel (stage 5): turns the omp-cef download protocol into plain
// files under <filesDir>/cef/resources/<resource>/<path>, which is exactly the
// tree CefResourceProvider (Java) serves for http://cef/<resource>/<path>.
//
// Mirrors src/client/core/system/resource_manager.cpp:
//
//   JoinResponse.manifest_json  -> { "<resource>": [ {path,hash,size}, ... ] }
//   ServerConfig.master_resource_key
//   client: RequestFiles([resource, path])        <- only files failing the
//                                                   local SHA-256 cache check
//   server: FileData(chunkIndex/totalChunks/data)  <- 1200-byte chunks
//   client: verify SHA-256 against the *manifest*, store the .pak, unzip every
//           entry, AES-128-CBC decrypt it (OCEF2), write plaintext to disk
//   client: DownloadComplete                       <- once, when nothing is left
//
// Threading: the state machine (manifest walk, chunk assembly, packet sends)
// stays on the game thread, exactly like the PC client. The expensive parts -
// hashing a cached .pak, SHA-256 of a finished download, unzip + AES decrypt +
// file writes - run on a single dedicated worker thread so a multi-megabyte
// resource can never stall a frame. Results come back through a queue that
// Tick() drains on the game thread.

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "CefProtocol.h"

namespace cefres
{

class CefResources
{
public:
    static CefResources& Get();

    // True when the sender returned true and the packet is on the wire.
    using Sender = std::function<bool(const cefproto::NetworkPacket&)>;
    // Fired once the resource tree is usable (all downloads done, or nothing
    // to download). CefBrowser replays its queued CreateBrowser events here.
    using ReadyHandler = std::function<void()>;

    void SetSender(Sender sender);
    void SetReadyHandler(ReadyHandler handler);

    // <filesDir>/cef ; empty until CefManager hands it over from Java.
    void SetStorageRoot(const std::string& root);
    std::string ResourceRoot() const;

    // <host>_<port> cache bucket, matching the PC client's naming.
    void SetServerIdentity(const std::string& host, uint16_t port);

    void OnJoinResponse(const std::string& manifestJson);
    void OnServerConfig(const std::vector<uint8_t>& masterKey);
    void OnFileData(const cefproto::FileDataPacket& packet);
    void OnSessionLost();

    // Game thread, every frame: kicks the download off once manifest + key +
    // transport are all available, reports stalled transfers, and drains the
    // results produced by the worker thread.
    void Tick();

    bool IsReady() const { return state_ == State::Completed; }
    const char* StateName() const;
    size_t DownloadedFileCount() const { return completedFiles_; }

    // Tear the worker down. Called at process exit.
    void Shutdown();

private:
    CefResources() = default;
    ~CefResources();

    enum class State
    {
        Idle,             // no session / after reset
        AwaitingTrigger,  // manifest received, waiting for key + transport
        VerifyingCache,   // walking the manifest, hashing local .pak files
        Downloading,      // RequestFiles sent, FileData arriving
        Completed,        // resource tree usable
    };

    struct ExpectedFile
    {
        std::string resourceName;
        std::string relativePath;
        std::string hash;
        size_t size = 0;
        std::string savePath;     // <cache>/<relativePath>
        std::string extractDir;   // <resources>/<resourceName>
    };

    struct Expected
    {
        std::string hash;
        size_t size = 0;
        size_t receivedBytes = 0;
        uint32_t totalChunks = 0;
        uint32_t receivedChunks = 0;
        std::vector<std::vector<uint8_t>> chunks;
    };

    // Work item handed to the worker thread. It carries *everything* the job
    // needs so the worker never touches member state: expected_, masterKey_ and
    // the state machine belong to the game thread only.
    struct WorkItem
    {
        std::string key;
        // Bumped on every reset; a result whose generation is stale is dropped
        // so a job still in flight when the session was lost cannot corrupt the
        // counters of the next session.
        uint32_t generation = 0;
        // true  -> cache verify: read the pak back from pakPath, hash it, and
        //          extract only when it still matches the manifest.
        // false -> freshly downloaded: verify `data` and extract it.
        bool cacheVerify = false;
        std::string pakPath;
        std::vector<uint8_t> data;
        std::string hash;
        size_t size = 0;
        std::string savePath;
        std::string extractDir;
        std::vector<uint8_t> masterKey;
    };

    struct WorkResult
    {
        std::string key;
        uint32_t generation = 0;
        bool ok = false;
        bool cacheVerify = false;
    };

    void MarkReadyToDownload();
    void TriggerDownload();
    void FinishCacheVerification();
    void RequestFiles(const std::vector<std::string>& keys);
    bool ExtractPak(const ExpectedFile& file, const std::vector<uint8_t>& pak,
                    const std::vector<uint8_t>& masterKey);
    void CompleteIfDone();
    void ResetState();
    void SendDownloadComplete();
    void SendClientEvent(const char* name);

    // Enqueue a heavy job; the worker picks it up. Thread-safe.
    void EnqueueWork(WorkItem item);
    // Drain completed jobs on the game thread. Called from Tick().
    void DrainResults();

    // Start the worker once, lazily, from the game thread.
    void EnsureWorker();

    State state_ = State::Idle;
    Sender sender_;
    ReadyHandler readyHandler_;

    std::string storageRoot_;
    std::string serverId_ = "unknown_0";
    std::string cacheDir_;

    std::string manifestJson_;
    std::vector<ExpectedFile> requested_;
    std::map<std::string, ExpectedFile> expected_;
    std::map<std::string, Expected> assembling_;
    // Files that must be asked for: everything not verified from the local
    // cache, plus any file whose download failed verification.
    std::set<std::string> needRequest_;
    // Per-file re-request counter, so a server that keeps sending a corrupt pak
    // cannot put the client into an endless request loop.
    std::map<std::string, int> retries_;

    std::vector<uint8_t> masterKey_;
    bool downloadCompleteSent_ = false;
    size_t completedFiles_ = 0;
    size_t totalFiles_ = 0;
    size_t pendingCacheChecks_ = 0;
    size_t pendingExtractions_ = 0;
    uint32_t generation_ = 0;
    uint64_t lastActivityMs_ = 0;
    uint64_t lastStallLogMs_ = 0;

    // Worker thread. Created lazily by EnsureWorker() on the game thread.
    std::thread worker_;
    bool workerStarted_ = false;
    std::mutex workMutex_;
    std::condition_variable workCv_;
    std::deque<WorkItem> workQueue_;
    bool workerStop_ = false;
    std::mutex resultMutex_;
    std::vector<WorkResult> resultQueue_;

    void WorkerMain();
};

} // namespace cefres
