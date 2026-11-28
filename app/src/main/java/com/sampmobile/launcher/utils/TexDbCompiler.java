package com.sampmobile.launcher.utils;

import java.io.BufferedOutputStream;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.CRC32;

import com.sampmobile.launcher.utils.ModAssetTool.DecodedTexture;

final class TexDbCompiler {
    private static final int GL_UNSIGNED_BYTE_RGBA = 0x1401;

    private TexDbCompiler() {}

    // ══════════ WarDrum TextureDatabaseRuntime Compiler (.dxt.* w/ RGBA8888) ══════════

    public static void compileTextureDatabase(File outDir, String dbName, List<DecodedTexture> textures) throws IOException {
        if (!outDir.exists() && !outDir.mkdirs()) {
            throw new IOException(I18n.tr("无法创建贴图库目录: ") + outDir.getAbsolutePath());
        }

        File txtFile = new File(outDir, dbName + ".txt");
        File datFile = new File(outDir, dbName + ".dxt.dat");
        File tocFile = new File(outDir, dbName + ".dxt.toc");
        File tmbFile = new File(outDir, dbName + ".dxt.tmb");

        int count = textures.size();
        int[] offsets = new int[count];
        int datPos = 0;

        StringBuilder txt = new StringBuilder();
        txt.append("cat=0 name=Default onfoot=0 slow=0 fast=0 defaultstream=0\n");

        try (BufferedOutputStream datOut = new BufferedOutputStream(new FileOutputStream(datFile), 65536);
             BufferedOutputStream tmbOut = new BufferedOutputStream(new FileOutputStream(tmbFile), 16384)) {

            byte[] hdr12 = new byte[12];
            byte[] zero4 = new byte[4];

            for (int i = 0; i < count; i++) {
                DecodedTexture tex = textures.get(i);
                offsets[i] = datPos;

                // Generate mipmap chain from w*h down to 1*1 (2x2 box filter)
                List<byte[]> mips = new ArrayList<>();
                byte[] curBytes = tex.rgba;
                int mw = tex.width;
                int mh = tex.height;
                int totalMipBytes = 0;
                byte[] thumb8x8 = null;

                while (true) {
                    mips.add(curBytes);
                    totalMipBytes += curBytes.length;
                    if (mw == 8 && mh == 8) {
                        thumb8x8 = curBytes;
                    }
                    if (mw == 1 && mh == 1) break;
                    int nw = Math.max(1, mw >> 1);
                    int nh = Math.max(1, mh >> 1);
                    curBytes = downsampleBox2x2(curBytes, mw, mh, nw, nh);
                    mw = nw;
                    mh = nh;
                }
                if (thumb8x8 == null) {
                    thumb8x8 = buildThumb8x8(tex.rgba, tex.width, tex.height);
                }

                // 12-byte WarDrum header: <HHHHI (streamId, gl_type=0x1401, w, h|0x8000, 4+mipBytes)
                putLE16(hdr12, 0, i);
                putLE16(hdr12, 2, GL_UNSIGNED_BYTE_RGBA);
                putLE16(hdr12, 4, tex.width);
                putLE16(hdr12, 6, tex.height | 0x8000);
                putLE32(hdr12, 8, 4 + totalMipBytes);

                datOut.write(hdr12);
                datOut.write(zero4);
                for (byte[] mip : mips) {
                    datOut.write(mip);
                }
                datPos += 12 + 4 + totalMipBytes;

                // 8x8 thumbnail entry (no mipmap bit on height)
                putLE16(hdr12, 0, i);
                putLE16(hdr12, 2, GL_UNSIGNED_BYTE_RGBA);
                putLE16(hdr12, 4, 8);
                putLE16(hdr12, 6, 8);
                putLE32(hdr12, 8, 4 + 256);
                tmbOut.write(hdr12);
                tmbOut.write(zero4);
                tmbOut.write(thumb8x8, 0, 256);

                String safeTexName = tex.name.replace('"', '_').replace('\r', '_').replace('\n', '_').trim();
                if (safeTexName.isEmpty()) safeTexName = "tex_" + i;
                CRC32 crc = new CRC32();
                crc.update(safeTexName.getBytes(StandardCharsets.UTF_8));
                String hashHex = String.format(Locale.ROOT, "%08x", crc.getValue() & 0xFFFFFFFFL);
                txt.append('"').append(safeTexName).append('"')
                        .append(" width=").append(tex.width)
                        .append(" height=").append(tex.height)
                        .append(" img=").append(hashHex)
                        .append(" alphamode=").append(tex.hasAlpha ? 2 : 0)
                        .append('\n');
            }
            datOut.flush();
            tmbOut.flush();
        }

        // Write .dxt.toc: uint32[N+1] = [totalDatSize, off_0, ..., off_{N-1}]
        try (BufferedOutputStream tocOut = new BufferedOutputStream(new FileOutputStream(tocFile))) {
            byte[] u32 = new byte[4];
            putLE32(u32, 0, datPos);
            tocOut.write(u32);
            for (int off : offsets) {
                putLE32(u32, 0, off);
                tocOut.write(u32);
            }
            tocOut.flush();
        }

        // Write .txt manifest
        try (BufferedWriter bw = new BufferedWriter(new FileWriter(txtFile))) {
            bw.write(txt.toString());
        }
    }

