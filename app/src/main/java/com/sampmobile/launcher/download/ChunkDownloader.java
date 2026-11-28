package com.sampmobile.launcher.download;


import com.sampmobile.launcher.utils.I18n;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 分片下载器：断点续传 + 有限并发 + 逐片校验。
 *
 * 选 HttpURLConnection 而不是 OkHttp：本项目历史上零第三方网络依赖，
 * 为了一个下载器引入 800KB 的 OkHttp（还带 Okio）不值得。HttpURLConnection
 * 支持 Range、能设超时、能中断，够用。
 *
 * 状态机（每片独立）：
 *   不存在 -> .part(下载中) -> 校验通过 -> .chunk(完成)
 *                          └ 校验失败 -> 删除 .part，下一轮重下
 *
 * 断点续传靠 HTTP Range：.part 已存在时从它的长度往后请求；服务器若不支持
 * Range（返回 200 而非 206），回退成整片重下，不静默拼错。
 */
public final class ChunkDownloader {

    private static final String TAG = "ChunkDownloader";

    /** 单片下载的超时。跨境线路慢，给足时间但别无限等。 */
    private static final int CONNECT_TIMEOUT_MS = 15000;
    private static final int READ_TIMEOUT_MS = 30000;

    /** 单片最多重试次数（含首次）。超过就整体失败，不无限重试。 */
    private static final int MAX_ATTEMPTS = 3;

    /** 回调（在下载线程上调用，调用方自己切主线程）。 */
    public interface Listener {
        /** @param bytesDone 已完成的字节数（跨所有片累计） */
        void onProgress(long bytesDone, long bytesTotal);

        /** 单片完成。 */
        void onChunkDone(int chunkIndex, int chunkCount);

        /** 正在下载某片时的小提示，如"重试 2/3"。 */
        void onStatus(String message);
    }

    private final File cacheDir;
    private final AtomicBoolean cancelled = new AtomicBoolean(false);

    public ChunkDownloader(File cacheDir) {
        this.cacheDir = cacheDir;
    }

    public void cancel() {
        cancelled.set(true);
    }

    public boolean isCancelled() {
        return cancelled.get();
    }

    /**
     * 下载一个包的全部分片。
     *
     * @param baseUrl 清单所在目录的绝对前缀（如 https://cdn.example.com/），
     *                分片的相对 url 会拼在它后面
     * @return 所有分片都已就位且校验通过
     */
    public boolean downloadPackage(String baseUrl, DataManifest.Package pkg, Listener listener)
            throws IOException, InterruptedException {

        File dir = new File(cacheDir, pkg.id);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            throw new IOException(I18n.tr("无法创建缓存目录：") + dir);
        }

        final int count = pkg.chunks.size();

        // 清掉长度不符的 .chunk（把半个文件当成品会让拼装直接失败），
        // 然后按磁盘现状报一次初始进度，避免进度条从 0 跳。
        for (DataManifest.Chunk c : pkg.chunks) {
            File finalized = chunkFile(dir, c, true);
            if (finalized.isFile() && finalized.length() != c.bytes) {
                if (!finalized.delete()) {
                    Log.w(TAG, I18n.tr("无法删除长度不符的分片 ") + finalized);
                }
            }
        }
        final AtomicLong done = new AtomicLong(completedBytes(dir, pkg));
        listener.onProgress(done.get(), pkg.bytes);

        // 逐片顺序下载。并发留给上层多个包并行（game 与 online 可同时下），
        // 单包内部串行更好排查问题，也不会把带宽切得太碎。
        for (DataManifest.Chunk c : pkg.chunks) {
            if (cancelled.get()) throw new InterruptedException(I18n.tr("已取消"));

            File finalized = chunkFile(dir, c, true);
            if (finalized.isFile() && finalized.length() == c.bytes) {
                listener.onChunkDone(c.index, count);
                continue;
            }

            boolean ok = downloadChunk(baseUrl, dir, c, listener, done, pkg.bytes);
            if (!ok) {
                throw new IOException(I18n.tr("分片 ") + c.index + I18n.tr(" 下载失败（已重试 ") + MAX_ATTEMPTS + I18n.tr(" 次）"));
            }
            // 一片落定后按磁盘现状重算，作为下一片的进度基数。
            // 只增不减也无法出错：completedBytes 只统计真正到位的字节。
            done.set(completedBytes(dir, pkg));
            listener.onProgress(done.get(), pkg.bytes);
            listener.onChunkDone(c.index, count);
        }

