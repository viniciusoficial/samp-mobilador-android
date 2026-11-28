package com.sampmobile.launcher.ui;


import com.sampmobile.launcher.utils.I18n;
import android.app.Activity;
import android.net.Uri;
import android.os.StatFs;
import android.text.TextUtils;
import android.util.TypedValue;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;

import com.sampmobile.launcher.utils.BundledGamedata;
import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.GameDataImporter;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Shared one-click game-data .zip import progress dialog used by both
 * MainActivity (when launching with missing game data) and SettingsPageFragment.
 *
 * Two sources share the same UI and extraction core:
 *  - {@link #startImport}: a user-picked .zip document (SAF Uri)
 *  - {@link #startBundledImport}: the game-data bundle embedded in APK assets
 *    (bundled packages built by tools/packaging/build_bundled_apk.py)
 */
public final class GameDataImportDialog {

    private GameDataImportDialog() {}

    /** A lazily-opened zip byte stream plus its total compressed length. */
    private interface StreamSource {
        InputStream open() throws IOException;
        long totalBytes();
    }

    public static void startImport(Activity activity, Uri zipUri, Runnable onCompleted) {
        if (activity == null || zipUri == null) {
            return;
        }
        long total = GameDataImporter.queryDocumentSize(activity, zipUri);
        startImportInternal(activity, zipUri, total, I18n.tr("正在导入游戏数据包"), onCompleted);
    }

    /**
     * Extracts the APK-embedded game-data bundle with the same progress UI.
     * Runs a free-space pre-check against bundle metadata before starting:
     * unpacking hundreds of MB only to die on ENOSPC at 90% is a terrible
     * first-run experience on a friend's phone.
     */
    public static void startBundledImport(Activity activity, Runnable onCompleted) {
        if (activity == null || activity.isFinishing() || activity.isDestroyed()) {
            return;
        }
        if (!BundledGamedata.isPresent(activity)) {
            Toast.makeText(activity, I18n.tr("当前安装包未内置游戏资源"), Toast.LENGTH_LONG).show();
            return;
        }
        BundledGamedata.Info info = BundledGamedata.inspect(activity);
        File filesDir = activity.getExternalFilesDir(null);
        if (filesDir != null && info.uncompressedBytes > 0) {
            long free = freeBytesOf(filesDir);
            // 200 MiB slack: extraction overwrites in place, but settings/logs/obb dirs
            // and the system itself keep writing while the user plays.
            long need = info.uncompressedBytes + 200L * 1024 * 1024;
            if (free >= 0 && free < need) {
                new AlertDialog.Builder(activity)
                        .setTitle(I18n.tr("存储空间不足"))
                        .setMessage(String.format(Locale.US,
                                I18n.tr("解压内置资源需要约 %s 可用空间，当前仅剩 %s。请清理存储后重试。"),
                                formatMb(need), formatMb(free)))
                        .setPositiveButton(I18n.tr("确定"), null)
                        .show();
                return;
            }
        }
        startImportInternal(activity, null, info.zipBytes, I18n.tr("正在解压内置游戏资源"), onCompleted);
    }

    private static long freeBytesOf(File dir) {
        try {
            File probe = dir;
            while (probe != null && !probe.exists()) {
                probe = probe.getParentFile();
            }
            if (probe == null) return -1L;
            return new StatFs(probe.getAbsolutePath()).getAvailableBytes();
        } catch (Exception e) {
            return -1L;
        }
    }

    private static String formatMb(long bytes) {
        return String.format(Locale.US, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }

    private static void startImportInternal(Activity activity, Uri zipUri,
                                            long totalBytes, String title, Runnable onCompleted) {
        if (activity.isFinishing() || activity.isDestroyed()) {
            return;
        }
        File filesDir = activity.getExternalFilesDir(null);
        if (filesDir == null) {
            Toast.makeText(activity, I18n.tr("无法访问应用外部存储目录"), Toast.LENGTH_LONG).show();
            return;
        }

        StreamSource source = new StreamSource() {
            @Override
            public InputStream open() throws IOException {
                if (zipUri != null) {
                    InputStream in = activity.getContentResolver().openInputStream(zipUri);
                    if (in == null) {
                        throw new IOException(I18n.tr("无法打开所选 ZIP 文件"));
                    }
                    return in;
                }
                return BundledGamedata.openBundle(activity);
            }

            @Override
            public long totalBytes() {
                return totalBytes;
            }
        };

        int pad = dp(activity, 20);
        LinearLayout box = new LinearLayout(activity);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, dp(activity, 14), pad, dp(activity, 8));

        TextView tvStatus = new TextView(activity);
        tvStatus.setText(I18n.tr("正在准备解压数据包..."));
        tvStatus.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        box.addView(tvStatus);

        ProgressBar progressBar = new ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        progressBar.setIndeterminate(true);
        LinearLayout.LayoutParams pbLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        pbLp.topMargin = dp(activity, 12);
        box.addView(progressBar, pbLp);

        TextView tvFile = new TextView(activity);
        tvFile.setText("");
        tvFile.setSingleLine(true);
        tvFile.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        tvFile.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        tvFile.setAlpha(0.75f);
        LinearLayout.LayoutParams fLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        fLp.topMargin = dp(activity, 6);
        box.addView(tvFile, fLp);

        AtomicBoolean cancelled = new AtomicBoolean(false);
        AlertDialog dialog = new AlertDialog.Builder(activity)
                .setTitle(title)
                .setView(box)
                .setCancelable(false)
                .setNegativeButton(I18n.tr("取消"), (d, w) -> cancelled.set(true))
                .create();
        dialog.show();

        Thread worker = new Thread(() -> {
            try {
                GameDataImporter.ImportResult res;
                try (InputStream raw = source.open()) {
                    res = GameDataImporter.importFromStream(
                            raw,
                            source.totalBytes(),
                            filesDir,
                            (pct, count, relPath) -> activity.runOnUiThread(() -> {
                                if (activity.isFinishing() || activity.isDestroyed() || !dialog.isShowing()) {
                                    return;
                                }
                                if (pct >= 0) {
                                    progressBar.setIndeterminate(false);
                                    progressBar.setProgress(pct);
                                    tvStatus.setText(String.format(Locale.US, I18n.tr("解压中 %d%%（已写入 %d 个文件）"), pct, count));
                                } else {
                                    progressBar.setIndeterminate(true);
                                    tvStatus.setText(String.format(Locale.US, I18n.tr("解压中（已写入 %d 个文件）..."), count));
                                }
                                if (relPath != null && !relPath.isEmpty()) {
                                    tvFile.setText(relPath);
                                }
                            }),
                            cancelled
                    );
                }

                // Ensure settings.json exists after clean import
                if (!res.cancelled && res.extractedFiles > 0) {
                    try {
                        ClientSettings.ensureExists(activity);
                    } catch (Exception ignored) {
                    }
                }

                activity.runOnUiThread(() -> {
                    if (dialog.isShowing()) {
                        dialog.dismiss();
                    }
                    if (activity.isFinishing() || activity.isDestroyed()) {
                        return;
                    }
                    if (res.cancelled) {
                        Toast.makeText(activity, I18n.tr("已取消导入"), Toast.LENGTH_SHORT).show();
                    } else if (res.extractedFiles == 0) {
                        new AlertDialog.Builder(activity)
                                .setTitle(I18n.tr("未识别到游戏数据包"))
                                .setMessage(I18n.tr("所选 ZIP 中未找到 GTA:SA / SA-MP 游戏目录（需包含 SAMP/、texdb/、anim/、data/、models/ 等）。请确认选择了 tools/gamedata/build_gamedata.py 生成的 .zip 文件。"))
                                .setPositiveButton(I18n.tr("确定"), null)
                                .show();
                    } else if (res.ready) {
                        String mb = String.format(Locale.US, "%.1f MB", res.extractedBytes / (1024.0 * 1024.0));
                        Toast.makeText(activity, I18n.tr("导入完成（") + res.extractedFiles + I18n.tr(" 个文件，") + mb + "）", Toast.LENGTH_LONG).show();
                    } else {
                        StringBuilder sb = new StringBuilder(I18n.tr("已解压 "));
                        sb.append(res.extractedFiles).append(I18n.tr(" 个文件，但仍缺少以下核心文件：\n"));
                        for (String m : res.missingCoreFiles) {
                            sb.append("• ").append(m).append('\n');
                        }
                        new AlertDialog.Builder(activity)
                                .setTitle(I18n.tr("数据包不完整"))
                                .setMessage(sb.toString().trim())
                                .setPositiveButton(I18n.tr("确定"), null)
                                .show();
                    }
                    if (onCompleted != null) {
                        onCompleted.run();
                    }
                });
            } catch (Exception e) {
                activity.runOnUiThread(() -> {
                    if (dialog.isShowing()) {
                        dialog.dismiss();
                    }
                    if (!activity.isFinishing() && !activity.isDestroyed()) {
                        new AlertDialog.Builder(activity)
                                .setTitle(I18n.tr("导入失败"))
                                .setMessage(e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName())
                                .setPositiveButton(I18n.tr("确定"), null)
                                .show();
                    }
                    if (onCompleted != null) {
                        onCompleted.run();
                    }
                });
            }
        }, "GameDataImporter");
        worker.start();
    }

    private static int dp(Activity activity, int value) {
        return Math.round(TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, value, activity.getResources().getDisplayMetrics()));
    }
}
