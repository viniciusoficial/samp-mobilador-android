package com.sampmobile.launcher.utils;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Process;
import android.provider.OpenableColumns;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class ModManager {
    public static final String DISABLED_SUFFIX = ".disabled";
    public static final String JPATCH_OFFICIAL_URL = "https://github.com/AndroidModLoader/JPatch";
    public static final String AML_FORUM_JPATCH_URL = "https://aml-mods.ru/viewtopic.php?t=37";

    private static final Set<String> GTA_ROOT_DIRS = new HashSet<>(Arrays.asList(
            "anim", "audio", "data", "models", "samp", "texdb", "cleo", "text"
    ));

    private static final Set<String> CLEO_EXTS = new HashSet<>(Arrays.asList(
            ".csa", ".csi", ".cs", ".fxt"
    ));

    public static class ModLoaderEntry {
        public String name;
        public boolean enabled;
        public int priority;
        public int fileCount;
        public long totalBytes;
    }

    public static class FileModEntry {
        public String name;        // raw filename on disk
        public String displayName; // without .disabled
        public File file;
        public boolean enabled;
        public long sizeBytes;
        public String abiLabel;    // for .so: "64位 (arm64-v8a)", "32位 (armeabi-v7a)", etc.
        public boolean abiCompatible = true;
        public boolean isJPatch;
    }

    public static class ElfInfo {
        public boolean valid;
        public boolean is64Bit;
        public String abiName; // "arm64-v8a", "armeabi-v7a", "未知架构", "非 ELF 文件"
        public boolean matchesProcess;
    }

    public static class JPatchOption {
        public String section;
        public String key;
        public String title;
        public String description;
        public boolean safeForSamp;
        public String defaultSafeValue;
        public String value;
        public boolean isBoolean;
        public boolean fromKnownCatalog;

        public JPatchOption(String section, String key, String title, String description,
                            boolean safeForSamp, String defaultSafeValue, boolean isBoolean) {
            this.section = section;
            this.key = key;
            this.title = title;
            this.description = description;
            this.safeForSamp = safeForSamp;
            this.defaultSafeValue = defaultSafeValue;
            this.value = defaultSafeValue;
            this.isBoolean = isBoolean;
            this.fromKnownCatalog = true;
        }
    }

    public static class AmlCoreSettings {
        public String fakePackageName = "com.rockstargames.gtasa";
        public boolean showModsOverlay = false;
        public boolean printLogsToFile = true;
        public boolean verboseLogs = true;
        public boolean signalHandler = false;
    }

    // ══════════ Process ABI & Paths ══════════

    public static boolean is64BitProcess() {
        return Process.is64Bit();
    }

    public static String getProcessAbiName() {
        return is64BitProcess() ? "arm64-v8a" : "armeabi-v7a";
    }

    public static String getProcessAbiLabel() {
        return is64BitProcess() ? I18n.tr("64位 (arm64-v8a)") : I18n.tr("32位 (armeabi-v7a)");
    }

    public static File getGameFilesDir(Context ctx) {
        return ctx.getExternalFilesDir(null);
    }

    public static File getAndroidDataRootDir(Context ctx) {
        File files = getGameFilesDir(ctx);
        return (files != null && files.getParentFile() != null) ? files.getParentFile() : files;
    }

    public static File getModLoaderDir(Context ctx) {
        return new File(getGameFilesDir(ctx), "modloader");
    }

    public static File getAmlModsDir(Context ctx) {
        return new File(getAndroidDataRootDir(ctx), "mods");
    }

    public static File getAmlConfigsDir(Context ctx) {
        return new File(getAndroidDataRootDir(ctx), "configs");
    }

    public static File getCleoDir(Context ctx) {
        return new File(getGameFilesDir(ctx), "cleo");
    }

    public static File getMonetDir(Context ctx) {
        return new File(getGameFilesDir(ctx), "monetloader");
    }

    public static File getAmlLogFile(Context ctx) {
        return new File(getAndroidDataRootDir(ctx), "android_log_print.txt");
    }

    public static File getMonetLogFile(Context ctx) {
        return new File(getMonetDir(ctx), "logs/monetloader.log");
    }

    public static File getCleoLogFile(Context ctx) {
        return new File(getCleoDir(ctx), "cleo.log");
    }

    public static File getLatestClientLogFile(Context ctx) {
        File dir = new File(getGameFilesDir(ctx), "logcat");
        if (!dir.isDirectory()) return null;
        File[] files = dir.listFiles((d, name) -> name.endsWith(".log"));
        if (files == null || files.length == 0) return null;
        File latest = files[0];
        for (File f : files) {
            if (f.lastModified() > latest.lastModified()) latest = f;
        }
        return latest;
    }

    private static final String[] GAME_SUBDIRS = {
            "SAMP", "SAMP/fonts", "SAMP/screens",
            "anim", "audio", "audio/config", "audio/sfx", "audio/streams",
            "data", "data/Decision", "data/Decision/Allowed",
            "data/maps", "data/maps/country", "data/maps/generic", "data/maps/interior",
            "data/maps/LA", "data/maps/leveldes", "data/maps/SF", "data/maps/vegas", "data/maps/veh_mods",
            "data/paths", "data/script",
            "models", "models/coll", "models/grass",
            "texdb", "texdb/cutscene", "texdb/gta3", "texdb/gta_int", "texdb/menu",
            "texdb/mobile", "texdb/player", "texdb/playerhi", "texdb/samp", "texdb/samp_extra", "texdb/txd"
    };

    public static void ensureDirectories(Context ctx) {
        mkdirSafe(getModLoaderDir(ctx));
        mkdirSafe(getAmlModsDir(ctx));
        mkdirSafe(getAmlConfigsDir(ctx));
        mkdirSafe(getCleoDir(ctx));
        mkdirSafe(getMonetDir(ctx));
        File base = getGameFilesDir(ctx);
        for (String sub : GAME_SUBDIRS) {
            mkdirSafe(new File(base, sub));
        }
    }

    private static void mkdirSafe(File dir) {
        if (dir != null && !dir.exists()) {
            dir.mkdirs();
        }
    }

    // ══════════ ELF ABI Inspector ══════════

    public static ElfInfo inspectElfAbi(File file) {
        ElfInfo info = new ElfInfo();
        info.abiName = I18n.tr("非 ELF 文件");
        if (file == null || !file.isFile() || file.length() < 20) {
            return info;
        }
        byte[] hdr = new byte[20];
        try (FileInputStream fis = new FileInputStream(file)) {
            int n = fis.read(hdr);
            if (n < 20) return info;
        } catch (IOException e) {
            return info;
        }
        if (hdr[0] != 0x7F || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') {
            return info;
        }
        int eiClass = hdr[4] & 0xFF;
        int eMachine = (hdr[18] & 0xFF) | ((hdr[19] & 0xFF) << 8);
        info.valid = true;
        if (eiClass == 2 && eMachine == 183) { // ELFCLASS64 + EM_AARCH64
            info.is64Bit = true;
            info.abiName = I18n.tr("64位 (arm64-v8a)");
            info.matchesProcess = is64BitProcess();
        } else if (eiClass == 1 && eMachine == 40) { // ELFCLASS32 + EM_ARM
            info.is64Bit = false;
            info.abiName = I18n.tr("32位 (armeabi-v7a)");
            info.matchesProcess = !is64BitProcess();
        } else {
            info.abiName = I18n.tr("不兼容架构 (machine=") + eMachine + ")";
            info.matchesProcess = false;
        }
        return info;
    }

    // ══════════ AML Core Config (configs/ModLoaderCore.ini) ══════════

    public static File getAmlCoreConfigFile(Context ctx) {
        return new File(getAmlConfigsDir(ctx), "ModLoaderCore.ini");
    }

    public static boolean isRunningUnderX86Translation() {
        if (Build.SUPPORTED_ABIS != null) {
            for (String abi : Build.SUPPORTED_ABIS) {
                if (abi != null && (abi.startsWith("x86") || abi.contains("x86"))) {
                    return true;
                }
            }
        }
        return false;
    }

    public static void prepareAmlConfig(Context ctx) {
        ensureDirectories(ctx);
        File cfgFile = getAmlCoreConfigFile(ctx);
        IniDocument doc = IniDocument.load(cfgFile);
        boolean changed = false;

        // Ensure GTA:SA package spoofing so NEEDGAME(com.rockstargames.gtasa) mods load cleanly
        String curFake = doc.get("Preferences", "FakePackageName", "");
        if (curFake.trim().isEmpty()) {
            doc.set("Preferences", "FakePackageName", "com.rockstargames.gtasa");
            changed = true;
        }
        if (!doc.has("Preferences", "EnableLogcats")) {
            doc.set("Preferences", "EnableLogcats", "1");
            changed = true;
        }
        // PrintLogsToFile hooks system liblog.so (__android_log_print), which is a non-ARM host bridge
        // under x86/x86_64 emulators (Houdini/libndk_translation) and causes immediate SIGSEGV SEGV_ACCERR.
        // Keep off by default (matching upstream AML) and always force off under x86 translation.
        if (isRunningUnderX86Translation()) {
            if (!"0".equals(doc.get("Preferences", "PrintLogsToFile", ""))) {
                doc.set("Preferences", "PrintLogsToFile", "0");
                changed = true;
            }
        } else if (!doc.has("Preferences", "PrintLogsToFile")) {
            doc.set("Preferences", "PrintLogsToFile", "0");
            changed = true;
        }
        if (!doc.has("Preferences", "PrintLogsToFile_Verbose")) {
            doc.set("Preferences", "PrintLogsToFile_Verbose", "0");
            changed = true;
        }
        if (!doc.has("Preferences", "PrintLogsToFile_NoLogCat")) {
            doc.set("Preferences", "PrintLogsToFile_NoLogCat", "0");
            changed = true;
        }
        if (!doc.has("Preferences", "EnableUpdater")) {
            doc.set("Preferences", "EnableUpdater", "0");
            changed = true;
        }
        if (!doc.has("Preferences", "ShowNewsForFewTimes")) {
            doc.set("Preferences", "ShowNewsForFewTimes", "0");
            changed = true;
        }
        // Force ShowModsLoadedOverlay off: AML's net.rusjj.aml.Overlay creates a full-screen
        // TYPE_APPLICATION_SUB_PANEL window attached to the game Activity, which on Android 12+
        // (targetSdk 35) triggers InputDispatcher "Untrusted touch due to occlusion by SubPanel"
        // and drops all touches on the IME soft keyboard.
        if (!"0".equals(doc.get("Preferences", "ShowModsLoadedOverlay", ""))) {
            doc.set("Preferences", "ShowModsLoadedOverlay", "0");
            changed = true;
        }
        // Keep SAMPMOBILE's async-signal-safe SIGSEGV handler active by default
        if (!doc.has("Preferences", "SignalHandler")) {
            doc.set("Preferences", "SignalHandler", "0");
            changed = true;
        }

        if (changed || !cfgFile.exists()) {
            try {
                doc.save(cfgFile);
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    public static AmlCoreSettings readAmlCoreSettings(Context ctx) {
        prepareAmlConfig(ctx);
        IniDocument doc = IniDocument.load(getAmlCoreConfigFile(ctx));
        AmlCoreSettings s = new AmlCoreSettings();
        s.fakePackageName = doc.get("Preferences", "FakePackageName", "com.rockstargames.gtasa");
        s.showModsOverlay = parseIniBool(doc.get("Preferences", "ShowModsLoadedOverlay", "0"), false);
        s.printLogsToFile = parseIniBool(doc.get("Preferences", "PrintLogsToFile", "0"), false);
        s.verboseLogs = parseIniBool(doc.get("Preferences", "PrintLogsToFile_Verbose", "0"), false);
        s.signalHandler = parseIniBool(doc.get("Preferences", "SignalHandler", "0"), false);
        return s;
    }

    public static void saveAmlCoreSettings(Context ctx, AmlCoreSettings s) {
        prepareAmlConfig(ctx);
        File cfgFile = getAmlCoreConfigFile(ctx);
        IniDocument doc = IniDocument.load(cfgFile);
        doc.set("Preferences", "FakePackageName",
                s.fakePackageName != null && !s.fakePackageName.trim().isEmpty()
                        ? s.fakePackageName.trim() : "com.rockstargames.gtasa");
        // Android 12+ (API 31+) blocks IME touch events when AML's SubPanel overlay window is attached
        boolean allowOverlay = s.showModsOverlay && Build.VERSION.SDK_INT < 31;
        doc.set("Preferences", "ShowModsLoadedOverlay", allowOverlay ? "1" : "0");
        boolean allowLogFile = s.printLogsToFile && !isRunningUnderX86Translation();
        doc.set("Preferences", "PrintLogsToFile", allowLogFile ? "1" : "0");
        doc.set("Preferences", "PrintLogsToFile_Verbose", (allowLogFile && s.verboseLogs) ? "1" : "0");
        doc.set("Preferences", "SignalHandler", s.signalHandler ? "1" : "0");
        try {
            doc.save(cfgFile);
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public static boolean parseIniBool(String val, boolean def) {
        if (val == null) return def;
        String v = val.trim().toLowerCase(Locale.ROOT);
        if (v.equals("1") || v.equals("true") || v.equals("yes") || v.equals("on")) return true;
        if (v.equals("0") || v.equals("false") || v.equals("no") || v.equals("off")) return false;
        return def;
    }

    // ══════════ Modloader Packs (files/modloader/) ══════════

    public static List<ModLoaderEntry> listModLoaderPacks(Context ctx) {
        ensureDirectories(ctx);
        File root = getModLoaderDir(ctx);
        File cfgFile = new File(root, "modloader.json");

        Map<String, ModLoaderEntry> cfgMap = new HashMap<>();
        if (cfgFile.isFile()) {
            try {
                JSONObject json = new JSONObject(Utils.readJSONFromFile(cfgFile));
                JSONArray arr = json.optJSONArray("mods");
                if (arr != null) {
                    for (int i = 0; i < arr.length(); i++) {
                        JSONObject o = arr.optJSONObject(i);
                        if (o == null || !o.has("name")) continue;
                        ModLoaderEntry e = new ModLoaderEntry();
                        e.name = o.getString("name");
                        e.enabled = o.optBoolean("enabled", true);
                        e.priority = o.optInt("priority", 50);
                        cfgMap.put(e.name, e);
                    }
                }
            } catch (Exception ignored) {
            }
        }

        List<ModLoaderEntry> result = new ArrayList<>();
        File[] children = root.listFiles();
        if (children != null) {
            for (File f : children) {
                if (!f.isDirectory() || f.getName().startsWith(".")) continue;
                ModLoaderEntry entry = new ModLoaderEntry();
                entry.name = f.getName();
                ModLoaderEntry saved = cfgMap.get(entry.name);
                if (saved != null) {
                    entry.enabled = saved.enabled;
                    entry.priority = saved.priority;
                } else {
                    entry.enabled = !entry.name.toLowerCase(Locale.ROOT).endsWith(DISABLED_SUFFIX);
                    entry.priority = 50;
                }
                long[] stats = new long[2]; // [count, bytes]
                measureDir(f, stats, 0);
                entry.fileCount = (int) stats[0];
                entry.totalBytes = stats[1];
                result.add(entry);
            }
        }

        Collections.sort(result, (a, b) -> {
            if (a.priority != b.priority) return Integer.compare(b.priority, a.priority);
            return a.name.compareToIgnoreCase(b.name);
        });
        return result;
    }

    public static void saveModLoaderConfig(Context ctx, List<ModLoaderEntry> entries) {
        ensureDirectories(ctx);
        File cfgFile = new File(getModLoaderDir(ctx), "modloader.json");
        try {
            JSONObject root = new JSONObject();
            JSONArray arr = new JSONArray();
            for (ModLoaderEntry e : entries) {
                JSONObject o = new JSONObject();
                o.put("name", e.name);
                o.put("enabled", e.enabled);
                o.put("priority", e.priority);
                arr.put(o);
            }
            root.put("mods", arr);
            Utils.writeJSONToFile(cfgFile, root);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public static File createModLoaderFolder(Context ctx, String rawName) {
        ensureDirectories(ctx);
        String clean = sanitizeFolderName(rawName);
        if (clean.isEmpty()) return null;
        File dir = new File(getModLoaderDir(ctx), clean);
        if (!dir.exists() && !dir.mkdirs()) return null;
        return dir;
    }

    public static String importModLoaderZip(Context ctx, Uri uri) throws IOException {
        ensureDirectories(ctx);
        String fileName = queryDisplayName(ctx, uri);
        if (fileName == null || fileName.trim().isEmpty()) {
            fileName = "mod_" + System.currentTimeMillis() + ".zip";
        }
        String baseName = fileName;
        if (baseName.toLowerCase(Locale.ROOT).endsWith(".zip")) {
            baseName = baseName.substring(0, baseName.length() - 4);
        }
        String folderName = sanitizeFolderName(baseName);
        if (folderName.isEmpty()) folderName = "mod_" + System.currentTimeMillis();

        File destRoot = new File(getModLoaderDir(ctx), folderName);
        int suffix = 1;
        while (destRoot.exists()) {
            destRoot = new File(getModLoaderDir(ctx), folderName + "_" + (suffix++));
        }
        if (!destRoot.mkdirs()) {
            throw new IOException(I18n.tr("无法创建目录: ") + destRoot.getName());
        }

        String canonicalDest = destRoot.getCanonicalPath() + File.separator;
        int extractedFiles = 0;
        long totalExtractedBytes = 0;
        final int MAX_ZIP_ENTRIES = 4096;
        final long MAX_ZIP_TOTAL_BYTES = 512L * 1024L * 1024L;

        try (InputStream raw = ctx.getContentResolver().openInputStream(uri)) {
            if (raw == null) throw new IOException(I18n.tr("无法读取所选文件"));
            try (ZipInputStream zis = new ZipInputStream(raw)) {
                byte[] buf = new byte[8192];
                ZipEntry entry;
                int entryCount = 0;
                while ((entry = zis.getNextEntry()) != null) {
                    if (++entryCount > MAX_ZIP_ENTRIES) {
                        throw new IOException(I18n.tr("压缩包内条目数超过限制 (") + MAX_ZIP_ENTRIES + ")");
                    }
                    String entryName = entry.getName().replace('\\', '/');
                    if (entryName.startsWith("/") || entryName.contains("..")) {
                        zis.closeEntry();
                        continue;
                    }
                    File outFile = new File(destRoot, entryName);
                    String canonicalOut = outFile.getCanonicalPath();
                    if (!canonicalOut.startsWith(canonicalDest)) {
                        zis.closeEntry();
                        continue; // Zip-Slip protection
                    }
                    if (entry.isDirectory()) {
                        outFile.mkdirs();
                    } else {
                        File parent = outFile.getParentFile();
                        if (parent != null && !parent.exists()) parent.mkdirs();
                        try (FileOutputStream fos = new FileOutputStream(outFile)) {
                            int n;
                            while ((n = zis.read(buf)) > 0) {
                                totalExtractedBytes += n;
                                if (totalExtractedBytes > MAX_ZIP_TOTAL_BYTES) {
                                    throw new IOException(I18n.tr("压缩包解压总大小超过 512 MB 限制"));
                                }
                                fos.write(buf, 0, n);
                            }
                        }
                        extractedFiles++;
                    }
                    zis.closeEntry();
                }
            }
        } catch (IOException e) {
            deleteRecursive(destRoot);
            throw e;
        }

        if (extractedFiles == 0) {
            deleteRecursive(destRoot);
            throw new IOException(I18n.tr("压缩包内未找到有效文件"));
        }

        // Flatten nested non-GTA wrapper folders (e.g. MyHud.zip -> Android/data/com.rockstargames.gtasa/files/texdb/... => texdb/...)
        for (int depth = 0; depth < 6; depth++) {
            File[] top = destRoot.listFiles();
            if (top == null || top.length != 1 || !top[0].isDirectory()) {
                break;
            }
            String onlyDir = top[0].getName().toLowerCase(Locale.ROOT);
            if (GTA_ROOT_DIRS.contains(onlyDir)) {
                break;
            }
            File inner = top[0];
            // Rename inner folder to a unique temp name first so nested same-name folders
            // (e.g. mod/mod/...) don't collide with inner or get deleted by deleteRecursive(inner).
            File tempInner = new File(destRoot, ".flatten_tmp_" + depth + "_" + System.nanoTime());
            if (!inner.renameTo(tempInner)) {
                break;
            }
            File[] innerItems = tempInner.listFiles();
            boolean movedAny = false;
            boolean allMoved = true;
            if (innerItems != null) {
                for (File item : innerItems) {
                    File dst = new File(destRoot, item.getName());
                    if (item.renameTo(dst)) {
                        movedAny = true;
                    } else {
                        allMoved = false;
                    }
                }
            }
            if (!movedAny && innerItems != null && innerItems.length > 0) {
                tempInner.renameTo(inner);
                break;
            }
            if (allMoved) {
                deleteRecursive(tempInner);
            } else {
                break;
            }
        }

        return destRoot.getName();
    }

    // ══════════ AML Plugins (mods/*.so) ══════════

    public static List<FileModEntry> listAmlPlugins(Context ctx) {
        ensureDirectories(ctx);
        File dir = getAmlModsDir(ctx);
        List<FileModEntry> result = new ArrayList<>();
        File[] files = dir.listFiles();
        if (files != null) {
            for (File f : files) {
                if (!f.isFile()) continue;
                String name = f.getName();
                String lower = name.toLowerCase(Locale.ROOT);
                boolean enabled;
                String display;
                if (lower.endsWith(".so")) {
                    enabled = true;
                    display = name;
                } else if (lower.endsWith(".so" + DISABLED_SUFFIX)) {
                    enabled = false;
                    display = name.substring(0, name.length() - DISABLED_SUFFIX.length());
                } else {
                    continue;
                }
                FileModEntry e = new FileModEntry();
                e.name = name;
                e.displayName = display;
                e.file = f;
                e.enabled = enabled;
                e.sizeBytes = f.length();
                ElfInfo elf = inspectElfAbi(f);
                e.abiLabel = elf.abiName;
                e.abiCompatible = elf.valid && elf.matchesProcess;
                e.isJPatch = display.toLowerCase(Locale.ROOT).contains("jpatch");
                result.add(e);
            }
        }
        Collections.sort(result, Comparator.comparing(a -> a.displayName.toLowerCase(Locale.ROOT)));
        return result;
    }

    // ══════════ JPatch Binary + Config Editor (delegated to JPatchConfigManager) ══════════

    public static FileModEntry findJPatchBinary(Context ctx) {
        return JPatchConfigManager.findJPatchBinary(ctx);
    }

    public static boolean syncJPatchBinaryState(Context ctx, boolean enable) {
        return JPatchConfigManager.syncJPatchBinaryState(ctx, enable);
    }

    public static File getJPatchConfigFile(Context ctx) {
        return JPatchConfigManager.getJPatchConfigFile(ctx);
    }

    public static List<JPatchOption> loadJPatchOptions(Context ctx) {
        return JPatchConfigManager.loadJPatchOptions(ctx);
    }

    public static void saveJPatchOption(Context ctx, String section, String key, String value) {
        JPatchConfigManager.saveJPatchOption(ctx, section, key, value);
    }

    public static void applyJPatchSampSafePreset(Context ctx) {
        JPatchConfigManager.applyJPatchSampSafePreset(ctx);
    }

    public static void enforceJPatchTouchSafety(Context ctx) {
        JPatchConfigManager.enforceTouchSafety(ctx);
    }

    // ══════════ MonetLoader (.lua) & CLEO (.csa/.csi/.cs/.fxt) Scripts ══════════

    public static boolean ensureMonetLibsInstalled(Context ctx, boolean force) {
        ensureDirectories(ctx);
        File monetDir = getMonetDir(ctx);
        File sentinel = new File(monetDir, "lib/.installed_v380");
        File moonloader = new File(monetDir, "lib/moonloader.lua");
        if (!force && sentinel.isFile() && moonloader.isFile()) {
            return true;
        }
        try {
            if (sentinel.exists()) {
                sentinel.delete();
            }
            String canonicalDest = monetDir.getCanonicalPath() + File.separator;
            try (InputStream raw = ctx.getAssets().open("monetloader_lib.zip");
                 ZipInputStream zis = new ZipInputStream(raw)) {
                byte[] buf = new byte[8192];
                ZipEntry entry;
                while ((entry = zis.getNextEntry()) != null) {
                    String entryName = entry.getName().replace('\\', '/');
                    if (entryName.startsWith("/") || entryName.contains("..")) {
                        zis.closeEntry();
                        continue;
                    }
                    File outFile = new File(monetDir, entryName);
                    String canonicalOut = outFile.getCanonicalPath();
                    if (!canonicalOut.startsWith(canonicalDest)) {
                        zis.closeEntry();
                        continue;
                    }
                    if (entry.isDirectory()) {
                        outFile.mkdirs();
                    } else {
                        File parent = outFile.getParentFile();
                        if (parent != null && !parent.exists()) parent.mkdirs();
                        try (FileOutputStream fos = new FileOutputStream(outFile)) {
                            int n;
                            while ((n = zis.read(buf)) > 0) {
                                fos.write(buf, 0, n);
                            }
                        }
                    }
                    zis.closeEntry();
                }
            }
            if (moonloader.isFile()) {
                File parent = sentinel.getParentFile();
                if (parent != null && !parent.exists()) parent.mkdirs();
                try (FileOutputStream fos = new FileOutputStream(sentinel)) {
                    fos.write("3.8.0\n".getBytes(StandardCharsets.UTF_8));
                }
                return true;
            }
            return false;
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        }
    }

    public static List<FileModEntry> listMonetScripts(Context ctx) {
        ensureDirectories(ctx);
        return listScriptsByExt(getMonetDir(ctx), Collections.singleton(".lua"));
    }

    public static List<FileModEntry> listCleoScripts(Context ctx) {
        ensureDirectories(ctx);
        return listScriptsByExt(getCleoDir(ctx), CLEO_EXTS);
    }

    private static List<FileModEntry> listScriptsByExt(File dir, Set<String> exts) {
        List<FileModEntry> result = new ArrayList<>();
        File[] files = dir.listFiles();
        if (files != null) {
            for (File f : files) {
                if (!f.isFile()) continue;
                String name = f.getName();
                String lower = name.toLowerCase(Locale.ROOT);
                boolean enabled = !lower.endsWith(DISABLED_SUFFIX);
                String stripped = enabled ? name : name.substring(0, name.length() - DISABLED_SUFFIX.length());
                String strippedLower = stripped.toLowerCase(Locale.ROOT);

                boolean matched = false;
                for (String ext : exts) {
                    if (strippedLower.endsWith(ext)) {
                        matched = true;
                        break;
                    }
                }
                if (!matched) continue;

                FileModEntry e = new FileModEntry();
                e.name = name;
                e.displayName = stripped;
                e.file = f;
                e.enabled = enabled;
                e.sizeBytes = f.length();
                result.add(e);
            }
        }
        Collections.sort(result, Comparator.comparing(a -> a.displayName.toLowerCase(Locale.ROOT)));
        return result;
    }

    // ══════════ Generic File Operations & Log Reader ══════════

    public static File toggleDisabledFile(File file, boolean enable) {
        if (file == null || !file.exists()) return null;
        String name = file.getName();
        boolean currentlyDisabled = name.toLowerCase(Locale.ROOT).endsWith(DISABLED_SUFFIX);
        if (enable && !currentlyDisabled) return file;
        if (!enable && currentlyDisabled) return file;

        String newName = enable
                ? name.substring(0, name.length() - DISABLED_SUFFIX.length())
                : name + DISABLED_SUFFIX;
        File dest = new File(file.getParentFile(), newName);
        if (dest.exists()) dest.delete();
        return file.renameTo(dest) ? dest : null;
    }

    public static File importFileToDir(Context ctx, Uri uri, File targetDir, String fallbackName) throws IOException {
        mkdirSafe(targetDir);
        String name = queryDisplayName(ctx, uri);
        if (name == null || name.trim().isEmpty()) {
            name = fallbackName;
        }
        name = sanitizeFileName(name);
        if (name.isEmpty()) name = fallbackName;

        File dest = new File(targetDir, name);
        String canonicalTarget = targetDir.getCanonicalPath() + File.separator;
        if (!dest.getCanonicalPath().startsWith(canonicalTarget)) {
            throw new IOException(I18n.tr("非法文件名: ") + name);
        }
        File tmp = new File(targetDir, name + ".importing");
        try (InputStream in = ctx.getContentResolver().openInputStream(uri)) {
            if (in == null) throw new IOException(I18n.tr("无法读取所选文件"));
            try (FileOutputStream out = new FileOutputStream(tmp)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            }
        } catch (IOException e) {
            tmp.delete();
            throw e;
        }
        if (dest.exists()) dest.delete();
        if (!tmp.renameTo(dest)) {
            tmp.delete();
            throw new IOException(I18n.tr("保存文件失败: ") + name);
        }
        return dest;
    }

    public static boolean deleteRecursive(File file) {
        if (file == null || !file.exists()) return false;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File c : children) deleteRecursive(c);
            }
        }
        return file.delete();
    }

    public static String readLogTail(File file, int maxBytes) {
        if (file == null || !file.isFile()) return "";
        long len = file.length();
        if (len <= 0) return "";
        long start = Math.max(0, len - maxBytes);
        int toRead = (int) (len - start);
        byte[] buf = new byte[toRead];
        try (RandomAccessFile raf = new RandomAccessFile(file, "r")) {
            raf.seek(start);
            raf.readFully(buf);
            String text = new String(buf, StandardCharsets.UTF_8);
            if (start > 0) {
                int nl = text.indexOf('\n');
                if (nl >= 0 && nl + 1 < text.length()) {
                    text = I18n.tr("... (仅显示末尾日志)\n") + text.substring(nl + 1);
                }
            }
            return text;
        } catch (IOException e) {
            return I18n.tr("读取日志失败: ") + e.getMessage();
        }
    }

    public static String queryDisplayName(Context ctx, Uri uri) {
        if (uri == null) return null;
        try (Cursor cursor = ctx.getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) return cursor.getString(idx);
            }
        } catch (Exception ignored) {
        }
        String last = uri.getLastPathSegment();
        if (last != null) {
            int slash = last.lastIndexOf('/');
            return slash >= 0 ? last.substring(slash + 1) : last;
        }
        return null;
    }

    private static void measureDir(File dir, long[] stats, int depth) {
        if (depth > 16 || dir == null) return;
        File[] list = dir.listFiles();
        if (list == null) return;
        for (File f : list) {
            if (f.getName().startsWith(".")) continue;
            if (f.isDirectory()) {
                measureDir(f, stats, depth + 1);
            } else if (f.isFile()) {
                stats[0]++;
                stats[1] += f.length();
            }
        }
    }

    private static String sanitizeFolderName(String raw) {
        if (raw == null) return "";
        String cleaned = raw.trim().replaceAll("[\\\\/:*?\"<>|]", "_");
        while (cleaned.startsWith(".") || cleaned.startsWith(" ")) {
            cleaned = cleaned.substring(1);
        }
        cleaned = cleaned.trim();
        if (".".equals(cleaned) || "..".equals(cleaned)) return "";
        return cleaned;
    }

    private static String sanitizeFileName(String raw) {
        if (raw == null) return "";
        String cleaned = raw.trim().replaceAll("[\\\\/:*?\"<>|]", "_");
        while (cleaned.startsWith(".") || cleaned.startsWith(" ")) {
            cleaned = cleaned.substring(1);
        }
        cleaned = cleaned.trim();
        if (".".equals(cleaned) || "..".equals(cleaned)) return "";
        return cleaned;
    }
}
