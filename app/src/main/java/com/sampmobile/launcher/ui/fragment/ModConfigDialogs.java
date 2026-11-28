package com.sampmobile.launcher.ui.fragment;


import com.sampmobile.launcher.utils.I18n;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.Typeface;
import android.view.ViewGroup;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;

import com.sampmobile.launcher.utils.ModManager;

import java.io.File;

/**
 * AML / JPatch configuration dialogs and mod log viewer for {@link ModsManagerFragment}.
 */
final class ModConfigDialogs {
    private final ModsManagerFragment host;

    ModConfigDialogs(ModsManagerFragment host) {
        this.host = host;
    }

    void showAmlCoreSettingsDialog() {
        Context ctx = host.requireContext();
        ModManager.AmlCoreSettings cur = ModManager.readAmlCoreSettings(ctx);

        int pad = (int) (14 * host.getResources().getDisplayMetrics().density);
        LinearLayout layout = new LinearLayout(ctx);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(pad, pad / 2, pad, 0);

        TextView lblFake = new TextView(ctx);
        lblFake.setText(I18n.tr("伪装包名 (FakePackageName，供 GTA:SA 插件识别)："));
        lblFake.setTextSize(12);
        layout.addView(lblFake);

        EditText etFake = new EditText(ctx);
        etFake.setText(cur.fakePackageName);
        etFake.setSingleLine(true);
        layout.addView(etFake);

        CheckBox cbOverlay = new CheckBox(ctx);
        cbOverlay.setText(I18n.tr("进游戏时显示 AML 已加载模组提示（Android 12+ 建议关闭，以免遮挡输入法）"));
        cbOverlay.setChecked(cur.showModsOverlay);
        layout.addView(cbOverlay);

        CheckBox cbLogFile = new CheckBox(ctx);
        cbLogFile.setText(I18n.tr("将 AML 运行日志写入 android_log_print.txt"));
        cbLogFile.setChecked(cur.printLogsToFile);
        layout.addView(cbLogFile);

        CheckBox cbVerbose = new CheckBox(ctx);
        cbVerbose.setText(I18n.tr("详细日志输出（记录 INFO 级别加载信息）"));
        cbVerbose.setChecked(cur.verboseLogs);
        layout.addView(cbVerbose);

        CheckBox cbSignal = new CheckBox(ctx);
        cbSignal.setText(I18n.tr("启用 AML 崩溃信号捕获（默认关闭，推荐保留本端原生崩溃日志）"));
        cbSignal.setChecked(cur.signalHandler);
        layout.addView(cbSignal);

        new AlertDialog.Builder(ctx)
                .setTitle(I18n.tr("AML 核心配置 (ModLoaderCore.ini)"))
                .setView(layout)
                .setPositiveButton(I18n.tr("保存"), (d, w) -> {
                    ModManager.AmlCoreSettings updated = new ModManager.AmlCoreSettings();
                    updated.fakePackageName = etFake.getText().toString().trim();
                    updated.showModsOverlay = cbOverlay.isChecked();
                    updated.printLogsToFile = cbLogFile.isChecked();
                    updated.verboseLogs = cbVerbose.isChecked();
                    updated.signalHandler = cbSignal.isChecked();
                    Context appCtx = ctx.getApplicationContext();
                    host.ioExecutor.execute(() -> {
                        ModManager.saveAmlCoreSettings(appCtx, updated);
                        if (!host.isAdded()) return;
                        host.requireActivity().runOnUiThread(() ->
                                Toast.makeText(ctx, I18n.tr("已保存 ModLoaderCore.ini"), Toast.LENGTH_SHORT).show());
                    });
                })
                .setNegativeButton(I18n.tr("取消"), null)
                .show();
    }

    void applyJPatchSafePreset() {
        Context ctx = host.requireContext();
        new AlertDialog.Builder(ctx)
                .setTitle(I18n.tr("应用 SA-MP 安全预设"))
                .setMessage(I18n.tr("将为 JPatch.ini 写入 SA-MP 2.10 安全推荐值：开启 2.10 防崩/雾墙/高帧率物理/NEON 优化，关闭与本端冲突的 UVAnim/强锁贴图格式/流送内存重复扩容项。是否继续？"))
                .setPositiveButton(I18n.tr("应用"), (d, w) -> {
                    Context appCtx = ctx.getApplicationContext();
                    host.ioExecutor.execute(() -> {
                        ModManager.applyJPatchSampSafePreset(appCtx);
                        if (!host.isAdded()) return;
                        host.requireActivity().runOnUiThread(() -> {
                            Toast.makeText(ctx, I18n.tr("已应用 SA-MP 安全预设到 JPatch.ini"), Toast.LENGTH_SHORT).show();
                            host.reloadCurrentTab();
                        });
                    });
                })
                .setNegativeButton(I18n.tr("取消"), null)
                .show();
    }

