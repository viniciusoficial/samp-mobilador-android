package com.sampmobile.launcher.download;


import com.sampmobile.launcher.utils.I18n;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * 游戏数据清单（manifest.json）的解析结果。
 *
 * 与 docs/GAMEDATA_DISTRIBUTION.md 的格式一一对应。解析是**严格**的：
 * 缺必填字段、类型不对、schema 不认识，一律抛 {@link ManifestException}，
 * 不做"猜一个默认值继续跑"——数据包下载失败的成本是玩家等半小时后报错，
 * 早失败比晚失败好。
 */
public final class DataManifest {

    /** 当前客户端认识的清单 schema。不匹配直接拒绝。 */
    public static final int SCHEMA = 1;

    public static final class Chunk {
        public final int index;
        public final String url;
        public final long bytes;
        public final String sha256;

        Chunk(int index, String url, long bytes, String sha256) {
            this.index = index;
            this.url = url;
            this.bytes = bytes;
            this.sha256 = sha256;
        }

        /** 分片在本地缓存里的文件名（固定 4 位序号，便于排序与人工排查）。 */
        public String fileName() {
            return String.format(java.util.Locale.US, "%04d", index);
        }
    }

    public static final class Package {
        public final String id;
        public final String label;
        public final long bytes;
        public final String sha256;
        public final List<Chunk> chunks;

        Package(String id, String label, long bytes, String sha256, List<Chunk> chunks) {
            this.id = id;
            this.label = label;
            this.bytes = bytes;
            this.sha256 = sha256;
            this.chunks = Collections.unmodifiableList(chunks);
        }
    }

    public static final class Group {
        public final String key;          // "game" / "online"
        public final String label;
        public final long totalBytes;
        public final boolean required;
        public final List<Package> packages;

        Group(String key, String label, long totalBytes, boolean required, List<Package> packages) {
            this.key = key;
            this.label = label;
            this.totalBytes = totalBytes;
            this.required = required;
            this.packages = Collections.unmodifiableList(packages);
        }
    }

    public final int schema;
    public final String version;
    public final int minClientVersionCode;
    public final List<Group> groups;

    private DataManifest(int schema, String version, int minClientVersionCode, List<Group> groups) {
        this.schema = schema;
        this.version = version;
        this.minClientVersionCode = minClientVersionCode;
        this.groups = Collections.unmodifiableList(groups);
    }

    public long totalBytes() {
        long total = 0;
        for (Group g : groups) total += g.totalBytes;
        return total;
    }

    /** 找不到对应分组时返回 null（清单可能只含 game，不含 online）。 */
    public Group group(String key) {
        for (Group g : groups) {
            if (g.key.equals(key)) return g;
        }
        return null;
    }

    // ------------------------------------------------------------------ 解析

    public static DataManifest parse(String json, int clientVersionCode) throws ManifestException {
        final JSONObject root;
        try {
            root = new JSONObject(json);
        } catch (JSONException e) {
            throw new ManifestException(I18n.tr("清单不是合法 JSON：") + e.getMessage());
        }

        int schema = root.optInt("schema", -1);
        if (schema != SCHEMA) {
            throw new ManifestException(I18n.tr("清单 schema=") + schema + I18n.tr("，客户端只支持 ") + SCHEMA
                    + I18n.tr("（请升级客户端）"));
        }

        String version = root.optString("version", "");
        if (version.isEmpty()) {
            throw new ManifestException(I18n.tr("清单缺少 version"));
        }

        int minClient = root.optInt("minClientVersionCode", 0);
        if (minClient > 0 && clientVersionCode < minClient) {
            throw new ManifestException(I18n.tr("该数据包要求客户端 versionCode >= ") + minClient
                    + I18n.tr("，当前为 ") + clientVersionCode);
        }

        List<Group> groups = new ArrayList<>();
        // 顺序固定：先游戏本体，再联机包。玩家看到的是"先装大的"。
        for (String key : new String[]{"game", "online"}) {
            JSONObject g = root.optJSONObject(key);
            if (g == null) continue;
            groups.add(parseGroup(key, g));
        }

        if (groups.isEmpty()) {
            throw new ManifestException(I18n.tr("清单里既没有 game 也没有 online 分组"));
        }

        return new DataManifest(schema, version, minClient, groups);
    }

