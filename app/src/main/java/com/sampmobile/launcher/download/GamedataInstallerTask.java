package com.sampmobile.launcher.download;


import com.sampmobile.launcher.utils.I18n;
import android.content.Context;
import android.util.Log;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * 数据包准备流程的总调度。
 *
 * 一次完整流程：
 *   1. 拉 manifest.json
 *   2. 比对本地已安装版本 → 相同则直接返回"已就绪"
 *   3. 逐包下载分片（支持断点续传）
 *   4. 拼接成整包并校验整包 SHA-256
 *   5. 解压到 staging
 *   6. staging 提升为正式目录
 *   7. 记录已安装版本
 *
 * 每一步都可中断，重新进入时从上次的断点继续（分片级别的续传）。
 */
public final class GamedataInstallerTask {

    private static final String TAG = "GamedataTask";

    /** 最大单次读清单的字节数，防止被喂一个巨大的 JSON 撑爆内存。 */
    private static final int MAX_MANIFEST_BYTES = 8 * 1024 * 1024;

    public interface Listener {
        void onStage(Stage stage);

        /** @param bytesDone / bytesTotal 跨全部包的累计进度 */
        void onProgress(long bytesDone, long bytesTotal);

        void onStatus(String message);

        void onError(String message, Throwable cause);

        void onDone(boolean installedNow);
    }

    public enum Stage {
        IDLE,
        FETCHING_MANIFEST,
        CHECKING,
        DOWNLOADING,
        VERIFYING,
        EXTRACTING,
        ACTIVATING,
        READY
    }

    private final Context context;
    private final String manifestUrl;
    private final boolean forceRedownload;
    private final GamedataInstaller installer;
    private final ChunkDownloader downloader;

    private volatile boolean cancelled = false;
    private volatile Listener listener;

    public GamedataInstallerTask(Context context, String manifestUrl) {
        this(context, manifestUrl, false);
    }

    public GamedataInstallerTask(Context context, String manifestUrl, boolean forceRedownload) {
        this.context = context.getApplicationContext();
        this.manifestUrl = manifestUrl;
        this.forceRedownload = forceRedownload;
        this.installer = new GamedataInstaller(this.context);
        this.downloader = new ChunkDownloader(cacheDir());
    }

    public GamedataInstaller installer() {
        return installer;
    }

    public void cancel() {
        cancelled = true;
        downloader.cancel();
    }

    private File cacheDir() {
        File d = new File(installer.filesRoot(), ".dl");
        //noinspection ResultOfMethodCallIgnored
        d.mkdirs();
        return d;
    }

    private File stateFile() {
        return new File(cacheDir(), "installed.json");
    }

    /**
     * 数据是否可用。判定规则集中在 {@link GamedataState}，这里只做转调，
     * 避免和 Splash 的检查各写一份。
     */
    public boolean isReady() {
        return GamedataState.isReady(context);
    }

    /** 已安装的版本号；没装过返回 null。 */
    public String installedVersion() {
        return GamedataState.installedVersion(context);
    }

