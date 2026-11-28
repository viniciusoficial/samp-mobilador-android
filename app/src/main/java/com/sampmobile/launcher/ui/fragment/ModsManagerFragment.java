package com.sampmobile.launcher.ui.fragment;


import com.sampmobile.launcher.utils.I18n;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.joom.paranoid.Obfuscate;
import com.sampmobile.launcher.R;
import com.sampmobile.launcher.ui.widget.GtaTabStrip;
import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.LauncherThemeManager;
import com.sampmobile.launcher.utils.ModAssetTool;
import com.sampmobile.launcher.utils.ModManager;
import com.sampmobile.launcher.utils.Utils;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

@Obfuscate
public class ModsManagerFragment extends Fragment implements LauncherThemeManager.ThemeListener {

    private View rootView;

    private static final int TAB_MODLOADER = 0;
    private static final int TAB_AML = 1;
    private static final int TAB_JPATCH = 2;
    private static final int TAB_MONET = 3;
    private static final int TAB_CLEO = 4;

    private int currentTab = TAB_MODLOADER;
    final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();
    private final ModAssetImportWizard assetImportWizard = new ModAssetImportWizard(this);
    private final ModConfigDialogs configDialogs = new ModConfigDialogs(this);

    private View modsContent;
    private TextView tvSectionTitle, tvSectionStatus, tvSectionDesc, tvSectionPath, tvModEmpty;
    private Switch switchSectionMaster;
    private TextView btnActionPrimary, btnActionSecondary, btnActionTertiary, btnActionQuaternary;
    private RecyclerView rvModItems;

    private final List<RowItem> rows = new ArrayList<>();
    private final List<ModManager.ModLoaderEntry> modLoaderCache = new ArrayList<>();
    private ModListAdapter adapter;

    static final int IMPORT_MODE_ASSET_TOOL_ZIP = 100;

    private int pendingImportTab = -1;
    private ActivityResultLauncher<String[]> filePickerLauncher;
    ActivityResultLauncher<String[]> multiFilePickerLauncher;

    private static class RowItem {
        String title;
        String subtitle;
        String badge;
        boolean badgeDanger;
        boolean showSwitch = true;
        boolean checked;
        boolean showUpDown;
        boolean showDelete = true;
        String valueText; // non-null for non-boolean JPatch INI values
        Object payload;
    }

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        filePickerLauncher = registerForActivityResult(
                new ActivityResultContracts.OpenDocument(),
                uri -> {
                    if (uri == null || pendingImportTab < 0) return;
                    if (pendingImportTab == IMPORT_MODE_ASSET_TOOL_ZIP) {
                        assetImportWizard.showConfigDialog(Collections.singletonList(uri));
                    } else {
                        handleImportedFile(pendingImportTab, uri);
                    }
                }
        );
        multiFilePickerLauncher = registerForActivityResult(
                new ActivityResultContracts.OpenMultipleDocuments(),
                uris -> {
                    if (uris != null && !uris.isEmpty()) {
                        assetImportWizard.showConfigDialog(uris);
                    }
                }
        );
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        View v = inflater.inflate(R.layout.fragment_mods_manager, container, false);
        rootView = v;

        TextView tvAbi = v.findViewById(R.id.tv_process_abi);
        tvAbi.setText(ModManager.getProcessAbiLabel());

        v.findViewById(R.id.btn_mods_logs).setOnClickListener(view -> configDialogs.showLogsDialog());
        v.findViewById(R.id.btn_mods_refresh).setOnClickListener(view -> reloadCurrentTab());

        modsContent = v.findViewById(R.id.mods_content);
        GtaTabStrip modTabs = v.findViewById(R.id.mod_tabs);
        modTabs.setTabs("Modloader", I18n.tr("AML 插件"), I18n.tr("JPatch 补丁"), "MonetLoader", I18n.tr("CLEO 脚本"));
        modTabs.setOnTabSelectedListener((index, reselected) -> {
            if (reselected) {
                reloadCurrentTab();
            } else {
                selectTab(index);
                modsContent.animate().cancel();
                modsContent.setAlpha(0f);
                modsContent.animate().alpha(1f).setDuration(180).start();
            }
        });

        tvSectionTitle = v.findViewById(R.id.tv_section_title);
        tvSectionStatus = v.findViewById(R.id.tv_section_status);
        tvSectionDesc = v.findViewById(R.id.tv_section_desc);
        tvSectionPath = v.findViewById(R.id.tv_section_path);
        tvModEmpty = v.findViewById(R.id.tv_mod_empty);
        switchSectionMaster = v.findViewById(R.id.switch_section_master);