    private static Group parseGroup(String key, JSONObject g) throws ManifestException {
        String label = g.optString("label", key);
        boolean required = g.optBoolean("required", true);

        JSONArray pkgArray = g.optJSONArray("packages");
        if (pkgArray == null || pkgArray.length() == 0) {
            throw new ManifestException(I18n.tr("分组 ") + key + I18n.tr(" 没有 packages"));
        }

        List<Package> packages = new ArrayList<>();
        for (int i = 0; i < pkgArray.length(); i++) {
            JSONObject p = pkgArray.optJSONObject(i);
            if (p == null) throw new ManifestException(I18n.tr("分组 ") + key + I18n.tr(" 的 packages[") + i + I18n.tr("] 不是对象"));
            packages.add(parsePackage(key, p, i));
        }

        // 分组总大小优先用清单声明值；缺失或不合理时按各包求和兜底。
        long declared = g.optLong("totalBytes", 0);
        long sum = 0;
        for (Package p : packages) sum += p.bytes;
        long total = declared > 0 ? declared : sum;

        return new Group(key, label, total, required, packages);
    }

    private static Package parsePackage(String groupKey, JSONObject p, int pos)
            throws ManifestException {
        String where = groupKey + ".packages[" + pos + "]";

        String id = p.optString("id", "");
        if (id.isEmpty()) throw new ManifestException(where + I18n.tr(" 缺少 id"));
        // id 会拼进本地路径，必须挡住穿越。
        if (id.contains("/") || id.contains("\\") || id.contains("..")) {
            throw new ManifestException(where + I18n.tr(" 的 id 含非法字符：") + id);
        }

        long bytes = p.optLong("bytes", -1);
        if (bytes <= 0) throw new ManifestException(where + I18n.tr(" 的 bytes 非法：") + bytes);

        String sha = p.optString("sha256", "").toLowerCase(java.util.Locale.US);
        if (!isHex64(sha)) throw new ManifestException(where + I18n.tr(" 的 sha256 不是 64 位 hex"));

        JSONArray chunkArray = p.optJSONArray("chunks");
        if (chunkArray == null || chunkArray.length() == 0) {
            throw new ManifestException(where + I18n.tr(" 没有 chunks"));
        }

        List<Chunk> chunks = new ArrayList<>();
        long chunkSum = 0;
        for (int i = 0; i < chunkArray.length(); i++) {
            JSONObject c = chunkArray.optJSONObject(i);
            if (c == null) throw new ManifestException(where + ".chunks[" + i + I18n.tr("] 不是对象"));
            Chunk chunk = parseChunk(where, c, i);
            chunks.add(chunk);
            chunkSum += chunk.bytes;
        }

        // 分片大小之和必须等于整包大小，否则拼出来的文件一定不对，早点报错。
        if (chunkSum != bytes) {
            throw new ManifestException(where + I18n.tr(" 的分片总大小 ") + chunkSum
                    + I18n.tr(" 与 bytes ") + bytes + I18n.tr(" 不一致"));
        }

        String label = p.optString("label", id);
        return new Package(id, label, bytes, sha, chunks);
    }

    private static Chunk parseChunk(String where, JSONObject c, int pos) throws ManifestException {
        String w = where + ".chunks[" + pos + "]";
        int index = c.optInt("index", -1);
        if (index < 0) throw new ManifestException(w + I18n.tr(" 的 index 非法：") + index);

        String url = c.optString("url", "");
        if (url.isEmpty()) throw new ManifestException(w + I18n.tr(" 缺少 url"));

        long bytes = c.optLong("bytes", -1);
        if (bytes <= 0) throw new ManifestException(w + I18n.tr(" 的 bytes 非法：") + bytes);

        // 分片 sha256 可选：缺失时跳过单片校验，整包仍会校验。
        String sha = c.optString("sha256", "").toLowerCase(java.util.Locale.US);
        if (!sha.isEmpty() && !isHex64(sha)) {
            throw new ManifestException(w + I18n.tr(" 的 sha256 不是 64 位 hex"));
        }

        return new Chunk(index, url, bytes, sha);
    }

    private static boolean isHex64(String s) {
        if (s == null || s.length() != 64) return false;
        for (int i = 0; i < 64; i++) {
            char ch = s.charAt(i);
            boolean hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            if (!hex) return false;
        }
        return true;
    }

    public static final class ManifestException extends Exception {
        public ManifestException(String message) {
            super(message);
        }
    }
}
