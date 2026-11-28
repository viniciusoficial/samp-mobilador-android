package com.sampmobile.game;

import android.util.Log;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.CRC32;

public final class ArtworkDownloader {
    private static final String TAG = "ArtworkDownloader";
    private static final String USER_AGENT = "SAMP/0.3";
    private static final int MAX_WORKERS = 4;
    private static final int MAX_REDIRECTS = 5;
    private static final int MAX_URL_LEN = 1024;
    private static final int CONNECT_TIMEOUT_MS = 10000;
    private static final int READ_TIMEOUT_MS = 15000;
    private static final long MAX_FILE_BYTES = 64L * 1024L * 1024L;

    // Must stay numerically identical to artwork::DownloadFailureKind in ArtworkCore.h.
    public static final int ERR_NONE = 0;
    public static final int ERR_TRANSIENT_NET = 1;
    public static final int ERR_HTTP_SERVER = 2;
    public static final int ERR_HTTP_CLIENT = 3;
    public static final int ERR_UNSAFE_URL = 4;
    public static final int ERR_SIZE_MISMATCH = 5;
    public static final int ERR_CRC_MISMATCH = 6;
    public static final int ERR_INVALID_FORMAT = 7;
    public static final int ERR_CANCELLED = 8;
    public static final int ERR_BUDGET_EXCEEDED = 9;
    public static final int ERR_UNSUPPORTED = 10;

    private static final class ActiveTask {
        final int generation;
        volatile HttpURLConnection connection;
        volatile Future<?> future;

        ActiveTask(int generation) {
            this.generation = generation;
        }
    }

    private static final AtomicInteger sGeneration = new AtomicInteger(0);
    private static final Set<ActiveTask> sActiveTasks = ConcurrentHashMap.newKeySet();

    private static final ExecutorService sPool = Executors.newFixedThreadPool(MAX_WORKERS, r -> {
        Thread t = new Thread(r, "SAMP-ArtworkDL");
        t.setDaemon(true);
        return t;
    });

    private ArtworkDownloader() {}

    private static native void onDownloadProgress(int generation, int fileType, int crc, long downloaded, long total);

    private static native void onDownloadFinished(int generation, int fileType, int crc, boolean success, int failureKind, String error);

    // Pure stateless off-thread native DFF/TXD structural validator + atomic rename(tempPath -> finalPath).
    // Returns one of ERR_NONE / ERR_INVALID_FORMAT / ERR_BUDGET_EXCEEDED / ERR_TRANSIENT_NET.
    private static native int validateAndCommitFile(int fileType, String tempPath, String finalPath);

    public static void cancelAll(int newGeneration) {
        sGeneration.set(newGeneration);
        for (ActiveTask task : sActiveTasks) {
            if (task.generation != newGeneration) {
                Future<?> f = task.future;
                if (f != null) {
                    f.cancel(true);
                }
                HttpURLConnection c = task.connection;
                if (c != null) {
                    try {
                        c.disconnect();
                    } catch (Throwable ignored) {
                    }
                }
            }
        }
    }

    public static void enqueue(int generation, int fileType, int crc, String url, String fallbackUrl,
                               String tempPath, String finalPath, long expectedSize) {
        if (url == null || url.isEmpty() || tempPath == null || tempPath.isEmpty() || finalPath == null || finalPath.isEmpty()) {
            safeNotifyFinished(generation, fileType, crc, false, ERR_UNSAFE_URL, "invalid arguments");
            return;
        }
        ActiveTask task = new ActiveTask(generation);
        sActiveTasks.add(task);
        task.future = sPool.submit(() -> runDownload(task, fileType, crc, url, fallbackUrl, tempPath, finalPath, expectedSize));
    }

    private static boolean isSafeRedirectUrl(String url) {
        if (url == null || url.isEmpty() || url.length() > MAX_URL_LEN) {
            return false;
        }
        for (int i = 0; i < url.length(); ++i) {
            char ch = url.charAt(i);
            if (ch <= 0x20 || ch >= 0x7F) {
                return false;
            }
        }
        try {
            URL u = new URL(url);
            String proto = u.getProtocol();
            String host = u.getHost();
            return ("http".equalsIgnoreCase(proto) || "https".equalsIgnoreCase(proto))
                    && host != null && !host.isEmpty();
        } catch (Throwable t) {
            return false;
        }
    }

