package com.sampmobile.launcher.utils;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

import com.sampmobile.launcher.utils.ModAssetTool.IdeEntry;

final class IdeCatalogParser {
    private IdeCatalogParser() {}

    // ══════════ IDE Catalog Parser (SAMP/peds.ide & SAMP/vehicles.ide) ══════════

    public static List<IdeEntry> loadPedSkins(Context ctx) {
        File gameDir = ModManager.getGameFilesDir(ctx);
        if (gameDir == null) return Collections.emptyList();
        File sampIde = new File(gameDir, "SAMP/peds.ide");
        File fallbackIde = new File(gameDir, "data/peds.ide");
        return parseIdeSection(sampIde.isFile() ? sampIde : fallbackIde, "peds");
    }

    public static List<IdeEntry> loadVehicles(Context ctx) {
        File gameDir = ModManager.getGameFilesDir(ctx);
        if (gameDir == null) return Collections.emptyList();
        File sampIde = new File(gameDir, "SAMP/vehicles.ide");
        File fallbackIde = new File(gameDir, "data/vehicles.ide");
        return parseIdeSection(sampIde.isFile() ? sampIde : fallbackIde, "cars");
    }

    static List<IdeEntry> parseIdeSection(File ideFile, String targetSection) {
        List<IdeEntry> out = new ArrayList<>();
        if (ideFile == null || !ideFile.isFile()) return out;
        boolean inSection = false;
        try (BufferedReader br = new BufferedReader(new FileReader(ideFile))) {
            String line;
            while ((line = br.readLine()) != null) {
                String trimmed = line.trim();
                if (trimmed.isEmpty() || trimmed.startsWith("#") || trimmed.startsWith("//")) {
                    continue;
                }
                if (!inSection) {
                    if (trimmed.equalsIgnoreCase(targetSection)) {
                        inSection = true;
                    }
                    continue;
                }
                if (trimmed.equalsIgnoreCase("end")) {
                    break;
                }
                String[] parts = trimmed.split(",");
                if (parts.length < 3) continue;
                try {
                    int id = Integer.parseInt(parts[0].trim());
                    String model = parts[1].trim().toLowerCase(Locale.ROOT);
                    String txd = parts[2].trim().toLowerCase(Locale.ROOT);
                    if (model.isEmpty()) continue;
                    String extra = "";
                    if ("cars".equalsIgnoreCase(targetSection) && parts.length >= 6) {
                        extra = parts[5].trim();
                    } else if ("peds".equalsIgnoreCase(targetSection) && parts.length >= 4) {
                        extra = parts[3].trim();
                    }
                    out.add(new IdeEntry(id, model, txd, extra));
                } catch (NumberFormatException ignored) {
                }
            }
        } catch (IOException ignored) {
        }
        Collections.sort(out, Comparator.comparingInt(a -> a.id));
        return out;
    }
}
