package com.sampmobile.launcher.ui.fragment;


import com.sampmobile.launcher.utils.I18n;
import android.app.Activity;
import android.content.Context;
import android.content.res.ColorStateList;
import android.net.Uri;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.FragmentActivity;

import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.ArtworkCacheManager;
import com.sampmobile.launcher.utils.GameDataVerifier;
import com.sampmobile.launcher.utils.LauncherThemeManager;

import java.io.File;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Manages the 0.3.DL custom model cache card inside {@link SettingsPageFragment}'s Multiplayer tab.
 */
final class SettingsArtworkCacheController {

    private final SettingsPageFragment host;
    private TextView tvSummary;
    private TextView tvEmpty;
    private TextView btnImport;
    private TextView btnRefresh;
    private TextView btnClearAll;
    private LinearLayout listContainer;
    private ArtworkCacheManager.CacheSummary lastSummary;

    SettingsArtworkCacheController(SettingsPageFragment host) {
        this.host = host;
    }

    void bindViews(View root) {
        tvSummary = root.findViewById(R.id.tv_dl_cache_summary);
        tvEmpty = root.findViewById(R.id.tv_dl_cache_empty);
        btnImport = root.findViewById(R.id.btn_dl_cache_import);
        btnRefresh = root.findViewById(R.id.btn_dl_cache_refresh);
        btnClearAll = root.findViewById(R.id.btn_dl_cache_clear_all);
        listContainer = root.findViewById(R.id.ll_dl_cache_list);

        if (btnImport != null) {
            btnImport.setOnClickListener(v -> host.artworkCachePicker.launch(
                    new String[]{"application/zip", "application/octet-stream", "*/*"}));
        }
        if (btnRefresh != null) {
            btnRefresh.setOnClickListener(v -> {
                refresh();
                Toast.makeText(host.requireContext(), I18n.tr("已刷新模型缓存状态"), Toast.LENGTH_SHORT).show();
            });
        }
        if (btnClearAll != null) {
            btnClearAll.setOnClickListener(v -> onClickClearAll());
        }
        refresh();
    }

    void updateTheme(int accentColor) {
        if (btnImport == null || !host.isAdded()) return;
        btnImport.setBackgroundResource(R.drawable.launcher_btn_primary);
        btnImport.setBackgroundTintList(ColorStateList.valueOf(accentColor));
        btnImport.setTextColor(host.requireContext().getResources().getColor(R.color.launcher_on_accent));
    }

    void refresh() {
        Context ctx = host.getContext();
        if (ctx == null || tvSummary == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.ioExecutor.execute(() -> {
            File cacheRoot = ArtworkCacheManager.getCacheRoot(appCtx);
            ArtworkCacheManager.CacheSummary summary = ArtworkCacheManager.scan(cacheRoot);
            FragmentActivity act = host.getActivity();
            if (act != null && host.isAdded()) {
                act.runOnUiThread(() -> {
                    if (!host.isAdded() || host.getView() == null) return;
                    renderSummary(summary);
                });
            }
        });
    }

    private void renderSummary(ArtworkCacheManager.CacheSummary summary) {
        lastSummary = summary;
        if (tvSummary == null || listContainer == null || !host.isAdded()) return;

        if (summary.totalFiles == 0) {
            tvSummary.setText(I18n.tr("当前占用：0 文件 · 0 B（尚未缓存任何 0.3.DL 自定义模型）"));
        } else {
            tvSummary.setText(String.format(Locale.US,
                    I18n.tr("当前占用：%d 个缓存目录 · %,d 个文件 · %s"),
                    summary.folders.size(),
                    summary.totalFiles,
                    GameDataVerifier.formatBytes(summary.totalBytes)));
        }

        if (btnClearAll != null) {
            btnClearAll.setEnabled(summary.totalFiles > 0);
            btnClearAll.setAlpha(summary.totalFiles > 0 ? 1.0f : 0.45f);
        }

        listContainer.removeAllViews();
        if (summary.folders.isEmpty()) {
            if (tvEmpty != null) tvEmpty.setVisibility(View.VISIBLE);
            return;
        }
        if (tvEmpty != null) tvEmpty.setVisibility(View.GONE);

        Context ctx = host.requireContext();
        int padH = dp(ctx, 10);
        int padV = dp(ctx, 7);
        int gap = dp(ctx, 6);
        int colorText = ctx.getResources().getColor(R.color.launcher_text);
        int colorDim = ctx.getResources().getColor(R.color.launcher_text_dim);
        int colorDanger = ctx.getResources().getColor(R.color.launcher_danger);

        for (int i = 0; i < summary.folders.size(); i++) {
            ArtworkCacheManager.FolderEntry entry = summary.folders.get(i);
            LinearLayout row = new LinearLayout(ctx);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setBackgroundResource(R.drawable.ui_item_bg);
            row.setPadding(padH, padV, padH, padV);
            LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            if (i > 0) rowLp.topMargin = gap;
            row.setLayoutParams(rowLp);

            LinearLayout info = new LinearLayout(ctx);
            info.setOrientation(LinearLayout.VERTICAL);
            LinearLayout.LayoutParams infoLp = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
            infoLp.setMarginEnd(dp(ctx, 8));
            info.setLayoutParams(infoLp);

            TextView tvTitle = new TextView(ctx);
            tvTitle.setText(entry.isLocal ? I18n.tr("local（全服共享缓存）") : entry.name);
            tvTitle.setTextColor(colorText);
            tvTitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            tvTitle.setTypeface(null, android.graphics.Typeface.BOLD);
            tvTitle.setSingleLine(true);
            tvTitle.setEllipsize(TextUtils.TruncateAt.MIDDLE);
            info.addView(tvTitle);

            TextView tvMeta = new TextView(ctx);
            tvMeta.setText(String.format(Locale.US, I18n.tr("%,d 个文件 · %s"),
                    entry.fileCount, GameDataVerifier.formatBytes(entry.totalBytes)));
            tvMeta.setTextColor(colorDim);
            tvMeta.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f);
            info.addView(tvMeta);

            TextView btnDel = new TextView(ctx);
            btnDel.setText(I18n.tr("删除"));
            btnDel.setTextColor(colorDanger);
            btnDel.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
            btnDel.setTypeface(null, android.graphics.Typeface.BOLD);
            btnDel.setGravity(Gravity.CENTER);
            btnDel.setBackgroundResource(R.drawable.launcher_btn_danger);
            btnDel.setPadding(dp(ctx, 10), dp(ctx, 4), dp(ctx, 10), dp(ctx, 4));
            btnDel.setOnClickListener(v -> confirmDeleteFolder(entry));

            row.addView(info);
            row.addView(btnDel);
            listContainer.addView(row);
        }
    }

