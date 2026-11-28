package com.sampmobile.launcher.ui.fragment;


import com.sampmobile.launcher.utils.I18n;
import android.content.Context;
import android.content.res.ColorStateList;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.FragmentActivity;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.sampmobile.launcher.R;
import com.sampmobile.launcher.download.GamedataInstallerTask;
import com.sampmobile.launcher.download.GamedataState;
import com.sampmobile.launcher.ui.widget.GtaOptionView;
import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.GameDataImporter;
import com.sampmobile.launcher.utils.GameDataVerifier;
import com.sampmobile.launcher.utils.LauncherThemeManager;
import com.sampmobile.launcher.utils.Utils;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Manages the Game Data Verification & Manual Download tab inside {@link SettingsPageFragment}.
 */
final class SettingsGameDataController {
    private static final String[] EDITION_ITEMS = {I18n.tr("精简版（推荐 · 约 1.04 GiB）"), I18n.tr("完整版（含全音频 · 2.10 GiB）")};

    private final SettingsPageFragment host;
    private GtaOptionView optionEdition;
    private TextView tvSummary, tvStorageMeta, tvListSummary, tvListEmpty;
    private TextView btnImport, btnDownload, btnVerify, btnFilterAll, btnFilterIssues;
    private View layoutDownloadProgress;
    private TextView tvDlStatus, tvDlBytes;
    private ProgressBar pbDownload;
    private RecyclerView rvFiles;

    private final List<GameDataVerifier.FileCheckItem> allItems = new ArrayList<>();
    private final List<GameDataVerifier.FileCheckItem> displayedItems = new ArrayList<>();
    private GameDataFileAdapter fileAdapter;
    private GameDataVerifier.Edition selectedEdition = GameDataVerifier.Edition.LITE;
    private GameDataVerifier.Report lastReport;
    private boolean filterIssuesOnly = false;
    private final AtomicBoolean downloading = new AtomicBoolean(false);
    private volatile GamedataInstallerTask activeDownloadTask;
    private long lastDownloadProgressPostMs = 0L;

    SettingsGameDataController(SettingsPageFragment host) {
        this.host = host;
    }

    void bindViews(View view) {
        optionEdition = view.findViewById(R.id.option_gd_edition);
        tvSummary = view.findViewById(R.id.tv_gd_summary);
        tvStorageMeta = view.findViewById(R.id.tv_gd_storage_meta);
        tvListSummary = view.findViewById(R.id.tv_gd_list_summary);
        tvListEmpty = view.findViewById(R.id.tv_gd_list_empty);
        btnImport = view.findViewById(R.id.btn_gd_import);
        btnDownload = view.findViewById(R.id.btn_gd_download);
        btnVerify = view.findViewById(R.id.btn_gd_verify);
        btnFilterAll = view.findViewById(R.id.btn_gd_filter_all);
        btnFilterIssues = view.findViewById(R.id.btn_gd_filter_issues);
        layoutDownloadProgress = view.findViewById(R.id.layout_gd_download_progress);
        tvDlStatus = view.findViewById(R.id.tv_gd_dl_status);
        tvDlBytes = view.findViewById(R.id.tv_gd_dl_bytes);
        pbDownload = view.findViewById(R.id.pb_gd_download);
        rvFiles = view.findViewById(R.id.rv_gd_files);
        if (rvFiles != null) {
            rvFiles.setLayoutManager(new LinearLayoutManager(host.requireContext()));
            fileAdapter = new GameDataFileAdapter();
            rvFiles.setAdapter(fileAdapter);
        }
        setupControls();
        refreshVerification();
    }

