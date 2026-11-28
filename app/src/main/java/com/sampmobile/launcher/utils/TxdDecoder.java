package com.sampmobile.launcher.utils;

import androidx.annotation.Nullable;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import com.sampmobile.launcher.utils.ModAssetTool.DecodedTexture;

final class TxdDecoder {
    private TxdDecoder() {}

    // ══════════ Pure-Java PC RenderWare .txd Decoder ══════════

    private static final int RW_STRUCT = 0x0001;
    private static final int RW_TEXTURENATIVE = 0x0015;
    private static final int RW_TEXDICTIONARY = 0x0016;

    private static final int FOURCC_DXT1 = 0x31545844; // "DXT1"
    private static final int FOURCC_DXT3 = 0x33545844; // "DXT3"
    private static final int FOURCC_DXT5 = 0x35545844; // "DXT5"

    public static List<DecodedTexture> decodeTxdBytes(byte[] data, @Nullable List<String> warnings) {
        List<DecodedTexture> result = new ArrayList<>();
        if (data == null || data.length < 24) return result;
        int rootType = readLE32(data, 0);
        long rootSize = readLE32(data, 4) & 0xFFFFFFFFL;
        if (rootType != RW_TEXDICTIONARY) {
            if (warnings != null) warnings.add(I18n.tr("TXD 文件头无效 (非 RenderWare TEXDICTIONARY)"));
            return result;
        }
        int end = (int) Math.min((long) data.length, 12L + rootSize);
        int off = 12;
        while (off + 12 <= end) {
            int type = readLE32(data, off);
            long size = readLE32(data, off + 4) & 0xFFFFFFFFL;
            long childStart = off + 12L;
            long childEndLong = Math.min((long) end, childStart + size);
            if (childEndLong < childStart || childEndLong > end) break;
            int childEnd = (int) childEndLong;
            if (type == RW_TEXTURENATIVE) {
                DecodedTexture tex = parseTextureNative(data, (int) childStart, childEnd, warnings);
                if (tex != null) {
                    result.add(tex);
                }
            }
            if (childEnd <= off) break;
            off = childEnd;
        }
        return result;
    }

    @Nullable
    private static DecodedTexture parseTextureNative(byte[] data, int start, int end, @Nullable List<String> warnings) {
        int off = start;
        while (off + 12 <= end) {
            int type = readLE32(data, off);
            long size = readLE32(data, off + 4) & 0xFFFFFFFFL;
            long payloadStart = off + 12L;
            long payloadEndLong = Math.min((long) end, payloadStart + size);
            if (payloadEndLong < payloadStart || payloadEndLong > end) break;
            int payloadEnd = (int) payloadEndLong;
            if (type == RW_STRUCT && (payloadEnd - (int) payloadStart) >= 92) {
                return decodePcRasterStruct(data, (int) payloadStart, payloadEnd, warnings);
            }
            if (payloadEnd <= off) break;
            off = payloadEnd;
        }
        return null;
    }

    @Nullable
    private static DecodedTexture decodePcRasterStruct(byte[] data, int s, int end, @Nullable List<String> warnings) {
        int platformId = readLE32(data, s);
        // 8 = D3D8 (GTA3/VC/SA), 9 = D3D9 (GTA:SA PC)
        if (platformId != 8 && platformId != 9) {
            if (warnings != null) warnings.add(I18n.tr("跳过非 PC 平台 TXD 贴图 (platform=") + platformId + ")");
            return null;
        }
        String name = readNullTermAscii(data, s + 8, 32).trim();
        if (name.isEmpty()) return null;

        int rasterFormat = readLE32(data, s + 72);
        int d3dFmtOrAlpha = readLE32(data, s + 76);
        int width = readLE16(data, s + 80);
        int height = readLE16(data, s + 82);
        int depth = data[s + 84] & 0xFF;
        int flagsOrComp = data[s + 87] & 0xFF;

        if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
            if (warnings != null) warnings.add(I18n.tr("跳过异常尺寸贴图: ") + name + " (" + width + "x" + height + ")");
            return null;
        }

        int cur = s + 88;
        int formatMask = rasterFormat & 0x0F00;
        boolean isPal8 = (rasterFormat & 0x2000) != 0;
        boolean isPal4 = (rasterFormat & 0x4000) != 0;

        byte[] palette = null;
        if (isPal8) {
            if (cur + 1024 > end) return null;
            palette = Arrays.copyOfRange(data, cur, cur + 1024);
            cur += 1024;
        } else if (isPal4) {
            if (cur + 64 > end) return null;
            palette = Arrays.copyOfRange(data, cur, cur + 64);
            cur += 64;
        }

