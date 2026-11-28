#include "ModLoader.h"
#include "../Settings.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <spdlog/spdlog.h>

std::atomic<bool> ModLoader::m_enabled{false};
std::atomic<bool> ModLoader::m_hasVirtualImg{false};
std::vector<std::string> ModLoader::m_extraTexDbs;

namespace {

// Maps normalized lowercase forward-slash GTA-relative path
// -> actual gameDir-relative path ("modloader/<mod>/<subpath>")
std::unordered_map<std::string, std::string> g_fileMap;

struct ModEntry {
	std::string name;
	bool enabled = true;
	int priority = 50;
};

struct VirtualImgEntry {
	std::string entryName; // lowercase basename (<= 23 chars)
	std::string absPath;
	uint64_t fileSize = 0;
	int64_t mtimeSec = 0;
};

constexpr size_t kImgSectorSize = 2048;
constexpr size_t kImgMaxEntryNameLen = 23; // char[24] with NUL terminator
constexpr uint64_t kImgMaxEntryBytes = 0xFFFFULL * kImgSectorSize; // uint16_t sector count

bool isStreamableLooseModel(const std::string& lowerName)
{
	auto hasExt = [&](const char* ext) {
		size_t elen = strlen(ext);
		return lowerName.size() > elen &&
			lowerName.compare(lowerName.size() - elen, elen, ext) == 0;
	};
	return hasExt(".dff") || hasExt(".ifp") || hasExt(".col") || hasExt(".ipl");
}

bool readSmallFile(const std::string& path, std::string& out)
{
	FILE* fp = fopen(path.c_str(), "rb");
	if (!fp) return false;
	char buf[4096];
	out.clear();
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		out.append(buf, n);
		if (out.size() > 1024 * 1024) {
			fclose(fp);
			return false;
		}
	}
	bool ok = !ferror(fp);
	fclose(fp);
	return ok;
}

bool writeSmallFileAtomic(const std::string& path, const std::string& content)
{
	std::string tmpPath = path + ".tmp";
	FILE* fp = fopen(tmpPath.c_str(), "wb");
	if (!fp) return false;
	if (!content.empty() && fwrite(content.data(), 1, content.size(), fp) != content.size()) {
		fclose(fp);
		unlink(tmpPath.c_str());
		return false;
	}
	bool ok = (fflush(fp) == 0) && !ferror(fp);
	fclose(fp);
	if (!ok) {
		unlink(tmpPath.c_str());
		return false;
	}
	if (rename(tmpPath.c_str(), path.c_str()) != 0) {
		unlink(tmpPath.c_str());
		return false;
	}
	return true;
}

