package com.sampmobile.launcher.utils;

import android.content.Context;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

import com.sampmobile.launcher.utils.ModManager.FileModEntry;
import com.sampmobile.launcher.utils.ModManager.JPatchOption;

final class JPatchConfigManager {
    private JPatchConfigManager() {}

    // ══════════ JPatch Binary + Config Editor ══════════

    public static FileModEntry findJPatchBinary(Context ctx) {
        for (FileModEntry e : ModManager.listAmlPlugins(ctx)) {
            if (e.isJPatch) return e;
        }
        return null;
    }

    public static boolean syncJPatchBinaryState(Context ctx, boolean enable) {
        FileModEntry jp = findJPatchBinary(ctx);
        if (jp == null) return false;
        if (jp.enabled == enable) return true;
        return ModManager.toggleDisabledFile(jp.file, enable) != null;
    }

    public static File getJPatchConfigFile(Context ctx) {
        File cfgDir = ModManager.getAmlConfigsDir(ctx);
        // Official AML JPatch (32-bit & 64-bit) uses AML Config("net.rusjj.jpatch") -> configs/net.rusjj.jpatch.ini
        File official = new File(cfgDir, "net.rusjj.jpatch.ini");
        if (official.exists()) return official;
        File legacyIni = new File(cfgDir, "JPatch.ini");
        if (legacyIni.exists()) return legacyIni;
        File legacyCfg = new File(cfgDir, "JPatch.cfg");
        if (legacyCfg.exists()) return legacyCfg;
        return official;
    }

