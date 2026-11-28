package com.sampmobile.launcher.utils;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;

import androidx.annotation.Nullable;

import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Built-in zero-pollution GTA IMG + TXD Tool for SAMP-Mobile.
 *
 * - Parses SAMP/peds.ide & SAMP/vehicles.ide for target Skin / Vehicle ID lookup.
 * - Decodes PC RenderWare .txd archives (DXT1/DXT3/DXT5, BGRA8888, RGB565/1555/4444, PAL8/PAL4)
 *   as well as loose .png/.jpg/.webp images.
 * - Compiles textures into WarDrum TextureDatabaseRuntime (.txt + .dxt.dat/.toc/.tmb with
 *   uncompressed RGBA8888 gl_type=0x1401 + full mipmap chain + 8x8 thumbnails) under
 *   files/modloader/<packName>/texdb/<dbName>/.
 * - Places loose .dff/.ifp/.col/.ipl models into files/modloader/<packName>/ where native
 *   ModLoader auto-packs them into the virtual VER2 archive files/modloader/models.img at startup,
 *   never touching or duplicating base gta3.img or base texdb files.
 */
public final class ModAssetTool {

    private static final int MAX_TEX_DIM = 1024;
    private static final int MIN_TEX_DIM = 16;
    private static final int GL_UNSIGNED_BYTE_RGBA = 0x1401;
    private static final int MAX_IMG_BASE_LEN = 19; // + ".dff" = 23 <= VER2 char[24]
    private static final int MAX_ZIP_ENTRIES = 2048;
    private static final long MAX_TOTAL_IMPORT_BYTES = 256L * 1024L * 1024L;

    private static final Set<String> BUILTIN_TEXDBS = new HashSet<>(Arrays.asList(
            "gta3", "gta_int", "samp", "samp_extra", "menu",
            "mobile", "cutscene", "txd", "player", "playerhi"
    ));

    private static final Set<String> MODEL_EXTS = new HashSet<>(Arrays.asList(
            ".dff", ".ifp", ".col", ".ipl"
    ));

    private static final Set<String> IMAGE_EXTS = new HashSet<>(Arrays.asList(
            ".png", ".jpg", ".jpeg", ".webp", ".bmp"
    ));

    private ModAssetTool() {}

    public static final class IdeEntry {
        public final int id;
        public final String modelName;
        public final String txdName;
        public final String extra;

        public IdeEntry(int id, String modelName, String txdName, String extra) {
            this.id = id;
            this.modelName = modelName;
            this.txdName = txdName;
            this.extra = extra;
        }

        public String displayLabel() {
            if (extra != null && !extra.isEmpty() && !extra.equalsIgnoreCase(modelName)) {
                return "ID " + id + " — " + modelName + " (" + extra + ")";
            }
            return "ID " + id + " — " + modelName;
        }
    }

    public static final class DecodedTexture {
        public final String name;
        public final int width;
        public final int height;
        public final byte[] rgba; // width * height * 4 bytes (R, G, B, A)
        public final boolean hasAlpha;

        public DecodedTexture(String name, int width, int height, byte[] rgba, boolean hasAlpha) {
            this.name = name;
            this.width = width;
            this.height = height;
            this.rgba = rgba;
            this.hasAlpha = hasAlpha;
        }
    }

    public static final class ImportResult {
        public String packName;
        public int modelCount;
        public int compiledTextureCount;
        public int passthroughFileCount;
        public String texDbName;
        public String renamedPrimaryModel;
        public final List<String> warnings = new ArrayList<>();
    }

    public interface ProgressCallback {
        void onProgress(String message);
    }

    public static List<IdeEntry> loadPedSkins(Context ctx) {
        return IdeCatalogParser.loadPedSkins(ctx);
    }

    public static List<IdeEntry> loadVehicles(Context ctx) {
        return IdeCatalogParser.loadVehicles(ctx);
    }