bool buildVirtualModelsImg(const std::string& modRoot, const std::vector<VirtualImgEntry>& entries)
{
	const std::string imgPath = modRoot + "/models.img";
	const std::string cachePath = modRoot + "/.models_cache.txt";

	if (entries.empty()) {
		unlink(imgPath.c_str());
		unlink(cachePath.c_str());
		return false;
	}

	// Deterministic fingerprint (sorted by entryName)
	std::vector<VirtualImgEntry> sorted = entries;
	std::sort(sorted.begin(), sorted.end(), [](const VirtualImgEntry& a, const VirtualImgEntry& b) {
		return a.entryName < b.entryName;
	});

	std::string fingerprint = "v1\n";
	fingerprint.reserve(sorted.size() * 96);
	for (const auto& e : sorted) {
		char lineBuf[128];
		snprintf(lineBuf, sizeof(lineBuf), "|%llu|%lld\n",
			static_cast<unsigned long long>(e.fileSize),
			static_cast<long long>(e.mtimeSec));
		fingerprint += e.entryName;
		fingerprint += '|';
		fingerprint += e.absPath;
		fingerprint += lineBuf;
	}

	std::string existingCache;
	struct stat imgSt{};
	if (readSmallFile(cachePath, existingCache) && existingCache == fingerprint &&
		stat(imgPath.c_str(), &imgSt) == 0 && imgSt.st_size >= static_cast<off_t>(kImgSectorSize)) {
		spdlog::info("ModLoader: virtual models.img up-to-date ({} loose models, {} KB)",
			sorted.size(), static_cast<unsigned long long>(imgSt.st_size / 1024));
		return true;
	}

	const uint32_t entryCount = static_cast<uint32_t>(sorted.size());
	const uint64_t headerBytes = 8ULL + static_cast<uint64_t>(entryCount) * 32ULL;
	const uint32_t firstDataSector = static_cast<uint32_t>((headerBytes + kImgSectorSize - 1) / kImgSectorSize);

	struct DirRecord {
		uint32_t offset;
		uint16_t streamingSize;
		uint16_t sizeInArchive;
		char name[24];
	};
	static_assert(sizeof(DirRecord) == 32, "VER2 IMG directory entry must be 32 bytes");

	std::vector<DirRecord> dir(entryCount);
	uint32_t curSector = firstDataSector;
	for (uint32_t i = 0; i < entryCount; ++i) {
		const auto& e = sorted[i];
		uint32_t sectors = static_cast<uint32_t>((e.fileSize + kImgSectorSize - 1) / kImgSectorSize);
		if (sectors == 0 || sectors > 0xFFFFu || curSector > (UINT32_MAX - sectors)) {
			spdlog::warn("ModLoader: skipping '{}' in virtual IMG (invalid/overflowing sector count {})", e.entryName, sectors);
			return false;
		}
		dir[i].offset = curSector;
		dir[i].streamingSize = static_cast<uint16_t>(sectors);
		dir[i].sizeInArchive = 0;
		memset(dir[i].name, 0, sizeof(dir[i].name));
		memcpy(dir[i].name, e.entryName.c_str(), std::min(e.entryName.size(), kImgMaxEntryNameLen));
		curSector += sectors;
	}

	const std::string tmpImgPath = imgPath + ".tmp";
	FILE* out = fopen(tmpImgPath.c_str(), "wb");
	if (!out) {
		spdlog::error("ModLoader: failed to create {}", tmpImgPath);
		return false;
	}

	bool ok = true;
	const char magic[4] = {'V', 'E', 'R', '2'};
	if (fwrite(magic, 1, 4, out) != 4 ||
		fwrite(&entryCount, sizeof(entryCount), 1, out) != 1 ||
		fwrite(dir.data(), sizeof(DirRecord), entryCount, out) != entryCount) {
		ok = false;
	}

	// Pad header + directory to firstDataSector
	std::vector<uint8_t> zeros(kImgSectorSize, 0);
	if (ok) {
		size_t headerPad = static_cast<size_t>(firstDataSector) * kImgSectorSize - static_cast<size_t>(headerBytes);
		if (headerPad > 0 && fwrite(zeros.data(), 1, headerPad, out) != headerPad) {
			ok = false;
		}
	}

	std::vector<uint8_t> copyBuf(64 * 1024);
	for (uint32_t i = 0; ok && i < entryCount; ++i) {
		const auto& e = sorted[i];
		FILE* in = fopen(e.absPath.c_str(), "rb");
		if (!in) {
			spdlog::error("ModLoader: failed to open '{}' for virtual IMG", e.absPath);
			ok = false;
			break;
		}
		uint64_t written = 0;
		while (written < e.fileSize) {
			size_t want = static_cast<size_t>(std::min<uint64_t>(copyBuf.size(), e.fileSize - written));
			size_t got = fread(copyBuf.data(), 1, want, in);
			if (got == 0) {
				spdlog::error("ModLoader: short read on '{}' ({}/{})", e.absPath, written, e.fileSize);
				ok = false;
				break;
			}
			if (fwrite(copyBuf.data(), 1, got, out) != got) {
				ok = false;
				break;
			}
			written += got;
		}
		fclose(in);
		if (!ok) break;

		size_t rem = static_cast<size_t>(written % kImgSectorSize);
		if (rem != 0) {
			size_t pad = kImgSectorSize - rem;
			if (fwrite(zeros.data(), 1, pad, out) != pad) {
				ok = false;
				break;
			}
		}
	}

	if (ok && (fflush(out) != 0 || ferror(out))) {
		ok = false;
	}
	fclose(out);

	if (!ok) {
		unlink(tmpImgPath.c_str());
		spdlog::error("ModLoader: failed to build virtual models.img");
		return false;
	}

	if (rename(tmpImgPath.c_str(), imgPath.c_str()) != 0) {
		unlink(tmpImgPath.c_str());
		spdlog::error("ModLoader: failed to commit virtual models.img");
		return false;
	}

	writeSmallFileAtomic(cachePath, fingerprint);
	spdlog::info("ModLoader: built virtual models.img ({} loose models, {} KB)",
		entryCount, static_cast<unsigned long long>((static_cast<uint64_t>(curSector) * kImgSectorSize) / 1024));
	return true;
}