    void setupControls() {
        if (optionEdition != null) {
            optionEdition.setAdapter(host.createDropdownAdapter(EDITION_ITEMS));
            optionEdition.setText(EDITION_ITEMS[0], false);
            optionEdition.setOnItemClickListener((parent, v, pos, id) -> {
                selectedEdition = (pos == 1)
                        ? GameDataVerifier.Edition.FULL
                        : GameDataVerifier.Edition.LITE;
                refreshVerification();
            });
        }
        if (btnImport != null) {
            btnImport.setOnClickListener(v -> {
                if (downloading.get()) {
                    Toast.makeText(host.requireContext(), I18n.tr("正在下载数据包，请先取消后再导入"), Toast.LENGTH_SHORT).show();
                    return;
                }
                host.gameDataZipPicker.launch(new String[]{"application/zip", "application/octet-stream", "*/*"});
            });
        }
        if (btnDownload != null) {
            btnDownload.setOnClickListener(v -> {
                if (downloading.get()) {
                    cancelActiveDownload();
                } else {
                    onClickManualDownload();
                }
            });
        }
        if (btnVerify != null) {
            btnVerify.setOnClickListener(v -> {
                refreshVerification();
                Toast.makeText(host.requireContext(), I18n.tr("正在重新验证游戏文件…"), Toast.LENGTH_SHORT).show();
            });
        }
        if (btnFilterAll != null) {
            btnFilterAll.setOnClickListener(v -> {
                filterIssuesOnly = false;
                updateFilterUI();
                applyListFilter();
            });
        }
        if (btnFilterIssues != null) {
            btnFilterIssues.setOnClickListener(v -> {
                filterIssuesOnly = true;
                updateFilterUI();
                applyListFilter();
            });
        }
        updateFilterUI();
        updateDownloadButtonUI();
    }

    void updateFilterUI() {
        if (btnFilterAll == null || btnFilterIssues == null || !host.isAdded()) return;
        int accentColor = LauncherThemeManager.getAccentColor(host.requireContext());
        int colorOnAccent = host.requireContext().getResources().getColor(R.color.launcher_on_accent);
        int colorText = host.requireContext().getResources().getColor(R.color.launcher_text);

        boolean all = !filterIssuesOnly;
        btnFilterAll.setBackgroundResource(all ? R.drawable.launcher_btn_primary : R.drawable.launcher_btn_outline);
        btnFilterAll.setBackgroundTintList(all ? ColorStateList.valueOf(accentColor) : null);
        btnFilterAll.setTextColor(all ? colorOnAccent : colorText);

        btnFilterIssues.setBackgroundResource(!all ? R.drawable.launcher_btn_primary : R.drawable.launcher_btn_outline);
        btnFilterIssues.setBackgroundTintList(!all ? ColorStateList.valueOf(accentColor) : null);
        btnFilterIssues.setTextColor(!all ? colorOnAccent : colorText);
    }

    void updateDownloadButtonUI() {
        if (btnDownload == null || !host.isAdded()) return;
        if (downloading.get()) {
            btnDownload.setBackgroundResource(R.drawable.launcher_btn_danger);
            btnDownload.setBackgroundTintList(null);
            btnDownload.setTextColor(host.requireContext().getResources().getColor(R.color.launcher_danger));
            btnDownload.setText(I18n.tr("取消下载"));
        } else {
            int accentColor = LauncherThemeManager.getAccentColor(host.requireContext());
            btnDownload.setBackgroundResource(R.drawable.launcher_btn_primary);
            btnDownload.setBackgroundTintList(ColorStateList.valueOf(accentColor));
            btnDownload.setTextColor(host.requireContext().getResources().getColor(R.color.launcher_on_accent));
            btnDownload.setText(I18n.tr("启动器下载"));
        }
    }

    void refreshVerification() {
        Context ctx = host.getContext();
        if (ctx == null || tvSummary == null) return;
        final Context appCtx = ctx.getApplicationContext();
        final GameDataVerifier.Edition edition = selectedEdition;
        host.ioExecutor.execute(() -> {
            GameDataVerifier.Report report = GameDataVerifier.verify(appCtx, edition);
            FragmentActivity act = host.getActivity();
            if (act != null && host.isAdded()) {
                act.runOnUiThread(() -> {
                    if (!host.isAdded() || host.getView() == null) return;
                    renderReport(report);
                });
            }
        });
    }

    void cancelActiveDownload() {
        GamedataInstallerTask task = activeDownloadTask;
        if (task != null) {
            task.cancel();
            if (host.isAdded() && tvDlStatus != null) {
                tvDlStatus.setText(I18n.tr("正在取消下载…"));
            }
        }
    }

