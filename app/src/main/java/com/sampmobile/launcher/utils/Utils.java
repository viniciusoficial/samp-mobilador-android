package com.sampmobile.launcher.utils;

import android.app.Activity;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;

import com.joom.paranoid.Obfuscate;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.text.CharacterIterator;
import java.text.StringCharacterIterator;

import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;

@Obfuscate
public class Utils {
    public static String copyright = "SA-MP Mobile";

    public enum GPUType {
        NONE,
        DXT,
        PVR,
        ETC
    }

    public static GPUType GPU_TYPE = GPUType.NONE;

    public static boolean isOnline(Activity activity) {
        System.out.println("isOnline");
        ConnectivityManager connectivityManager = (ConnectivityManager) activity.getSystemService(Context.CONNECTIVITY_SERVICE);
        NetworkInfo activeNetworkInfo = connectivityManager != null ? connectivityManager.getActiveNetworkInfo() : null;
        return activeNetworkInfo != null && activeNetworkInfo.isConnectedOrConnecting();
    }

    public static boolean addServerToFavorites(Context ctx, String address) {
        try {
            if (ctx == null || address == null) return false;
            String[] parts = address.split(":");
            if (parts.length < 2) return false;

            File extDir = ctx.getExternalFilesDir(null);
            if (extDir == null) return false;
            File serversFile = new File(extDir, "servers.json");

            if (!serversFile.exists() || serversFile.length() == 0) {
                // initialize servers.json with an empty array
                JSONObject initialJson = new JSONObject();
                initialJson.put("servers", new JSONArray());
                writeJSONToFile(serversFile, initialJson);
            }

            String jsonString = readJSONFromFile(serversFile);
            JSONObject json = new JSONObject(jsonString);
            JSONArray serversArray = json.getJSONArray("servers");

            // check if the server already exists in favorites
            for (int i = 0; i < serversArray.length(); i++) {
                JSONObject serverJson = serversArray.getJSONObject(i);
                String host = serverJson.getString("host");
                String port = serverJson.getString("port");

                if (address.equals(host + ":" + port)) {
                    return false; // Server already exists in favorites
                }
            }

            // add the new server to the JSON array
            String host = parts[0];
            String port = parts[1];

            JSONObject newServerJson = new JSONObject();
            newServerJson.put("host", host);
            newServerJson.put("port", port);
            serversArray.put(newServerJson);

            // update the JSON object and write it back to the file
            writeJSONToFile(serversFile, json);

            return true;
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        }
    }

