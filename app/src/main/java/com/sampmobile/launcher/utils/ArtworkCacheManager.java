package com.sampmobile.launcher.utils;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Manages the SA-MP 0.3.DL / open.mp custom model disk cache under {@code files/SAMP/cache/}.
 * Supports scanning per-server & shared {@code local/} cache directories, safely importing
 * PC 0.3.DL cache {@code .zip} archives or individual {@code 0x*.dff} / {@code 0x*.txd} files,
 * and deleting individual server folders or clearing the entire cache.
 */
public final class ArtworkCacheManager {

    private static final int IO_BUFFER_SIZE = 64 * 1024;
    private static final long MAX_SINGLE_FILE_BYTES = 64L * 1024L * 1024L;
    private static final int MAX_FOLDER_NAME_LEN = 96;

    private ArtworkCacheManager() {}

    public static final class FolderEntry {
        public final String name;
        public final boolean isLocal;
        public final int fileCount;
        public final long totalBytes;

        public FolderEntry(String name, boolean isLocal, int fileCount, long totalBytes) {
            this.name = name;
            this.isLocal = isLocal;
            this.fileCount = fileCount;
            this.totalBytes = totalBytes;
        }
    }

    public static final class CacheSummary {
        public final int totalFiles;
        public final long totalBytes;
        public final List<FolderEntry> folders;

        public CacheSummary(int totalFiles, long totalBytes, List<FolderEntry> folders) {
            this.totalFiles = totalFiles;
            this.totalBytes = totalBytes;
            this.folders = folders;
        }
    }

    public static final class ImportResult {
        public final int importedFiles;
        public final int skippedFiles;
        public final long importedBytes;
        public final boolean cancelled;

        public ImportResult(int importedFiles, int skippedFiles, long importedBytes, boolean cancelled) {
            this.importedFiles = importedFiles;
            this.skippedFiles = skippedFiles;
            this.importedBytes = importedBytes;
            this.cancelled = cancelled;
        }
    }

    public interface ProgressListener {
        void onProgress(int importedFiles, int skippedFiles, String currentName);
    }

    public static File getCacheRoot(Context context) {
        if (context == null) return null;
        File base = context.getExternalFilesDir(null);
        if (base == null) return null;
        return new File(base, "SAMP/cache");
    }

    /**
     * Validates and canonicalizes a 0.3.DL cache filename ({@code 0x<hex>.dff} or {@code 0x<hex>.txd})
     * to match C++ {@code artwork::formatCacheFileName()} ({@code 0x%X.dff} / {@code 0x%X.txd}).
     * Returns {@code null} for any non-cache filename.
     */
    public static String normalizeCacheFileName(String rawName) {
        if (rawName == null) return null;
        String trimmed = rawName.trim();
        if (trimmed.length() < 7 || trimmed.length() > 16) return null;
        if (trimmed.charAt(0) != '0' || (trimmed.charAt(1) != 'x' && trimmed.charAt(1) != 'X')) {
            return null;
        }
        int dot = trimmed.lastIndexOf('.');
        if (dot <= 2 || dot >= trimmed.length() - 1) return null;

        String ext = trimmed.substring(dot + 1).toLowerCase(Locale.US);
        if (!"dff".equals(ext) && !"txd".equals(ext)) {
            return null;
        }

        String hex = trimmed.substring(2, dot);
        if (hex.isEmpty() || hex.length() > 8) return null;
        long val = 0;
        for (int i = 0; i < hex.length(); i++) {
            char c = hex.charAt(i);
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
            else return null;
            val = (val << 4) | digit;
        }
        if (val == 0L) return null;
        return String.format(Locale.US, "0x%X.%s", (int) val, ext);
    }