    private void renderReport(GameDataVerifier.Report report) {
        lastReport = report;
        String editionLabel = (report.edition == GameDataVerifier.Edition.FULL)
                ? I18n.tr("完整版校验（2.10 GiB）")
                : I18n.tr("精简版校验");
        String detectedPack = report.hasFullAudio ? I18n.tr("已含完整版音频") : I18n.tr("精简版结构");
        if (report.problemCount() == 0) {
            tvSummary.setText(String.format(Locale.US,
                    I18n.tr("状态：已就绪（%s · %s）"), editionLabel, detectedPack));
            tvSummary.setTextColor(host.requireContext().getResources().getColor(R.color.ui_good));
        } else if (report.coreReady && report.edition == GameDataVerifier.Edition.FULL) {
            tvSummary.setText(String.format(Locale.US,
                    I18n.tr("状态：核心可进服，缺少完整版音频项 %d 处（切回精简版即可正常游玩）"), report.problemCount()));
            tvSummary.setTextColor(host.requireContext().getResources().getColor(R.color.ui_warn));
        } else {
            tvSummary.setText(String.format(Locale.US,
                    I18n.tr("状态：未就绪（缺失 %d 项，异常 %d 项）· 请导入或下载游戏数据"),
                    report.missingCount, report.invalidCount));
            tvSummary.setTextColor(host.requireContext().getResources().getColor(R.color.ui_bad));
        }

        String verText = (report.installedVersion != null && !report.installedVersion.isEmpty())
                ? I18n.tr(" · 清单版本 ") + report.installedVersion
                : "";
        tvStorageMeta.setText(String.format(Locale.US,
                I18n.tr("当前占用 %s · 可用空间 %s%s · 统一提供精简版下载（完整版 2.10 GiB 暂仅支持本地导入与校验）"),
                GameDataVerifier.formatBytes(report.totalDataBytes),
                GameDataVerifier.formatBytes(report.freeSpaceBytes),
                verText));

        String skipHint = report.skippedCount > 0
                ? String.format(Locale.US, I18n.tr(" · 精简版跳过 %d 项"), report.skippedCount)
                : "";
        tvListSummary.setText(String.format(Locale.US,
                I18n.tr("已通过 %d/%d · 缺失 %d · 异常 %d%s"),
                report.okCount, report.requiredCount(),
                report.missingCount, report.invalidCount, skipHint));

        allItems.clear();
        allItems.addAll(report.items);
        applyListFilter();
    }

    private void applyListFilter() {
        displayedItems.clear();
        for (GameDataVerifier.FileCheckItem item : allItems) {
            if (!filterIssuesOnly || item.isProblem()) {
                displayedItems.add(item);
            }
        }
        if (fileAdapter != null) {
            fileAdapter.notifyDataSetChanged();
        }
        if (tvListEmpty != null && rvFiles != null) {
            boolean empty = displayedItems.isEmpty();
            tvListEmpty.setVisibility(empty ? View.VISIBLE : View.GONE);
            rvFiles.setVisibility(empty ? View.GONE : View.VISIBLE);
        }
    }

    private void onClickManualDownload() {
        if (!host.isAdded()) return;
        if (selectedEdition == GameDataVerifier.Edition.FULL) {
            new AlertDialog.Builder(host.requireActivity())
                    .setTitle(I18n.tr("完整版暂未开放下载"))
                    .setMessage(I18n.tr("当前启动器统一提供精简版游戏资源下载，完整版（2.10 GiB）暂仅支持通过「本地导入」安装与文件校验。\n\n是否切换至精简版并开始下载？"))
                    .setPositiveButton(I18n.tr("切换精简版并下载"), (d, w) -> {
                        selectedEdition = GameDataVerifier.Edition.LITE;
                        if (optionEdition != null) {
                            optionEdition.setText(EDITION_ITEMS[0], false);
                        }
                        refreshVerification();
                        proceedLiteManualDownload();
                    })
                    .setNegativeButton(I18n.tr("取消"), null)
                    .show();
            return;
        }
        proceedLiteManualDownload();
    }

