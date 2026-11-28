package com.sampmobile.launcher.utils;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

import static org.junit.Assert.*;

public class ArtworkCacheManagerTest {

    @Rule
    public TemporaryFolder tmp = new TemporaryFolder();

    @Test
    public void normalizeCacheFileName_canonicalizesValidAndRejectsInvalid() {
        assertEquals("0x12AB.dff", ArtworkCacheManager.normalizeCacheFileName("0x0012ab.DFF"));
        assertEquals("0x99BAE090.txd", ArtworkCacheManager.normalizeCacheFileName("0X99bae090.txd"));
        assertEquals("0x1.dff", ArtworkCacheManager.normalizeCacheFileName("0x1.dff"));

        assertNull(ArtworkCacheManager.normalizeCacheFileName(null));
        assertNull(ArtworkCacheManager.normalizeCacheFileName(""));
        assertNull(ArtworkCacheManager.normalizeCacheFileName("0x0.dff"));
        assertNull(ArtworkCacheManager.normalizeCacheFileName("0x123456789.dff")); // > 8 hex digits
        assertNull(ArtworkCacheManager.normalizeCacheFileName("0x1234.col"));
        assertNull(ArtworkCacheManager.normalizeCacheFileName("model.dff"));
        assertNull(ArtworkCacheManager.normalizeCacheFileName("0xGGGG.txd"));
    }

    @Test
    public void resolveZipEntryTarget_preservesServerAndLocalAndBlocksZipSlip() {
        assertEquals("116.62.52.151.7777/0x99BAE090.txd",
                ArtworkCacheManager.resolveZipEntryTarget("cache/116.62.52.151.7777/0x99bae090.TXD"));
        assertEquals("local/0xDEADBEEF.dff",
                ArtworkCacheManager.resolveZipEntryTarget("SAMP/cache/local/0xdeadbeef.dff"));
        assertEquals("local/0xCAFEBABE.dff",
                ArtworkCacheManager.resolveZipEntryTarget("0xcafebabe.dff"));
        assertEquals("local/0x1234.txd",
                ArtworkCacheManager.resolveZipEntryTarget("models/0x00001234.txd"));

        assertNull(ArtworkCacheManager.resolveZipEntryTarget("../116.62.52.151.7777/0x1234.dff"));
        assertNull(ArtworkCacheManager.resolveZipEntryTarget("cache/../../0x1234.dff"));
        assertNull(ArtworkCacheManager.resolveZipEntryTarget("cache/116.62.52.151.7777/readme.txt"));
        assertNull(ArtworkCacheManager.resolveZipEntryTarget("cache/116.62.52.151.7777/"));
    }

    @Test
    public void scanDeleteFolderAndClearAll_workSafely() throws IOException {
        File cacheRoot = tmp.newFolder("cache");
        File localDir = new File(cacheRoot, "local");
        File srvDir = new File(cacheRoot, "127.0.0.1.7777");
        File emptyDir = new File(cacheRoot, "empty.7777");
        assertTrue(localDir.mkdirs());
        assertTrue(srvDir.mkdirs());
        assertTrue(emptyDir.mkdirs());

        writeBytes(new File(localDir, "0x1111.dff"), 100);
        writeBytes(new File(srvDir, "0x2222.dff"), 500);
        writeBytes(new File(srvDir, "0x3333.txd"), 400);

        ArtworkCacheManager.CacheSummary sum = ArtworkCacheManager.scan(cacheRoot);
        assertEquals(3, sum.totalFiles);
        assertEquals(1000L, sum.totalBytes);
        assertEquals(2, sum.folders.size());
        // local/ is always ordered first
        assertEquals("local", sum.folders.get(0).name);
        assertTrue(sum.folders.get(0).isLocal);
        assertEquals("127.0.0.1.7777", sum.folders.get(1).name);
        assertEquals(2, sum.folders.get(1).fileCount);
        assertEquals(900L, sum.folders.get(1).totalBytes);

        // Path traversal rejected
        assertFalse(ArtworkCacheManager.deleteFolder(cacheRoot, "../cache"));
        assertFalse(ArtworkCacheManager.deleteFolder(cacheRoot, "a/b"));

        // Delete single server folder
        assertTrue(ArtworkCacheManager.deleteFolder(cacheRoot, "127.0.0.1.7777"));
        assertFalse(srvDir.exists());
        assertTrue(localDir.exists());

        ArtworkCacheManager.CacheSummary afterDel = ArtworkCacheManager.scan(cacheRoot);
        assertEquals(1, afterDel.totalFiles);
        assertEquals(100L, afterDel.totalBytes);

        // Clear all
        ArtworkCacheManager.clearAll(cacheRoot);
        ArtworkCacheManager.CacheSummary afterClear = ArtworkCacheManager.scan(cacheRoot);
        assertEquals(0, afterClear.totalFiles);
        assertEquals(0L, afterClear.totalBytes);
    }

    private static void writeBytes(File f, int len) throws IOException {
        try (FileOutputStream fos = new FileOutputStream(f)) {
            fos.write(new byte[len]);
        }
    }
}
