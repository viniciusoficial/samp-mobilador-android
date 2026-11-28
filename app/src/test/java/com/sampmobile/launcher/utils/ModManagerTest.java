package com.sampmobile.launcher.utils;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class ModManagerTest {

    @Rule
    public TemporaryFolder tmp = new TemporaryFolder();

    @Test
    public void iniDoc_roundTripPreservesSectionsKeysAndCustomEntries() throws Exception {
        File ini = tmp.newFile("net.rusjj.jpatch.ini");
        try (FileWriter fw = new FileWriter(ini)) {
            fw.write("; comment\n");
            fw.write("# hash comment\n");
            fw.write("[Gameplay]\n");
            fw.write("Fix210Crash=1\n");
            fw.write("MultiTouch4=1\n");
            fw.write("[Custom]\n");
            fw.write("UserFlag=hello\n");
        }

        IniDocument doc = IniDocument.load(ini);
        assertTrue(doc.has("Gameplay", "Fix210Crash"));
        assertEquals("1", doc.get("Gameplay", "Fix210Crash", "0"));
        assertEquals("hello", doc.get("Custom", "UserFlag", ""));
        assertEquals("fallback", doc.get("Gameplay", "MissingKey", "fallback"));

        doc.set("Gameplay", "MultiTouch4", "0");
        doc.set("Visual", "FixFogWall", "1");
        doc.save(ini);

        IniDocument reloaded = IniDocument.load(ini);
        assertEquals("0", reloaded.get("Gameplay", "MultiTouch4", "1"));
        assertEquals("1", reloaded.get("Gameplay", "Fix210Crash", "0"));
        assertEquals("hello", reloaded.get("Custom", "UserFlag", ""));
        assertEquals("1", reloaded.get("Visual", "FixFogWall", "0"));
    }

    @Test
    public void jpatchSafeCatalog_disablesKnownSampConflictingPatches() {
        List<ModManager.JPatchOption> catalog = JPatchConfigManager.buildKnownJPatchCatalog();
        Map<String, ModManager.JPatchOption> byKey = new HashMap<>();
        for (ModManager.JPatchOption opt : catalog) {
            byKey.put(opt.key, opt);
        }

        List<String> mustDisable = Arrays.asList(
                "MultiTouch4",
                "BuffStreamingMem",
                "DynamicStreamingMem",
                "EnableAnimatedTextures",
                "FixCrosshair",
                "ForceLoadDXT",
                "ForceLoadETC",
                "ForceLoadPVR"
        );
        for (String key : mustDisable) {
            ModManager.JPatchOption opt = byKey.get(key);
            assertNotNull("Missing expected JPatch catalog key: " + key, opt);
            assertFalse(key + " must be marked unsafe for SA-MP", opt.safeForSamp);
            assertEquals(key + " must default to 0 in SA-MP safe preset", "0", opt.defaultSafeValue);
        }
    }

    @Test
    public void toggleDisabledFile_togglesSuffixAndIsIdempotent() throws Exception {
        File plugin = tmp.newFile("libTest64.so");
        assertTrue(plugin.isFile());

        // Already enabled -> returns same file
        assertEquals(plugin, ModManager.toggleDisabledFile(plugin, true));

        // Disable -> appends .disabled
        File disabled = ModManager.toggleDisabledFile(plugin, false);
        assertNotNull(disabled);
        assertEquals("libTest64.so.disabled", disabled.getName());
        assertTrue(disabled.isFile());
        assertFalse(plugin.exists());

        // Re-enable -> strips .disabled
        File reEnabled = ModManager.toggleDisabledFile(disabled, true);
        assertNotNull(reEnabled);
        assertEquals("libTest64.so", reEnabled.getName());
        assertTrue(reEnabled.isFile());

        assertNull(ModManager.toggleDisabledFile(null, true));
    }

    @Test
    public void inspectElfAbi_rejectsNonElfAndUnsupportedArch() throws Exception {
        File notElf = tmp.newFile("plain.txt");
        try (FileOutputStream fos = new FileOutputStream(notElf)) {
            fos.write("hello world, not an elf binary at all".getBytes(StandardCharsets.UTF_8));
        }
        ModManager.ElfInfo plainInfo = ModManager.inspectElfAbi(notElf);
        assertFalse(plainInfo.valid);
        assertFalse(plainInfo.matchesProcess);

        // ELF64 + EM_X86_64 (62) -> valid ELF but incompatible with ARM Android builds
        File x86Elf = tmp.newFile("x86_64.so");
        byte[] hdr = new byte[24];
        hdr[0] = 0x7F;
        hdr[1] = 'E';
        hdr[2] = 'L';
        hdr[3] = 'F';
        hdr[4] = 2;  // ELFCLASS64
        hdr[18] = 62; // EM_X86_64
        try (FileOutputStream fos = new FileOutputStream(x86Elf)) {
            fos.write(hdr);
        }
        ModManager.ElfInfo x86Info = ModManager.inspectElfAbi(x86Elf);
        assertTrue(x86Info.valid);
        assertFalse(x86Info.matchesProcess);
    }

    @Test
    public void readLogTail_returnsFullSmallFileAndTruncatesLargeFile() throws Exception {
        File log = tmp.newFile("client.log");
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 40; i++) {
            sb.append("line-").append(i).append(": sample log message\n");
        }
        try (FileWriter fw = new FileWriter(log)) {
            fw.write(sb.toString());
        }

        String full = ModManager.readLogTail(log, 65536);
        assertEquals(sb.toString(), full);

        String tail = ModManager.readLogTail(log, 120);
        assertTrue(tail.startsWith("... (仅显示末尾日志)\n"));
        assertTrue(tail.contains("line-39: sample log message"));
        assertFalse(tail.contains("line-0:"));
    }

    @Test
    public void deleteRecursive_removesNestedDirectories() throws Exception {
        File root = tmp.newFolder("mod_pack");
        File sub = new File(root, "texdb/custom");
        assertTrue(sub.mkdirs());
        File leaf = new File(sub, "custom.txt");
        assertTrue(leaf.createNewFile());

        assertTrue(ModManager.deleteRecursive(root));
        assertFalse(root.exists());
    }
}
