package com.sampmobile.launcher.utils;

import android.content.Context;
import android.os.StatFs;

import com.sampmobile.launcher.download.GamedataState;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * Verifies local GTA:SA + SA-MP game files under external files/ and produces a
 * structured per-file status report for the Settings -> 文件验证 tab.
 *
 * Supports Lite (unified default, ~1.04 GiB without .osw audio streams) and
 * Full (~2.10 GiB with complete GTA:SA SFX/radio streams) verification profiles.
 */
public final class GameDataVerifier {

    public enum Edition {
        LITE,
        FULL
    }

    public enum Status {
        OK,
        MISSING,
        INVALID,
        SKIPPED_LITE
    }

    public static final class FileCheckItem {
        public final String path;
        public final String label;
        public final String group;
        public final boolean directory;
        public final boolean fullOnly;
        public final Status status;
        public final long bytes;
        public final int fileCount;

        public FileCheckItem(String path, String label, String group,
                             boolean directory, boolean fullOnly,
                             Status status, long bytes, int fileCount) {
            this.path = path;
            this.label = label;
            this.group = group;
            this.directory = directory;
            this.fullOnly = fullOnly;
            this.status = status;
            this.bytes = bytes;
            this.fileCount = fileCount;
        }

        public boolean isProblem() {
            return status == Status.MISSING || status == Status.INVALID;
        }
    }

    public static final class Report {
        public final Edition edition;
        public final List<FileCheckItem> items;
        public final int okCount;
        public final int missingCount;
        public final int invalidCount;
        public final int skippedCount;
        public final long totalDataBytes;
        public final long freeSpaceBytes;
        public final boolean coreReady;
        public final boolean hasFullAudio;
        public final String installedVersion;

        public Report(Edition edition, List<FileCheckItem> items,
                      int okCount, int missingCount, int invalidCount, int skippedCount,
                      long totalDataBytes, long freeSpaceBytes,
                      boolean coreReady, boolean hasFullAudio, String installedVersion) {
            this.edition = edition;
            this.items = Collections.unmodifiableList(items);
            this.okCount = okCount;
            this.missingCount = missingCount;
            this.invalidCount = invalidCount;
            this.skippedCount = skippedCount;
            this.totalDataBytes = totalDataBytes;
            this.freeSpaceBytes = freeSpaceBytes;
            this.coreReady = coreReady;
            this.hasFullAudio = hasFullAudio;
            this.installedVersion = installedVersion;
        }

        public int requiredCount() {
            return okCount + missingCount + invalidCount;
        }

        public int problemCount() {
            return missingCount + invalidCount;
        }
    }

    private static final class EntrySpec {
        final String[] candidatePaths;
        final String label;
        final String group;
        final boolean directory;
        final boolean fullOnly;
        final long minBytes;

        EntrySpec(String path, String label, String group, boolean directory, boolean fullOnly, long minBytes) {
            this(new String[]{path}, label, group, directory, fullOnly, minBytes);
        }

        EntrySpec(String[] candidatePaths, String label, String group, boolean directory, boolean fullOnly, long minBytes) {
            this.candidatePaths = candidatePaths;
            this.label = label;
            this.group = group;
            this.directory = directory;
            this.fullOnly = fullOnly;
            this.minBytes = minBytes;
        }
    }