    public static String sanitizeModelBaseName(@Nullable String raw) {
        if (raw == null) return "";
        String s = raw.trim().toLowerCase(Locale.ROOT);
        if (s.endsWith(".dff")) s = s.substring(0, s.length() - 4);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
                sb.append(c);
            }
        }
        if (sb.length() > MAX_IMG_BASE_LEN) {
            return sb.substring(0, MAX_IMG_BASE_LEN);
        }
        return sb.toString();
    }

    public static String sanitizeTexDbName(String raw) {
        StringBuilder sb = new StringBuilder();
        if (raw != null) {
            String lower = raw.toLowerCase(Locale.ROOT);
            for (int i = 0; i < lower.length(); i++) {
                char c = lower.charAt(i);
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
                    sb.append(c);
                } else if (c == '-' || c == ' ') {
                    if (sb.length() > 0 && sb.charAt(sb.length() - 1) != '_') {
                        sb.append('_');
                    }
                }
            }
        }
        String res = sb.toString().replaceAll("^_+|_+$", "");
        if (res.length() > 20) res = res.substring(0, 20);
        if (res.isEmpty() || BUILTIN_TEXDBS.contains(res)) {
            res = "ml_" + (res.isEmpty() ? Long.toHexString(System.currentTimeMillis() & 0xFFFFFL) : res);
        }
        return res;
    }

    // ══════════ Smart Pack Importer (ZIP or Multi-File) ══════════

    private static final class RawAsset {
        final String relPath; // forward-slash normalized path inside zip or bare filename
        final String baseName;
        final String lowerExt;
        final byte[] bytes;

        RawAsset(String relPath, byte[] bytes) {
            this.relPath = relPath.replace('\\', '/');
            int slash = this.relPath.lastIndexOf('/');
            this.baseName = (slash >= 0) ? this.relPath.substring(slash + 1) : this.relPath;
            int dot = this.baseName.lastIndexOf('.');
            this.lowerExt = (dot >= 0) ? this.baseName.substring(dot).toLowerCase(Locale.ROOT) : "";
            this.bytes = bytes;
        }
    }

    public static ImportResult importAssetPack(
            Context ctx,
            List<Uri> uris,
            String rawPackName,
            @Nullable String rawTargetModelName,
            @Nullable ProgressCallback progress
    ) throws IOException {
        if (uris == null || uris.isEmpty()) {
            throw new IllegalArgumentException(I18n.tr("未选择任何文件"));
        }
        ModManager.ensureDirectories(ctx);

        notifyProgress(progress, I18n.tr("正在读取所选文件…"));
        List<RawAsset> assets = collectAssetsFromUris(ctx, uris);
        if (assets.isEmpty()) {
            throw new IOException(I18n.tr("未在所选文件中找到可导入的资产（.dff / .ifp / .col / .txd / .png）"));
        }

        String cleanPack = sanitizePackFolder(rawPackName);
        if (cleanPack.isEmpty()) {
            cleanPack = deriveDefaultPackName(assets);
        }
        File modRoot = ModManager.getModLoaderDir(ctx);
        File packDir = new File(modRoot, cleanPack);
        int suffix = 1;
        while (packDir.exists()) {
            packDir = new File(modRoot, cleanPack + "_" + (suffix++));
        }
        if (!packDir.mkdirs()) {
            throw new IOException(I18n.tr("无法创建模组目录: ") + packDir.getName());
        }

        ImportResult res = new ImportResult();
        res.packName = packDir.getName();

        try {
            List<RawAsset> modelAssets = new ArrayList<>();
            List<RawAsset> imageAssets = new ArrayList<>();
            List<RawAsset> txdAssets = new ArrayList<>();
            List<RawAsset> prebuiltTexDbAssets = new ArrayList<>();
            List<RawAsset> extraConfigAssets = new ArrayList<>();

            for (RawAsset a : assets) {
                String lowerPath = a.relPath.toLowerCase(Locale.ROOT);
                // Ignore macOS / junk metadata
                if (lowerPath.contains("__macosx/") || a.baseName.startsWith("._") || a.baseName.startsWith(".")) {
                    continue;
                }
                int texdbIdx = findPathSegment(lowerPath, "texdb/");
                if (texdbIdx >= 0) {
                    // Pre-built Android texdb folder inside ZIP -> preserve as-is
                    String sub = a.relPath.substring(texdbIdx);
                    prebuiltTexDbAssets.add(new RawAsset(sub, a.bytes));
                    continue;
                }
                if (MODEL_EXTS.contains(a.lowerExt)) {
                    modelAssets.add(a);
                } else if (IMAGE_EXTS.contains(a.lowerExt)) {
                    imageAssets.add(a);
                } else if (".txd".equals(a.lowerExt)) {
                    txdAssets.add(a);
                } else if (".dat".equals(a.lowerExt) || ".cfg".equals(a.lowerExt) || ".ide".equals(a.lowerExt)) {
                    extraConfigAssets.add(a);
                }
            }

            // 1. Write model files (.dff / .ifp / .col / .ipl) to pack root
            String targetBase = sanitizeModelBaseName(rawTargetModelName);
            RawAsset primaryDff = null;
            if (!targetBase.isEmpty()) {
                for (RawAsset m : modelAssets) {
                    if (".dff".equals(m.lowerExt)) {
                        if (primaryDff == null || m.bytes.length > primaryDff.bytes.length) {
                            primaryDff = m;
                        }
                    }
                }
            }

            Set<String> writtenModels = new HashSet<>();
            for (RawAsset m : modelAssets) {
                String outName;
                if (m == primaryDff && !targetBase.isEmpty()) {
                    outName = targetBase + ".dff";
                    res.renamedPrimaryModel = outName;
                } else {
                    outName = m.baseName.toLowerCase(Locale.ROOT);
                }
                if (outName.length() > 23) {
                    res.warnings.add(I18n.tr("跳过过长模型名（>23字符）: ") + outName);
                    continue;
                }
                if (!writtenModels.add(outName)) {
                    continue;
                }
                writeBytes(new File(packDir, outName), m.bytes);
                res.modelCount++;
            }

            // 2. Copy pre-built Android texdb/ files if any
            for (RawAsset p : prebuiltTexDbAssets) {
                File out = safeSubFile(packDir, p.relPath);
                if (out != null) {
                    writeBytes(out, p.bytes);
                    res.passthroughFileCount++;
                }
            }

            // 3. Copy optional data/ config files
            for (RawAsset cfg : extraConfigAssets) {
                int dataIdx = findPathSegment(cfg.relPath.toLowerCase(Locale.ROOT), "data/");
                String sub = (dataIdx >= 0) ? cfg.relPath.substring(dataIdx) : ("data/" + cfg.baseName);
                File out = safeSubFile(packDir, sub);
                if (out != null) {
                    writeBytes(out, cfg.bytes);
                    res.passthroughFileCount++;
                }
            }

            // 4. Decode loose images (.png/.jpg/.webp) first (highest quality / Android exports)
            Map<String, DecodedTexture> texMap = new LinkedHashMap<>();
            int imgTotal = imageAssets.size();
            for (int i = 0; i < imgTotal; i++) {
                RawAsset img = imageAssets.get(i);
                notifyProgress(progress, I18n.tr("正在解码图片贴图 (") + (i + 1) + "/" + imgTotal + "): " + img.baseName);
                String texName = stripExtension(img.baseName);
                if (texName.isEmpty()) continue;
                String lowerKey = texName.toLowerCase(Locale.ROOT);
                if (texMap.containsKey(lowerKey)) continue;

                Boolean folderAlphaHint = detectAlphaHintFromPath(img.relPath);
                DecodedTexture dt = decodeImageBytes(texName, img.bytes, folderAlphaHint);
                if (dt != null) {
                    texMap.put(lowerKey, dt);
                } else {
                    res.warnings.add(I18n.tr("无法解码图片: ") + img.baseName);
                }
            }

            // 5. Decode PC RenderWare .txd archives (fill any textures not already provided by PNGs)
            for (int i = 0; i < txdAssets.size(); i++) {
                RawAsset txd = txdAssets.get(i);
                notifyProgress(progress, I18n.tr("正在解析 PC TXD 贴图库 (") + (i + 1) + "/" + txdAssets.size() + "): " + txd.baseName);
                List<DecodedTexture> extracted = decodeTxdBytes(txd.bytes, res.warnings);
                for (DecodedTexture dt : extracted) {
                    String lowerKey = dt.name.toLowerCase(Locale.ROOT);
                    if (!texMap.containsKey(lowerKey)) {
                        texMap.put(lowerKey, dt);
                    }
                }
            }

            // 6. Compile collected textures into WarDrum TextureDatabaseRuntime (.dxt.* with RGBA8888)
            if (!texMap.isEmpty()) {
                String dbName = sanitizeTexDbName(res.packName);
                File texDbDir = new File(packDir, "texdb/" + dbName);
                notifyProgress(progress, I18n.tr("正在编译 Android 贴图库 texdb/") + dbName + " (" + texMap.size() + I18n.tr(" 张)…"));
                compileTextureDatabase(texDbDir, dbName, new ArrayList<>(texMap.values()));
                res.compiledTextureCount = texMap.size();
                res.texDbName = dbName;
            }

            if (res.modelCount == 0 && res.compiledTextureCount == 0 && res.passthroughFileCount == 0) {
                throw new IOException(I18n.tr("未检测到任何有效的模型 (.dff/.ifp/.col) 或贴图 (.png/.txd)"));
            }

            // 7. Register pack in modloader.json at top priority so it overrides base/Remastered packs
            registerPackAtTopPriority(ctx, res.packName);
            return res;
        } catch (IOException | RuntimeException e) {
            ModManager.deleteRecursive(packDir);
            throw e;
        }
    }

    private static void registerPackAtTopPriority(Context ctx, String packName) {
        List<ModManager.ModLoaderEntry> list = ModManager.listModLoaderPacks(ctx);
        int maxPrio = 50;
        ModManager.ModLoaderEntry found = null;
        for (ModManager.ModLoaderEntry e : list) {
            if (e.name.equals(packName)) {
                found = e;
            } else if (e.priority > maxPrio) {
                maxPrio = e.priority;
            }
        }
        if (found != null) {
            found.enabled = true;
            found.priority = maxPrio + 10;
        } else {
            ModManager.ModLoaderEntry e = new ModManager.ModLoaderEntry();
            e.name = packName;
            e.enabled = true;
            e.priority = maxPrio + 10;
            list.add(0, e);
        }
        ModManager.saveModLoaderConfig(ctx, list);
    }

    private static List<RawAsset> collectAssetsFromUris(Context ctx, List<Uri> uris) throws IOException {
        List<RawAsset> out = new ArrayList<>();
        long totalBytes = 0;
        int entryCount = 0;
        for (Uri uri : uris) {
            String displayName = ModManager.queryDisplayName(ctx, uri);
            if (displayName == null || displayName.trim().isEmpty()) {
                displayName = "asset_" + out.size();
            }
            String lowerName = displayName.toLowerCase(Locale.ROOT);
            if (lowerName.endsWith(".zip")) {
                try (InputStream raw = ctx.getContentResolver().openInputStream(uri)) {
                    if (raw == null) throw new IOException(I18n.tr("无法读取压缩包: ") + displayName);
                    try (ZipInputStream zis = new ZipInputStream(raw)) {
                        ZipEntry entry;
                        while ((entry = zis.getNextEntry()) != null) {
                            if (++entryCount > MAX_ZIP_ENTRIES) {
                                throw new IOException(I18n.tr("压缩包内文件数超过限制 (") + MAX_ZIP_ENTRIES + ")");
                            }
                            if (!entry.isDirectory()) {
                                String entryName = entry.getName().replace('\\', '/');
                                if (!entryName.startsWith("/") && !entryName.contains("..")) {
                                    long remaining = MAX_TOTAL_IMPORT_BYTES - totalBytes;
                                    if (remaining <= 0) {
                                        throw new IOException(I18n.tr("导入资源总大小超过 256 MB 限制"));
                                    }
                                    int cap = (int) Math.min(128L * 1024L * 1024L, remaining);
                                    byte[] data = readAllBytesBounded(zis, cap);
                                    totalBytes += data.length;
                                    out.add(new RawAsset(entryName, data));
                                }
                            }
                            zis.closeEntry();
                        }
                    }
                }
            } else {
                if (++entryCount > MAX_ZIP_ENTRIES) {
                    throw new IOException(I18n.tr("导入文件数超过限制 (") + MAX_ZIP_ENTRIES + ")");
                }
                try (InputStream is = ctx.getContentResolver().openInputStream(uri)) {
                    if (is == null) throw new IOException(I18n.tr("无法读取文件: ") + displayName);
                    long remaining = MAX_TOTAL_IMPORT_BYTES - totalBytes;
                    if (remaining <= 0) {
                        throw new IOException(I18n.tr("导入资源总大小超过 256 MB 限制"));
                    }
                    int cap = (int) Math.min(128L * 1024L * 1024L, remaining);
                    byte[] data = readAllBytesBounded(is, cap);
                    totalBytes += data.length;
                    out.add(new RawAsset(displayName, data));
                }
            }
        }
        return out;
    }

    // ══════════ Loose Image Decoder (PNG / JPG / WEBP -> PoT RGBA8888) ══════════

    @Nullable
    private static Boolean detectAlphaHintFromPath(String relPath) {
        String lower = relPath.toLowerCase(Locale.ROOT);
        if (lower.contains("without alpha") || lower.contains("without_alpha")
                || lower.contains("no alpha") || lower.contains("no_alpha") || lower.contains("noalpha")) {
            return Boolean.FALSE;
        }
        if (lower.contains("with alpha") || lower.contains("with_alpha")) {
            return Boolean.TRUE;
        }
        return null;
    }

    @Nullable
    private static DecodedTexture decodeImageBytes(String name, byte[] data, @Nullable Boolean forceAlpha) {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeByteArray(data, 0, data.length, bounds);
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null;

        int sampleSize = 1;
        while ((bounds.outWidth / sampleSize) > (MAX_TEX_DIM * 2)
                || (bounds.outHeight / sampleSize) > (MAX_TEX_DIM * 2)) {
            sampleSize <<= 1;
        }

        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
        opts.inPremultiplied = false; // Keep raw straight-alpha RGB for GTA:SA shaders
        opts.inSampleSize = sampleSize;
        Bitmap bmp = BitmapFactory.decodeByteArray(data, 0, data.length, opts);
        if (bmp == null) return null;
        try {
            return bitmapToDecodedTexture(name, bmp, forceAlpha);
        } finally {
            bmp.recycle();
        }
    }

    private static DecodedTexture bitmapToDecodedTexture(String name, Bitmap src, @Nullable Boolean forceAlpha) {
        int sw = src.getWidth();
        int sh = src.getHeight();
        if (sw <= 0 || sh <= 0) return null;

        int[] srcArgb = new int[sw * sh];
        src.getPixels(srcArgb, 0, sw, 0, 0, sw, sh);

        int tw = nearestPow2Clamped(sw);
        int th = nearestPow2Clamped(sh);
        int[] argb = (sw == tw && sh == th) ? srcArgb : resizeArgbBilinear(srcArgb, sw, sh, tw, th);

        int pixelCount = tw * th;
        byte[] rgba = new byte[pixelCount * 4];
        boolean detectedAlpha = false;
        for (int i = 0; i < pixelCount; i++) {
            int p = argb[i];
            int a = (p >>> 24) & 0xFF;
            if (a < 250) detectedAlpha = true;
            int off = i << 2;
            rgba[off] = (byte) ((p >> 16) & 0xFF);
            rgba[off + 1] = (byte) ((p >> 8) & 0xFF);
            rgba[off + 2] = (byte) (p & 0xFF);
            rgba[off + 3] = (byte) ((forceAlpha != null && !forceAlpha) ? 0xFF : a);
        }
        boolean hasAlpha = (forceAlpha != null) ? forceAlpha : detectedAlpha;
        return new DecodedTexture(name, tw, th, rgba, hasAlpha);
    }

    // Pure pixel-array bilinear resize: safely handles straight-alpha (non-premultiplied) bitmaps
    // without touching Android Canvas (which throws RuntimeException on non-premultiplied bitmaps).
    private static int[] resizeArgbBilinear(int[] src, int sw, int sh, int dw, int dh) {
        int[] dst = new int[dw * dh];
        float scaleX = (float) sw / (float) dw;
        float scaleY = (float) sh / (float) dh;
        for (int y = 0; y < dh; y++) {
            float fy = (y + 0.5f) * scaleY - 0.5f;
            int y0 = Math.max(0, Math.min(sh - 1, (int) Math.floor(fy)));
            int y1 = Math.min(sh - 1, y0 + 1);
            float wy = Math.max(0f, Math.min(1f, fy - y0));
            float iwy = 1f - wy;
            int row0 = y0 * sw;
            int row1 = y1 * sw;
            int dstRow = y * dw;
            for (int x = 0; x < dw; x++) {
                float fx = (x + 0.5f) * scaleX - 0.5f;
                int x0 = Math.max(0, Math.min(sw - 1, (int) Math.floor(fx)));
                int x1 = Math.min(sw - 1, x0 + 1);
                float wx = Math.max(0f, Math.min(1f, fx - x0));
                float iwx = 1f - wx;

                int p00 = src[row0 + x0];
                int p10 = src[row0 + x1];
                int p01 = src[row1 + x0];
                int p11 = src[row1 + x1];

                float w00 = iwx * iwy;
                float w10 = wx * iwy;
                float w01 = iwx * wy;
                float w11 = wx * wy;

                int a = Math.round(((p00 >>> 24) & 0xFF) * w00 + ((p10 >>> 24) & 0xFF) * w10
                        + ((p01 >>> 24) & 0xFF) * w01 + ((p11 >>> 24) & 0xFF) * w11);
                int r = Math.round(((p00 >>> 16) & 0xFF) * w00 + ((p10 >>> 16) & 0xFF) * w10
                        + ((p01 >>> 16) & 0xFF) * w01 + ((p11 >>> 16) & 0xFF) * w11);
                int g = Math.round(((p00 >>> 8) & 0xFF) * w00 + ((p10 >>> 8) & 0xFF) * w10
                        + ((p01 >>> 8) & 0xFF) * w01 + ((p11 >>> 8) & 0xFF) * w11);
                int b = Math.round((p00 & 0xFF) * w00 + (p10 & 0xFF) * w10
                        + (p01 & 0xFF) * w01 + (p11 & 0xFF) * w11);

                dst[dstRow + x] = ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            }
        }
        return dst;
    }

    static DecodedTexture normalizeRgbaToPow2(String name, int w, int h, byte[] rgba, boolean hasAlpha) {
        int tw = nearestPow2Clamped(w);
        int th = nearestPow2Clamped(h);
        if (w == tw && h == th) {
            return new DecodedTexture(name, w, h, rgba, hasAlpha);
        }
        int[] srcArgb = new int[w * h];
        for (int i = 0; i < w * h; i++) {
            int o = i << 2;
            int r = rgba[o] & 0xFF;
            int g = rgba[o + 1] & 0xFF;
            int b = rgba[o + 2] & 0xFF;
            int a = rgba[o + 3] & 0xFF;
            srcArgb[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        int[] resized = resizeArgbBilinear(srcArgb, w, h, tw, th);
        byte[] outRgba = new byte[tw * th * 4];
        for (int i = 0; i < resized.length; i++) {
            int p = resized[i];
            int o = i << 2;
            outRgba[o] = (byte) ((p >>> 16) & 0xFF);
            outRgba[o + 1] = (byte) ((p >>> 8) & 0xFF);
            outRgba[o + 2] = (byte) (p & 0xFF);
            outRgba[o + 3] = (byte) (hasAlpha ? ((p >>> 24) & 0xFF) : 0xFF);
        }
        return new DecodedTexture(name, tw, th, outRgba, hasAlpha);
    }

    private static int nearestPow2Clamped(int v) {
        if (v <= MIN_TEX_DIM) return MIN_TEX_DIM;
        if (v >= MAX_TEX_DIM) return MAX_TEX_DIM;
        int p = MIN_TEX_DIM;
        while (p < v && p < MAX_TEX_DIM) {
            p <<= 1;
        }
        if (p == v) return p;
        int prev = p >> 1;
        return (v - prev < p - v) ? prev : p;
    }

    public static List<DecodedTexture> decodeTxdBytes(byte[] data, @Nullable List<String> warnings) {
        return TxdDecoder.decodeTxdBytes(data, warnings);
    }

    public static void compileTextureDatabase(File outDir, String dbName, List<DecodedTexture> textures) throws IOException {
        TexDbCompiler.compileTextureDatabase(outDir, dbName, textures);
    }

    // ══════════ Helpers ══════════

    private static void notifyProgress(@Nullable ProgressCallback cb, String msg) {
        if (cb != null) cb.onProgress(msg);
    }

    private static String sanitizePackFolder(@Nullable String raw) {
        if (raw == null) return "";
        String cleaned = raw.trim().replaceAll("[\\\\/:*?\"<>|]", "_");
        while (cleaned.startsWith(".") || cleaned.startsWith(" ")) {
            cleaned = cleaned.substring(1);
        }
        cleaned = cleaned.trim();
        if (".".equals(cleaned) || "..".equals(cleaned)) return "";
        return cleaned;
    }

    private static String deriveDefaultPackName(List<RawAsset> assets) {
        for (RawAsset a : assets) {
            if (".dff".equals(a.lowerExt) || ".txd".equals(a.lowerExt)) {
                String base = sanitizePackFolder(stripExtension(a.baseName));
                if (!base.isEmpty()) return base;
            }
        }
        return "CustomMod_" + (System.currentTimeMillis() % 100000);
    }

    private static String stripExtension(String name) {
        int dot = name.lastIndexOf('.');
        return (dot > 0) ? name.substring(0, dot) : name;
    }

    private static int findPathSegment(String lowerPath, String segmentWithSlash) {
        if (lowerPath.startsWith(segmentWithSlash)) return 0;
        int idx = lowerPath.indexOf("/" + segmentWithSlash);
        return (idx >= 0) ? (idx + 1) : -1;
    }

    @Nullable
    private static File safeSubFile(File root, String relPath) throws IOException {
        File out = new File(root, relPath);
        String canonicalRoot = root.getCanonicalPath() + File.separator;
        if (!out.getCanonicalPath().startsWith(canonicalRoot)) return null;
        return out;
    }

    private static void writeBytes(File dest, byte[] bytes) throws IOException {
        File parent = dest.getParentFile();
        if (parent != null && !parent.exists()) parent.mkdirs();
        try (FileOutputStream fos = new FileOutputStream(dest)) {
            fos.write(bytes);
        }
    }

    private static byte[] readAllBytesBounded(InputStream is, int maxBytes) throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        byte[] buf = new byte[16384];
        int total = 0;
        int n;
        while ((n = is.read(buf)) > 0) {
            total += n;
            if (total > maxBytes) {
                throw new IOException(I18n.tr("单个文件超过 128 MB 限制"));
            }
            bos.write(buf, 0, n);
        }
        return bos.toByteArray();
    }

}