    private void proceedLiteManualDownload() {
        if (!host.isAdded()) return;
        String manifestUrl = GamedataState.resolveManifestUrl(host.requireContext());
        if (manifestUrl.isEmpty()) {
            new AlertDialog.Builder(host.requireActivity())
                    .setTitle(I18n.tr("未配置下载源"))
                    .setMessage(I18n.tr("当前安装包未内置精简版数据下载源。\n\n请先准备好游戏数据包 .zip，点击「本地导入」直接导入到启动器中。"))
                    .setPositiveButton(I18n.tr("去本地导入"), (d, w) ->
                            host.gameDataZipPicker.launch(new String[]{"application/zip", "application/octet-stream", "*/*"}))
                    .setNegativeButton(I18n.tr("知道了"), null)
                    .show();
            return;
        }

        boolean coreReady = GameDataImporter.isGameDataReady(host.requireContext().getExternalFilesDir(null));
        boolean hasProblems = lastReport != null && lastReport.problemCount() > 0;
        if (coreReady && !hasProblems) {
            new AlertDialog.Builder(host.requireActivity())
                    .setTitle(I18n.tr("本地资源已完整"))
                    .setMessage(I18n.tr("检测到当前精简版核心文件已全部通过校验。是否仍要重新下载并覆盖修复？（您的设置与服务器列表将完整保留）"))
                    .setPositiveButton(I18n.tr("强制重新下载"), (d, w) -> startManualDownload(manifestUrl, true))
                    .setNeutralButton(I18n.tr("仅检查更新"), (d, w) -> startManualDownload(manifestUrl, false))
                    .setNegativeButton(I18n.tr("取消"), null)
                    .show();
            return;
        }

        startManualDownload(manifestUrl, true);
    }

    private void startManualDownload(String manifestUrl, boolean forceRedownload) {
        if (!downloading.compareAndSet(false, true)) return;
        lastDownloadProgressPostMs = 0L;

        if (layoutDownloadProgress != null) {
            layoutDownloadProgress.setVisibility(View.VISIBLE);
        }
        if (pbDownload != null) {
            pbDownload.setProgress(0);
        }
        if (tvDlStatus != null) {
            tvDlStatus.setText(I18n.tr("正在连接下载服务…"));
            tvDlStatus.setTextColor(host.requireContext().getResources().getColor(R.color.launcher_text));
        }
        if (tvDlBytes != null) {
            tvDlBytes.setText("0%");
        }
        updateDownloadButtonUI();

        final Context appCtx = host.requireContext().getApplicationContext();
        final GamedataInstallerTask task = new GamedataInstallerTask(appCtx, manifestUrl, forceRedownload);
        activeDownloadTask = task;

        Thread worker = new Thread(() -> task.run(new GamedataInstallerTask.Listener() {
            @Override
            public void onStage(GamedataInstallerTask.Stage stage) {
                postToUi(() -> {
                    if (tvDlStatus == null) return;
                    switch (stage) {
                        case FETCHING_MANIFEST:
                            tvDlStatus.setText(I18n.tr("正在获取精简版数据清单…"));
                            break;
                        case CHECKING:
                            tvDlStatus.setText(I18n.tr("正在检查数据版本…"));
                            break;
                        case DOWNLOADING:
                            tvDlStatus.setText(I18n.tr("正在下载游戏数据（支持断点续传）…"));
                            break;
                        case VERIFYING:
                            tvDlStatus.setText(I18n.tr("正在校验数据包完整性（SHA-256）…"));
                            break;
                        case EXTRACTING:
                            tvDlStatus.setText(I18n.tr("正在解压安装游戏文件…"));
                            break;
                        case ACTIVATING:
                            tvDlStatus.setText(I18n.tr("正在启用已安装资源…"));
                            break;
                        case READY:
                        default:
                            break;
                    }
                });
            }

            @Override
            public void onProgress(long bytesDone, long bytesTotal) {
                long now = System.currentTimeMillis();
                int perMille = bytesTotal > 0
                        ? (int) Math.min(1000L, (bytesDone * 1000L) / bytesTotal)
                        : 0;
                boolean force = perMille >= 1000 || lastDownloadProgressPostMs == 0L;
                if (!force && (now - lastDownloadProgressPostMs) < 200L) return;
                lastDownloadProgressPostMs = now;
                postToUi(() -> {
                    if (pbDownload != null && bytesTotal > 0) {
                        pbDownload.setProgress(perMille);
                    }
                    if (tvDlBytes != null && bytesTotal > 0) {
                        tvDlBytes.setText(String.format(Locale.US, "%d%%  (%s / %s)",
                                perMille / 10,
                                GameDataVerifier.formatBytes(bytesDone),
                                GameDataVerifier.formatBytes(bytesTotal)));
                    }
                });
            }

            @Override
            public void onStatus(String message) {
                if (message == null || message.isEmpty()) return;
                postToUi(() -> {
                    if (tvDlStatus != null) {
                        tvDlStatus.setText(message);
                    }
                });
            }

            @Override
            public void onError(String message, Throwable cause) {
                downloading.set(false);
                activeDownloadTask = null;
                postToUi(() -> {
                    updateDownloadButtonUI();
                    if (tvDlStatus != null) {
                        tvDlStatus.setText(message != null ? message : I18n.tr("下载失败"));
                        tvDlStatus.setTextColor(host.requireContext().getResources().getColor(R.color.ui_bad));
                    }
                    refreshVerification();
                });
            }

            @Override
            public void onDone(boolean installedNow) {
                downloading.set(false);
                activeDownloadTask = null;
                if (installedNow) {
                    try {
                        ClientSettings.ensureExists(appCtx);
                    } catch (Exception ignored) {
                    }
                }
                postToUi(() -> {
                    updateDownloadButtonUI();
                    if (pbDownload != null) {
                        pbDownload.setProgress(1000);
                    }
                    if (tvDlBytes != null) {
                        tvDlBytes.setText("100%");
                    }
                    if (tvDlStatus != null) {
                        tvDlStatus.setText(installedNow ? I18n.tr("精简版资源下载并安装完成") : I18n.tr("本地数据已是最新，无需重新下载"));
                        tvDlStatus.setTextColor(host.requireContext().getResources().getColor(R.color.ui_good));
                    }
                    refreshVerification();
                });
            }
        }), "gamedata-manual-download");
        worker.start();
    }