    void showAddJPatchKeyDialog() {
        Context ctx = host.requireContext();
        int pad = (int) (14 * host.getResources().getDisplayMetrics().density);
        LinearLayout layout = new LinearLayout(ctx);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(pad, pad / 2, pad, 0);

        EditText etSec = new EditText(ctx);
        etSec.setHint(I18n.tr("节名 Section（默认 Patches）"));
        etSec.setText("Patches");
        layout.addView(etSec);

        EditText etKey = new EditText(ctx);
        etKey.setHint(I18n.tr("配置键 Key（如 FixWaterQuadrant）"));
        layout.addView(etKey);

        EditText etVal = new EditText(ctx);
        etVal.setHint(I18n.tr("值 Value（如 1 或 0）"));
        etVal.setText("1");
        layout.addView(etVal);

        new AlertDialog.Builder(ctx)
                .setTitle(I18n.tr("新增 JPatch.ini 配置项"))
                .setView(layout)
                .setPositiveButton(I18n.tr("保存"), (d, w) -> {
                    String rawSec = etSec.getText().toString().trim();
                    final String sec = rawSec.isEmpty() ? "Patches" : rawSec;
                    final String key = etKey.getText().toString().trim();
                    final String val = etVal.getText().toString().trim();
                    if (key.isEmpty()) return;
                    Context appCtx = ctx.getApplicationContext();
                    host.ioExecutor.execute(() -> {
                        ModManager.saveJPatchOption(appCtx, sec, key, val);
                        if (!host.isAdded()) return;
                        host.requireActivity().runOnUiThread(() -> {
                            Toast.makeText(ctx, I18n.tr("已写入 ") + key + "=" + val, Toast.LENGTH_SHORT).show();
                            host.reloadCurrentTab();
                        });
                    });
                })
                .setNegativeButton(I18n.tr("取消"), null)
                .show();
    }

    void showEditJPatchValueDialog(ModManager.JPatchOption opt) {
        Context ctx = host.requireContext();
        EditText etVal = new EditText(ctx);
        etVal.setText(opt.value);
        etVal.setSingleLine(true);
        int pad = (int) (16 * host.getResources().getDisplayMetrics().density);
        LinearLayout wrap = new LinearLayout(ctx);
        wrap.setPadding(pad, pad / 2, pad, 0);
        wrap.addView(etVal, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        new AlertDialog.Builder(ctx)
                .setTitle(I18n.tr("编辑 ") + opt.key)
                .setView(wrap)
                .setPositiveButton(I18n.tr("保存"), (d, w) -> {
                    String val = etVal.getText().toString().trim();
                    opt.value = val;
                    Context appCtx = ctx.getApplicationContext();
                    host.ioExecutor.execute(() -> {
                        ModManager.saveJPatchOption(appCtx, opt.section, opt.key, val);
                        if (!host.isAdded()) return;
                        host.requireActivity().runOnUiThread(host::reloadCurrentTab);
                    });
                })
                .setNegativeButton(I18n.tr("取消"), null)
                .show();
    }

    void showLogsDialog() {
        Context ctx = host.requireContext();
        String[] choices = {
                I18n.tr("AML 日志 (android_log_print.txt)"),
                I18n.tr("最新客户端日志 (ModLoader / Hook)"),
                I18n.tr("MonetLoader 日志 (logs/monetloader.log)"),
                I18n.tr("CLEO 引擎日志 (cleo/cleo.log)")
        };
        new AlertDialog.Builder(ctx)
                .setTitle(I18n.tr("选择要查看的日志"))
                .setItems(choices, (d, which) -> {
                    Context appCtx = ctx.getApplicationContext();
                    host.ioExecutor.execute(() -> {
                        File logFile;
                        if (which == 0) logFile = ModManager.getAmlLogFile(appCtx);
                        else if (which == 1) logFile = ModManager.getLatestClientLogFile(appCtx);
                        else if (which == 2) logFile = ModManager.getMonetLogFile(appCtx);
                        else logFile = ModManager.getCleoLogFile(appCtx);

                        String content = ModManager.readLogTail(logFile, 32 * 1024);
                        if (content.isEmpty()) {
                            content = I18n.tr("暂无日志内容。\n文件路径：") + (logFile != null ? logFile.getAbsolutePath() : I18n.tr("未生成"));
                        }
                        final String finalContent = content;
                        final File finalFile = logFile;
                        if (!host.isAdded()) return;
                        host.requireActivity().runOnUiThread(() -> showLogTextPopup(choices[which], finalContent, finalFile));
                    });
                })
                .show();
    }

    private void showLogTextPopup(String title, String content, @Nullable File file) {
        Context ctx = host.requireContext();
        ScrollView sv = new ScrollView(ctx);
        TextView tv = new TextView(ctx);
        int pad = (int) (12 * host.getResources().getDisplayMetrics().density);
        tv.setPadding(pad, pad, pad, pad);
        tv.setTypeface(Typeface.MONOSPACE);
        tv.setTextSize(10);
        tv.setTextIsSelectable(true);
        tv.setText(content);
        sv.addView(tv);

        AlertDialog.Builder b = new AlertDialog.Builder(ctx)
                .setTitle(title)
                .setView(sv)
                .setPositiveButton(I18n.tr("复制"), (d, w) -> {
                    ClipboardManager cm = (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
                    if (cm != null) {
                        cm.setPrimaryClip(ClipData.newPlainText("mod_log", content));
                        Toast.makeText(ctx, I18n.tr("已复制日志"), Toast.LENGTH_SHORT).show();
                    }
                })
                .setNegativeButton(I18n.tr("关闭"), null);
        if (file != null && file.isFile()) {
            b.setNeutralButton(I18n.tr("清空"), (d, w) -> host.ioExecutor.execute(() -> {
                file.delete();
                if (!host.isAdded()) return;
                host.requireActivity().runOnUiThread(() -> Toast.makeText(ctx, I18n.tr("已清空日志文件"), Toast.LENGTH_SHORT).show());
            }));
        }
        b.show();
    }
}