    static List<JPatchOption> buildKnownJPatchCatalog() {
        List<JPatchOption> list = new ArrayList<>();
        // Safe & high-value GTA:SA 2.10 fixes (exact keys from official net.rusjj.jpatch.ini)
        list.add(new JPatchOption("Gameplay", "Fix210Crash", I18n.tr("修复 2.10 原版随机崩溃"),
                I18n.tr("应用 2.10 引擎崩溃补丁（推荐开启）"), true, "1", true));
        list.add(new JPatchOption("Gameplay", "GetCompositeMatrixFixPossibleCrash", I18n.tr("修复矩阵合成崩溃"),
                I18n.tr("防止模型/骨骼矩阵空指针崩溃"), true, "1", true));
        list.add(new JPatchOption("Gameplay", "FixCollisionAllocatingData", I18n.tr("修复碰撞分配异常"),
                I18n.tr("修复动态碰撞数据分配越界崩溃"), true, "1", true));
        list.add(new JPatchOption("Gameplay", "FixEnterVehicleTasks", I18n.tr("修复上车任务异常"),
                I18n.tr("修复部分情形下上车动作卡住问题"), true, "1", true));
        list.add(new JPatchOption("Gameplay", "FixChassisSwingingHighFPS", I18n.tr("高帧率载具防抖动"),
                I18n.tr("修复 60+ FPS 下车辆底盘剧烈晃动"), true, "1", true));
        list.add(new JPatchOption("Gameplay", "FixVehiclesSpeedHighFPS", I18n.tr("高帧率载具速度修正"),
                I18n.tr("修复高帧率下车辆加速与阻力异常"), true, "1", true));
        list.add(new JPatchOption("Visual", "FixFogWall", I18n.tr("修复雾气背后黑墙"),
                I18n.tr("修复默认着色器下远景雾墙显示异常"), true, "1", true));
        list.add(new JPatchOption("Visual", "FixWheelsRotationSpeed", I18n.tr("高帧率车轮转速修正"),
                I18n.tr("修复高帧率下车轮旋转速度过快"), true, "1", true));
        list.add(new JPatchOption("Visual", "FixGreenTextures", I18n.tr("修复绿色贴图异常"),
                I18n.tr("修复部分 GPU/贴图组合发绿问题"), true, "1", true));
        list.add(new JPatchOption("Visual", "SP_BilinearFilterForPlate", I18n.tr("修复车牌双线性过滤"),
                I18n.tr("提升载具车牌清晰度（SilentPatch）"), true, "1", true));
        list.add(new JPatchOption("Visual", "FixAdjustableSizeLowering", I18n.tr("修复 HUD 缩放偏移"),
                I18n.tr("修复 Adjustable.cfg 自定义 HUD 按钮尺寸偏移"), true, "1", true));
        list.add(new JPatchOption("Visual", "FixXclipseCrash", I18n.tr("修复 Xclipse/Adreno 着色器崩溃"),
                I18n.tr("增强部分新 GPU 驱动下的渲染稳定性"), true, "1", true));

        // Caution: overlap with SAMPMOBILE hooks/patches — disabled in SA-MP safe preset
        list.add(new JPatchOption("Gameplay", "MultiTouch4", I18n.tr("JPatch 四指触控补丁 [联机冲突]"),
                I18n.tr("本端已内置 MultiTouch 拦截与 MonetLoader/CLEO 触控路由，建议关闭以免冲突"), false, "0", true));
        list.add(new JPatchOption("Gameplay", "BuffStreamingMem", I18n.tr("JPatch 固定流送扩容 [联机慎用]"),
                I18n.tr("本端已内置 CStreaming×3 扩容，重复开启可能加大内存压力"), false, "0", true));
        list.add(new JPatchOption("Gameplay", "DynamicStreamingMem", I18n.tr("JPatch 动态流送扩容 [联机慎用]"),
                I18n.tr("与本端内置流送内存策略重叠，联机安全预设默认关闭"), false, "0", true));
        list.add(new JPatchOption("Visual", "EnableAnimatedTextures", I18n.tr("JPatch UV 动画补丁 [联机冲突]"),
                I18n.tr("本端已自研 2.10 有界 UVAnim 驱动，建议关闭以免重复 Hook"), false, "0", true));
        list.add(new JPatchOption("Visual", "FixCrosshair", I18n.tr("JPatch 准星修正 [联机冲突]"),
                I18n.tr("本端已内置联机准星 Hook，建议关闭以免冲突"), false, "0", true));
        list.add(new JPatchOption("Gameplay", "ForceLoadDXT", I18n.tr("强制 DXT 贴图格式 [联机冲突]"),
                I18n.tr("本端已自动按实际存在的 texdb 格式加载，开启会导致黑贴图"), false, "0", true));
        list.add(new JPatchOption("Gameplay", "ForceLoadETC", I18n.tr("强制 ETC 贴图格式 [联机冲突]"),
                I18n.tr("本端已自动选择贴图格式，不建议强制覆盖"), false, "0", true));
        list.add(new JPatchOption("Gameplay", "ForceLoadPVR", I18n.tr("强制 PVR 贴图格式 [联机冲突]"),
                I18n.tr("本端已自动选择贴图格式，不建议强制覆盖"), false, "0", true));
        list.add(new JPatchOption("Visual", "AlwaysDrawWantedStars", I18n.tr("常显通缉星级 [联机慎用]"),
                I18n.tr("联机通缉星级由服务端控制，默认关闭"), false, "0", true));
        return list;
    }

    public static List<JPatchOption> loadJPatchOptions(Context ctx) {
        ModManager.ensureDirectories(ctx);
        File cfgFile = getJPatchConfigFile(ctx);
        IniDocument doc = IniDocument.load(cfgFile);

        List<JPatchOption> options = buildKnownJPatchCatalog();
        Set<String> seenKeys = new HashSet<>();
        for (JPatchOption opt : options) {
            seenKeys.add((opt.section + ":" + opt.key).toLowerCase(Locale.ROOT));
            // Check both opt.section and any other section in JPatch.ini (JPatch may use [Patches]/[Main]/[Preferences])
            String val = findIniValueAnySection(doc, opt.section, opt.key);
            if (val != null) {
                opt.value = val;
            }
        }

        // Also include every extra key present in the user's real JPatch.ini so nothing is hidden!
        for (Map.Entry<String, LinkedHashMap<String, String>> se : doc.sections.entrySet()) {
            String sec = se.getKey();
            for (Map.Entry<String, String> kv : se.getValue().entrySet()) {
                String k = kv.getKey();
                String v = kv.getValue();
                if (seenKeys.contains((sec + ":" + k).toLowerCase(Locale.ROOT))) continue;
                // Avoid duplicating a known key if it lived in another section
                boolean dupKey = false;
                for (JPatchOption opt : options) {
                    if (opt.key.equalsIgnoreCase(k)) {
                        opt.section = sec;
                        opt.value = v;
                        dupKey = true;
                        break;
                    }
                }
                if (dupKey) continue;

                boolean isBool = isBooleanLike(v);
                JPatchOption custom = new JPatchOption(sec, k, k, I18n.tr("来自 ") + cfgFile.getName() + " [" + sec + "]", true, v, isBool);
                custom.value = v;
                custom.fromKnownCatalog = false;
                options.add(custom);
            }
        }
        return options;
    }

