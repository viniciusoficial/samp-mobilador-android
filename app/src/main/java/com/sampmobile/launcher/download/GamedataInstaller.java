package com.sampmobile.launcher.download;


import com.sampmobile.launcher.utils.I18n;
import android.content.Context;
import android.util.Log;

import com.sampmobile.launcher.utils.GameDataImporter;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * 把下载好的数据包解压/安装到应用私有目录。
 *
 * 落盘位置：{@code getExternalFilesDir(null)}，即
 * {@code /sdcard/Android/data/<pkg>/files} —— **不是** files/gamedata 子目录。
 * 游戏（SAMP.java）与 GameDataImporter 都以 files/ 为根读 texdb/、SAMP/、audio/，
 * 装到子目录里游戏会读不到。
 *
 * 这个路径在 Android 11+ 上**应用自己可以随便写**，不需要 MANAGE_EXTERNAL_STORAGE。
 * 只有第三方（adb、文件管理器）才受 FUSE 的 secure_mkdirs 限制，所以之前
 * push_gamedata_to_device.py 才要绕道"打 tar → 设备端解压"。现在下载器直接在
 * 应用进程里写，那条绕行完全不需要。
 *
 * 安装是**先解到临时目录再整体合并**的：中途断电/杀进程不会留下半套数据
 * 让游戏读到一半崩溃。
 */
public final class GamedataInstaller {

    private static final String TAG = "GamedataInstaller";

    /** 解压中的临时目录（与 files/ 同级，避免被游戏误读）。 */
    private static final String STAGING_DIR = ".gamedata.staging";

    public interface Listener {
        /** @param entryName 当前正在解压的条目（已归一化后的相对路径） */
        void onProgress(String entryName, long bytesWritten, long bytesTotal);

        void onStatus(String message);
    }

    private final Context context;

    public GamedataInstaller(Context context) {
        this.context = context.getApplicationContext();
    }

    public File filesRoot() {
        File dir = context.getExternalFilesDir(null);
        if (dir == null) {
            // 极少数设备外置存储未挂载，退回内部存储。容量可能不够，但至少不崩。
            dir = new File(context.getFilesDir(), "ext");
        }
        return dir;
    }

    /** 安装目标就是 files/ 根目录。 */
    public File installDir() {
        return filesRoot();
    }

    public File stagingDir() {
        return new File(filesRoot(), STAGING_DIR);
    }

    /** 数据是否已就绪：复用 GameDataImporter 的核心文件判定，口径与手动导入一致。 */
    public boolean isInstalled() {
        return GameDataImporter.isGameDataReady(filesRoot());
    }

    /** 缺少哪些核心文件（空列表表示就绪）。 */
    public java.util.List<String> missingCoreFiles() {
        return GameDataImporter.findMissingCoreFiles(filesRoot());
    }

    /**
     * 解压一个 zip 到 staging 目录。
     *
     * 条目路径用 {@link GameDataImporter#normalizeZipEntryPath} 归一化：
     * 包的顶层可能带 files/ 或 gamedata/files/ 包装，归一化后统一落到根，
     * 这样无论打包脚本怎么包，装出来都对。
     *
     * @param zip        已下载并校验通过的 zip
     * @param totalBytes 进度分母（用压缩包大小近似）
     */
    public void extractToStaging(File zip, long totalBytes, Listener listener) throws IOException {
        extractToStaging(zip, totalBytes, listener, true);
    }

    /**
     * @param fresh true = 先清空 staging（第一个包用）；
     *              false = 追加到已有 staging（后续包，用于覆盖同名资源）
     */
    public void extractToStaging(File zip, long totalBytes, Listener listener, boolean fresh)
            throws IOException {
        File staging = stagingDir();
        if (fresh) {
            deleteRecursively(staging);
        }
        if (!staging.isDirectory() && !staging.mkdirs()) {
            throw new IOException(I18n.tr("无法创建临时目录：") + staging);
        }

        long written = 0;
        byte[] buf = new byte[128 * 1024];

        try (ZipInputStream zis = new ZipInputStream(
                new BufferedInputStream(new FileInputStream(zip)))) {
            ZipEntry entry;
            while ((entry = zis.getNextEntry()) != null) {
                String raw = entry.getName();

                if (entry.isDirectory()) {
                    zis.closeEntry();
                    continue;
                }

                // 归一化 + 安全校验（拒绝 .. / 绝对路径 / __MACOSX）
                String rel = GameDataImporter.normalizeZipEntryPath(raw);
                if (rel == null) {
                    Log.d(TAG, I18n.tr("跳过条目: ") + raw);
                    zis.closeEntry();
                    continue;
                }
                if (!isSafeEntryName(rel)) {
                    Log.w(TAG, I18n.tr("跳过不安全的 zip 条目: ") + raw);
                    zis.closeEntry();
                    continue;
                }

                File out = new File(staging, rel);
                File parent = out.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                    throw new IOException(I18n.tr("无法创建父目录：") + parent);
                }

                try (OutputStream os = new FileOutputStream(out)) {
                    int n;
                    while ((n = zis.read(buf)) > 0) {
                        os.write(buf, 0, n);
                        written += n;
                        if (listener != null) {
                            listener.onProgress(rel, Math.min(written, totalBytes), totalBytes);
                        }
                    }
                }
                zis.closeEntry();
            }
        }