    private void postToUi(Runnable action) {
        FragmentActivity act = host.getActivity();
        if (act != null && host.isAdded()) {
            act.runOnUiThread(() -> {
                if (host.isAdded() && host.getView() != null) {
                    action.run();
                }
            });
        }
    }

    private class GameDataFileAdapter extends RecyclerView.Adapter<GameDataFileAdapter.VH> {
        class VH extends RecyclerView.ViewHolder {
            final View statusDot;
            final TextView tvPath, tvDesc, tvSize, tvBadge;

            VH(View itemView) {
                super(itemView);
                statusDot = itemView.findViewById(R.id.v_gd_status_dot);
                tvPath = itemView.findViewById(R.id.tv_gd_file_path);
                tvDesc = itemView.findViewById(R.id.tv_gd_file_desc);
                tvSize = itemView.findViewById(R.id.tv_gd_file_size);
                tvBadge = itemView.findViewById(R.id.tv_gd_file_badge);
            }
        }

        @NonNull
        @Override
        public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View v = LayoutInflater.from(parent.getContext())
                    .inflate(R.layout.item_gamedata_file, parent, false);
            return new VH(v);
        }

        @Override
        public void onBindViewHolder(@NonNull VH h, int position) {
            GameDataVerifier.FileCheckItem item = displayedItems.get(position);
            Context ctx = h.itemView.getContext();
            h.tvPath.setText(item.path);
            h.tvDesc.setText(item.group + " · " + item.label);

            if (item.status == GameDataVerifier.Status.OK) {
                String sizeText = item.directory
                        ? String.format(Locale.US, I18n.tr("%d 个文件 · %s"), item.fileCount, GameDataVerifier.formatBytes(item.bytes))
                        : GameDataVerifier.formatBytes(item.bytes);
                h.tvSize.setText(sizeText);
            } else if (item.status == GameDataVerifier.Status.INVALID) {
                h.tvSize.setText(GameDataVerifier.formatBytes(item.bytes));
            } else {
                h.tvSize.setText("—");
            }

            int colorRes;
            String badgeText;
            switch (item.status) {
                case OK:
                    colorRes = R.color.ui_good;
                    badgeText = I18n.tr("正常");
                    break;
                case INVALID:
                    colorRes = R.color.ui_warn;
                    badgeText = I18n.tr("文件异常");
                    break;
                case SKIPPED_LITE:
                    colorRes = R.color.launcher_text_dim;
                    badgeText = I18n.tr("精简版跳过");
                    break;
                case MISSING:
                default:
                    colorRes = R.color.ui_bad;
                    badgeText = I18n.tr("缺失");
                    break;
            }
            int color = ctx.getResources().getColor(colorRes);
            h.statusDot.setBackgroundTintList(ColorStateList.valueOf(color));
            h.tvBadge.setText(badgeText);
            h.tvBadge.setTextColor(color);
        }

        @Override
        public int getItemCount() {
            return displayedItems.size();
        }
    }
}