        if (cur + 4 > end) return null;
        int mip0Size = readLE32(data, cur);
        cur += 4;
        if (mip0Size <= 0 || cur + mip0Size > end) {
            mip0Size = Math.max(0, end - cur);
        }
        if (mip0Size <= 0) return null;

        boolean headerHasAlpha = (platformId == 9) ? ((flagsOrComp & 0x01) != 0) : (d3dFmtOrAlpha != 0);
        byte[] rgba;

        int dxtMode = 0;
        if (platformId == 9) {
            if (d3dFmtOrAlpha == FOURCC_DXT1) dxtMode = 1;
            else if (d3dFmtOrAlpha == FOURCC_DXT3) dxtMode = 3;
            else if (d3dFmtOrAlpha == FOURCC_DXT5) dxtMode = 5;
        } else {
            if (flagsOrComp == 1) dxtMode = 1;
            else if (flagsOrComp == 3) dxtMode = 3;
            else if (flagsOrComp == 5) dxtMode = 5;
        }

        if (dxtMode == 1) {
            rgba = decodeDxt1(data, cur, mip0Size, width, height, headerHasAlpha);
        } else if (dxtMode == 3) {
            rgba = decodeDxt3(data, cur, mip0Size, width, height);
            headerHasAlpha = true;
        } else if (dxtMode == 5) {
            rgba = decodeDxt5(data, cur, mip0Size, width, height);
            headerHasAlpha = true;
        } else if (palette != null && isPal8) {
            rgba = decodePal8(data, cur, mip0Size, palette, width, height);
        } else if (palette != null && isPal4) {
            rgba = decodePal4(data, cur, mip0Size, palette, width, height);
        } else if (depth == 32 || formatMask == 0x0500 || formatMask == 0x0600) {
            boolean hasAlphaChan = (formatMask == 0x0500) || (platformId == 9 && d3dFmtOrAlpha == 21) || headerHasAlpha;
            rgba = decodeBgra32(data, cur, mip0Size, width, height, hasAlphaChan);
        } else if (depth == 16) {
            rgba = decode16Bit(data, cur, mip0Size, width, height, formatMask);
        } else {
            if (warnings != null) {
                warnings.add(I18n.tr("不支持的 TXD 贴图格式: ") + name + " (depth=" + depth + ", fmt=0x"
                        + Integer.toHexString(rasterFormat) + ", d3d=0x" + Integer.toHexString(d3dFmtOrAlpha) + ")");
            }
            return null;
        }