    public static boolean isSafeServerFolderName(String name) {
        if (name == null || name.isEmpty() || name.length() > MAX_FOLDER_NAME_LEN) {
            return false;
        }
        if (".".equals(name) || "..".equals(name) || name.startsWith(".") || name.endsWith(".")) {
            return false;
        }
        boolean hasDot = false;
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z')
                    || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9')
                    || c == '.' || c == '_' || c == '-';
            if (!ok) return false;
            if (c == '.') hasDot = true;
        }
        return hasDot;
    }

    /**
     * Resolves a ZIP entry path to a safe relative destination under {@code SAMP/cache/}, e.g.
     * {@code "local/0x1234ABCD.dff"} or {@code "116.62.52.151.7777/0x1234ABCD.txd"}.
     * Returns {@code null} if the entry is not a valid 0.3.DL cache file or fails safety checks.
     */
    public static String resolveZipEntryTarget(String rawEntryName) {
        if (rawEntryName == null || rawEntryName.isEmpty()) return null;
        String unified = rawEntryName.replace('\\', '/');
        while (unified.startsWith("/")) {
            unified = unified.substring(1);
        }
        if (unified.isEmpty() || unified.endsWith("/")) return null;

        String[] parts = unified.split("/");
        List<String> clean = new ArrayList<>(parts.length);
        for (String p : parts) {
            if (p.isEmpty() || ".".equals(p)) continue;
            if ("..".equals(p)) return null; // Zip-Slip guard
            clean.add(p);
        }
        if (clean.isEmpty()) return null;

        String canonicalFile = normalizeCacheFileName(clean.get(clean.size() - 1));
        if (canonicalFile == null) return null;

        if (clean.size() >= 2) {
            String parent = clean.get(clean.size() - 2);
            if ("local".equalsIgnoreCase(parent)) {
                return "local/" + canonicalFile;
            }
            if (isSafeServerFolderName(parent)) {
                return parent + "/" + canonicalFile;
            }
        }
        // Flat zip or generic wrapper directory -> put into shared local/ cache so every server hits it.
        return "local/" + canonicalFile;
    }

    public static CacheSummary scan(File cacheRoot) {
        if (cacheRoot == null || !cacheRoot.isDirectory()) {
            return new CacheSummary(0, 0L, Collections.emptyList());
        }
        File[] children = cacheRoot.listFiles();
        if (children == null || children.length == 0) {
            return new CacheSummary(0, 0L, Collections.emptyList());
        }

        List<FolderEntry> folders = new ArrayList<>();
        int totalFiles = 0;
        long totalBytes = 0L;

        for (File child : children) {
            if (!child.isDirectory()) continue;
            String name = child.getName();
            if (".".equals(name) || "..".equals(name)) continue;

            int count = 0;
            long bytes = 0L;
            File[] files = child.listFiles();
            if (files != null) {
                for (File f : files) {
                    if (f.isFile()) {
                        count++;
                        bytes += Math.max(0L, f.length());
                    }
                }
            }
            if (count == 0) continue; // skip empty directories like freshly created local/
            boolean isLocal = "local".equalsIgnoreCase(name);
            folders.add(new FolderEntry(name, isLocal, count, bytes));
            totalFiles += count;
            totalBytes += bytes;
        }

        Collections.sort(folders, (a, b) -> {
            if (a.isLocal != b.isLocal) return a.isLocal ? -1 : 1;
            int cmp = Long.compare(b.totalBytes, a.totalBytes);
            return cmp != 0 ? cmp : a.name.compareToIgnoreCase(b.name);
        });

        return new CacheSummary(totalFiles, totalBytes, folders);
    }

    public static boolean deleteFolder(File cacheRoot, String folderName) {
        if (cacheRoot == null || folderName == null || folderName.isEmpty()) return false;
        if (folderName.contains("/") || folderName.contains("\\") || "..".equals(folderName) || ".".equals(folderName)) {
            return false;
        }
        File target = new File(cacheRoot, folderName);
        try {
            String rootCanon = cacheRoot.getCanonicalPath();
            String targetCanon = target.getCanonicalPath();
            if (!targetCanon.startsWith(rootCanon + File.separator)) {
                return false;
            }
        } catch (IOException e) {
            return false;
        }
        return deleteRecursively(target);
    }

    public static int clearAll(File cacheRoot) {
        if (cacheRoot == null || !cacheRoot.isDirectory()) return 0;
        File[] children = cacheRoot.listFiles();
        if (children == null) return 0;
        int deleted = 0;
        for (File child : children) {
            if (deleteRecursively(child)) {
                deleted++;
            }
        }
        return deleted;
    }

    private static boolean deleteRecursively(File f) {
        if (f == null || !f.exists()) return false;
        if (f.isDirectory()) {
            File[] kids = f.listFiles();
            if (kids != null) {
                for (File k : kids) {
                    deleteRecursively(k);
                }
            }
        }
        return f.delete();
    }

    public static ImportResult importFromUris(Context context, List<Uri> uris, File cacheRoot,
                                              ProgressListener listener, AtomicBoolean cancelled) throws IOException {
        if (context == null || uris == null || uris.isEmpty() || cacheRoot == null) {
            return new ImportResult(0, 0, 0L, false);
        }
        if (!cacheRoot.exists()) {
            cacheRoot.mkdirs();
        }
        String cacheRootCanon = cacheRoot.getCanonicalPath();
        ContentResolver resolver = context.getContentResolver();

        int[] counts = new int[2]; // [0]=imported, [1]=skipped
        long[] bytes = new long[1];
        byte[] buf = new byte[IO_BUFFER_SIZE];

        for (Uri uri : uris) {
            if (cancelled != null && cancelled.get()) {
                return new ImportResult(counts[0], counts[1], bytes[0], true);
            }
            if (uri == null) continue;
            String displayName = queryDisplayName(resolver, uri);
            String lower = displayName != null ? displayName.toLowerCase(Locale.US) : "";

            if (lower.endsWith(".zip")) {
                importZipUri(resolver, uri, cacheRoot, cacheRootCanon, buf, counts, bytes, listener, cancelled);
            } else {
                String canonical = normalizeCacheFileName(displayName);
                if (canonical == null) {
                    counts[1]++;
                    continue;
                }
                File dstDir = new File(cacheRoot, "local");
                if (!dstDir.exists()) dstDir.mkdirs();
                File dst = new File(dstDir, canonical);
                try (InputStream raw = resolver.openInputStream(uri)) {
                    if (raw == null) {
                        counts[1]++;
                        continue;
                    }
                    long written = writeAtomically(raw, dst, buf, cancelled);
                    if (written > 0) {
                        counts[0]++;
                        bytes[0] += written;
                        if (listener != null) {
                            listener.onProgress(counts[0], counts[1], "local/" + canonical);
                        }
                    } else {
                        counts[1]++;
                    }
                }
            }
        }

        boolean wasCancelled = cancelled != null && cancelled.get();
        return new ImportResult(counts[0], counts[1], bytes[0], wasCancelled);
    }

    private static void importZipUri(ContentResolver resolver, Uri zipUri, File cacheRoot, String cacheRootCanon,
                                     byte[] buf, int[] counts, long[] bytes,
                                     ProgressListener listener, AtomicBoolean cancelled) throws IOException {
        try (InputStream raw = resolver.openInputStream(zipUri)) {
            if (raw == null) {
                throw new IOException(I18n.tr("无法打开所选压缩包"));
            }
            try (ZipInputStream zis = new ZipInputStream(new BufferedInputStream(raw, IO_BUFFER_SIZE))) {
                ZipEntry entry;
                long lastNotifyMs = 0L;
                while ((entry = zis.getNextEntry()) != null) {
                    if (cancelled != null && cancelled.get()) {
                        return;
                    }
                    if (entry.isDirectory()) {
                        zis.closeEntry();
                        continue;
                    }
                    String relTarget = resolveZipEntryTarget(entry.getName());
                    if (relTarget == null) {
                        counts[1]++;
                        zis.closeEntry();
                        continue;
                    }
                    File dst = new File(cacheRoot, relTarget);
                    File parent = dst.getParentFile();
                    if (parent != null && !parent.exists()) {
                        parent.mkdirs();
                    }
                    String dstCanon = dst.getCanonicalPath();
                    if (!dstCanon.startsWith(cacheRootCanon + File.separator)) {
                        counts[1]++;
                        zis.closeEntry();
                        continue;
                    }
                    long written = writeAtomically(zis, dst, buf, cancelled);
                    zis.closeEntry();
                    if (written > 0) {
                        counts[0]++;
                        bytes[0] += written;
                        long now = System.currentTimeMillis();
                        if (listener != null && (now - lastNotifyMs >= 80L || counts[0] <= 3)) {
                            lastNotifyMs = now;
                            listener.onProgress(counts[0], counts[1], relTarget);
                        }
                    } else {
                        counts[1]++;
                    }
                }
            }
        }
    }

    private static long writeAtomically(InputStream in, File dst, byte[] buf, AtomicBoolean cancelled) throws IOException {
        File tmp = new File(dst.getParentFile(), dst.getName() + ".imp.tmp");
        long total = 0L;
        boolean ok = false;
        try {
            try (FileOutputStream fos = new FileOutputStream(tmp)) {
                int n;
                while ((n = in.read(buf)) != -1) {
                    if (cancelled != null && cancelled.get()) {
                        return -1L;
                    }
                    total += n;
                    if (total > MAX_SINGLE_FILE_BYTES) {
                        return -1L;
                    }
                    fos.write(buf, 0, n);
                }
                fos.flush();
            }
            if (total <= 0L) {
                return -1L;
            }
            if (dst.exists()) {
                dst.delete();
            }
            ok = tmp.renameTo(dst);
            return ok ? total : -1L;
        } finally {
            if (!ok && tmp.exists()) {
                tmp.delete();
            }
        }
    }

    private static String queryDisplayName(ContentResolver resolver, Uri uri) {
        if (resolver == null || uri == null) return null;
        try (Cursor c = resolver.query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                String name = c.getString(0);
                if (name != null && !name.isEmpty()) return name;
            }
        } catch (Throwable ignored) {
        }
        String last = uri.getLastPathSegment();
        if (last != null) {
            int slash = last.lastIndexOf('/');
            return (slash >= 0) ? last.substring(slash + 1) : last;
        }
        return null;
    }
}