    private static byte[] downsampleBox2x2(byte[] src, int sw, int sh, int dw, int dh) {
        byte[] dst = new byte[dw * dh * 4];
        for (int y = 0; y < dh; y++) {
            int sy0 = Math.min(sh - 1, y << 1);
            int sy1 = Math.min(sh - 1, sy0 + 1);
            for (int x = 0; x < dw; x++) {
                int sx0 = Math.min(sw - 1, x << 1);
                int sx1 = Math.min(sw - 1, sx0 + 1);
                int i00 = (sy0 * sw + sx0) << 2;
                int i10 = (sy0 * sw + sx1) << 2;
                int i01 = (sy1 * sw + sx0) << 2;
                int i11 = (sy1 * sw + sx1) << 2;
                int d = (y * dw + x) << 2;
                dst[d] = (byte) (((src[i00] & 0xFF) + (src[i10] & 0xFF) + (src[i01] & 0xFF) + (src[i11] & 0xFF) + 2) >> 2);
                dst[d + 1] = (byte) (((src[i00 + 1] & 0xFF) + (src[i10 + 1] & 0xFF) + (src[i01 + 1] & 0xFF) + (src[i11 + 1] & 0xFF) + 2) >> 2);
                dst[d + 2] = (byte) (((src[i00 + 2] & 0xFF) + (src[i10 + 2] & 0xFF) + (src[i01 + 2] & 0xFF) + (src[i11 + 2] & 0xFF) + 2) >> 2);
                dst[d + 3] = (byte) (((src[i00 + 3] & 0xFF) + (src[i10 + 3] & 0xFF) + (src[i01 + 3] & 0xFF) + (src[i11 + 3] & 0xFF) + 2) >> 2);
            }
        }
        return dst;
    }

    private static byte[] buildThumb8x8(byte[] rgba, int w, int h) {
        byte[] dst = new byte[8 * 8 * 4];
        for (int y = 0; y < 8; y++) {
            int sy = Math.min(h - 1, (y * h) / 8);
            for (int x = 0; x < 8; x++) {
                int sx = Math.min(w - 1, (x * w) / 8);
                int s = (sy * w + sx) << 2;
                int d = (y * 8 + x) << 2;
                dst[d] = rgba[s];
                dst[d + 1] = rgba[s + 1];
                dst[d + 2] = rgba[s + 2];
                dst[d + 3] = rgba[s + 3];
            }
        }
        return dst;
    }

    private static void putLE16(byte[] b, int o, int v) {
        b[o] = (byte) (v & 0xFF);
        b[o + 1] = (byte) ((v >>> 8) & 0xFF);
    }

    private static void putLE32(byte[] b, int o, int v) {
        b[o] = (byte) (v & 0xFF);
        b[o + 1] = (byte) ((v >>> 8) & 0xFF);
        b[o + 2] = (byte) ((v >>> 16) & 0xFF);
        b[o + 3] = (byte) ((v >>> 24) & 0xFF);
    }
}