        if (cancelled.get()) throw new InterruptedException(I18n.tr("已取消"));
        return true;
    }

    /**
     * 已完成字节数 = 已转正的 .chunk 长度之和 + 当前 .part 的长度。
     *
     * 不用累加式计数器：哈希失败重下、服务器忽略 Range 重来、续传起点
     * 来自上次运行的文件……这些情况下"加了多少就减多少"很难保持平衡，
     * 一旦减多进度就变负、减少了进度就虚高。按磁盘现状重算永远是对的，
     * 代价只是每片扫一次目录（片数最多几百）。
     */
    private static long completedBytes(File dir, DataManifest.Package pkg) {
        long total = 0;
        for (DataManifest.Chunk c : pkg.chunks) {
            File finalized = chunkFile(dir, c, true);
            if (finalized.isFile() && finalized.length() == c.bytes) {
                total += c.bytes;
                continue;
            }
            File part = chunkFile(dir, c, false);
            if (part.isFile() && part.length() > 0) {
                total += Math.min(part.length(), c.bytes);
            }
        }
        return total;
    }

    private boolean downloadChunk(String baseUrl, File dir, DataManifest.Chunk c,
                                  Listener listener, AtomicLong done, long totalBytes)
            throws InterruptedException {

        String urlStr = resolveUrl(baseUrl, c.url);
        File part = chunkFile(dir, c, false);
        File finalized = chunkFile(dir, c, true);

        for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
            if (cancelled.get()) throw new InterruptedException(I18n.tr("已取消"));

            try {
                long existing = part.isFile() ? part.length() : 0;
                // 已下完但还没改名：直接校验后转正。
                if (existing == c.bytes) {
                    if (verify(part, c.sha256)) {
                        if (!part.renameTo(finalized)) {
                            throw new IOException(I18n.tr("重命名分片失败：") + part);
                        }
                        return true;
                    }
                    // 大小对但哈希不对：删掉整片重来，不能续传（内容已损坏）。
                    // 注意不能减 done —— 这片只有在校验通过转正时才 addAndGet，
                    // 此处从未计入，做减法会让进度变成负数。
                    if (!part.delete()) {
                        throw new IOException(I18n.tr("无法删除损坏分片：") + part);
                    }
                    existing = 0;
                }

                if (attempt > 1) {
                    listener.onStatus(I18n.tr("第 ") + c.index + I18n.tr(" 片重试 ") + attempt + "/" + MAX_ATTEMPTS);
                }

                boolean got = fetchTo(urlStr, part, existing, c.bytes, done, totalBytes, listener);
                if (!got) {
                    // fetchTo 返回 false 表示服务器不支持 Range 且已有残留，已重置，下一轮再来
                    continue;
                }

                if (part.length() != c.bytes) {
                    throw new IOException(I18n.tr("分片长度不符：期望 ") + c.bytes + I18n.tr(" 实际 ") + part.length());
                }
                if (!verify(part, c.sha256)) {
                    throw new IOException(I18n.tr("分片哈希不符：") + c.index);
                }
                if (!part.renameTo(finalized)) {
                    throw new IOException(I18n.tr("重命名分片失败：") + part);
                }
                return true;

            } catch (IOException e) {
                Log.w(TAG, I18n.tr("分片 ") + c.index + I18n.tr(" 第 ") + attempt + I18n.tr(" 次失败: ") + e.getMessage());
                if (attempt == MAX_ATTEMPTS) return false;
                // 退避，避免连续失败打爆服务器
                Thread.sleep(800L * attempt);
            }
        }
        return false;
    }

    /**
     * 把一片下载/续传到 {@code target}。
     *
     * @param skip 已存在的字节数，作为 Range 起点
     * @return false 表示服务器忽略了 Range（返回 200 全量），已清空 target 让上层重来
     */
    private boolean fetchTo(String urlStr, File target, long skip, long expected,
                            AtomicLong done, long totalBytes, Listener listener)
            throws IOException, InterruptedException {

        HttpURLConnection conn = null;
        try {
            URL url = new URL(urlStr);
            conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
            conn.setReadTimeout(READ_TIMEOUT_MS);
            conn.setInstanceFollowRedirects(true);
            conn.setRequestProperty("Accept-Encoding", "identity");  // 压缩会破坏 Range
            if (skip > 0) {
                conn.setRequestProperty("Range", "bytes=" + skip + "-");
            }

            int code = conn.getResponseCode();
            if (code == 416) {
                // 请求区间不合法：说明本地 part 比远端还大，整片重来
                if (target.isFile() && !target.delete()) {
                    throw new IOException(I18n.tr("无法重置分片：") + target);
                }
                return false;
            }
            if (code != HttpURLConnection.HTTP_OK && code != HttpURLConnection.HTTP_PARTIAL) {
                throw new IOException("HTTP " + code + " from " + urlStr);
            }

            boolean serverHonouredRange = (code == HttpURLConnection.HTTP_PARTIAL);
            if (skip > 0 && !serverHonouredRange) {
                // 服务器不支持 Range：必须从头写，先把旧内容清掉。
                // skip 是 part 的既有长度，来源可能是上次运行留下的文件，
                // 并不一定计入过本轮的 done，减它会让进度变成负数。
                if (!target.delete() && target.exists()) {
                    throw new IOException(I18n.tr("无法重置分片：") + target);
                }
                skip = 0;
            }

            try (InputStream in = conn.getInputStream();
                 FileOutputStream out = new FileOutputStream(target, skip > 0)) {

                byte[] buf = new byte[64 * 1024];
                // done 记的是"已转正分片"的字节；当前片按实际写入量叠加，
                // 这样无论续传起点来自哪里、失败重试几次，进度都不会错。
                final long base = done.get();
                long written = skip;
                int n;
                while ((n = in.read(buf)) > 0) {
                    if (cancelled.get()) throw new InterruptedException(I18n.tr("已取消"));
                    out.write(buf, 0, n);
                    written += n;
                    listener.onProgress(base + written, totalBytes);
                    if (written > expected) {
                        // 远端比清单声明的还长：数据不对，别继续写。
                        throw new IOException(I18n.tr("分片超出预期长度 ") + expected);
                    }
                }
                out.flush();
            }
            return true;

        } finally {
            if (conn != null) conn.disconnect();
        }
    }

    // ------------------------------------------------------------------ 工具

    private static String resolveUrl(String baseUrl, String relative) {
        if (relative.startsWith("http://") || relative.startsWith("https://")) return relative;
        if (!baseUrl.endsWith("/")) baseUrl = baseUrl + "/";
        // 去掉相对路径开头的 ./ 或 /
        while (relative.startsWith("./")) relative = relative.substring(2);
        while (relative.startsWith("/")) relative = relative.substring(1);
        return baseUrl + relative;
    }

    private static File chunkFile(File dir, DataManifest.Chunk c, boolean finalized) {
        return new File(dir, c.fileName() + (finalized ? ".chunk" : ".part"));
    }

    /** 算文件 SHA-256 并比对。sha 为空表示不校验（整包仍会校验）。 */
    static boolean verify(File f, String sha256) {
        if (sha256 == null || sha256.isEmpty()) return true;
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            try (InputStream in = new java.io.FileInputStream(f)) {
                byte[] buf = new byte[128 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            }
            return toHex(md.digest()).equalsIgnoreCase(sha256);
        } catch (NoSuchAlgorithmException | IOException e) {
            Log.w(TAG, I18n.tr("校验失败 ") + f + ": " + e.getMessage());
            return false;
        }
    }

    /**
     * 把一个包的所有分片按序号拼接成单文件。
     *
     * 边拼边删：每片拷进输出流后立即删掉该片，峰值占用只有"输出文件 + 一个分片"，
     * 而不是"输出 + 全部分片"。2GB 包上这能省 2GB 空间 —— 手机上这点很关键。
     *
     * @param keepChunks true 时保留分片（调试用），正常路径传 false
     */
    public static boolean concatChunks(File dir, DataManifest.Package pkg, File out,
                                       boolean keepChunks) throws IOException {
        File parent = out.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException(I18n.tr("无法创建输出目录：") + parent);
        }
        File tmp = new File(out.getPath() + ".tmp");
        try (FileOutputStream fos = new FileOutputStream(tmp)) {
            for (DataManifest.Chunk c : pkg.chunks) {
                File f = chunkFile(dir, c, true);
                if (!f.isFile()) throw new IOException(I18n.tr("缺少分片 ") + f);
                try (InputStream in = new java.io.FileInputStream(f)) {
                    byte[] buf = new byte[256 * 1024];
                    int n;
                    while ((n = in.read(buf)) > 0) fos.write(buf, 0, n);
                }
                if (!keepChunks) {
                    // 已经拼进 tmp，原片不再需要。删失败不影响正确性，只影响空间。
                    if (!f.delete()) Log.w(TAG, I18n.tr("分片删除失败（不影响结果）: ") + f);
                }
            }
            fos.flush();
            fos.getFD().sync();
        }
        if (tmp.length() != pkg.bytes) {
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            throw new IOException(I18n.tr("拼装后大小不符：期望 ") + pkg.bytes + I18n.tr(" 实际 ") + tmp.length());
        }
        //noinspection ResultOfMethodCallIgnored
        out.delete();
        if (!tmp.renameTo(out)) throw new IOException(I18n.tr("无法重命名为 ") + out);
        return true;
    }

    /** 兼容旧调用（保留分片）。 */
    public static boolean concatChunks(File dir, DataManifest.Package pkg, File out)
            throws IOException {
        return concatChunks(dir, pkg, out, true);
    }

    static String toHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) sb.append(String.format(Locale.US, "%02x", b));
        return sb.toString();
    }

    /** 读取指定区间的字节（生成清单时用不到，留给上传脚本做一致性抽查）。 */
    static byte[] readRange(File f, long offset, int len) throws IOException {
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            raf.seek(offset);
            byte[] buf = new byte[len];
            int read = raf.read(buf);
            if (read <= 0) return new byte[0];
            if (read == len) return buf;
            byte[] trimmed = new byte[read];
            System.arraycopy(buf, 0, trimmed, 0, read);
            return trimmed;
        }
    }
}
