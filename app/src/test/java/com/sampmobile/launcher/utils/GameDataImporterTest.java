package com.sampmobile.launcher.utils;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import static org.junit.Assert.*;

public class GameDataImporterTest {

    @Rule
    public TemporaryFolder tmp = new TemporaryFolder();

    @Test
    public void normalizeZipEntryPath_handlesFlatWrappedAndUnsafePaths() {
        assertEquals("SAMP/main.scm", GameDataImporter.normalizeZipEntryPath("SAMP/main.scm"));
        assertEquals("texdb/samp.img", GameDataImporter.normalizeZipEntryPath("files/texdb/samp.img"));
        assertEquals("models/coll/peds.col", GameDataImporter.normalizeZipEntryPath("gamedata/files/models/coll/peds.col"));
        assertEquals("SAMP/fonts/arial_bold.ttf", GameDataImporter.normalizeZipEntryPath("dist\\files\\SAMP\\fonts\\arial_bold.ttf"));
        assertEquals("stream.ini", GameDataImporter.normalizeZipEntryPath("files/stream.ini"));

        // Safety & noise filters
        assertNull(GameDataImporter.normalizeZipEntryPath("../evil.txt"));
        assertNull(GameDataImporter.normalizeZipEntryPath("SAMP/../../escape.txt"));
        assertNull(GameDataImporter.normalizeZipEntryPath("__MACOSX/SAMP/._main.scm"));
        assertNull(GameDataImporter.normalizeZipEntryPath("README.txt"));
        assertNull(GameDataImporter.normalizeZipEntryPath("SAMP/"));
    }

    @Test
    public void shouldPreserveExisting_protectsUserConfigsAndLogs() {
        assertTrue(GameDataImporter.shouldPreserveExisting("SAMP/settings.json"));
        assertTrue(GameDataImporter.shouldPreserveExisting("samp/settings.json"));
        assertTrue(GameDataImporter.shouldPreserveExisting("servers.json"));
        assertTrue(GameDataImporter.shouldPreserveExisting("recent.json"));
        assertTrue(GameDataImporter.shouldPreserveExisting("logcat/client_1.log"));

        assertFalse(GameDataImporter.shouldPreserveExisting("SAMP/main.scm"));
        assertFalse(GameDataImporter.shouldPreserveExisting("texdb/samp/samp.txt"));
    }

    @Test
    public void importFromStream_extractsWrappedZipAndPreservesSettings() throws Exception {
        File filesDir = tmp.newFolder("files");
        File sampDir = new File(filesDir, "SAMP");
        assertTrue(sampDir.mkdirs());
        File existingSettings = new File(sampDir, "settings.json");
        try (FileOutputStream fos = new FileOutputStream(existingSettings)) {
            fos.write("{\"nick_name\":\"KeepMe\"}".getBytes(StandardCharsets.UTF_8));
        }

        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        try (ZipOutputStream zos = new ZipOutputStream(baos)) {
            addZipEntry(zos, "gamedata/files/texdb/gta3.img", "GTA3IMG");
            addZipEntry(zos, "gamedata/files/texdb/samp.img", "SAMPIMG");
            addZipEntry(zos, "gamedata/files/texdb/samp/samp.txt", "cat=0\n");
            addZipEntry(zos, "gamedata/files/SAMP/main.scm", "SCM");
            addZipEntry(zos, "gamedata/files/models/coll/peds.col", "COL");
            addZipEntry(zos, "gamedata/files/stream.ini", "memory 64\n");
            addZipEntry(zos, "gamedata/files/SAMP/settings.json", "{\"nick_name\":\"OverwriteAttempt\"}");
            addZipEntry(zos, "unrelated/notes.txt", "ignore");
        }
        byte[] zipBytes = baos.toByteArray();

        GameDataImporter.ImportResult res = GameDataImporter.importFromStream(
                new ByteArrayInputStream(zipBytes),
                zipBytes.length,
                filesDir,
                null,
                new AtomicBoolean(false)
        );

        assertTrue(res.ready);
        assertFalse(res.cancelled);
        assertEquals(6, res.extractedFiles);
        assertTrue(GameDataImporter.isGameDataReady(filesDir));
        assertTrue(new File(filesDir, "stream.ini").isFile());
        assertFalse(new File(filesDir, "unrelated/notes.txt").exists());

        String kept = new String(Files.readAllBytes(existingSettings.toPath()), StandardCharsets.UTF_8);
        assertEquals("{\"nick_name\":\"KeepMe\"}", kept);
    }

    @Test
    public void importFromStream_reportsMissingCoreFilesWhenIncomplete() throws Exception {
        File filesDir = tmp.newFolder("incomplete_files");
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        try (ZipOutputStream zos = new ZipOutputStream(baos)) {
            addZipEntry(zos, "SAMP/main.scm", "SCM");
        }
        byte[] zipBytes = baos.toByteArray();

        GameDataImporter.ImportResult res = GameDataImporter.importFromStream(
                new ByteArrayInputStream(zipBytes),
                zipBytes.length,
                filesDir,
                null,
                new AtomicBoolean(false)
        );

        assertFalse(res.ready);
        assertEquals(1, res.extractedFiles);
        assertTrue(res.missingCoreFiles.contains("texdb/gta3.img"));
    }

    private static void addZipEntry(ZipOutputStream zos, String name, String content) throws Exception {
        zos.putNextEntry(new ZipEntry(name));
        zos.write(content.getBytes(StandardCharsets.UTF_8));
        zos.closeEntry();
    }
}
