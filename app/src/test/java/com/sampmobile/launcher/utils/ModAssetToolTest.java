package com.sampmobile.launcher.utils;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileWriter;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class ModAssetToolTest {

    @Rule
    public TemporaryFolder tmp = new TemporaryFolder();

    @Test
    public void sanitizeModelBaseName_normalizesAndClampsLength() {
        assertEquals("", ModAssetTool.sanitizeModelBaseName(null));
        assertEquals("truth", ModAssetTool.sanitizeModelBaseName("  TRUTH.dff  "));
        assertEquals("infernus_01", ModAssetTool.sanitizeModelBaseName("Infernus_01!.dff"));
        // Clamped to 19 chars so + ".dff" fits VER2 IMG 24-byte name field
        String clamped = ModAssetTool.sanitizeModelBaseName("abcdefghijklmnopqrstuvwxyz.dff");
        assertEquals(19, clamped.length());
        assertEquals("abcdefghijklmnopqrs", clamped);
    }

    @Test
    public void sanitizeTexDbName_avoidsBuiltinCollisions() {
        assertEquals("my_custom_skin", ModAssetTool.sanitizeTexDbName("  My Custom-Skin!! "));
        assertEquals("ml_gta3", ModAssetTool.sanitizeTexDbName("gta3"));
        assertEquals("ml_samp", ModAssetTool.sanitizeTexDbName("SAMP"));
        assertEquals("ml_mobile", ModAssetTool.sanitizeTexDbName("mobile"));
        assertTrue(ModAssetTool.sanitizeTexDbName("!!!").startsWith("ml_"));
        assertTrue(ModAssetTool.sanitizeTexDbName("very_long_database_name_exceeding_limit").length() <= 20);
    }

    @Test
    public void parseIdeSection_parsesPedsAndCarsSortedById() throws Exception {
        File ide = tmp.newFile("test.ide");
        try (FileWriter fw = new FileWriter(ide)) {
            fw.write("# comment line\n");
            fw.write("peds\n");
            fw.write("7, male01, male01, CIVMALE, STAT_STD_GUY\n");
            fw.write("1, truth, truth, CIVMALE, STAT_STD_GUY\n");
            fw.write("bad_line_without_commas\n");
            fw.write("end\n");
            fw.write("cars\n");
            fw.write("411, infernus, infernus, car, INFERNUS, INFERNU\n");
            fw.write("end\n");
        }

        List<ModAssetTool.IdeEntry> peds = IdeCatalogParser.parseIdeSection(ide, "peds");
        assertEquals(2, peds.size());
        assertEquals(1, peds.get(0).id);
        assertEquals("truth", peds.get(0).modelName);
        assertEquals("CIVMALE", peds.get(0).extra);
        assertEquals(7, peds.get(1).id);
        assertEquals("male01", peds.get(1).modelName);

        List<ModAssetTool.IdeEntry> cars = IdeCatalogParser.parseIdeSection(ide, "cars");
        assertEquals(1, cars.size());
        assertEquals(411, cars.get(0).id);
        assertEquals("infernus", cars.get(0).modelName);
        assertEquals("INFERNU", cars.get(0).extra);
    }

    @Test
    public void decodeTxdBytes_rejectsCorruptAndTruncatedInputsSafely() {
        List<String> warnings = new ArrayList<>();
        assertTrue(ModAssetTool.decodeTxdBytes(null, warnings).isEmpty());
        assertTrue(ModAssetTool.decodeTxdBytes(new byte[10], warnings).isEmpty());

        byte[] badHeader = new byte[64];
        assertTrue(ModAssetTool.decodeTxdBytes(badHeader, warnings).isEmpty());
        assertFalse(warnings.isEmpty());
    }

    @Test
    public void decodeTxdBytes_decodesMinimalPcBgra8888Txd() {
        byte[] txd = buildMinimalPcBgraTxd("mai_body", 16, 16, (byte) 0x10, (byte) 0x20, (byte) 0x30, (byte) 0x80);
        List<String> warnings = new ArrayList<>();
        List<ModAssetTool.DecodedTexture> textures = ModAssetTool.decodeTxdBytes(txd, warnings);

        assertEquals(1, textures.size());
        ModAssetTool.DecodedTexture tex = textures.get(0);
        assertEquals("mai_body", tex.name);
        assertEquals(16, tex.width);
        assertEquals(16, tex.height);
        assertTrue(tex.hasAlpha);
        assertEquals(16 * 16 * 4, tex.rgba.length);
        // BGRA (0x10, 0x20, 0x30, 0x80) -> RGBA (0x30, 0x20, 0x10, 0x80)
        assertEquals((byte) 0x30, tex.rgba[0]);
        assertEquals((byte) 0x20, tex.rgba[1]);
        assertEquals((byte) 0x10, tex.rgba[2]);
        assertEquals((byte) 0x80, tex.rgba[3]);
    }

    @Test
    public void compileTextureDatabase_writesValidWarDrumFiles() throws Exception {
        byte[] rgba = new byte[16 * 16 * 4];
        Arrays.fill(rgba, (byte) 0xFF);
        ModAssetTool.DecodedTexture tex = new ModAssetTool.DecodedTexture("skin_tex", 16, 16, rgba, false);

        File outDir = tmp.newFolder("texdb_out");
        ModAssetTool.compileTextureDatabase(outDir, "customdb", Arrays.asList(tex));

        File txt = new File(outDir, "customdb.txt");
        File dat = new File(outDir, "customdb.dxt.dat");
        File toc = new File(outDir, "customdb.dxt.toc");
        File tmb = new File(outDir, "customdb.dxt.tmb");

        assertTrue(txt.isFile());
        assertTrue(dat.isFile());
        assertTrue(toc.isFile());
        assertTrue(tmb.isFile());

        // Mip chain for 16x16 RGBA: 16^2 + 8^2 + 4^2 + 2^2 + 1^2 = 341 pixels * 4 = 1364 bytes
        int expectedDatSize = 12 + 4 + 1364;
        assertEquals(expectedDatSize, dat.length());
        // 8x8 thumbnail: 12-byte header + 4-byte pad + 256 bytes
        assertEquals(12 + 4 + 256, tmb.length());
        // TOC: uint32[2] = [totalDatSize, offset0=0]
        byte[] tocBytes = Files.readAllBytes(toc.toPath());
        assertEquals(8, tocBytes.length);
        ByteBuffer tocBuf = ByteBuffer.wrap(tocBytes).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(expectedDatSize, tocBuf.getInt());
        assertEquals(0, tocBuf.getInt());

        // Check WarDrum 12-byte header in .dxt.dat
        byte[] datBytes = Files.readAllBytes(dat.toPath());
        ByteBuffer datBuf = ByteBuffer.wrap(datBytes).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(0, datBuf.getShort() & 0xFFFF);          // streamId
        assertEquals(0x1401, datBuf.getShort() & 0xFFFF);     // GL_UNSIGNED_BYTE RGBA8888
        assertEquals(16, datBuf.getShort() & 0xFFFF);         // width
        assertEquals(16 | 0x8000, datBuf.getShort() & 0xFFFF);// height | mipmap flag
        assertEquals(4 + 1364, datBuf.getInt());              // payload length

        // Check .txt manifest content
        String manifest = new String(Files.readAllBytes(txt.toPath()), StandardCharsets.UTF_8);
        assertTrue(manifest.contains("cat=0 name=Default"));
        assertTrue(manifest.contains("\"skin_tex\" width=16 height=16"));
        assertTrue(manifest.contains("alphamode=0"));
    }

    private static byte[] buildMinimalPcBgraTxd(String name, int w, int h, byte b, byte g, byte r, byte a) {
        int pixelBytes = w * h * 4;
        ByteBuffer rasterStruct = ByteBuffer.allocate(88 + 4 + pixelBytes).order(ByteOrder.LITTLE_ENDIAN);
        rasterStruct.putInt(0, 9);                // platformId = D3D9 (GTA:SA PC)
        byte[] nameBytes = name.getBytes(StandardCharsets.ISO_8859_1);
        for (int i = 0; i < Math.min(31, nameBytes.length); i++) {
            rasterStruct.put(8 + i, nameBytes[i]);
        }
        rasterStruct.putInt(72, 0x0500);          // RASTER_8888
        rasterStruct.putInt(76, 21);              // D3DFMT_A8R8G8B8 (non-DXT)
        rasterStruct.putShort(80, (short) w);
        rasterStruct.putShort(82, (short) h);
        rasterStruct.put(84, (byte) 32);          // depth = 32bpp
        rasterStruct.put(87, (byte) 1);           // flags: hasAlpha = true
        rasterStruct.putInt(88, pixelBytes);
        for (int i = 0; i < w * h; i++) {
            int off = 92 + i * 4;
            rasterStruct.put(off, b);
            rasterStruct.put(off + 1, g);
            rasterStruct.put(off + 2, r);
            rasterStruct.put(off + 3, a);
        }

        byte[] rwStructChunk = wrapRwChunk(0x0001, rasterStruct.array());
        byte[] texNativeChunk = wrapRwChunk(0x0015, rwStructChunk);
        return wrapRwChunk(0x0016, texNativeChunk);
    }

    private static byte[] wrapRwChunk(int type, byte[] payload) {
        ByteArrayOutputStream out = new ByteArrayOutputStream(12 + payload.length);
        ByteBuffer hdr = ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN);
        hdr.putInt(type);
        hdr.putInt(payload.length);
        hdr.putInt(0x1803FFFF); // RenderWare 3.6
        out.write(hdr.array(), 0, 12);
        out.write(payload, 0, payload.length);
        return out.toByteArray();
    }
}