        boolean pixelAlpha = false;
        for (int i = 3; i < rgba.length; i += 4) {
            if ((rgba[i] & 0xFF) < 250) {
                pixelAlpha = true;
                break;
            }
        }
        return ModAssetTool.normalizeRgbaToPow2(name, width, height, rgba, pixelAlpha || (headerHasAlpha && dxtMode != 1));
    }

    private static byte[] decodeDxt1(byte[] src, int off, int len, int w, int h, boolean allowPunchthroughAlpha) {
        byte[] out = new byte[w * h * 4];
        int bw = Math.max(1, (w + 3) >> 2);
        int bh = Math.max(1, (h + 3) >> 2);
        int[] c = new int[4];
        int p = off;
        int limit = off + len;
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                if (p + 8 > limit) return out;
                int c0 = readLE16(src, p);
                int c1 = readLE16(src, p + 2);
                int idx = readLE32(src, p + 4);
                p += 8;

                c[0] = rgb565ToRgba(c0);
                c[1] = rgb565ToRgba(c1);
                if (c0 > c1) {
                    c[2] = lerpRgba(c[0], c[1], 2, 1, 3);
                    c[3] = lerpRgba(c[0], c[1], 1, 2, 3);
                } else {
                    c[2] = lerpRgba(c[0], c[1], 1, 1, 2);
                    c[3] = allowPunchthroughAlpha ? 0x00000000 : 0x000000FF;
                }

                for (int py = 0; py < 4; py++) {
                    int y = (by << 2) + py;
                    if (y >= h) continue;
                    for (int px = 0; px < 4; px++) {
                        int x = (bx << 2) + px;
                        int ci = (idx >>> (((py << 2) + px) << 1)) & 0x3;
                        if (x >= w) continue;
                        int col = c[ci];
                        int dst = (y * w + x) << 2;
                        out[dst] = (byte) ((col >>> 24) & 0xFF);
                        out[dst + 1] = (byte) ((col >>> 16) & 0xFF);
                        out[dst + 2] = (byte) ((col >>> 8) & 0xFF);
                        out[dst + 3] = (byte) (col & 0xFF);
                    }
                }
            }
        }
        return out;
    }

    private static byte[] decodeDxt3(byte[] src, int off, int len, int w, int h) {
        byte[] out = new byte[w * h * 4];
        int bw = Math.max(1, (w + 3) >> 2);
        int bh = Math.max(1, (h + 3) >> 2);
        int[] c = new int[4];
        int p = off;
        int limit = off + len;
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                if (p + 16 > limit) return out;
                long alphaBits = readLE64(src, p);
                int c0 = readLE16(src, p + 8);
                int c1 = readLE16(src, p + 10);
                int idx = readLE32(src, p + 12);
                p += 16;

                c[0] = rgb565ToRgba(c0);
                c[1] = rgb565ToRgba(c1);
                c[2] = lerpRgba(c[0], c[1], 2, 1, 3);
                c[3] = lerpRgba(c[0], c[1], 1, 2, 3);

                for (int py = 0; py < 4; py++) {
                    int y = (by << 2) + py;
                    if (y >= h) continue;
                    for (int px = 0; px < 4; px++) {
                        int x = (bx << 2) + px;
                        if (x >= w) continue;
                        int bitIdx = (py << 2) + px;
                        int ci = (idx >>> (bitIdx << 1)) & 0x3;
                        int a4 = (int) ((alphaBits >>> (bitIdx << 2)) & 0xF);
                        int a8 = (a4 << 4) | a4;
                        int col = c[ci];
                        int dst = (y * w + x) << 2;
                        out[dst] = (byte) ((col >>> 24) & 0xFF);
                        out[dst + 1] = (byte) ((col >>> 16) & 0xFF);
                        out[dst + 2] = (byte) ((col >>> 8) & 0xFF);
                        out[dst + 3] = (byte) a8;
                    }
                }
            }
        }
        return out;
    }

    private static byte[] decodeDxt5(byte[] src, int off, int len, int w, int h) {
        byte[] out = new byte[w * h * 4];
        int bw = Math.max(1, (w + 3) >> 2);
        int bh = Math.max(1, (h + 3) >> 2);
        int[] c = new int[4];
        int[] aTable = new int[8];
        int p = off;
        int limit = off + len;
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                if (p + 16 > limit) return out;
                int a0 = src[p] & 0xFF;
                int a1 = src[p + 1] & 0xFF;
                long aIdx = (src[p + 2] & 0xFFL)
                        | ((src[p + 3] & 0xFFL) << 8)
                        | ((src[p + 4] & 0xFFL) << 16)
                        | ((src[p + 5] & 0xFFL) << 24)
                        | ((src[p + 6] & 0xFFL) << 32)
                        | ((src[p + 7] & 0xFFL) << 40);
                aTable[0] = a0;
                aTable[1] = a1;
                if (a0 > a1) {
                    aTable[2] = (6 * a0 + 1 * a1) / 7;
                    aTable[3] = (5 * a0 + 2 * a1) / 7;
                    aTable[4] = (4 * a0 + 3 * a1) / 7;
                    aTable[5] = (3 * a0 + 4 * a1) / 7;
                    aTable[6] = (2 * a0 + 5 * a1) / 7;
                    aTable[7] = (1 * a0 + 6 * a1) / 7;
                } else {
                    aTable[2] = (4 * a0 + 1 * a1) / 5;
                    aTable[3] = (3 * a0 + 2 * a1) / 5;
                    aTable[4] = (2 * a0 + 3 * a1) / 5;
                    aTable[5] = (1 * a0 + 4 * a1) / 5;
                    aTable[6] = 0;
                    aTable[7] = 255;
                }

                int c0 = readLE16(src, p + 8);
                int c1 = readLE16(src, p + 10);
                int idx = readLE32(src, p + 12);
                p += 16;

                c[0] = rgb565ToRgba(c0);
                c[1] = rgb565ToRgba(c1);
                c[2] = lerpRgba(c[0], c[1], 2, 1, 3);
                c[3] = lerpRgba(c[0], c[1], 1, 2, 3);

                for (int py = 0; py < 4; py++) {
                    int y = (by << 2) + py;
                    if (y >= h) continue;
                    for (int px = 0; px < 4; px++) {
                        int x = (bx << 2) + px;
                        if (x >= w) continue;
                        int bitIdx = (py << 2) + px;
                        int ci = (idx >>> (bitIdx << 1)) & 0x3;
                        int ai = (int) ((aIdx >>> (bitIdx * 3)) & 0x7);
                        int col = c[ci];
                        int dst = (y * w + x) << 2;
                        out[dst] = (byte) ((col >>> 24) & 0xFF);
                        out[dst + 1] = (byte) ((col >>> 16) & 0xFF);
                        out[dst + 2] = (byte) ((col >>> 8) & 0xFF);
                        out[dst + 3] = (byte) aTable[ai];
                    }
                }
            }
        }
        return out;
    }

    private static byte[] decodeBgra32(byte[] src, int off, int len, int w, int h, boolean hasAlpha) {
        byte[] out = new byte[w * h * 4];
        int pixels = Math.min(w * h, len / 4);
        for (int i = 0; i < pixels; i++) {
            int s = off + (i << 2);
            int d = i << 2;
            out[d] = src[s + 2];     // R
            out[d + 1] = src[s + 1]; // G
            out[d + 2] = src[s];     // B
            out[d + 3] = hasAlpha ? src[s + 3] : (byte) 0xFF;
        }
        return out;
    }

    private static byte[] decode16Bit(byte[] src, int off, int len, int w, int h, int formatMask) {
        byte[] out = new byte[w * h * 4];
        int pixels = Math.min(w * h, len / 2);
        for (int i = 0; i < pixels; i++) {
            int v = readLE16(src, off + (i << 1));
            int r, g, b, a;
            if (formatMask == 0x0100) { // C1555
                a = ((v >>> 15) & 1) != 0 ? 255 : 0;
                r = (((v >>> 10) & 0x1F) * 255) / 31;
                g = (((v >>> 5) & 0x1F) * 255) / 31;
                b = ((v & 0x1F) * 255) / 31;
            } else if (formatMask == 0x0300) { // C4444
                a = ((v >>> 12) & 0xF) * 17;
                r = ((v >>> 8) & 0xF) * 17;
                g = ((v >>> 4) & 0xF) * 17;
                b = (v & 0xF) * 17;
            } else { // C565
                a = 255;
                r = (((v >>> 11) & 0x1F) * 255) / 31;
                g = (((v >>> 5) & 0x3F) * 255) / 63;
                b = ((v & 0x1F) * 255) / 31;
            }
            int d = i << 2;
            out[d] = (byte) r;
            out[d + 1] = (byte) g;
            out[d + 2] = (byte) b;
            out[d + 3] = (byte) a;
        }
        return out;
    }

    private static byte[] decodePal8(byte[] src, int off, int len, byte[] pal, int w, int h) {
        byte[] out = new byte[w * h * 4];
        int pixels = Math.min(w * h, len);
        for (int i = 0; i < pixels; i++) {
            int idx = (src[off + i] & 0xFF) << 2;
            int d = i << 2;
            out[d] = pal[idx];
            out[d + 1] = pal[idx + 1];
            out[d + 2] = pal[idx + 2];
            out[d + 3] = pal[idx + 3];
        }
        return out;
    }

    private static byte[] decodePal4(byte[] src, int off, int len, byte[] pal, int w, int h) {
        byte[] out = new byte[w * h * 4];
        int pixels = Math.min(w * h, len * 2);
        for (int i = 0; i < pixels; i++) {
            int b = src[off + (i >> 1)] & 0xFF;
            int idx = ((i & 1) == 0 ? (b & 0x0F) : ((b >>> 4) & 0x0F)) << 2;
            int d = i << 2;
            out[d] = pal[idx];
            out[d + 1] = pal[idx + 1];
            out[d + 2] = pal[idx + 2];
            out[d + 3] = pal[idx + 3];
        }
        return out;
    }

    private static int rgb565ToRgba(int v) {
        int r = ((v >>> 11) & 0x1F);
        int g = ((v >>> 5) & 0x3F);
        int b = (v & 0x1F);
        r = (r << 3) | (r >>> 2);
        g = (g << 2) | (g >>> 4);
        b = (b << 3) | (b >>> 2);
        return (r << 24) | (g << 16) | (b << 8) | 0xFF;
    }

    private static int lerpRgba(int c0, int c1, int w0, int w1, int div) {
        int r = (((c0 >>> 24) & 0xFF) * w0 + ((c1 >>> 24) & 0xFF) * w1) / div;
        int g = (((c0 >>> 16) & 0xFF) * w0 + ((c1 >>> 16) & 0xFF) * w1) / div;
        int b = (((c0 >>> 8) & 0xFF) * w0 + ((c1 >>> 8) & 0xFF) * w1) / div;
        return (r << 24) | (g << 16) | (b << 8) | 0xFF;
    }

    private static int readLE16(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8);
    }

    private static int readLE32(byte[] b, int o) {
        return (b[o] & 0xFF)
                | ((b[o + 1] & 0xFF) << 8)
                | ((b[o + 2] & 0xFF) << 16)
                | ((b[o + 3] & 0xFF) << 24);
    }

    private static long readLE64(byte[] b, int o) {
        return (readLE32(b, o) & 0xFFFFFFFFL) | ((readLE32(b, o + 4) & 0xFFFFFFFFL) << 32);
    }

    private static String readNullTermAscii(byte[] b, int o, int maxLen) {
        int len = 0;
        while (len < maxLen && (o + len) < b.length && b[o + len] != 0) {
            len++;
        }
        return new String(b, o, len, StandardCharsets.ISO_8859_1);
    }
}
