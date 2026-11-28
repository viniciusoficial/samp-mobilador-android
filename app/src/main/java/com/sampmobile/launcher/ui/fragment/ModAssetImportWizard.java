package com.sampmobile.launcher.ui.fragment;


import com.sampmobile.launcher.utils.I18n;
import android.content.Context;
import android.net.Uri;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.FragmentActivity;

import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.ModAssetTool;
import com.sampmobile.launcher.utils.ModManager;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Built-in GTA IMG + TXD zero-pollution asset import wizard for {@link ModsManagerFragment}.
 */
final class ModAssetImportWizard {
    private final ModsManagerFragment host;

    ModAssetImportWizard(ModsManagerFragment host) {
        this.host = host;
    }

    void showSourceChooser() {
        Context ctx = host.requireContext();
        String[] choices = {
                I18n.tr("从 ZIP 压缩包导入（自动识别 .dff + .png / PC .txd）"),
                I18n.tr("多选散文件导入（.dff / .ifp / .col / .txd / .png）")
        };
        new AlertDialog.Builder(ctx)
                .setTitle(I18n.tr("内置 IMG + TXD 零污染导入"))
                .setItems(choices, (d, which) -> {
                    if (which == 0) {
                        host.launchPicker(ModsManagerFragment.IMPORT_MODE_ASSET_TOOL_ZIP,
                                new String[]{"application/zip", "application/x-zip-compressed", "*/*"});
                    } else {
                        try {
                            host.multiFilePickerLauncher.launch(new String[]{"*/*"});
                        } catch (Exception e) {
                            Toast.makeText(ctx, I18n.tr("无法打开多文件选择器: ") + e.getMessage(), Toast.LENGTH_LONG).show();
                        }
                    }
                })
                .setNegativeButton(I18n.tr("取消"), null)
                .show();
    }

    void showConfigDialog(List<Uri> uris) {
        if (!host.isAdded() || uris == null || uris.isEmpty()) return;
        Context ctx = host.requireContext();

        String suggestedPack = "";
        StringBuilder summary = new StringBuilder();
        if (uris.size() == 1) {
            String name = ModManager.queryDisplayName(ctx, uris.get(0));
            if (name == null) name = I18n.tr("所选文件");
            summary.append(I18n.tr("已选文件：")).append(name);
            int dot = name.lastIndexOf('.');
            suggestedPack = (dot > 0) ? name.substring(0, dot) : name;
        } else {
            summary.append(I18n.tr("已多选 ")).append(uris.size()).append(I18n.tr(" 个文件："));
            int shown = 0;
            for (Uri u : uris) {
                String n = ModManager.queryDisplayName(ctx, u);
                if (n == null) continue;
                if (suggestedPack.isEmpty() && n.toLowerCase(Locale.ROOT).endsWith(".dff")) {
                    int dot = n.lastIndexOf('.');
                    suggestedPack = (dot > 0) ? n.substring(0, dot) : n;
                }
                if (shown < 3) {
                    if (shown > 0) summary.append("、");
                    summary.append(n);
                    shown++;
                }
            }
            if (uris.size() > 3) summary.append(I18n.tr(" 等"));
            if (suggestedPack.isEmpty()) suggestedPack = "CustomSkin";
        }
        suggestedPack = suggestedPack.replaceAll("[\\\\/:*?\"<>|]", "_").trim();
        if (suggestedPack.length() > 36) suggestedPack = suggestedPack.substring(0, 36);

        int pad = (int) (14 * host.getResources().getDisplayMetrics().density);
        ScrollView sv = new ScrollView(ctx);
        LinearLayout layout = new LinearLayout(ctx);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(pad, pad / 2, pad, pad / 2);
        sv.addView(layout);

        TextView tvSummary = new TextView(ctx);
        tvSummary.setText(summary.toString());
        tvSummary.setTextSize(12);
        layout.addView(tvSummary);

        TextView lblPack = new TextView(ctx);
        lblPack.setText(I18n.tr("\n模组包名称（生成在 modloader/<名称>/，可随时开关或删除）："));
        lblPack.setTextSize(12);
        layout.addView(lblPack);

        EditText etPack = new EditText(ctx);
        etPack.setText(suggestedPack);
        etPack.setHint(I18n.tr("如 MaiShiranui_Skin1"));
        etPack.setSingleLine(true);
        layout.addView(etPack);

        TextView lblTarget = new TextView(ctx);
        lblTarget.setText(I18n.tr("\n目标模型名（可选；留空 = 保持原 .dff 文件名，如皮肤/载具需改名请点击下方速查）："));
        lblTarget.setTextSize(12);
        layout.addView(lblTarget);

        EditText etTarget = new EditText(ctx);
        etTarget.setHint(I18n.tr("留空保持原名，或填如 truth / infernus"));
        etTarget.setSingleLine(true);
        layout.addView(etTarget);

        LinearLayout btnRow = new LinearLayout(ctx);
        btnRow.setOrientation(LinearLayout.HORIZONTAL);
        Button btnPickSkin = new Button(ctx);
        btnPickSkin.setText(I18n.tr("选择皮肤 ID"));
        btnPickSkin.setOnClickListener(v -> showIdeSelectorDialog(true, etTarget));
        Button btnPickVeh = new Button(ctx);
        btnPickVeh.setText(I18n.tr("选择载具 ID"));
        btnPickVeh.setOnClickListener(v -> showIdeSelectorDialog(false, etTarget));
        Button btnClearTarget = new Button(ctx);
        btnClearTarget.setText(I18n.tr("保持原名"));
        btnClearTarget.setOnClickListener(v -> etTarget.setText(""));

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        btnRow.addView(btnPickSkin, lp);
        btnRow.addView(btnPickVeh, lp);
        btnRow.addView(btnClearTarget, lp);
        layout.addView(btnRow);

        TextView tvHint = new TextView(ctx);
        tvHint.setText(I18n.tr("\n说明：PNG/JPG/WEBP 与 PC .txd 会自动编译为 Android 贴图库，.dff/.ifp/.col 在进游戏时自动装入虚拟 models.img，完全不修改官方 gta3.img。"));
        tvHint.setTextSize(11);
        layout.addView(tvHint);

        new AlertDialog.Builder(ctx)
                .setTitle(I18n.tr("配置并编译模型 / 贴图包"))
                .setView(sv)
                .setPositiveButton(I18n.tr("开始编译并安装"), (d, w) -> {
                    String packName = etPack.getText().toString().trim();
                    String targetModel = etTarget.getText().toString().trim();
                    runImport(uris, packName, targetModel.isEmpty() ? null : targetModel);
                })
                .setNegativeButton(I18n.tr("取消"), null)
                .show();
    }