    private void confirmDeleteFolder(ArtworkCacheManager.FolderEntry entry) {
        Activity act = host.getActivity();
        if (act == null || act.isFinishing()) return;
        new AlertDialog.Builder(act)
                .setTitle(I18n.tr("删除缓存目录"))
                .setMessage(String.format(Locale.US,
                        I18n.tr("确定删除「%s」中的 %,d 个缓存文件（%s）吗？\n删除后下次进入对应服务器时将重新下载所需模型。"),
                        entry.name, entry.fileCount, GameDataVerifier.formatBytes(entry.totalBytes)))
                .setNegativeButton(I18n.tr("取消"), null)
                .setPositiveButton(I18n.tr("确认删除"), (d, w) -> {
                    final Context appCtx = act.getApplicationContext();
                    host.ioExecutor.execute(() -> {
                        File cacheRoot = ArtworkCacheManager.getCacheRoot(appCtx);
                        boolean ok = ArtworkCacheManager.deleteFolder(cacheRoot, entry.name);
                        act.runOnUiThread(() -> {
                            if (!host.isAdded()) return;
                            Toast.makeText(act, ok ? (I18n.tr("已删除缓存：") + entry.name) : I18n.tr("删除失败"), Toast.LENGTH_SHORT).show();
                            refresh();
                        });
                    });
                })
                .show();
    }

    private void onClickClearAll() {
        Activity act = host.getActivity();
        if (act == null || act.isFinishing()) return;
        ArtworkCacheManager.CacheSummary snap = lastSummary;
        if (snap == null || snap.totalFiles == 0) {
            Toast.makeText(act, I18n.tr("当前没有可清理的模型缓存"), Toast.LENGTH_SHORT).show();
            return;
        }
        new AlertDialog.Builder(act)
                .setTitle(I18n.tr("清空全部自定义模型缓存"))
                .setMessage(String.format(Locale.US,
                        I18n.tr("将永久删除 SAMP/cache/ 下全部 %d 个目录（共 %,d 个文件，%s）。\n确定继续吗？"),
                        snap.folders.size(), snap.totalFiles, GameDataVerifier.formatBytes(snap.totalBytes)))
                .setNegativeButton(I18n.tr("取消"), null)
                .setPositiveButton(I18n.tr("全部清空"), (d, w) -> {
                    final Context appCtx = act.getApplicationContext();
                    host.ioExecutor.execute(() -> {
                        File cacheRoot = ArtworkCacheManager.getCacheRoot(appCtx);
                        ArtworkCacheManager.clearAll(cacheRoot);
                        act.runOnUiThread(() -> {
                            if (!host.isAdded()) return;
                            Toast.makeText(act, I18n.tr("已清空全部自定义模型缓存"), Toast.LENGTH_SHORT).show();
                            refresh();
                        });
                    });
                })
                .show();
    }