    private static final EntrySpec[] SPECS = {
            // SA-MP 联机核心
            new EntrySpec("SAMP/main.scm", I18n.tr("SA-MP 主脚本"), I18n.tr("SA-MP 联机"), false, false, 64),
            new EntrySpec("SAMP/SAMP.ide", I18n.tr("联机模型定义"), I18n.tr("SA-MP 联机"), false, false, 128),
            new EntrySpec(new String[]{"texdb/SAMPCOL.img", "SAMP/SAMPCOL.img"}, I18n.tr("联机碰撞库"), I18n.tr("SA-MP 联机"), false, false, 1024),
            new EntrySpec("SAMP/gta.dat", I18n.tr("世界资源索引"), I18n.tr("SA-MP 联机"), false, false, 128),
            new EntrySpec("SAMP/peds.ide", I18n.tr("角色皮肤定义表"), I18n.tr("SA-MP 联机"), false, false, 128),
            new EntrySpec("SAMP/vehicles.ide", I18n.tr("载具属性定义表"), I18n.tr("SA-MP 联机"), false, false, 128),
            new EntrySpec("texdb/samp.img", I18n.tr("SA-MP 模型档案"), I18n.tr("SA-MP 联机"), false, false, 4096),
            new EntrySpec("texdb/samp/samp.txt", I18n.tr("SA-MP 贴图索引"), I18n.tr("SA-MP 联机"), false, false, 64),
            new EntrySpec("texdb/samp_extra/samp_extra.txt", I18n.tr("TextDraw 扩展贴图索引"), I18n.tr("SA-MP 联机"), false, false, 64),

            // GTA:SA 核心地图、模型与贴图
            new EntrySpec("texdb/gta3.img", I18n.tr("主世界模型档案"), I18n.tr("GTA:SA 核心"), false, false, 1024 * 1024),
            new EntrySpec("texdb/gta_int.img", I18n.tr("室内场景模型档案"), I18n.tr("GTA:SA 核心"), false, false, 1024 * 1024),
            new EntrySpec("texdb/player.img", I18n.tr("主角模型与服装档案"), I18n.tr("GTA:SA 核心"), false, false, 1024 * 1024),
            new EntrySpec("texdb/gta3/gta3.txt", I18n.tr("主世界贴图索引"), I18n.tr("GTA:SA 核心"), false, false, 64),
            new EntrySpec("texdb/gta_int/gta_int.txt", I18n.tr("室内贴图索引"), I18n.tr("GTA:SA 核心"), false, false, 64),
            new EntrySpec("texdb/txd/txd.txt", I18n.tr("通用贴图索引"), I18n.tr("GTA:SA 核心"), false, false, 64),
            new EntrySpec("texdb/mobile/mobile.txt", I18n.tr("移动端贴图索引"), I18n.tr("GTA:SA 核心"), false, false, 64),
            new EntrySpec("texdb/player/player.txt", I18n.tr("主角贴图索引"), I18n.tr("GTA:SA 核心"), false, false, 64),
            new EntrySpec("models/coll/peds.col", I18n.tr("人物骨骼碰撞数据"), I18n.tr("GTA:SA 核心"), false, false, 512),
            new EntrySpec("anim/anim.img", I18n.tr("角色动作动画库"), I18n.tr("GTA:SA 核心"), false, false, 1024 * 1024),
            new EntrySpec("data/handling.cfg", I18n.tr("载具物理参数表"), I18n.tr("GTA:SA 核心"), false, false, 1024),
            new EntrySpec("data/weapon.dat", I18n.tr("武器数据配置表"), I18n.tr("GTA:SA 核心"), false, false, 1024),

            // 音频配置与完整版音频流（按 GTA:SA 引擎规则忽略大小写解析）
            new EntrySpec("audio/CONFIG/TrkHead.dat", I18n.tr("音频引擎核心配置"), I18n.tr("音频资源"), false, false, 16),
            new EntrySpec("audio/CONFIG/BankLkup.dat", I18n.tr("音频库映射表"), I18n.tr("音频资源"), false, false, 16),
            new EntrySpec("audio/sfx", I18n.tr("完整版音效流目录（2.10 GiB）"), I18n.tr("音频资源"), true, true, 1024 * 1024),
            new EntrySpec("audio/streams", I18n.tr("完整版电台与语音流（2.10 GiB）"), I18n.tr("音频资源"), true, true, 1024 * 1024),
    };

    private GameDataVerifier() {}

    /**
     * Scans local filesDir and returns a complete verification report.
     * Call from a background thread (disk stat / recursive size scan).
     */
    public static Report verify(Context context, Edition edition) {
        File root = context.getExternalFilesDir(null);
        if (root == null) {
            root = new File(context.getFilesDir(), "ext");
        }
        Edition targetEdition = (edition == null) ? Edition.LITE : edition;

        List<FileCheckItem> items = new ArrayList<>(SPECS.length);
        int ok = 0;
        int missing = 0;
        int invalid = 0;
        int skipped = 0;
        boolean hasSfx = false;
        boolean hasStreams = false;

        for (EntrySpec spec : SPECS) {
            File target = resolveAnyCandidate(root, spec.candidatePaths, spec.directory);
            String displayPath = (target != null)
                    ? relativePathOf(root, target, spec.candidatePaths[0])
                    : spec.candidatePaths[0];
            String primaryLower = spec.candidatePaths[0].toLowerCase(Locale.US);
            long bytes = 0L;
            int fileCount = 0;
            Status status;

            if (spec.directory) {
                if (target != null && target.isDirectory()) {
                    long[] stats = measureDir(target);
                    bytes = stats[0];
                    fileCount = (int) stats[1];
                    boolean validDir = fileCount > 0 && bytes >= spec.minBytes;
                    if (primaryLower.equals("audio/sfx") && validDir) hasSfx = true;
                    if (primaryLower.equals("audio/streams") && validDir) hasStreams = true;

                    if (validDir) {
                        status = Status.OK;
                    } else if (spec.fullOnly && targetEdition == Edition.LITE) {
                        status = Status.SKIPPED_LITE;
                    } else {
                        status = (fileCount == 0) ? Status.MISSING : Status.INVALID;
                    }
                } else {
                    status = (spec.fullOnly && targetEdition == Edition.LITE)
                            ? Status.SKIPPED_LITE
                            : Status.MISSING;
                }
            } else {
                if (target == null || !target.isFile()) {
                    status = (spec.fullOnly && targetEdition == Edition.LITE)
                            ? Status.SKIPPED_LITE
                            : Status.MISSING;
                } else {
                    bytes = target.length();
                    fileCount = 1;
                    if (bytes <= 0 || bytes < spec.minBytes) {
                        status = Status.INVALID;
                    } else {
                        status = Status.OK;
                    }
                }
            }

            switch (status) {
                case OK:
                    ok++;
                    break;
                case MISSING:
                    missing++;
                    break;
                case INVALID:
                    invalid++;
                    break;
                case SKIPPED_LITE:
                    skipped++;
                    break;
            }
            items.add(new FileCheckItem(
                    displayPath, spec.label, spec.group,
                    spec.directory, spec.fullOnly, status, bytes, fileCount));
        }

        long totalDataBytes = measureGameDataTotalBytes(root);
        long freeSpaceBytes = queryFreeBytes(root);
        boolean coreReady = GameDataImporter.isGameDataReady(root);
        String installedVer = GamedataState.installedVersion(context);

        return new Report(
                targetEdition,
                items,
                ok, missing, invalid, skipped,
                totalDataBytes, freeSpaceBytes,
                coreReady, hasSfx && hasStreams, installedVer
        );
    }