std::string normalizePath(const char* raw)
{
	if (!raw) return {};
	std::string out;
	out.reserve(strlen(raw));
	for (const char* p = raw; *p; ++p) {
		char c = *p;
		if (c == '\\') c = '/';
		// collapse duplicate slashes
		if (c == '/' && (out.empty() || out.back() == '/')) continue;
		out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
	}
	// trim trailing whitespace or slash
	while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r' || out.back() == '\n' || out.back() == '/')) {
		out.pop_back();
	}
	return out;
}

bool endsWithIgnoreCase(const std::string& str, const char* suffix)
{
	size_t slen = strlen(suffix);
	if (str.size() < slen) return false;
	size_t off = str.size() - slen;
	for (size_t i = 0; i < slen; ++i) {
		if (std::tolower(static_cast<unsigned char>(str[off + i])) !=
			std::tolower(static_cast<unsigned char>(suffix[i]))) {
			return false;
		}
	}
	return true;
}

bool isBuiltInTexDb(const std::string& lowerName)
{
	static const std::unordered_set<std::string> kBuiltIn = {
		"gta3", "gta_int", "samp", "samp_extra", "menu",
		"mobile", "cutscene", "txd", "player", "playerhi"
	};
	return kBuiltIn.count(lowerName) > 0;
}