    void onDocumentsPicked(List<Uri> uris) {
        Activity act = host.getActivity();
        if (act == null || act.isFinishing() || uris == null || uris.isEmpty()) return;
        File cacheRoot = ArtworkCacheManager.getCacheRoot(act);
        if (cacheRoot == null) {
            Toast.makeText(act, I18n.tr("无法访问应用外部存储目录"), Toast.LENGTH_LONG).show();
            return;
        }

        int pad = dp(act, 20);
        LinearLayout box = new LinearLayout(act);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, dp(act, 14), pad, dp(act, 8));

        TextView tvStatus = new TextView(act);
        tvStatus.setText(I18n.tr("正在导入自定义模型缓存…"));
        tvStatus.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        box.addView(tvStatus);

        ProgressBar pb = new ProgressBar(act, null, android.R.attr.progressBarStyleHorizontal);
        pb.setIndeterminate(true);
        LinearLayout.LayoutParams pbLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        pbLp.topMargin = dp(act, 12);
        box.addView(pb, pbLp);

        TextView tvFile = new TextView(act);
        tvFile.setText("");
        tvFile.setSingleLine(true);
        tvFile.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        tvFile.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        tvFile.setAlpha(0.75f);
        LinearLayout.LayoutParams fLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        fLp.topMargin = dp(act, 6);
        box.addView(tvFile, fLp);

        AtomicBoolean cancelled = new AtomicBoolean(false);
        AlertDialog dialog = new AlertDialog.Builder(act)
                .setTitle(I18n.tr("导入 0.3.DL 模型缓存"))
                .setView(box)
                .setCancelable(false)
                .setNegativeButton(I18n.tr("取消"), (d, w) -> cancelled.set(true))
                .create();
        dialog.show();

        final Context appCtx = act.getApplicationContext();
        Thread worker = new Thread(() -> {
            try {
                ArtworkCacheManager.ImportResult res = ArtworkCacheManager.importFromUris(
                        appCtx,
                        uris,
                        cacheRoot,
                        (imported, skipped, relPath) -> act.runOnUiThread(() -> {
                            if (act.isFinishing() || !dialog.isShowing()) return;
                            tvStatus.setText(String.format(Locale.US,
                                    I18n.tr("已导入 %,d 个文件（跳过 %,d 个非缓存项）"), imported, skipped));
                            if (relPath != null) tvFile.setText(relPath);
                        }),
                        cancelled
                );

                act.runOnUiThread(() -> {
                    if (dialog.isShowing()) dialog.dismiss();
                    if (act.isFinishing() || !host.isAdded()) return;
                    refresh();
                    if (res.cancelled) {
                        Toast.makeText(act, String.format(Locale.US,
                                I18n.tr("已取消导入（已写入 %,d 个文件）"), res.importedFiles), Toast.LENGTH_LONG).show();
                    } else if (res.importedFiles == 0) {
                        new AlertDialog.Builder(act)
                                .setTitle(I18n.tr("未找到有效的 0.3.DL 缓存文件"))
                                .setMessage(I18n.tr("所选文件/压缩包中未检测到 0x<CRC>.dff 或 0x<CRC>.txd 格式的模型缓存文件。"))
                                .setPositiveButton(I18n.tr("知道了"), null)
                                .show();
                    } else {
                        Toast.makeText(act, String.format(Locale.US,
                                I18n.tr("导入完成：%,d 个文件（%s），跳过 %,d 项"),
                                res.importedFiles,
                                GameDataVerifier.formatBytes(res.importedBytes),
                                res.skippedFiles), Toast.LENGTH_LONG).show();
                    }
                });
            } catch (Throwable t) {
                act.runOnUiThread(() -> {
                    if (dialog.isShowing()) dialog.dismiss();
                    if (act.isFinishing() || !host.isAdded()) return;
                    refresh();
                    Toast.makeText(act, I18n.tr("导入失败：") + (t.getMessage() != null ? t.getMessage() : t.getClass().getSimpleName()),
                            Toast.LENGTH_LONG).show();
                });
            }
        }, "SAMP-ArtworkCacheImport");
        worker.start();
    }

    private static int dp(Context ctx, int v) {
        return Math.round(TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, ctx.getResources().getDisplayMetrics()));
    }
}
