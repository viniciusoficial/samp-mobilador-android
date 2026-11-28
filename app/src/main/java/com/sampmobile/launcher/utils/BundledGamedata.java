package com.sampmobile.launcher.utils;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.util.Log;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * Detects and reads the game-data bundle embedded inside the APK assets
 * (produced by tools/packaging/build_bundled_apk.py).
 *
 * Layout inside assets/:
 *   gamedata/bundle.gdz   — the game-data .zip (DEFLATE entries, stored
 *                           uncompressed in the APK via aaptOptions noCompress
 *                           so AssetManager streams it without a second inflate)
 *   gamedata/bundle.json  — small metadata: edition / label / zipBytes /
 *                           uncompressedBytes (all optional, see below)
 *
 * Normal builds ship neither file, in which case every method here degrades
 * gracefully and the launcher behaves exactly as before.
 */
public final class BundledGamedata {

    private static final String TAG = "BundledGamedata";

    public static final String BUNDLE_ASSET = "gamedata/bundle.gdz";
    public static final String META_ASSET = "gamedata/bundle.json";

    private static final int MAX_META_BYTES = 64 * 1024;

    public static final class Info {
        public final boolean present;
        public final String edition;
        public final String label;
        /** Compressed .gdz byte length — denominator for extraction progress. */
        public final long zipBytes;
        /** Sum of uncompressed entry sizes — how much free disk extraction needs. */
        public final long uncompressedBytes;

        Info(boolean present, String edition, String label, long zipBytes, long uncompressedBytes) {
            this.present = present;
            this.edition = edition;
            this.label = label;
            this.zipBytes = zipBytes;
            this.uncompressedBytes = uncompressedBytes;
        }

        public boolean isFull() {
            return "full".equalsIgnoreCase(edition);
        }

        /** Human edition label, translated through I18n when the edition is known. */
        public String displayLabel() {
            if (isFull()) {
                return I18n.tr("完整版");
            }
            if ("lite".equalsIgnoreCase(edition)) {
                return I18n.tr("精简版");
            }
            return (label != null && !label.isEmpty()) ? label : I18n.tr("精简版");
        }
    }

    private static final Info ABSENT = new Info(false, "", "", -1L, -1L);

    private BundledGamedata() {}

    /** True when the APK carries an embedded game-data bundle. */
    public static boolean isPresent(Context context) {
        return assetExists(context, BUNDLE_ASSET);
    }

    /**
     * Reads bundle metadata. Never throws; absent/corrupt metadata simply yields
     * default values so callers can still extract the bundle itself.
     */
    public static Info inspect(Context context) {
        if (!assetExists(context, BUNDLE_ASSET)) {
            return ABSENT;
        }
        String edition = "";
        String label = "";
        long zipBytes = -1L;
        long uncompressedBytes = -1L;
        try (InputStream in = context.getAssets().open(META_ASSET)) {
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            int total = 0;
            while ((n = in.read(buf)) > 0) {
                total += n;
                if (total > MAX_META_BYTES) break;
                bos.write(buf, 0, n);
            }
            JSONObject o = new JSONObject(new String(bos.toByteArray(), StandardCharsets.UTF_8));
            edition = o.optString("edition", "");
            label = o.optString("label", "");
            zipBytes = o.optLong("zipBytes", -1L);
            uncompressedBytes = o.optLong("uncompressedBytes", -1L);
        } catch (Exception e) {
            Log.w(TAG, "bundle.json unreadable, falling back to asset length", e);
        }
        if (zipBytes <= 0L) {
            zipBytes = compressedSize(context);
        }
        return new Info(true, edition, label, zipBytes, uncompressedBytes);
    }

    /** Opens the bundle zip stream. Caller must close. */
    public static InputStream openBundle(Context context) throws IOException {
        return context.getAssets().open(BUNDLE_ASSET);
    }

    /**
     * Exact byte length of the (uncompressed-in-APK) bundle asset, or -1 when
     * the asset is compressed / not seekable. Used only as a progress fallback.
     */
    public static long compressedSize(Context context) {
        try {
            AssetFileDescriptor afd = context.getAssets().openFd(BUNDLE_ASSET);
            long len = afd.getLength();
            afd.close();
            return len;
        } catch (Exception e) {
            return -1L;
        }
    }

    private static boolean assetExists(Context context, String name) {
        if (context == null) return false;
        try (InputStream in = context.getAssets().open(name)) {
            return in != null;
        } catch (Exception e) {
            return false;
        }
    }
}