void indexModDirectory(
	const std::string& absDir,
	const std::string& modName,
	const std::string& relPrefix,
	int depth,
	std::unordered_set<std::string>& extraTexDbSeen,
	std::vector<std::string>& extraTexDbsOut,
	std::unordered_set<std::string>& virtualImgSeen,
	std::vector<VirtualImgEntry>& virtualImgOut)
{
	if (depth > 16) return;

	DIR* dir = opendir(absDir.c_str());
	if (!dir) return;

	struct dirent* ent;
	while ((ent = readdir(dir)) != nullptr) {
		if (ent->d_name[0] == '.') continue;

		std::string name(ent->d_name);
		std::string childAbs = absDir + "/" + name;
		std::string childRel = relPrefix.empty() ? name : (relPrefix + "/" + name);

		struct stat st{};
		if (lstat(childAbs.c_str(), &st) != 0) continue;

		if (S_ISDIR(st.st_mode)) {
			indexModDirectory(childAbs, modName, childRel, depth + 1,
				extraTexDbSeen, extraTexDbsOut, virtualImgSeen, virtualImgOut);
		} else if (S_ISREG(st.st_mode)) {
			std::string key = normalizePath(childRel.c_str());
			if (key.empty()) continue;

			// Higher-priority mod already claimed this relative path
			if (g_fileMap.find(key) == g_fileMap.end()) {
				g_fileMap.emplace(key, "modloader/" + modName + "/" + childRel);
			}

			// Detect custom TextureDatabaseRuntime packs: texdb/<dbName>/<dbName>.txt
			if (key.rfind("texdb/", 0) == 0) {
				size_t firstSlash = 5; // index of '/' after "texdb"
				size_t secondSlash = key.find('/', firstSlash + 1);
				if (secondSlash != std::string::npos && key.find('/', secondSlash + 1) == std::string::npos) {
					std::string dbName = key.substr(firstSlash + 1, secondSlash - firstSlash - 1);
					std::string fileName = key.substr(secondSlash + 1);
					if (!dbName.empty() && fileName == (dbName + ".txt") && !isBuiltInTexDb(dbName)) {
						if (extraTexDbSeen.insert(dbName).second) {
							// Preserve original case from childRel for TextureDatabaseRuntime::Load
							size_t origFirst = childRel.find('/');
							size_t origSecond = (origFirst != std::string::npos) ? childRel.find('/', origFirst + 1) : std::string::npos;
							if (origFirst != std::string::npos && origSecond != std::string::npos) {
								extraTexDbsOut.push_back(childRel.substr(origFirst + 1, origSecond - origFirst - 1));
							} else {
								extraTexDbsOut.push_back(dbName);
							}
						}
					}
				}
				continue; // Never pack files inside texdb/ (e.g. realskybox/skybox.dff) into virtual models.img
			}

			// Collect loose streamable models (.dff/.ifp/.col/.ipl) into virtual VER2 IMG
			// Skip text IPLs under data/ and standalone COLFILEs under models/coll/
			size_t slashPos = key.rfind('/');
			std::string lowerBase = (slashPos == std::string::npos) ? key : key.substr(slashPos + 1);
			bool inDataDir = (key.rfind("data/", 0) == 0);
			bool inModelsCollDir = (key.rfind("models/coll/", 0) == 0);
			if (!inDataDir && !inModelsCollDir && isStreamableLooseModel(lowerBase)) {
				if (lowerBase.size() > kImgMaxEntryNameLen) {
					spdlog::warn("ModLoader: ignoring loose model '{}' (name > 23 chars for VER2 IMG)", childRel);
				} else if (st.st_size <= 0 || static_cast<uint64_t>(st.st_size) > kImgMaxEntryBytes) {
					spdlog::warn("ModLoader: ignoring loose model '{}' (unsupported size {})",
						childRel, static_cast<long long>(st.st_size));
				} else if (virtualImgSeen.insert(lowerBase).second) {
					VirtualImgEntry ve;
					ve.entryName = lowerBase;
					ve.absPath = childAbs;
					ve.fileSize = static_cast<uint64_t>(st.st_size);
					ve.mtimeSec = static_cast<int64_t>(st.st_mtime);
					virtualImgOut.push_back(std::move(ve));
				}
			}
		}
	}

	closedir(dir);
}

void loadConfigJson(const std::string& jsonPath, std::unordered_map<std::string, ModEntry>& cfgOut)
{
	FILE* fp = fopen(jsonPath.c_str(), "rb");
	if (!fp) return;

	char buf[32768];
	rapidjson::FileReadStream is(fp, buf, sizeof(buf));
	rapidjson::Document doc;
	doc.ParseStream(is);
	fclose(fp);

	if (doc.HasParseError() || !doc.IsObject()) {
		spdlog::warn("ModLoader: failed to parse {}, using directory defaults", jsonPath);
		return;
	}
	if (!doc.HasMember("mods") || !doc["mods"].IsArray()) return;

	for (const auto& item : doc["mods"].GetArray()) {
		if (!item.IsObject() || !item.HasMember("name") || !item["name"].IsString()) continue;
		ModEntry e;
		e.name = item["name"].GetString();
		if (item.HasMember("enabled") && item["enabled"].IsBool()) {
			e.enabled = item["enabled"].GetBool();
		}
		if (item.HasMember("priority") && item["priority"].IsInt()) {
			e.priority = item["priority"].GetInt();
		}
		cfgOut[e.name] = e;
	}
}

} // namespace