    private static File resolveAnyCandidate(File root, String[] candidates, boolean wantDir) {
        if (root == null || !root.isDirectory()) return null;
        for (String rel : candidates) {
            File found = resolveCaseInsensitive(root, rel, wantDir);
            if (found != null) return found;
        }
        return null;
    }

    /**
     * Resolves a relative path case-insensitively like GTA:SA's OS_FileOpen
     * (fast-path exact match first, falling back to segment-by-segment case-insensitive scan).
     */
    private static File resolveCaseInsensitive(File root, String relPath, boolean wantDir) {
        File exact = new File(root, relPath);
        if (wantDir ? exact.isDirectory() : exact.isFile()) {
            return exact;
        }
        String[] parts = relPath.split("/");
        File cur = root;
        for (int i = 0; i < parts.length; i++) {
            String seg = parts[i];
            if (seg.isEmpty()) continue;
            File direct = new File(cur, seg);
            if (direct.exists()) {
                cur = direct;
                continue;
            }
            File[] children = cur.listFiles();
            if (children == null) return null;
            File matched = null;
            for (File c : children) {
                if (c.getName().equalsIgnoreCase(seg)) {
                    matched = c;
                    break;
                }
            }
            if (matched == null) return null;
            cur = matched;
        }
        return (wantDir ? cur.isDirectory() : cur.isFile()) ? cur : null;
    }

    private static String relativePathOf(File root, File target, String fallback) {
        String r = root.getAbsolutePath();
        String t = target.getAbsolutePath();
        if (t.startsWith(r)) {
            String sub = t.substring(r.length()).replace('\\', '/');
            while (sub.startsWith("/")) sub = sub.substring(1);
            if (!sub.isEmpty()) return sub;
        }
        return fallback;
    }

    private static long measureGameDataTotalBytes(File root) {
        if (root == null || !root.isDirectory()) return 0L;
        String[] dirs = {"SAMP", "anim", "audio", "data", "models", "texdb"};
        long total = 0L;
        for (String d : dirs) {
            File sub = resolveCaseInsensitive(root, d, true);
            if (sub != null && sub.isDirectory()) {
                total += measureDir(sub)[0];
            }
        }
        File streamIni = resolveCaseInsensitive(root, "stream.ini", false);
        if (streamIni != null && streamIni.isFile()) {
            total += streamIni.length();
        }
        return total;
    }

    private static long[] measureDir(File dir) {
        long bytes = 0L;
        long count = 0L;
        File[] list = dir.listFiles();
        if (list == null) return new long[]{0L, 0L};
        for (File f : list) {
            if (f.isDirectory()) {
                long[] sub = measureDir(f);
                bytes += sub[0];
                count += sub[1];
            } else if (f.isFile()) {
                bytes += f.length();
                count++;
            }
        }
        return new long[]{bytes, count};
    }

    private static long queryFreeBytes(File dir) {
        try {
            File probe = dir;
            while (probe != null && !probe.exists()) {
                probe = probe.getParentFile();
            }
            if (probe == null) return -1L;
            StatFs stat = new StatFs(probe.getAbsolutePath());
            return stat.getAvailableBytes();
        } catch (Throwable ignored) {
            return -1L;
        }
    }

    public static String formatBytes(long bytes) {
        if (bytes < 0) return I18n.tr("未知");
        if (bytes < 1024) return bytes + " B";
        double kib = bytes / 1024.0;
        if (kib < 1024) return String.format(Locale.US, "%.1f KiB", kib);
        double mib = kib / 1024.0;
        if (mib < 1024) return String.format(Locale.US, "%.1f MiB", mib);
        return String.format(Locale.US, "%.2f GiB", mib / 1024.0);
    }
}