    /**
     * 跑一遍完整流程。**必须在后台线程调用。**
     */
    public void run(Listener l) {
        this.listener = l;
        try {
            if (!installer.isInstalled()) {
                l.onStage(Stage.FETCHING_MANIFEST);
                l.onStatus(I18n.tr("正在获取数据包信息…"));
            } else {
                l.onStage(Stage.CHECKING);
                l.onStatus(I18n.tr("正在检查数据更新…"));
            }

            String json = fetchManifest();
            DataManifest manifest;
            try {
                manifest = DataManifest.parse(json, clientVersionCode());
            } catch (DataManifest.ManifestException e) {
                fail(l, e.getMessage(), e);
                return;
            }

            if (cancelled) {
                fail(l, I18n.tr("已取消"), null);
                return;
            }

            // 非强制重下时：版本一致或本地数据已完整则跳过
            String installed = installedVersion();
            if (!forceRedownload && installed != null && installed.equals(manifest.version) && installer.isInstalled()) {
                l.onStage(Stage.READY);
                l.onStatus(I18n.tr("数据已是最新（") + manifest.version + "）");
                l.onDone(false);
                return;
            }

            // 非强制重下且本地核心文件已齐全（例如玩家手动导入）：只补写版本记录，不重复下载
            if (!forceRedownload && installed == null && installer.isInstalled()) {
                writeState(manifest.version);
                l.onStage(Stage.READY);
                l.onStatus(I18n.tr("已检测到本地数据，无需下载"));
                l.onDone(false);
                return;
            }

            long total = manifest.totalBytes();
            long doneBase = 0;

            List<DataManifest.Package> allPackages = new ArrayList<>();
            for (DataManifest.Group g : manifest.groups) {
                allPackages.addAll(g.packages);
            }

            l.onStage(Stage.DOWNLOADING);

            for (DataManifest.Package pkg : allPackages) {
                if (cancelled) {
                    fail(l, I18n.tr("已取消"), null);
                    return;
                }
                final long base = doneBase;
                l.onStatus(I18n.tr("正在下载 ") + pkg.label);
                boolean ok = downloader.downloadPackage(
                        baseUrlOf(manifestUrl), pkg,
                        new ChunkDownloader.Listener() {
                            @Override
                            public void onProgress(long bytesDone, long bytesTotal) {
                                if (listener != null) {
                                    listener.onProgress(base + bytesDone, total);
                                }
                            }

                            @Override
                            public void onChunkDone(int chunkIndex, int chunkCount) {
                                // 单片完成不单独播报，进度条已经反映
                            }

                            @Override
                            public void onStatus(String message) {
                                if (listener != null) listener.onStatus(message);
                            }
                        });
                if (!ok) {
                    fail(l, I18n.tr("下载失败：") + pkg.label, null);
                    return;
                }
                doneBase += pkg.bytes;
            }

            if (cancelled) {
                fail(l, I18n.tr("已取消"), null);
                return;
            }

            // ---- 校验 + 解压 ----
            l.onStage(Stage.VERIFYING);
            l.onProgress(total, total);
            // 只在最早的包之前清一次 staging；后续包解压到同一目录，
            // 从而 online 包能覆盖 game 包里同名的 SAMP/ 资源。
            installer.discardStaging();

            // 逐包处理：拼接 → 校验 → 解压 → 删除，不把所有 zip 攒在手上。
            // 2GB 包在手机上必须这样，否则"分片 2G + zip 2G + 解压 2G"三份并存直接撑爆。
            for (int i = 0; i < allPackages.size(); i++) {
                if (cancelled) {
                    fail(l, I18n.tr("已取消"), null);
                    return;
                }
                DataManifest.Package pkg = allPackages.get(i);
                File dir = new File(cacheDir(), pkg.id);
                File zip = new File(cacheDir(), pkg.id + ".zip");

                l.onStage(Stage.VERIFYING);
                l.onStatus(I18n.tr("正在校验 ") + pkg.label);

                // 已有同尺寸成品 zip 就跳过拼接（上次已到这一步）
                if (!(zip.isFile() && zip.length() == pkg.bytes)) {
                    // 拼接时边拼边删分片，峰值只比整包多一个分片
                    ChunkDownloader.concatChunks(dir, pkg, zip, false);
                }
                if (!ChunkDownloader.verify(zip, pkg.sha256)) {
                    //noinspection ResultOfMethodCallIgnored
                    zip.delete();
                    fail(l, I18n.tr("数据包校验失败（可能传输损坏），请重试：") + pkg.label, null);
                    return;
                }

                l.onStage(Stage.EXTRACTING);
                l.onStatus(I18n.tr("正在安装 ") + (i + 1) + "/" + allPackages.size() + "：" + pkg.label);
                // 第一个包清空 staging，后续包追加（覆盖同名资源）
                installer.extractToStaging(zip, zip.length(), new GamedataInstaller.Listener() {
                    @Override
                    public void onProgress(String entryName, long bytesWritten, long bytesTotal) {
                        if (listener != null) listener.onStatus(I18n.tr("正在解压 ") + entryName);
                    }

                    @Override
                    public void onStatus(String message) {
                        if (listener != null) listener.onStatus(message);
                    }
                }, i == 0);

                // 解压完立刻删 zip，给下一个包腾空间
                //noinspection ResultOfMethodCallIgnored
                zip.delete();
                GamedataInstaller.deleteRecursively(dir);
            }

            l.onStage(Stage.ACTIVATING);
            l.onStatus(I18n.tr("正在启用数据…"));
            installer.activateStaging();

            // 记录已安装版本
            writeState(manifest.version);

            // 成功后清理分片缓存（省几百 MB 空间）
            for (DataManifest.Package pkg : allPackages) {
                GamedataInstaller.deleteRecursively(new File(cacheDir(), pkg.id));
                //noinspection ResultOfMethodCallIgnored
                new File(cacheDir(), pkg.id + ".zip").delete();
            }

            l.onStage(Stage.READY);
            l.onStatus(I18n.tr("数据准备完成"));
            l.onDone(true);

        } catch (InterruptedException e) {
            fail(l, I18n.tr("已取消"), e);
        } catch (Exception e) {
            Log.e(TAG, I18n.tr("准备数据失败"), e);
            fail(l, e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage(), e);
        }
    }

