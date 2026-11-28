package com.sampmobile.launcher.utils;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

public class IniDocument {
    public final LinkedHashMap<String, LinkedHashMap<String, String>> sections = new LinkedHashMap<>();

    public static IniDocument load(File file) {
        IniDocument doc = new IniDocument();
        if (file == null || !file.isFile()) return doc;
        String curSection = "Preferences";
        try (BufferedReader br = new BufferedReader(new FileReader(file))) {
            String line;
            while ((line = br.readLine()) != null) {
                String trimmed = line.trim();
                if (trimmed.isEmpty() || trimmed.startsWith(";") || trimmed.startsWith("#")) {
                    continue;
                }
                if (trimmed.startsWith("[") && trimmed.endsWith("]") && trimmed.length() > 2) {
                    curSection = trimmed.substring(1, trimmed.length() - 1).trim();
                    if (!doc.sections.containsKey(curSection)) {
                        doc.sections.put(curSection, new LinkedHashMap<>());
                    }
                    continue;
                }
                int eq = trimmed.indexOf('=');
                if (eq > 0) {
                    String key = trimmed.substring(0, eq).trim();
                    String val = trimmed.substring(eq + 1).trim();
                    doc.set(curSection, key, val);
                }
            }
        } catch (IOException ignored) {
        }
        return doc;
    }

    public String get(String section, String key, String def) {
        LinkedHashMap<String, String> sec = sections.get(section);
        if (sec == null || !sec.containsKey(key)) return def;
        String v = sec.get(key);
        return v != null ? v : def;
    }

    public boolean has(String section, String key) {
        LinkedHashMap<String, String> sec = sections.get(section);
        return sec != null && sec.containsKey(key);
    }

    public void set(String section, String key, String value) {
        LinkedHashMap<String, String> sec = sections.get(section);
        if (sec == null) {
            sec = new LinkedHashMap<>();
            sections.put(section, sec);
        }
        sec.put(key, value != null ? value : "");
    }

    public void save(File file) throws IOException {
        File parent = file.getParentFile();
        if (parent != null && !parent.exists()) parent.mkdirs();
        File tmp = new File(parent, file.getName() + ".tmp");
        try (BufferedWriter bw = new BufferedWriter(new FileWriter(tmp))) {
            boolean firstSec = true;
            for (Map.Entry<String, LinkedHashMap<String, String>> se : sections.entrySet()) {
                if (!firstSec) bw.write("\n");
                firstSec = false;
                bw.write("[" + se.getKey() + "]\n");
                for (Map.Entry<String, String> kv : se.getValue().entrySet()) {
                    bw.write(kv.getKey() + "=" + kv.getValue() + "\n");
                }
            }
        }
        if (!tmp.renameTo(file)) {
            try (BufferedWriter bw = new BufferedWriter(new FileWriter(file))) {
                boolean firstSec = true;
                for (Map.Entry<String, LinkedHashMap<String, String>> se : sections.entrySet()) {
                    if (!firstSec) bw.write("\n");
                    firstSec = false;
                    bw.write("[" + se.getKey() + "]\n");
                    for (Map.Entry<String, String> kv : se.getValue().entrySet()) {
                        bw.write(kv.getKey() + "=" + kv.getValue() + "\n");
                    }
                }
            }
            tmp.delete();
        }
    }
}