        btnActionPrimary = v.findViewById(R.id.btn_action_primary);
        btnActionSecondary = v.findViewById(R.id.btn_action_secondary);
        btnActionTertiary = v.findViewById(R.id.btn_action_tertiary);
        btnActionQuaternary = v.findViewById(R.id.btn_action_quaternary);

        rvModItems = v.findViewById(R.id.rv_mod_items);
        rvModItems.setLayoutManager(new LinearLayoutManager(requireContext()));
        adapter = new ModListAdapter();
        rvModItems.setAdapter(adapter);

        ioExecutor.execute(() -> ModManager.ensureDirectories(requireContext().getApplicationContext()));

        LauncherThemeManager.addListener(this);
        selectTab(TAB_MODLOADER);
        onThemeColorChanged(LauncherThemeManager.getAccentColor(requireContext()));
        return v;
    }

    @Override
    public void onDestroyView() {
        LauncherThemeManager.removeListener(this);
        super.onDestroyView();
        ioExecutor.shutdownNow();
    }

    @Override
    public void onThemeColorChanged(int accentColor) {
        if (rootView == null || !isAdded()) return;
        LauncherThemeManager.applyThemeToTree(rootView, accentColor);
        if (btnActionPrimary != null) {
            btnActionPrimary.setBackgroundTintList(ColorStateList.valueOf(accentColor));
        }
        if (currentTab == TAB_MODLOADER || currentTab == TAB_AML || currentTab == TAB_MONET) {
            if (tvSectionStatus != null) tvSectionStatus.setTextColor(accentColor);
        }
        if (adapter != null) {
            adapter.notifyDataSetChanged();
        }
    }

    private void selectTab(int tab) {
        currentTab = tab;
        configureSectionPanel();
        reloadCurrentTab();
    }

    private String prefKeyForTab(int tab) {
        switch (tab) {
            case TAB_MODLOADER: return "modloader";
            case TAB_AML: return "aml_scripts";
            case TAB_JPATCH: return "jpatch_enabled";
            case TAB_MONET: return "monet_scripts";
            case TAB_CLEO: return "cleo_scripts";
            default: return "modloader";
        }
    }

    void configureSectionPanel() {
        Context ctx = requireContext();
        String key = prefKeyForTab(currentTab);
        ClientSettings settings = ClientSettings.loadLauncher(ctx);
        boolean enabled = settings.getBoolean(key);

        switchSectionMaster.setOnCheckedChangeListener(null);
        switchSectionMaster.setChecked(enabled);
        switchSectionMaster.setOnCheckedChangeListener((btn, isChecked) -> {
            if (currentTab == TAB_JPATCH) {
                if (isChecked && !ClientSettings.loadLauncher(ctx).getBoolean("aml_scripts")) {
                    java.util.Map<String, Object> updates = new java.util.HashMap<>();
                    updates.put(key, true);
                    updates.put("aml_scripts", true);
                    ClientSettings.updateBatchSync(ctx, updates);
                    Toast.makeText(ctx, I18n.tr("已自动同步开启 AML（JPatch 依赖 AML 运行）"), Toast.LENGTH_SHORT).show();
                } else {
                    ClientSettings.setSync(ctx, key, isChecked);
                }
                ioExecutor.execute(() -> ModManager.syncJPatchBinaryState(ctx.getApplicationContext(), isChecked));
            } else {
                ClientSettings.setSync(ctx, key, isChecked);
            }
            reloadCurrentTab();
        });

        btnActionSecondary.setVisibility(View.VISIBLE);
        btnActionTertiary.setVisibility(View.GONE);
        btnActionQuaternary.setVisibility(View.GONE);

        int accentColor = LauncherThemeManager.getAccentColor(ctx);
        btnActionPrimary.setBackgroundTintList(ColorStateList.valueOf(accentColor));
        btnActionTertiary.setTextColor(accentColor);
        LauncherThemeManager.applySwitchTheme(switchSectionMaster, accentColor);

        File pathDir;
        switch (currentTab) {
            case TAB_MODLOADER:
                tvSectionTitle.setText(I18n.tr("Modloader 资源覆写"));
                tvSectionStatus.setText(I18n.tr("状态：已就绪 · 内置 IMG+TXD 零污染覆写"));
                tvSectionStatus.setTextColor(accentColor);
                tvSectionDesc.setText(I18n.tr("无需修改官方 gta3.img 或基础贴图：内置 IMG+TXD 工具可直接导入皮肤/载具/武器模型（.dff/.ifp/.col）与 PNG/PC .txd 贴图，进游戏自动打包虚拟 models.img 并挂载自定义 texdb。"));
                pathDir = ModManager.getModLoaderDir(ctx);
                btnActionPrimary.setText(I18n.tr("导入皮肤/模型 (IMG+TXD)"));
                btnActionPrimary.setOnClickListener(v -> assetImportWizard.showSourceChooser());
                btnActionSecondary.setText(I18n.tr("导入完整 ZIP 模组包"));
                btnActionSecondary.setOnClickListener(v -> launchPicker(TAB_MODLOADER, new String[]{"application/zip", "application/x-zip-compressed", "*/*"}));
                btnActionTertiary.setVisibility(View.VISIBLE);
                btnActionTertiary.setText(I18n.tr("新建空模组目录"));
                btnActionTertiary.setOnClickListener(v -> showCreateModFolderDialog());
                break;

            case TAB_AML:
                tvSectionTitle.setText("AndroidModLoader (AML)");
                tvSectionStatus.setText(I18n.tr("状态：已内置 AML 1.5.0 · 支持 64位/32位"));
                tvSectionStatus.setTextColor(accentColor);
                tvSectionDesc.setText(I18n.tr("加载 mods/ 目录下的原生 .so 插件。启动时会自动将包名伪装为 com.rockstargames.gtasa 并输出日志，请确保导入与当前进程一致的 ") + ModManager.getProcessAbiName() + I18n.tr(" 插件。"));
                pathDir = ModManager.getAmlModsDir(ctx);
                btnActionPrimary.setText(I18n.tr("导入 AML 插件 (.so)"));
                btnActionPrimary.setOnClickListener(v -> launchPicker(TAB_AML, new String[]{"*/*"}));
                btnActionSecondary.setText(I18n.tr("AML 核心配置"));
                btnActionSecondary.setOnClickListener(v -> configDialogs.showAmlCoreSettingsDialog());
                break;

            case TAB_JPATCH:
                tvSectionTitle.setText(I18n.tr("JPatch 引擎补丁"));
                tvSectionStatus.setText(I18n.tr("正在检查 libJPatch.so 状态…"));
                tvSectionStatus.setTextColor(ctx.getColor(R.color.launcher_text_dim));
                tvSectionDesc.setText(I18n.tr("RusJJ 的 GTA:SA 2.10 修复补丁集（依赖 AML）。由于版权协议禁止重新分发二进制，请从官方页面下载对应 ") + ModManager.getProcessAbiName() + I18n.tr(" 的 libJPatch.so 后点击下方导入；右侧可直接可视化编辑 JPatch.ini。"));
                pathDir = ModManager.getJPatchConfigFile(ctx);
                btnActionPrimary.setText(I18n.tr("导入 libJPatch.so"));
                btnActionPrimary.setOnClickListener(v -> launchPicker(TAB_JPATCH, new String[]{"*/*"}));
                btnActionSecondary.setText(I18n.tr("应用 SA-MP 安全预设"));
                btnActionSecondary.setOnClickListener(v -> configDialogs.applyJPatchSafePreset());
                btnActionTertiary.setVisibility(View.VISIBLE);
                btnActionTertiary.setText(I18n.tr("新增自定义配置项"));
                btnActionTertiary.setOnClickListener(v -> configDialogs.showAddJPatchKeyDialog());
                btnActionQuaternary.setVisibility(View.VISIBLE);
                btnActionQuaternary.setText(I18n.tr("打开 JPatch 官方页"));
                btnActionQuaternary.setOnClickListener(v -> openUrl(ModManager.JPATCH_OFFICIAL_URL));
                break;

            case TAB_MONET:
                tvSectionTitle.setText("MonetLoader (Lua)");
                tvSectionStatus.setText(I18n.tr("状态：已内置 MonetLoader 3.8.0 · 双 ABI + 原生 SA-MP 桥接"));
                tvSectionStatus.setTextColor(accentColor);
                tvSectionDesc.setText(I18n.tr("运行 files/monetloader/ 目录下的 .lua 脚本（LuaJIT 5.1 + mimgui + samp.events + 原生 RakHook 桥接，兼容 MoonLoader 生态），首次开启将自动释放内置 Lua 标准库。"));
                pathDir = ModManager.getMonetDir(ctx);
                btnActionPrimary.setText(I18n.tr("导入 Lua 脚本 (.lua)"));
                btnActionPrimary.setOnClickListener(v -> launchPicker(TAB_MONET, new String[]{"*/*"}));
                btnActionSecondary.setVisibility(View.VISIBLE);
                btnActionSecondary.setText(I18n.tr("重装内置 Lua 标准库"));
                btnActionSecondary.setOnClickListener(v -> {
                    final Context appCtx = ctx.getApplicationContext();
                    Toast.makeText(ctx, I18n.tr("正在释放 MonetLoader 内置 Lua 库…"), Toast.LENGTH_SHORT).show();
                    ioExecutor.execute(() -> {
                        boolean ok = ModManager.ensureMonetLibsInstalled(appCtx, true);
                        if (isAdded()) {
                            requireActivity().runOnUiThread(() -> Toast.makeText(ctx,
                                    ok ? I18n.tr("已重装 mimgui / samp.events / moonloader 标准库") : I18n.tr("释放标准库失败"),
                                    Toast.LENGTH_SHORT).show());
                        }
                    });
                });
                break;

            case TAB_CLEO:
            default:
                tvSectionTitle.setText(I18n.tr("CLEO 脚本"));
                tvSectionStatus.setText(I18n.tr("状态：已内置原生 CLEO 引擎 · 支持 64位/32位"));
                tvSectionStatus.setTextColor(accentColor);
                tvSectionDesc.setText(I18n.tr("运行 files/cleo/ 目录下的 .csa/.cs（自动启动）、.csi（游戏内屏幕顶部中段向下滑动打开 CLEO 菜单调用）与 .fxt 自定义文本。双 ABI 原生兼容，自动同步联机玩家句柄。"));
                pathDir = ModManager.getCleoDir(ctx);
                btnActionPrimary.setText(I18n.tr("导入 CLEO 脚本"));
                btnActionPrimary.setOnClickListener(v -> launchPicker(TAB_CLEO, new String[]{"*/*"}));
                btnActionSecondary.setVisibility(View.GONE);
                break;
        }

        final String pathStr = pathDir != null ? pathDir.getAbsolutePath() : "";
        tvSectionPath.setText(I18n.tr("路径（点按复制）：\n") + pathStr);
        tvSectionPath.setOnClickListener(v -> {
            ClipboardManager cm = (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm != null) {
                cm.setPrimaryClip(ClipData.newPlainText("mod_path", pathStr));
                Toast.makeText(ctx, I18n.tr("已复制路径"), Toast.LENGTH_SHORT).show();
            }
        });
    }

    void reloadCurrentTab() {
        final int tab = currentTab;
        final Context appCtx = requireContext().getApplicationContext();

        ioExecutor.execute(() -> {
            List<RowItem> loaded = new ArrayList<>();
            String jpatchStatusText = null;
            boolean jpatchStatusOk = false;

            if (tab == TAB_MODLOADER) {
                List<ModManager.ModLoaderEntry> list = ModManager.listModLoaderPacks(appCtx);
                synchronized (modLoaderCache) {
                    modLoaderCache.clear();
                    modLoaderCache.addAll(list);
                }
                for (ModManager.ModLoaderEntry e : list) {
                    RowItem r = new RowItem();
                    r.title = e.name;
                    r.subtitle = e.fileCount + I18n.tr(" 个文件 · ") + Utils.convertBytesToString(e.totalBytes) + I18n.tr(" · 优先级 ") + e.priority;
                    r.badge = e.enabled ? I18n.tr("已启用") : I18n.tr("已禁用");
                    r.badgeDanger = !e.enabled;
                    r.checked = e.enabled;
                    r.showUpDown = true;
                    r.showDelete = true;
                    r.payload = e;
                    loaded.add(r);
                }
            } else if (tab == TAB_AML) {
                List<ModManager.FileModEntry> list = ModManager.listAmlPlugins(appCtx);
                for (ModManager.FileModEntry e : list) {
                    RowItem r = new RowItem();
                    r.title = e.displayName;
                    r.subtitle = Utils.convertBytesToString(e.sizeBytes)
                            + (e.isJPatch ? I18n.tr(" · JPatch 补丁插件") : I18n.tr(" · AML 原生插件"))
                            + (e.abiCompatible ? "" : I18n.tr(" · 与当前进程架构不匹配！"));
                    r.badge = e.abiCompatible ? e.abiLabel : (I18n.tr("不兼容: ") + e.abiLabel);
                    r.badgeDanger = !e.abiCompatible;
                    r.checked = e.enabled;
                    r.showDelete = true;
                    r.payload = e;
                    loaded.add(r);
                }
            } else if (tab == TAB_JPATCH) {
                ModManager.FileModEntry jpBin = ModManager.findJPatchBinary(appCtx);
                if (jpBin == null) {
                    jpatchStatusText = I18n.tr("未检测到 libJPatch.so — 请先点击下方导入（当前可预先编辑配置）");
                    jpatchStatusOk = false;
                } else if (!jpBin.abiCompatible) {
                    jpatchStatusText = I18n.tr("已检测到 ") + jpBin.displayName + I18n.tr("，但架构为 ") + jpBin.abiLabel + I18n.tr("（与当前进程不匹配！）");
                    jpatchStatusOk = false;
                } else {
                    jpatchStatusText = I18n.tr("已安装：") + jpBin.displayName + " (" + jpBin.abiLabel + ") · "
                            + (jpBin.enabled ? I18n.tr("已启用") : I18n.tr("已禁用"));
                    jpatchStatusOk = true;
                }

                List<ModManager.JPatchOption> opts = ModManager.loadJPatchOptions(appCtx);
                for (ModManager.JPatchOption opt : opts) {
                    RowItem r = new RowItem();
                    r.title = opt.title;
                    r.subtitle = opt.key + " — " + opt.description;
                    if (!opt.safeForSamp) {
                        r.badge = I18n.tr("联机慎用");
                        r.badgeDanger = true;
                    } else if (!opt.fromKnownCatalog) {
                        r.badge = opt.section;
                        r.badgeDanger = false;
                    }
                    r.showDelete = false;
                    if (opt.isBoolean) {
                        r.showSwitch = true;
                        r.checked = ModManager.parseIniBool(opt.value, false);
                    } else {
                        r.showSwitch = false;
                        r.valueText = opt.value;
                    }
                    r.payload = opt;
                    loaded.add(r);
                }
            } else if (tab == TAB_MONET) {
                List<ModManager.FileModEntry> list = ModManager.listMonetScripts(appCtx);
                for (ModManager.FileModEntry e : list) {
                    RowItem r = new RowItem();
                    r.title = e.displayName;
                    r.subtitle = Utils.convertBytesToString(e.sizeBytes) + I18n.tr(" · Lua 脚本");
                    r.badge = e.enabled ? I18n.tr("已启用") : I18n.tr("已禁用");
                    r.badgeDanger = !e.enabled;
                    r.checked = e.enabled;
                    r.showDelete = true;
                    r.payload = e;
                    loaded.add(r);
                }
            } else if (tab == TAB_CLEO) {
                List<ModManager.FileModEntry> list = ModManager.listCleoScripts(appCtx);
                for (ModManager.FileModEntry e : list) {
                    RowItem r = new RowItem();
                    r.title = e.displayName;
                    r.subtitle = Utils.convertBytesToString(e.sizeBytes) + I18n.tr(" · CLEO 脚本");
                    r.badge = e.enabled ? I18n.tr("已启用") : I18n.tr("已禁用");
                    r.badgeDanger = !e.enabled;
                    r.checked = e.enabled;
                    r.showDelete = true;
                    r.payload = e;
                    loaded.add(r);
                }
            }

            final String finalJpStatus = jpatchStatusText;
            final boolean finalJpOk = jpatchStatusOk;

            if (!isAdded()) return;
            requireActivity().runOnUiThread(() -> {
                if (!isAdded() || currentTab != tab) return;
                if (tab == TAB_JPATCH && finalJpStatus != null) {
                    tvSectionStatus.setText(finalJpStatus);
                    tvSectionStatus.setTextColor(finalJpOk
                            ? LauncherThemeManager.getAccentColor(requireContext())
                            : requireContext().getColor(R.color.launcher_danger));
                }
                rows.clear();
                rows.addAll(loaded);
                adapter.notifyDataSetChanged();

                if (rows.isEmpty()) {
                    tvModEmpty.setVisibility(View.VISIBLE);
                    tvModEmpty.setText(emptyHintForTab(tab));
                } else {
                    tvModEmpty.setVisibility(View.GONE);
                }
            });
        });
    }

    private String emptyHintForTab(int tab) {
        switch (tab) {
            case TAB_MODLOADER:
                return I18n.tr("暂无 Modloader 模组包\n点击右侧「导入皮肤/模型 (IMG+TXD)」或「导入完整 ZIP 模组包」，也可手动放入 files/modloader/");
            case TAB_AML:
                return I18n.tr("暂无 AML 插件 (.so)\n点击右侧「导入 AML 插件 (.so)」或放入 Android/data/com.sampmobile.launcher/mods/");
            case TAB_MONET:
                return I18n.tr("暂无 MonetLoader Lua 脚本\n点击右侧「导入 Lua 脚本 (.lua)」或放入 files/monetloader/");
            case TAB_CLEO:
                return I18n.tr("暂无 CLEO 脚本\n点击右侧「导入 CLEO 脚本」或放入 files/cleo/");
            default:
                return I18n.tr("暂无条目");
        }
    }

    void launchPicker(int tab, String[] mimeTypes) {
        pendingImportTab = tab;
        try {
            filePickerLauncher.launch(mimeTypes);
        } catch (Exception e) {
            Toast.makeText(requireContext(), I18n.tr("无法打开系统文件选择器: ") + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void handleImportedFile(int tab, Uri uri) {
        Context appCtx = requireContext().getApplicationContext();
        Toast.makeText(requireContext(), I18n.tr("正在导入…"), Toast.LENGTH_SHORT).show();
        ioExecutor.execute(() -> {
            String msg;
            try {
                if (tab == TAB_MODLOADER) {
                    String folder = ModManager.importModLoaderZip(appCtx, uri);
                    msg = I18n.tr("已导入模组包：") + folder;
                } else if (tab == TAB_AML || tab == TAB_JPATCH) {
                    String origName = ModManager.queryDisplayName(appCtx, uri);
                    if (origName == null || !origName.toLowerCase(Locale.ROOT).endsWith(".so")) {
                        throw new IllegalArgumentException(I18n.tr("请选择 .so 格式的插件文件"));
                    }
                    File dest = ModManager.importFileToDir(appCtx, uri, ModManager.getAmlModsDir(appCtx),
                            tab == TAB_JPATCH ? "libJPatch.so" : "plugin.so");
                    ModManager.ElfInfo elf = ModManager.inspectElfAbi(dest);
                    if (!elf.valid) {
                        dest.delete();
                        throw new IllegalArgumentException(I18n.tr("导入的文件不是有效的 Android .so (ELF) 库"));
                    }
                    if (!elf.matchesProcess) {
                        msg = I18n.tr("已导入 ") + dest.getName() + I18n.tr("，注意：该插件为 ") + elf.abiName + I18n.tr("，与当前 ") + ModManager.getProcessAbiLabel() + I18n.tr(" 不匹配！");
                    } else {
                        msg = I18n.tr("已导入 ") + dest.getName() + " (" + elf.abiName + ")";
                    }
                } else if (tab == TAB_MONET) {
                    File dest = ModManager.importFileToDir(appCtx, uri, ModManager.getMonetDir(appCtx), "script.lua");
                    msg = I18n.tr("已导入 Lua 脚本：") + dest.getName();
                } else {
                    File dest = ModManager.importFileToDir(appCtx, uri, ModManager.getCleoDir(appCtx), "script.csa");
                    msg = I18n.tr("已导入 CLEO 脚本：") + dest.getName();
                }
            } catch (Exception e) {
                msg = I18n.tr("导入失败：") + e.getMessage();
            }

            final String finalMsg = msg;
            if (!isAdded()) return;
            requireActivity().runOnUiThread(() -> {
                if (!isAdded()) return;
                Toast.makeText(requireContext(), finalMsg, Toast.LENGTH_LONG).show();
                reloadCurrentTab();
            });
        });
    }

    private void showCreateModFolderDialog() {
        EditText input = new EditText(requireContext());
        input.setHint(I18n.tr("输入模组文件夹名称（如 HD_Roads）"));
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        LinearLayout wrap = new LinearLayout(requireContext());
        wrap.setPadding(pad, pad / 2, pad, 0);
        wrap.addView(input, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        new AlertDialog.Builder(requireContext())
                .setTitle(I18n.tr("新建 Modloader 目录"))
                .setView(wrap)
                .setPositiveButton(I18n.tr("创建"), (d, w) -> {
                    String name = input.getText().toString().trim();
                    if (name.isEmpty()) return;
                    Context appCtx = requireContext().getApplicationContext();
                    ioExecutor.execute(() -> {
                        File created = ModManager.createModLoaderFolder(appCtx, name);
                        if (!isAdded()) return;
                        requireActivity().runOnUiThread(() -> {
                            if (!isAdded()) return;
                            Toast.makeText(requireContext(),
                                    created != null ? (I18n.tr("已创建目录：") + created.getName()) : I18n.tr("创建目录失败"),
                                    Toast.LENGTH_SHORT).show();
                            reloadCurrentTab();
                        });
                    });
                })
                .setNegativeButton(I18n.tr("取消"), null)
                .show();
    }

    private void openUrl(String url) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (Exception e) {
            Toast.makeText(requireContext(), I18n.tr("无法打开链接: ") + url, Toast.LENGTH_SHORT).show();
        }
    }

    private void moveModLoaderPriority(ModManager.ModLoaderEntry target, int delta) {
        Context appCtx = requireContext().getApplicationContext();
        ioExecutor.execute(() -> {
            synchronized (modLoaderCache) {
                int idx = modLoaderCache.indexOf(target);
                int swapIdx = idx + delta;
                if (idx < 0 || swapIdx < 0 || swapIdx >= modLoaderCache.size()) return;
                ModManager.ModLoaderEntry other = modLoaderCache.get(swapIdx);
                modLoaderCache.set(idx, other);
                modLoaderCache.set(swapIdx, target);
                // Reassign descending priorities cleanly (e.g. 100, 90, 80...)
                int base = modLoaderCache.size() * 10;
                for (int i = 0; i < modLoaderCache.size(); i++) {
                    modLoaderCache.get(i).priority = base - i * 10;
                }
                ModManager.saveModLoaderConfig(appCtx, modLoaderCache);
            }
            if (!isAdded()) return;
            requireActivity().runOnUiThread(this::reloadCurrentTab);
        });
    }

    // ══════════ RecyclerView Adapter ══════════

    private class ModListAdapter extends RecyclerView.Adapter<ModListAdapter.VH> {

        class VH extends RecyclerView.ViewHolder {
            final TextView title, subtitle, badge, btnValue;
            final ImageView btnUp, btnDown, btnDelete;
            final Switch swEnabled;

            VH(@NonNull View itemView) {
                super(itemView);
                title = itemView.findViewById(R.id.tv_item_title);
                subtitle = itemView.findViewById(R.id.tv_item_subtitle);
                badge = itemView.findViewById(R.id.tv_item_badge);
                btnUp = itemView.findViewById(R.id.btn_item_up);
                btnDown = itemView.findViewById(R.id.btn_item_down);
                btnValue = itemView.findViewById(R.id.btn_item_value);
                swEnabled = itemView.findViewById(R.id.switch_item_enabled);
                btnDelete = itemView.findViewById(R.id.btn_item_delete);
            }
        }

        @NonNull
        @Override
        public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View v = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_mod_entry, parent, false);
            return new VH(v);
        }

        @Override
        public void onBindViewHolder(@NonNull VH h, int position) {
            RowItem item = rows.get(position);
            Context ctx = h.itemView.getContext();

            h.title.setText(item.title);
            h.subtitle.setText(item.subtitle);

            int accentColor = LauncherThemeManager.getAccentColor(ctx);
            if (item.badge != null && !item.badge.isEmpty()) {
                h.badge.setVisibility(View.VISIBLE);
                h.badge.setText(item.badge);
                h.badge.setTextColor(item.badgeDanger ? ctx.getColor(R.color.launcher_danger) : accentColor);
            } else {
                h.badge.setVisibility(View.GONE);
            }

            h.btnUp.setVisibility(item.showUpDown ? View.VISIBLE : View.GONE);
            h.btnDown.setVisibility(item.showUpDown ? View.VISIBLE : View.GONE);
            if (item.showUpDown && item.payload instanceof ModManager.ModLoaderEntry) {
                ModManager.ModLoaderEntry entry = (ModManager.ModLoaderEntry) item.payload;
                h.btnUp.setOnClickListener(v -> moveModLoaderPriority(entry, -1));
                h.btnDown.setOnClickListener(v -> moveModLoaderPriority(entry, 1));
            }

            if (item.showSwitch) {
                h.swEnabled.setVisibility(View.VISIBLE);
                h.btnValue.setVisibility(View.GONE);
                LauncherThemeManager.applySwitchTheme(h.swEnabled, accentColor);
                h.swEnabled.setOnCheckedChangeListener(null);
                h.swEnabled.setChecked(item.checked);
                h.swEnabled.setOnCheckedChangeListener((btn, isChecked) -> {
                    item.checked = isChecked;
                    onRowToggle(item, isChecked);
                });
            } else {
                h.swEnabled.setVisibility(View.GONE);
                h.btnValue.setVisibility(View.VISIBLE);
                h.btnValue.setTextColor(accentColor);
                h.btnValue.setText(item.valueText != null ? item.valueText : "");
                h.btnValue.setOnClickListener(v -> {
                    if (item.payload instanceof ModManager.JPatchOption) {
                        configDialogs.showEditJPatchValueDialog((ModManager.JPatchOption) item.payload);
                    }
                });
            }

            h.btnDelete.setVisibility(item.showDelete ? View.VISIBLE : View.GONE);
            h.btnDelete.setOnClickListener(v -> confirmDelete(item));
        }

        @Override
        public int getItemCount() {
            return rows.size();
        }
    }

    private void onRowToggle(RowItem item, boolean isChecked) {
        Context appCtx = requireContext().getApplicationContext();
        if (item.payload instanceof ModManager.ModLoaderEntry) {
            ModManager.ModLoaderEntry entry = (ModManager.ModLoaderEntry) item.payload;
            entry.enabled = isChecked;
            ioExecutor.execute(() -> {
                synchronized (modLoaderCache) {
                    ModManager.saveModLoaderConfig(appCtx, modLoaderCache);
                }
                if (!isAdded()) return;
                requireActivity().runOnUiThread(this::reloadCurrentTab);
            });
        } else if (item.payload instanceof ModManager.FileModEntry) {
            ModManager.FileModEntry entry = (ModManager.FileModEntry) item.payload;
            ioExecutor.execute(() -> {
                File renamed = ModManager.toggleDisabledFile(entry.file, isChecked);
                if (renamed != null) {
                    entry.file = renamed;
                    entry.enabled = isChecked;
                    if (entry.isJPatch) {
                        ClientSettings.setSync(appCtx, "jpatch_enabled", isChecked);
                    }
                }
                androidx.fragment.app.FragmentActivity act = getActivity();
                if (act == null || !isAdded()) return;
                act.runOnUiThread(() -> {
                    if (isAdded()) reloadCurrentTab();
                });
            });
        } else if (item.payload instanceof ModManager.JPatchOption) {
            ModManager.JPatchOption opt = (ModManager.JPatchOption) item.payload;
            opt.value = isChecked ? "1" : "0";
            ioExecutor.execute(() -> ModManager.saveJPatchOption(appCtx, opt.section, opt.key, opt.value));
        }
    }

    private void confirmDelete(RowItem item) {
        Context ctx = requireContext();
        new AlertDialog.Builder(ctx)
                .setTitle(I18n.tr("确认删除"))
                .setMessage(I18n.tr("确定要永久删除「") + item.title + I18n.tr("」吗？此操作无法撤销。"))
                .setPositiveButton(I18n.tr("删除"), (d, w) -> {
                    Context appCtx = ctx.getApplicationContext();
                    ioExecutor.execute(() -> {
                        if (item.payload instanceof ModManager.ModLoaderEntry) {
                            ModManager.ModLoaderEntry entry = (ModManager.ModLoaderEntry) item.payload;
                            ModManager.deleteRecursive(new File(ModManager.getModLoaderDir(appCtx), entry.name));
                            synchronized (modLoaderCache) {
                                modLoaderCache.remove(entry);
                                ModManager.saveModLoaderConfig(appCtx, modLoaderCache);
                            }
                        } else if (item.payload instanceof ModManager.FileModEntry) {
                            ModManager.FileModEntry entry = (ModManager.FileModEntry) item.payload;
                            ModManager.deleteRecursive(entry.file);
                        }
                        if (!isAdded()) return;
                        requireActivity().runOnUiThread(() -> {
                            Toast.makeText(ctx, I18n.tr("已删除 ") + item.title, Toast.LENGTH_SHORT).show();
                            reloadCurrentTab();
                        });
                    });
                })
                .setNegativeButton(I18n.tr("取消"), null)
                .show();
    }
}