    private static void runDownload(ActiveTask task, int fileType, int crc, String initialUrl, String fallbackUrl,
                                    String tempPath, String finalPath, long expectedSize) {
        final int generation = task.generation;
        if (sGeneration.get() != generation || Thread.currentThread().isInterrupted()) {
            sActiveTasks.remove(task);
            return;
        }

        File outFile = new File(tempPath);
        File parent = outFile.getParentFile();
        if (parent != null && !parent.exists()) {
            parent.mkdirs();
        }
        if (outFile.exists()) {
            outFile.delete();
        }

        HttpURLConnection conn = null;
        boolean keepAliveReusable = false;
        String currentUrl = initialUrl;
        String nextFallback = (fallbackUrl != null && !fallbackUrl.isEmpty() && !fallbackUrl.equals(initialUrl))
                ? fallbackUrl : null;
        try {
            int redirects = 0;
            while (true) {
                if (sGeneration.get() != generation || Thread.currentThread().isInterrupted()) {
                    return;
                }
                if (!isSafeRedirectUrl(currentUrl)) {
                    safeNotifyFinished(generation, fileType, crc, false, ERR_UNSAFE_URL, "unsafe URL");
                    return;
                }
                int code;
                try {
                    URL u = new URL(currentUrl);
                    conn = (HttpURLConnection) u.openConnection();
                    task.connection = conn;
                    if (sGeneration.get() != generation || Thread.currentThread().isInterrupted()) {
                        return;
                    }
                    conn.setRequestMethod("GET");
                    conn.setConnectTimeout(nextFallback != null ? 4000 : CONNECT_TIMEOUT_MS);
                    conn.setReadTimeout(READ_TIMEOUT_MS);
                    conn.setInstanceFollowRedirects(false);
                    conn.setUseCaches(false);
                    conn.setRequestProperty("User-Agent", USER_AGENT);
                    conn.setRequestProperty("Accept", "*/*");
                    // Leave HTTP/1.1 Keep-Alive enabled so thousands of model files reuse a handful
                    // of persistent TCP connections instead of triggering server SYN/rate limits.

                    code = conn.getResponseCode();
                } catch (Throwable connectErr) {
                    if (conn != null) {
                        task.connection = null;
                        try { conn.disconnect(); } catch (Throwable ignored) {}
                        conn = null;
                    }
                    if (nextFallback != null && sGeneration.get() == generation && !Thread.currentThread().isInterrupted()) {
                        currentUrl = nextFallback;
                        nextFallback = null;
                        redirects = 0;
                        continue;
                    }
                    throw connectErr;
                }

                if (code == HttpURLConnection.HTTP_UNAUTHORIZED && nextFallback != null) {
                    // open.mp built-in webserver returns 401 when its advertised private adapter IP
                    // differs from the connected server IP; fall back to the game server host.
                    task.connection = null;
                    conn.disconnect();
                    conn = null;
                    currentUrl = nextFallback;
                    nextFallback = null;
                    redirects = 0;
                    continue;
                }

                if (code == HttpURLConnection.HTTP_MOVED_PERM
                        || code == HttpURLConnection.HTTP_MOVED_TEMP
                        || code == HttpURLConnection.HTTP_SEE_OTHER
                        || code == 307 || code == 308) {
                    String loc = conn.getHeaderField("Location");
                    URL baseUrl = conn.getURL();
                    task.connection = null;
                    conn.disconnect();
                    conn = null;
                    if (loc == null || loc.isEmpty() || ++redirects > MAX_REDIRECTS) {
                        safeNotifyFinished(generation, fileType, crc, false, ERR_UNSAFE_URL, "redirect failed");
                        return;
                    }
                    String nextUrl = new URL(baseUrl, loc).toString();
                    if (!isSafeRedirectUrl(nextUrl)) {
                        safeNotifyFinished(generation, fileType, crc, false, ERR_UNSAFE_URL, "unsafe redirect");
                        return;
                    }
                    currentUrl = nextUrl;
                    continue;
                }

                if (code != HttpURLConnection.HTTP_OK) {
                    int kind = (code >= 500 || code == 408 || code == 429) ? ERR_HTTP_SERVER : ERR_HTTP_CLIENT;
                    safeNotifyFinished(generation, fileType, crc, false, kind, "HTTP " + code);
                    return;
                }
                break;
            }

            long contentLength = conn.getContentLengthLong();
            long total = expectedSize > 0 ? expectedSize : Math.max(0L, contentLength);
            long sizeCap = expectedSize > 0 ? expectedSize : MAX_FILE_BYTES;
            if (sizeCap > MAX_FILE_BYTES) {
                sizeCap = MAX_FILE_BYTES;
            }
            if (contentLength > 0 && expectedSize > 0 && contentLength != expectedSize) {
                Log.w(TAG, "Content-Length mismatch for crc=0x" + Integer.toHexString(crc)
                        + ": header=" + contentLength + " expected=" + expectedSize);
            }

            long downloaded = 0L;
            long lastNotifyMs = 0L;
            byte[] buf = new byte[16384];
            CRC32 hasher = new CRC32();

            try (InputStream in = conn.getInputStream();
                 BufferedOutputStream out = new BufferedOutputStream(new FileOutputStream(outFile), 32768)) {
                int n;
                while ((n = in.read(buf)) != -1) {
                    if (sGeneration.get() != generation || Thread.currentThread().isInterrupted()) {
                        return;
                    }
                    downloaded += n;
                    if (downloaded > sizeCap) {
                        outFile.delete();
                        safeNotifyFinished(generation, fileType, crc, false, ERR_SIZE_MISMATCH, "file exceeded expected size");
                        return;
                    }
                    hasher.update(buf, 0, n);
                    out.write(buf, 0, n);
                    long now = System.currentTimeMillis();
                    if (now - lastNotifyMs >= 120L) {
                        lastNotifyMs = now;
                        safeNotifyProgress(generation, fileType, crc, downloaded, total);
                    }
                }
                out.flush();
                // Full response body read to EOF; closing InputStream returns the TCP socket to the
                // HTTP/1.1 Keep-Alive pool instead of tearing it down via conn.disconnect().
                keepAliveReusable = true;
            }

            if (sGeneration.get() != generation || Thread.currentThread().isInterrupted()) {
                outFile.delete();
                return;
            }

            if (downloaded <= 0 || (expectedSize > 0 && downloaded != expectedSize)) {
                outFile.delete();
                safeNotifyFinished(generation, fileType, crc, false, ERR_SIZE_MISMATCH,
                        "size mismatch (" + downloaded + " vs " + expectedSize + ")");
                return;
            }

            if (((int) hasher.getValue()) != crc) {
                outFile.delete();
                safeNotifyFinished(generation, fileType, crc, false, ERR_CRC_MISMATCH, "CRC32 mismatch");
                return;
            }

            // Perform structural DFF/TXD validation + atomic commit off GameMain thread.
            int commitCode = safeValidateAndCommit(fileType, tempPath, finalPath);
            if (commitCode != ERR_NONE) {
                outFile.delete();
                String errLabel = (commitCode == ERR_BUDGET_EXCEEDED) ? "texture budget exceeded"
                        : ((commitCode == ERR_INVALID_FORMAT) ? "invalid DFF/TXD format" : "commit failed");
                safeNotifyFinished(generation, fileType, crc, false, commitCode, errLabel);
                return;
            }

            safeNotifyProgress(generation, fileType, crc, downloaded, total);
            safeNotifyFinished(generation, fileType, crc, true, ERR_NONE, "");
        } catch (Throwable t) {
            if (outFile.exists()) {
                outFile.delete();
            }
            if (sGeneration.get() == generation && !Thread.currentThread().isInterrupted()) {
                String msg = t.getMessage();
                if (msg == null || msg.isEmpty()) {
                    msg = t.getClass().getSimpleName();
                }
                Log.w(TAG, "Download failed crc=0x" + Integer.toHexString(crc) + ": " + msg);
                safeNotifyFinished(generation, fileType, crc, false, ERR_TRANSIENT_NET, msg);
            }
        } finally {
            task.connection = null;
            sActiveTasks.remove(task);
            if (conn != null && !keepAliveReusable) {
                try {
                    conn.disconnect();
                } catch (Throwable ignored) {
                }
            }
        }
    }

    private static int safeValidateAndCommit(int fileType, String tempPath, String finalPath) {
        try {
            return validateAndCommitFile(fileType, tempPath, finalPath);
        } catch (UnsatisfiedLinkError e) {
            return ERR_UNSUPPORTED;
        }
    }

    private static void safeNotifyProgress(int generation, int fileType, int crc, long downloaded, long total) {
        try {
            onDownloadProgress(generation, fileType, crc, downloaded, total);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    private static void safeNotifyFinished(int generation, int fileType, int crc, boolean success, int failureKind, String error) {
        try {
            onDownloadFinished(generation, fileType, crc, success, failureKind, error != null ? error : "");
        } catch (UnsatisfiedLinkError ignored) {
        }
    }
}
