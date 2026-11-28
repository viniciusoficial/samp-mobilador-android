package com.sampmobile.launcher.download;

import android.content.Context;

import com.sampmobile.launcher.BuildConfig;
import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.GameDataImporter;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * 数据就绪判定 —— 只此一处，避免 Splash 与下载任务各写一份导致口径漂移。
 *
 * 判定规则：**安装记录存在** 且 **GTA 核心文件齐全**。
 *
 * 为什么不是只看核心文件：
 *   如果数据包只含联机资源（不含 texdb/gta3.img 等），每次启动都会判"未就绪"，
 *   形成"下完 → 重启 → 又要求下载"的死循环。
 *
 * 为什么不是只看安装记录：
 *   记录写了但文件被用户从文件管理器清掉，游戏进去会崩。核心文件是最终凭据。
 */
public final class GamedataState {

    private GamedataState() {}

    /** 安装记录文件（与 ChunkDownloader 的缓存目录一致）。 */
    public static File stateFile(Context context) {
        File root = context.getExternalFilesDir(null);
        if (root == null) root = new File(context.getFilesDir(), "ext");
        return new File(new File(root, ".dl"), "installed.json");
    }

    /** 已安装版本号；没有记录返回 null。 */
    public static String installedVersion(Context context) {
        File f = stateFile(context);
        if (!f.isFile()) return null;
        try (InputStream in = new FileInputStream(f)) {
            java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) bos.write(buf, 0, n);
            return new JSONObject(new String(bos.toByteArray(), StandardCharsets.UTF_8))
                    .optString("version", null);
        } catch (Exception e) {
            return null;
        }
    }

    /** 数据是否就绪（可以被游戏直接使用）。 */
    public static boolean isReady(Context context) {
        File root = context.getExternalFilesDir(null);

        // 核心文件齐全就是可用 —— 不管这些文件是本下载器装的、还是玩家用
        // GameDataImporter 手动导入的、还是 adb push 进去的。
        // 早期实现要求必须有 installed.json，导致已经手动放好数据的老玩家
        // 一配上下载地址就被强制重下，这是错的。
        if (GameDataImporter.isGameDataReady(root)) return true;

        // 核心文件不全时，如果下载器曾经成功装过一次（有版本记录），
        // 说明是后续被破坏（例如用户从文件管理器删了文件），需要重下。
        // 没有记录也没有文件，是首次启动。
        return false;
    }

    /**
     * 数据是否由本下载器安装（有版本记录）。
     *
     * 与 {@link #isReady} 的区别：这个用于"要不要检查新版本"，
     * isReady 用于"能不能直接进游戏"。手动导入的老玩家前者为 false、
     * 后者为 true —— 可以正常玩，只是不会自动追更新。
     */
    public static boolean isManagedByDownloader(Context context) {
        return installedVersion(context) != null;
    }

    /** 缺哪些核心文件（空表示齐全），用于给玩家更具体的提示。 */
    public static java.util.List<String> missingCoreFiles(Context context) {
        return GameDataImporter.findMissingCoreFiles(context.getExternalFilesDir(null));
    }

    /**
     * 解析内置下载清单地址：优先取构建期注入的 BuildConfig.GAMEDATA_MANIFEST_URL，
     * 其次回退到 strings.xml 的 gamedata_manifest_url；两者皆空则返回空串。
     */
    public static String resolveManifestUrl(Context context) {
        String url = BuildConfig.GAMEDATA_MANIFEST_URL;
        if (url != null && !url.trim().isEmpty()) {
            return url.trim();
        }
        if (context != null) {
            try {
                String resUrl = context.getString(R.string.gamedata_manifest_url);
                if (resUrl != null && !resUrl.trim().isEmpty()) {
                    return resUrl.trim();
                }
            } catch (Exception ignored) {
            }
        }
        return "";
    }
}