    private static String findIniValueAnySection(IniDocument doc, String preferredSection, String key) {
        if (doc.has(preferredSection, key)) {
            return doc.get(preferredSection, key, null);
        }
        for (Map.Entry<String, LinkedHashMap<String, String>> se : doc.sections.entrySet()) {
            for (Map.Entry<String, String> kv : se.getValue().entrySet()) {
                if (kv.getKey().equalsIgnoreCase(key)) {
                    return kv.getValue();
                }
            }
        }
        return null;
    }

    private static boolean isBooleanLike(String val) {
        if (val == null) return false;
        String v = val.trim().toLowerCase(Locale.ROOT);
        return v.equals("0") || v.equals("1") || v.equals("true") || v.equals("false");
    }

    public static void saveJPatchOption(Context ctx, String section, String key, String value) {
        ModManager.ensureDirectories(ctx);
        File cfgFile = getJPatchConfigFile(ctx);
        IniDocument doc = IniDocument.load(cfgFile);
        // Update existing section if key already exists elsewhere
        String targetSec = section;
        String targetKey = key;
        outer:
        for (Map.Entry<String, LinkedHashMap<String, String>> se : doc.sections.entrySet()) {
            for (String existingKey : se.getValue().keySet()) {
                if (existingKey.equalsIgnoreCase(key)) {
                    targetSec = se.getKey();
                    targetKey = existingKey;
                    break outer;
                }
            }
        }
        doc.set(targetSec, targetKey, value);
        try {
            doc.save(cfgFile);
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public static void applyJPatchSampSafePreset(Context ctx) {
        ModManager.ensureDirectories(ctx);
        File cfgFile = getJPatchConfigFile(ctx);
        IniDocument doc = IniDocument.load(cfgFile);
        for (JPatchOption opt : buildKnownJPatchCatalog()) {
            String targetSec = opt.section;
            String targetKey = opt.key;
            outer:
            for (Map.Entry<String, LinkedHashMap<String, String>> se : doc.sections.entrySet()) {
                for (String existingKey : se.getValue().keySet()) {
                    if (existingKey.equalsIgnoreCase(opt.key)) {
                        targetSec = se.getKey();
                        targetKey = existingKey;
                        break outer;
                    }
                }
            }
            doc.set(targetSec, targetKey, opt.defaultSafeValue);
        }
        try {
            doc.save(cfgFile);
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    /**
     * JPatch's MultiTouch4 conflicts with SAMPMOBILE's built-in 4-finger touch
     * pipeline (g_points/g_pointers relocation & NVEventGetNextEvent hook),
     * injecting phantom ptr=0 DOWN/UP events that cut off the virtual joystick.
     * Ensure MultiTouch4=0 before AML loads libJPatch.so without disturbing any
     * other user-configured JPatch options.
     */
    public static void enforceTouchSafety(Context ctx) {
        File cfgFile = getJPatchConfigFile(ctx);
        if (!cfgFile.exists()) return;
        IniDocument doc = IniDocument.load(cfgFile);
        String cur = findIniValueAnySection(doc, "Gameplay", "MultiTouch4");
        if ("0".equals(cur)) return;
        saveJPatchOption(ctx, "Gameplay", "MultiTouch4", "0");
    }
}