    private void showIdeSelectorDialog(boolean isSkins, EditText targetField) {
        Context ctx = host.requireContext();
        Context appCtx = ctx.getApplicationContext();
        Toast.makeText(ctx, isSkins ? I18n.tr("正在读取 SAMP/peds.ide…") : I18n.tr("正在读取 SAMP/vehicles.ide…"), Toast.LENGTH_SHORT).show();

        host.ioExecutor.execute(() -> {
            List<ModAssetTool.IdeEntry> allEntries = isSkins
                    ? ModAssetTool.loadPedSkins(appCtx)
                    : ModAssetTool.loadVehicles(appCtx);

            if (!host.isAdded()) return;
            host.requireActivity().runOnUiThread(() -> {
                if (!host.isAdded()) return;
                if (allEntries.isEmpty()) {
                    Toast.makeText(ctx,
                            isSkins ? I18n.tr("未找到 SAMP/peds.ide（请先部署游戏数据）") : I18n.tr("未找到 SAMP/vehicles.ide（请先部署游戏数据）"),
                            Toast.LENGTH_LONG).show();
                    return;
                }

                int pad = (int) (12 * host.getResources().getDisplayMetrics().density);
                LinearLayout box = new LinearLayout(ctx);
                box.setOrientation(LinearLayout.VERTICAL);
                box.setPadding(pad, pad / 2, pad, pad / 2);

                EditText etSearch = new EditText(ctx);
                etSearch.setHint(isSkins ? I18n.tr("搜索皮肤 ID 或名称（如 1 / truth / swat）") : I18n.tr("搜索载具 ID 或名称（如 411 / infernus / sultan）"));
                etSearch.setSingleLine(true);
                box.addView(etSearch);

                List<ModAssetTool.IdeEntry> filtered = new ArrayList<>(allEntries);
                List<String> labels = new ArrayList<>(allEntries.size());
                for (ModAssetTool.IdeEntry e : allEntries) labels.add(e.displayLabel());

                ArrayAdapter<String> listAdapter = new ArrayAdapter<>(ctx, android.R.layout.simple_list_item_1, labels);
                ListView lv = new ListView(ctx);
                lv.setAdapter(listAdapter);
                int listHeight = (int) (220 * host.getResources().getDisplayMetrics().density);
                box.addView(lv, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, listHeight));

                AlertDialog pickerDlg = new AlertDialog.Builder(ctx)
                        .setTitle(isSkins ? I18n.tr("选择目标 SA-MP 皮肤") : I18n.tr("选择目标 SA-MP 载具"))
                        .setView(box)
                        .setNegativeButton(I18n.tr("取消"), null)
                        .create();

                etSearch.addTextChangedListener(new TextWatcher() {
                    @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                    @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
                    @Override public void afterTextChanged(Editable s) {
                        String q = s.toString().trim().toLowerCase(Locale.ROOT);
                        filtered.clear();
                        labels.clear();
                        for (ModAssetTool.IdeEntry e : allEntries) {
                            if (q.isEmpty()
                                    || String.valueOf(e.id).contains(q)
                                    || e.modelName.toLowerCase(Locale.ROOT).contains(q)
                                    || e.extra.toLowerCase(Locale.ROOT).contains(q)) {
                                filtered.add(e);
                                labels.add(e.displayLabel());
                            }
                        }
                        listAdapter.notifyDataSetChanged();
                    }
                });

                lv.setOnItemClickListener((parent, view, position, id) -> {
                    if (position >= 0 && position < filtered.size()) {
                        ModAssetTool.IdeEntry picked = filtered.get(position);
                        targetField.setText(picked.modelName);
                        Toast.makeText(ctx, I18n.tr("已选 ") + picked.displayLabel(), Toast.LENGTH_SHORT).show();
                        pickerDlg.dismiss();
                    }
                });

                pickerDlg.show();
            });
        });
    }

    private void runImport(List<Uri> uris, String packName, @Nullable String targetModel) {
        Context ctx = host.requireContext();
        Context appCtx = ctx.getApplicationContext();

        TextView tvProg = new TextView(ctx);
        int pad = (int) (18 * host.getResources().getDisplayMetrics().density);
        tvProg.setPadding(pad, pad, pad, pad);
        tvProg.setText(I18n.tr("准备解析并编译资产…"));

        AlertDialog progDlg = new AlertDialog.Builder(ctx)
                .setTitle(I18n.tr("正在编译安装模组"))
                .setView(tvProg)
                .setCancelable(false)
                .create();
        progDlg.show();

        host.ioExecutor.execute(() -> {
            try {
                ModAssetTool.ImportResult res = ModAssetTool.importAssetPack(
                        appCtx,
                        uris,
                        packName,
                        targetModel,
                        msg -> {
                            if (!host.isAdded()) return;
                            host.requireActivity().runOnUiThread(() -> {
                                if (progDlg.isShowing()) tvProg.setText(msg);
                            });
                        }
                );

                boolean autoEnabledMaster = false;
                if (!ClientSettings.loadLauncher(appCtx).getBoolean("modloader")) {
                    ClientSettings.setSync(appCtx, "modloader", true);
                    autoEnabledMaster = true;
                }
                final boolean enabledNow = autoEnabledMaster;

                FragmentActivity act = host.getActivity();
                if (act == null || !host.isAdded()) return;
                act.runOnUiThread(() -> {
                    if (progDlg.isShowing()) progDlg.dismiss();
                    if (!host.isAdded() || host.getContext() == null) return;

                    if (enabledNow) {
                        host.configureSectionPanel();
                    }
                    host.reloadCurrentTab();

                    StringBuilder detail = new StringBuilder();
                    detail.append(I18n.tr("模组包目录：modloader/")).append(res.packName).append("\n");
                    detail.append(I18n.tr("• 模型文件：")).append(res.modelCount).append(I18n.tr(" 个（进游戏自动装入虚拟 models.img）\n"));
                    if (res.renamedPrimaryModel != null) {
                        detail.append(I18n.tr("• 主模型已重命名为：")).append(res.renamedPrimaryModel).append("\n");
                    }
                    if (res.compiledTextureCount > 0) {
                        detail.append(I18n.tr("• 已编译贴图：")).append(res.compiledTextureCount)
                                .append(I18n.tr(" 张 → texdb/")).append(res.texDbName).append("/\n");
                    }
                    if (res.passthroughFileCount > 0) {
                        detail.append(I18n.tr("• 原样导入附带文件：")).append(res.passthroughFileCount).append(I18n.tr(" 个\n"));
                    }
                    if (enabledNow) {
                        detail.append(I18n.tr("• 已自动开启 Modloader 总开关\n"));
                    }
                    if (!res.warnings.isEmpty()) {
                        detail.append(I18n.tr("\n注意事项：\n"));
                        for (String w : res.warnings) {
                            detail.append("- ").append(w).append("\n");
                        }
                    }

                    new AlertDialog.Builder(host.getContext())
                            .setTitle(I18n.tr("编译并安装成功"))
                            .setMessage(detail.toString().trim())
                            .setPositiveButton(I18n.tr("确定"), null)
                            .show();
                });
            } catch (Exception e) {
                FragmentActivity act = host.getActivity();
                if (act == null || !host.isAdded()) return;
                act.runOnUiThread(() -> {
                    if (progDlg.isShowing()) progDlg.dismiss();
                    if (!host.isAdded() || host.getContext() == null) return;
                    new AlertDialog.Builder(host.getContext())
                            .setTitle(I18n.tr("导入失败"))
                            .setMessage(e.getMessage() != null ? e.getMessage() : e.toString())
                            .setPositiveButton(I18n.tr("确定"), null)
                            .show();
                });
            }
        });
    }
}