void ModLoader::initialize(const std::string& gameDir)
{
	m_enabled.store(false, std::memory_order_release);
	m_hasVirtualImg.store(false, std::memory_order_release);
	m_extraTexDbs.clear();
	g_fileMap.clear();

	if (!Settings::modloader()) {
		spdlog::info("ModLoader: disabled in settings");
		return;
	}

	std::string rootDir = gameDir;
	while (!rootDir.empty() && (rootDir.back() == '/' || rootDir.back() == '\\')) {
		rootDir.pop_back();
	}
	std::string modRoot = rootDir + "/modloader";
	mkdir(modRoot.c_str(), 0777);

	std::unordered_map<std::string, ModEntry> configMap;
	loadConfigJson(modRoot + "/modloader.json", configMap);

	DIR* dir = opendir(modRoot.c_str());
	if (!dir) {
		spdlog::warn("ModLoader: unable to open {}", modRoot);
		return;
	}

	std::vector<ModEntry> activeMods;
	struct dirent* ent;
	while ((ent = readdir(dir)) != nullptr) {
		if (ent->d_name[0] == '.') continue;

		std::string name(ent->d_name);
		std::string fullPath = modRoot + "/" + name;
		struct stat st{};
		if (stat(fullPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

		ModEntry entry;
		entry.name = name;
		auto it = configMap.find(name);
		if (it != configMap.end()) {
			entry.enabled = it->second.enabled;
			entry.priority = it->second.priority;
		} else {
			entry.enabled = !endsWithIgnoreCase(name, ".disabled");
			entry.priority = 50;
		}

		if (entry.enabled) {
			activeMods.push_back(entry);
		}
	}
	closedir(dir);

	// Higher priority first; tie-break alphabetically
	std::sort(activeMods.begin(), activeMods.end(), [](const ModEntry& a, const ModEntry& b) {
		if (a.priority != b.priority) return a.priority > b.priority;
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});

	std::unordered_set<std::string> extraTexDbSeen;
	std::unordered_set<std::string> virtualImgSeen;
	std::vector<VirtualImgEntry> virtualImgEntries;
	for (const auto& mod : activeMods) {
		size_t beforeFiles = g_fileMap.size();
		size_t beforeModels = virtualImgEntries.size();
		indexModDirectory(modRoot + "/" + mod.name, mod.name, "", 0,
			extraTexDbSeen, m_extraTexDbs, virtualImgSeen, virtualImgEntries);
		spdlog::info("ModLoader: mounted '{}' (priority={}, +{} files, +{} loose models)",
				mod.name, mod.priority, g_fileMap.size() - beforeFiles, virtualImgEntries.size() - beforeModels);
	}

	bool hasVirtual = buildVirtualModelsImg(modRoot, virtualImgEntries);
	m_hasVirtualImg.store(hasVirtual, std::memory_order_release);

	m_enabled.store(true, std::memory_order_release);
	spdlog::info("ModLoader: ready ({} active mods, {} indexed overrides, {} extra texdb, {} virtual IMG models)",
			activeMods.size(), g_fileMap.size(), m_extraTexDbs.size(),
			hasVirtual ? virtualImgEntries.size() : 0);
}

bool ModLoader::resolve(const char* gtaRelPath, char* outRelPath, size_t outSize)
{
	if (!m_enabled.load(std::memory_order_acquire) || g_fileMap.empty() || !gtaRelPath || !*gtaRelPath || !outRelPath || outSize == 0) {
		return false;
	}

	std::string key = normalizePath(gtaRelPath);
	if (key.empty()) return false;

	auto it = g_fileMap.find(key);
	if (it == g_fileMap.end()) return false;

	if (it->second.size() >= outSize) {
		spdlog::warn("ModLoader: override path too long for buffer: {}", it->second);
		return false;
	}

	memcpy(outRelPath, it->second.c_str(), it->second.size() + 1);
	return true;
}