        if (listener != null) listener.onStatus(I18n.tr("解压完成，正在启用…"));
    }

    /**
     * 把 staging 合并进 files/ 根目录。
     *
     * 为什么是"合并"而不是"整体改名"：安装目标是 files/ 根，里面还可能有人工
     * 放进去的其他文件（设置、存档），整体替换会把它们一起干掉。合并只覆盖
     * 数据包里的同名文件，其余原样保留。
     */
    public void activateStaging() throws IOException {
        File staging = stagingDir();
        if (!staging.isDirectory()) {
            throw new IOException(I18n.tr("临时目录不存在，无法启用：") + staging);
        }

        File root = installDir();
        int files = mergeDir(staging, root, "");
        Log.i(TAG, I18n.tr("已合并 ") + files + I18n.tr(" 个文件到 ") + root);

        deleteRecursively(staging);
    }

    private int mergeDir(File srcDir, File dstDir, String relPrefix) throws IOException {
        File[] children = srcDir.listFiles();
        if (children == null) return 0;

        int count = 0;
        for (File src : children) {
            String rel = relPrefix.isEmpty() ? src.getName() : relPrefix + "/" + src.getName();
            File dst = new File(dstDir, src.getName());

            if (src.isDirectory()) {
                if (!dst.isDirectory() && !dst.mkdirs()) {
                    throw new IOException(I18n.tr("无法创建目录：") + dst);
                }
                count += mergeDir(src, dst, rel);
            } else {
                // 用户配置（服务器列表、设置、日志）不能被数据包覆盖，
                // 否则玩家每次更新数据都会丢掉自己的服务器书签。
                if (GameDataImporter.shouldPreserveExisting(rel) && dst.exists()) {
                    Log.i(TAG, I18n.tr("保留用户文件，不覆盖：") + rel);
                    continue;
                }
                // 覆盖同名文件；先删掉旧的
                if (dst.exists() && !dst.delete()) {
                    throw new IOException(I18n.tr("无法覆盖已有文件：") + dst);
                }

                // 优先 renameTo：staging 与 files/ 在同一文件系统内，改名是零拷贝，
                // 1GB+ 数据不需要额外一份空间，也没有逐字节 I/O。
                // 只有跨文件系统（理论上不该发生，但外置存储被拆分时有可能）才回退到拷贝。
                if (!src.renameTo(dst)) {
                    copyFile(src, dst);
                }
                count++;
            }
        }
        return count;
    }

    /**
     * 逐字节拷贝（renameTo 跨文件系统失败时的回退）。
     *
     * 刻意不用 java.nio.file.Files.copy：那是 API 26 才有的，
     * 而本项目 minSdk 24，Android 7.x 上会直接 NoClassDefFoundError。
     */
    private static void copyFile(File src, File dst) throws IOException {
        try (InputStream in = new FileInputStream(src);
             OutputStream out = new FileOutputStream(dst)) {
            byte[] buf = new byte[256 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
        if (!src.delete()) {
            Log.w(TAG, I18n.tr("临时文件删除失败（不影响结果）: ") + src);
        }
    }

    /** 安装失败时清理临时目录。 */
    public void discardStaging() {
        deleteRecursively(stagingDir());
    }

    /**
     * 把一个已解压的文件合并进安装目录（用于 online 包覆盖 game 包的同名资源）。
     */
    public void mergeIntoInstall(File srcFile, String relPath) throws IOException {
        if (!isSafeEntryName(relPath)) {
            throw new IOException(I18n.tr("不安全的相对路径：") + relPath);
        }
        File dst = new File(installDir(), relPath);
        File parent = dst.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException(I18n.tr("无法创建目录：") + parent);
        }
        try (InputStream in = new FileInputStream(srcFile);
             OutputStream out = new FileOutputStream(dst)) {
            byte[] buf = new byte[128 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
    }

    /**
     * 判断 zip 条目名是否安全。
     *
     * 拒绝：绝对路径（/ 开头、盘符）、含 .. 段、含反斜杠的 Windows 风格路径
     * （Android 上反斜杠会被当普通字符，但解出来的目录结构会和预期不符）。
     */
    static boolean isSafeEntryName(String name) {
        if (name == null || name.isEmpty()) return false;
        if (name.startsWith("/") || name.startsWith("\\")) return false;
        if (name.length() > 1 && name.charAt(1) == ':') return false;
        if (name.contains("\\")) return false;
        for (String seg : name.split("/")) {
            if (seg.equals("..")) return false;
        }
        return true;
    }

    /** 递归删除；失败不抛异常（清理场景抛了也没人处理）。 */
    public static void deleteRecursively(File f) {
        if (f == null || !f.exists()) return;
        if (f.isDirectory()) {
            File[] children = f.listFiles();
            if (children != null) {
                for (File c : children) deleteRecursively(c);
            }
        }
        if (!f.delete()) Log.w(TAG, I18n.tr("删除失败: ") + f);
    }
}