    public static boolean isServerInFavorites(Context ctx, String address) {
        try {
            if (ctx == null || address == null) return false;
            File extDir = ctx.getExternalFilesDir(null);
            if (extDir == null) return false;
            File serversFile = new File(extDir, "servers.json");
            if (!serversFile.exists() || serversFile.length() == 0) return false;

            JSONObject json = new JSONObject(readJSONFromFile(serversFile));
            JSONArray serversArray = json.getJSONArray("servers");
            for (int i = 0; i < serversArray.length(); i++) {
                JSONObject serverJson = serversArray.getJSONObject(i);
                if (address.equals(serverJson.getString("host") + ":" + serverJson.getString("port"))) {
                    return true;
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return false;
    }

    public static void deleteServerFromFavorites(Context ctx, String address) throws Exception {
        if (ctx == null || address == null) return;
        String[] parts = address.split(":");
        if (parts.length < 2) return;
        File extDir = ctx.getExternalFilesDir(null);
        if (extDir == null) return;
        File serversFile = new File(extDir, "servers.json");
        if (!serversFile.exists() || serversFile.length() == 0) return;

        String jsonString = readJSONFromFile(serversFile);
        JSONObject json = new JSONObject(jsonString);
        JSONArray serversArray = json.getJSONArray("servers");

        JSONArray newServersArray = new JSONArray();
        String host = parts[0];
        String port = parts[1];

        for (int i = 0; i < serversArray.length(); i++) {
            JSONObject serverJson = serversArray.getJSONObject(i);
            String serverHost = serverJson.getString("host");
            String serverPort = serverJson.getString("port");

            if (!host.equals(serverHost) || !port.equals(serverPort)) {
                newServersArray.put(serverJson);
            }
        }

        json.put("servers", newServersArray);
        writeJSONToFile(serversFile, json);
    }

    public static void addServerToRecent(Context ctx, String address) {
        try {
            if (ctx == null || address == null) return;
            String[] parts = address.split(":");
            if (parts.length < 2) return;
            File extDir = ctx.getExternalFilesDir(null);
            if (extDir == null) return;
            File recentFile = new File(extDir, "recent.json");

            JSONObject json;
            if (recentFile.exists() && recentFile.length() > 0) {
                json = new JSONObject(readJSONFromFile(recentFile));
            } else {
                json = new JSONObject();
                json.put("servers", new JSONArray());
            }

            JSONArray serversArray = json.getJSONArray("servers");
            String host = parts[0];
            String port = parts[1];

            JSONArray newServersArray = new JSONArray();
            JSONObject entry = new JSONObject();
            entry.put("host", host);
            entry.put("port", port);
            newServersArray.put(entry);

            for (int i = 0; i < serversArray.length() && newServersArray.length() < 30; i++) {
                JSONObject serverJson = serversArray.getJSONObject(i);
                String serverHost = serverJson.getString("host");
                String serverPort = serverJson.getString("port");
                if (!host.equals(serverHost) || !port.equals(serverPort)) {
                    newServersArray.put(serverJson);
                }
            }

            json.put("servers", newServersArray);
            writeJSONToFile(recentFile, json);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public static void deleteServerFromRecent(Context ctx, String address) throws Exception {
        if (ctx == null || address == null) return;
        String[] parts = address.split(":");
        if (parts.length < 2) return;
        File extDir = ctx.getExternalFilesDir(null);
        if (extDir == null) return;
        File recentFile = new File(extDir, "recent.json");
        if (!recentFile.exists() || recentFile.length() == 0) return;

        JSONObject json = new JSONObject(readJSONFromFile(recentFile));
        JSONArray serversArray = json.getJSONArray("servers");

        JSONArray newServersArray = new JSONArray();
        String host = parts[0];
        String port = parts[1];

        for (int i = 0; i < serversArray.length(); i++) {
            JSONObject serverJson = serversArray.getJSONObject(i);
            if (!host.equals(serverJson.getString("host")) || !port.equals(serverJson.getString("port"))) {
                newServersArray.put(serverJson);
            }
        }

        json.put("servers", newServersArray);
        writeJSONToFile(recentFile, json);
    }

    public static String readJSONFromFile(File file) throws IOException {
        StringBuilder sb = new StringBuilder();
        try (BufferedReader br = new BufferedReader(new FileReader(file))) {
            String line;
            while ((line = br.readLine()) != null) {
                sb.append(line);
            }
        }
        return sb.toString();
    }

    public static void writeJSONToFile(File file, JSONObject json) throws IOException, JSONException {
        File parent = file.getParentFile();
        if (parent != null && !parent.exists()) {
            parent.mkdirs();
        }
        // Write to a temp file then atomically rename so a crash mid-write never
        // leaves a truncated/empty JSON file behind.
        File tmp = new File(file.getParentFile(), file.getName() + ".tmp");
        try (BufferedWriter bw = new BufferedWriter(new FileWriter(tmp))) {
            bw.write(json.toString(4));
            bw.write(System.lineSeparator());
        }
        if (!tmp.renameTo(file)) {
            // renameTo should not fail on Linux filesystems; fall back to direct write
            try (BufferedWriter bw = new BufferedWriter(new FileWriter(file))) {
                bw.write(json.toString(4));
                bw.write(System.lineSeparator());
            }
            tmp.delete();
        }
    }

    private static final OkHttpClient HTTP_CLIENT = new OkHttpClient();

    public static String getStringOutputByURL(final String url) throws IOException {
        Request request = new Request.Builder()
                .url(url)
                .build();

        try (Response response = HTTP_CLIENT.newCall(request).execute()) {
            if (!response.isSuccessful()) {
                throw new IOException("Unexpected response code: " + response.code());
            }

            assert response.body() != null;
            return response.body().string();
        }
    }

    public static String convertBytesToString(long bytes) {
        if (-1000 < bytes && bytes < 1000) {
            return bytes + " B";
        }
        CharacterIterator ci = new StringCharacterIterator("kMGTPE");
        while (bytes <= -999_950 || bytes >= 999_950) {
            bytes /= 1000;
            ci.next();
        }
        return String.format("%.1f %cB", bytes / 1000.0, ci.current());
    }
}