    private void fail(Listener l, String msg, Throwable cause) {
        if (l == null) return;
        l.onError(msg, cause);
    }

    // ------------------------------------------------------------------ 网络

    private String fetchManifest() throws IOException {
        HttpURLConnection conn = null;
        try {
            URL url = new URL(manifestUrl);
            conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(15000);
            conn.setReadTimeout(20000);
            conn.setInstanceFollowRedirects(true);
            // 清单要最新，别让中间层缓存住
            conn.setRequestProperty("Cache-Control", "no-cache");

            int code = conn.getResponseCode();
            if (code != HttpURLConnection.HTTP_OK) {
                throw new IOException(I18n.tr("获取清单失败：HTTP ") + code);
            }

            try (InputStream in = conn.getInputStream()) {
                java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
                byte[] buf = new byte[16 * 1024];
                int n;
                int total = 0;
                while ((n = in.read(buf)) > 0) {
                    total += n;
                    if (total > MAX_MANIFEST_BYTES) {
                        throw new IOException(I18n.tr("清单异常大（>") + MAX_MANIFEST_BYTES + I18n.tr(" 字节）"));
                    }
                    bos.write(buf, 0, n);
                }
                return new String(bos.toByteArray(), StandardCharsets.UTF_8);
            }
        } finally {
            if (conn != null) conn.disconnect();
        }
    }

    /** 清单所在目录（去掉最后一段文件名）。 */
    static String baseUrlOf(String manifestUrl) {
        int idx = manifestUrl.lastIndexOf('/');
        return idx > 0 ? manifestUrl.substring(0, idx + 1) : manifestUrl;
    }

    private int clientVersionCode() {
        try {
            return context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0).versionCode;
        } catch (Exception e) {
            return 0;
        }
    }

    // ------------------------------------------------------------------ 状态

    private void writeState(String version) {
        JSONObject o = new JSONObject();
        try {
            o.put("version", version);
            o.put("installedAt", System.currentTimeMillis());
            try (FileOutputStream fos = new FileOutputStream(stateFile())) {
                fos.write(o.toString().getBytes(StandardCharsets.UTF_8));
            }
        } catch (JSONException | IOException e) {
            // 状态写失败不影响数据可用，下次会当作"版本未知"重新检查一遍
            Log.w(TAG, I18n.tr("写入安装状态失败: ") + e.getMessage());
        }
    }
}
