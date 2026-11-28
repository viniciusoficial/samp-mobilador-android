package com.sampmobile.launcher.ui.fragment;


import com.sampmobile.launcher.utils.I18n;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;
import android.os.StatFs;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.fragment.app.FragmentActivity;

import com.sampmobile.launcher.BuildConfig;
import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.ClientSettings;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * Manages the Diagnostics & Crash Feedback tab inside {@link SettingsPageFragment}.
 */
final class SettingsDiagnosticsController {
    private final SettingsPageFragment host;
    private TextView tvStatusTitle;
    private TextView tvStatusDesc;

    SettingsDiagnosticsController(SettingsPageFragment host) {
        this.host = host;
    }

    void bindViews(View root) {
        tvStatusTitle = root.findViewById(R.id.tv_diag_status_title);
        tvStatusDesc = root.findViewById(R.id.tv_diag_status_desc);
    }

    void refresh() {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.ioExecutor.execute(() -> {
            File baseDir = appCtx.getExternalFilesDir(null);
            if (baseDir == null) return;
            File logcatDir = new File(baseDir, "logcat");
            int logFileCount = 0;
            int crashHits = 0;

            if (logcatDir.exists()) {
                File[] all = logcatDir.listFiles((d, n) -> n.endsWith(".log"));
                if (all != null) {
                    logFileCount = all.length;
                    Arrays.sort(all, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
                    for (int i = 0; i < Math.min(all.length, 3); i++) {
                        crashHits += extractCrashAndErrorLines(all[i]).size();
                    }
                }
                File[] crashFiles = logcatDir.listFiles((d, n) -> n.startsWith("crash_") && n.endsWith(".log"));
                if (crashFiles != null && crashFiles.length > 0) {
                    crashHits += crashFiles.length;
                }
            }

            final int finalLogCount = logFileCount;
            final boolean hasCrash = crashHits > 0;
            FragmentActivity act = host.getActivity();
            if (act != null && host.isAdded()) {
                act.runOnUiThread(() -> {
                    if (!host.isAdded() || host.getView() == null) return;
                    if (hasCrash) {
                        tvStatusTitle.setText(I18n.tr("运行诊断状态：检测到历史异常记录"));
                        tvStatusDesc.setText(String.format(Locale.US,
                                I18n.tr("系统已在后台自动保存 %d 份运行诊断记录。如您刚遇到闪退或卡顿，可直接点击下方「一键复制故障反馈信息」发送给客服或开发者协助排查。"),
                                finalLogCount));
                    } else if (finalLogCount > 0) {
                        tvStatusTitle.setText(I18n.tr("运行诊断状态：正常"));
                        tvStatusDesc.setText(String.format(Locale.US,
                                I18n.tr("客户端运行环境自检通过，当前已归档 %d 份运行记录，近期未发现崩溃异常。"),
                                finalLogCount));
                    } else {
                        tvStatusTitle.setText(I18n.tr("运行诊断状态：就绪"));
                        tvStatusDesc.setText(I18n.tr("当前尚无历史故障记录。进入游戏后系统将在后台自动守护并记录运行状态。"));
                    }
                });
            }
        });
    }

    void copyReport() {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.ioExecutor.execute(() -> {
            String report = buildDiagnosticReportText(appCtx);
            FragmentActivity act = host.getActivity();
            if (act != null && host.isAdded()) {
                act.runOnUiThread(() -> {
                    if (!host.isAdded() || host.getContext() == null) return;
                    host.copyTextToClipboard(I18n.tr("SA-MP Mobile 故障反馈"), report);
                    Toast.makeText(host.getContext(), I18n.tr("故障反馈信息已复制到剪贴板，可直接粘贴发送！"), Toast.LENGTH_LONG).show();
                });
            }
        });
    }

    void shareLogFile() {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.ioExecutor.execute(() -> {
            try {
                File baseDir = appCtx.getExternalFilesDir(null);
                if (baseDir == null) throw new IllegalStateException(I18n.tr("存储目录不可用"));
                File dir = new File(baseDir, "logcat");
                if (!dir.exists()) dir.mkdirs();
                File reportFile = new File(dir, "SAMP_Feedback_Report.log");
                String report = buildDiagnosticReportText(appCtx);
                try (FileOutputStream fos = new FileOutputStream(reportFile)) {
                    fos.write(report.getBytes(StandardCharsets.UTF_8));
                }
                FragmentActivity act = host.getActivity();
                if (act != null && host.isAdded()) {
                    act.runOnUiThread(() -> {
                        if (!host.isAdded() || host.getContext() == null) return;
                        host.shareFile(reportFile, "text/plain", I18n.tr("导出故障反馈日志文件"));
                    });
                }
            } catch (Exception e) {
                FragmentActivity act = host.getActivity();
                if (act != null && host.isAdded()) {
                    act.runOnUiThread(() -> {
                        if (!host.isAdded() || host.getContext() == null) return;
                        Toast.makeText(host.getContext(), I18n.tr("导出反馈文件失败: ") + e.getMessage(), Toast.LENGTH_SHORT).show();
                    });
                }
            }
        });
    }

    void confirmClearLogs() {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.showAlertDialog(I18n.tr("确定要清理所有历史日志缓存文件吗？"), (d, w) -> {
            host.ioExecutor.execute(() -> {
                File baseDir = appCtx.getExternalFilesDir(null);
                File logcatDir = baseDir != null ? new File(baseDir, "logcat") : null;
                if (logcatDir != null && logcatDir.exists()) {
                    File[] files = logcatDir.listFiles();
                    if (files != null) {
                        for (File f : files) f.delete();
                    }
                }
                FragmentActivity act = host.getActivity();
                if (act != null && host.isAdded()) {
                    act.runOnUiThread(() -> {
                        if (!host.isAdded() || host.getContext() == null) return;
                        refresh();
                        Toast.makeText(host.getContext(), I18n.tr("日志缓存已清理"), Toast.LENGTH_SHORT).show();
                    });
                }
            });
            d.dismiss();
        }, (d, w) -> d.dismiss());
    }

    private String buildDiagnosticReportText(Context appCtx) {
        File baseDir = appCtx.getExternalFilesDir(null);
        File logcatDir = baseDir != null ? new File(baseDir, "logcat") : null;
        StringBuilder sb = new StringBuilder();
        sb.append(I18n.tr("========== SA-MP Mobile 故障反馈诊断信息 ==========\n"));
        sb.append(buildDeviceSummaryLine(appCtx)).append("\n\n");

        // Android 11+ (API 30+): query system-recorded process exit reasons (captures
        // native signals, tombstone traces, LMK/SIGKILL, ANR, Java crashes even when
        // in-process handlers never ran).
        appendHistoricalExitReasons(appCtx, sb);

        // Native/Java crash reports are small self-contained files written by our
        // fatal handlers; include the newest ones verbatim.
        List<File> crashFiles = findCrashReports(logcatDir);
        for (File crashFile : crashFiles) {
            sb.append(I18n.tr("--- 原生崩溃报告 (")).append(crashFile.getName()).append(") ---\n");
            sb.append(readFileHead(crashFile, 120));
            sb.append("\n");
        }

        File latest = findLatestClientLog(logcatDir);
        if (latest != null && latest.exists()) {
            List<String> crashes = extractCrashAndErrorLines(latest);
            if (!crashes.isEmpty()) {
                sb.append(I18n.tr("--- 异常摘要 (")).append(latest.getName()).append(") ---\n");
                for (String line : crashes) {
                    sb.append(line).append("\n");
                }
                sb.append("\n");
            }
            sb.append(I18n.tr("--- 最近一次客户端运行记录 (")).append(latest.getName()).append(") ---\n");
            String latestTail = readFileTail(latest, 160);
            boolean latestEmpty = latestTail.trim().isEmpty();
            if (latestEmpty) {
                sb.append(I18n.tr("（该日志文件无内容：客户端进程在写出首条日志前即终止，死亡点位于 native 初始化极早期）\n"));
            } else {
                sb.append(latestTail);
            }

            // LMK / vendor kills leave no crash file — the evidence is the memory
            // ramp in the session that died. When the newest log reports an unclean
            // previous exit, pull the error lines and tail of the previous log too.
            // An empty newest log means the process died before its first flush —
            // the previous session is then the only available evidence, so include
            // it even without the unclean-exit marker line.
            File previous = findPreviousClientLog(logcatDir, latest);
            if (previous != null && (latestEmpty || containsLine(latest, "did not exit cleanly"))) {
                List<String> prevCrashes = extractCrashAndErrorLines(previous);
                if (!prevCrashes.isEmpty()) {
                    sb.append(I18n.tr("\n--- 上次异常终止会话的异常摘要 (")).append(previous.getName()).append(") ---\n");
                    for (String line : prevCrashes) {
                        sb.append(line).append("\n");
                    }
                }
                sb.append(I18n.tr("\n--- 上次异常终止会话的运行记录末尾 (")).append(previous.getName()).append(") ---\n");
                sb.append(readFileTail(previous, 120));
            }
        } else {
            sb.append(I18n.tr("尚未生成客户端运行日志记录。\n"));
        }
        // Final safety net: strip any stray NUL bytes so clipboard paste in chat apps never truncates.
        return sb.toString().replace("\u0000", "");
    }

    private File findPreviousClientLog(File logcatDir, File latest) {
        if (logcatDir == null || !logcatDir.exists() || latest == null) return null;
        File[] files = logcatDir.listFiles((d, name) -> name.startsWith("client_") && name.endsWith(".log"));
        if (files == null || files.length < 2) return null;
        Arrays.sort(files, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
        for (File f : files) {
            if (!f.getName().equals(latest.getName())) return f;
        }
        return null;
    }

    private boolean containsLine(File file, String needle) {
        if (file == null || !file.exists()) return false;
        try (BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            int scanned = 0;
            while ((line = br.readLine()) != null && scanned < 400) {
                if (line.contains(needle)) return true;
                scanned++;
            }
        } catch (Exception ignored) {
        }
        return false;
    }

    private List<File> findCrashReports(File logcatDir) {
        List<File> result = new ArrayList<>();
        if (logcatDir == null || !logcatDir.exists()) return result;
        File[] files = logcatDir.listFiles((d, name) -> name.startsWith("crash_") && name.endsWith(".log"));
        if (files == null) return result;
        Arrays.sort(files, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
        for (int i = 0; i < Math.min(files.length, 3); i++) {
            result.add(files[i]);
        }
        return result;
    }

    private String readFileHead(File file, int maxLines) {
        if (file == null || !file.exists()) return "";
        StringBuilder sb = new StringBuilder();
        try (BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            int count = 0;
            while ((line = br.readLine()) != null && count < maxLines) {
                sb.append(line).append('\n');
                count++;
            }
        } catch (Exception ignored) {
        }
        return sb.toString();
    }

    private File findLatestClientLog(File logcatDir) {
        if (logcatDir == null || !logcatDir.exists()) return null;
        File[] files = logcatDir.listFiles((d, name) -> name.startsWith("client_") && name.endsWith(".log"));
        if (files == null || files.length == 0) return null;
        Arrays.sort(files, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
        return files[0];
    }

    private String readFileTail(File file, int maxLines) {
        if (file == null || !file.exists()) return "";
        List<String> lines = new ArrayList<>();
        try (BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            while ((line = br.readLine()) != null) {
                lines.add(line);
                if (lines.size() > maxLines * 2) {
                    lines.subList(0, maxLines).clear();
                }
            }
        } catch (Exception ignored) {
        }
        int start = Math.max(0, lines.size() - maxLines);
        StringBuilder sb = new StringBuilder();
        for (int i = start; i < lines.size(); i++) {
            sb.append(lines.get(i)).append('\n');
        }
        return sb.toString();
    }

    private List<String> extractCrashAndErrorLines(File file) {
        List<String> result = new ArrayList<>();
        try (BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            int contextAfterHit = 0;
            while ((line = br.readLine()) != null) {
                String lower = line.toLowerCase(Locale.US);
                boolean hit = lower.contains("crashed") || lower.contains("fatal signal")
                        || lower.contains("backtrace") || lower.contains("[error]")
                        || lower.contains("[critical]") || lower.contains("sigsegv")
                        || lower.contains("sigabrt") || lower.contains("last script command")
                        || lower.contains("last rendered object") || lower.contains("(pressure)");
                if (hit) {
                    result.add(line);
                    contextAfterHit = 10;
                } else if (contextAfterHit > 0) {
                    result.add(line);
                    contextAfterHit--;
                }
                if (result.size() > 150) break;
            }
        } catch (Exception ignored) {
        }
        return result;
    }

    private String buildDeviceSummaryLine(Context appCtx) {
        boolean is64 = android.os.Process.is64Bit();
        String abi = is64 ? I18n.tr("64位") : I18n.tr("32位");
        File extDir = appCtx.getExternalFilesDir(null);
        long freeMB = 0;
        if (extDir != null && extDir.exists()) {
            StatFs stat = new StatFs(extDir.getAbsolutePath());
            freeMB = (stat.getAvailableBlocksLong() * stat.getBlockSizeLong()) / (1024 * 1024);
        }
        long pageSizeKb = -1;
        try {
            pageSizeKb = android.system.Os.sysconf(android.system.OsConstants._SC_PAGESIZE) / 1024;
        } catch (Throwable ignored) {
        }
        ClientSettings settings = ClientSettings.loadLauncher(appCtx);
        return String.format(Locale.US,
                I18n.tr("版本: v%s (%s) | 设备: %s %s | 系统: Android %s (SDK %d) | 页大小: %dKB | 剩余空间: %d MB\n")
                + I18n.tr("模组开关: Modloader=%b, AML=%b, JPatch=%b, MonetLoader=%b, CLEO=%b"),
                BuildConfig.VERSION_NAME, abi,
                Build.MANUFACTURER, Build.MODEL, Build.VERSION.RELEASE, Build.VERSION.SDK_INT, pageSizeKb, freeMB,
                settings.getBoolean("modloader"),
                settings.getBoolean("aml_scripts"),
                settings.getBoolean("jpatch_enabled"),
                settings.getBoolean("monet_scripts"),
                settings.getBoolean("cleo_scripts"));
    }

    private void appendHistoricalExitReasons(Context appCtx, StringBuilder sb) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return;
        try {
            ActivityManager am = (ActivityManager) appCtx.getSystemService(Context.ACTIVITY_SERVICE);
            if (am == null) return;
            List<ApplicationExitInfo> exits = am.getHistoricalProcessExitReasons(appCtx.getPackageName(), 0, 8);
            if (exits == null || exits.isEmpty()) return;

            SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);
            sb.append(I18n.tr("--- 系统进程退出记录 (ApplicationExitInfo) ---\n"));
            ApplicationExitInfo traceCandidate = null;
            for (ApplicationExitInfo info : exits) {
                int reason = info.getReason();
                int status = info.getStatus();
                String ts = sdf.format(new Date(info.getTimestamp()));
                String reasonStr = exitReasonName(reason);
                String statusExtra = (reason == ApplicationExitInfo.REASON_CRASH_NATIVE
                        || reason == ApplicationExitInfo.REASON_SIGNALED)
                        ? ("/" + signalName(status)) : "";
                String desc = info.getDescription();
                sb.append(String.format(Locale.US,
                        "[%s] %s (pid=%d) reason=%s(%d) status=%d%s rss=%dMB pss=%dMB%s\n",
                        ts,
                        info.getProcessName(),
                        info.getPid(),
                        reasonStr,
                        reason,
                        status,
                        statusExtra,
                        info.getRss() / 1024L,
                        info.getPss() / 1024L,
                        (desc != null && !desc.isEmpty()) ? (" desc=" + desc) : ""));
                if (traceCandidate == null
                        && (reason == ApplicationExitInfo.REASON_CRASH_NATIVE
                        || reason == ApplicationExitInfo.REASON_CRASH
                        || reason == ApplicationExitInfo.REASON_ANR
                        || reason == ApplicationExitInfo.REASON_SIGNALED)) {
                    traceCandidate = info;
                }
            }
            if (traceCandidate != null) {
                String traceSummary = readExitTraceSummary(traceCandidate);
                if (!traceSummary.isEmpty()) {
                    sb.append(I18n.tr("--- 最近异常退出的系统 Trace 摘要 (pid="))
                            .append(traceCandidate.getPid()).append(") ---\n")
                            .append(traceSummary);
                    if (!traceSummary.endsWith("\n")) sb.append('\n');
                }
            }
            sb.append('\n');
        } catch (Throwable ignored) {
        }
    }

    private static String exitReasonName(int reason) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return "UNKNOWN";
        switch (reason) {
            case ApplicationExitInfo.REASON_EXIT_SELF: return "EXIT_SELF";
            case ApplicationExitInfo.REASON_SIGNALED: return "SIGNALED";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "LOW_MEMORY";
            case ApplicationExitInfo.REASON_CRASH: return "CRASH_JAVA";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "CRASH_NATIVE";
            case ApplicationExitInfo.REASON_ANR: return "ANR";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "INIT_FAILURE";
            case ApplicationExitInfo.REASON_PERMISSION_CHANGE: return "PERMISSION_CHANGE";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "EXCESSIVE_RESOURCE";
            case ApplicationExitInfo.REASON_USER_REQUESTED: return "USER_REQUESTED";
            case ApplicationExitInfo.REASON_USER_STOPPED: return "USER_STOPPED";
            case ApplicationExitInfo.REASON_DEPENDENCY_DIED: return "DEPENDENCY_DIED";
            case ApplicationExitInfo.REASON_OTHER: return "OTHER";
            default:
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && reason == ApplicationExitInfo.REASON_FREEZER) {
                    return "FREEZER";
                }
                return "REASON_" + reason;
        }
    }

    private static String signalName(int sig) {
        switch (sig) {
            case 4: return "SIGILL";
            case 5: return "SIGTRAP";
            case 6: return "SIGABRT";
            case 7: return "SIGBUS";
            case 8: return "SIGFPE";
            case 9: return "SIGKILL";
            case 11: return "SIGSEGV";
            case 13: return "SIGPIPE";
            case 15: return "SIGTERM";
            case 31: return "SIGSYS";
            default: return "SIG_" + sig;
        }
    }

    // Safely extract readable diagnostics from ApplicationExitInfo.getTraceInputStream():
    // ANR traces are plain UTF-8 text, whereas Android 12+ native crash traces are
    // binary AOSP Tombstone protobufs (system/core/debuggerd/proto/tombstone.proto).
    private static String readExitTraceSummary(ApplicationExitInfo info) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return "";
        try (InputStream is = info.getTraceInputStream()) {
            if (is == null) return "";
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int maxBytes = 1024 * 1024;
            int total = 0;
            int n;
            while (total < maxBytes && (n = is.read(buf, 0, Math.min(buf.length, maxBytes - total))) > 0) {
                baos.write(buf, 0, n);
                total += n;
            }
            byte[] data = baos.toByteArray();
            if (data.length == 0) return "";

            int probe = Math.min(data.length, 512);
            boolean hasNul = false;
            int ctrlBytes = 0;
            for (int i = 0; i < probe; i++) {
                int b = data[i] & 0xFF;
                if (b == 0) {
                    hasNul = true;
                    break;
                }
                if (b < 0x20 && b != '\n' && b != '\r' && b != '\t') {
                    ctrlBytes++;
                }
            }
            if (!hasNul && ctrlBytes < 3) {
                String text = new String(data, StandardCharsets.UTF_8);
                String[] lines = text.split("\n");
                StringBuilder out = new StringBuilder();
                int limit = Math.min(lines.length, 60);
                for (int i = 0; i < limit; i++) {
                    out.append(lines[i]).append('\n');
                }
                return out.toString();
            }

            // Binary AOSP Tombstone protobuf: parse structured signal/abort/causes/crashing-thread backtrace.
            String parsed = parseAospTombstoneProto(data);
            if (!parsed.isEmpty()) {
                return parsed;
            }

            // Fallback: extract meaningful printable ASCII tokens without binary garbage.
            Set<String> tokens = new LinkedHashSet<>();
            StringBuilder cur = new StringBuilder();
            for (int i = 0; i < data.length; i++) {
                int b = data[i] & 0xFF;
                if (b >= 0x20 && b <= 0x7E) {
                    cur.append((char) b);
                } else {
                    collectInterestingTombstoneToken(cur, tokens);
                    cur.setLength(0);
                    if (tokens.size() >= 60) break;
                }
            }
            collectInterestingTombstoneToken(cur, tokens);

            StringBuilder out = new StringBuilder();
            for (String tok : tokens) {
                if (out.length() + tok.length() + 1 > 1800) break;
                out.append(tok).append('\n');
            }
            return out.toString();
        } catch (Throwable ignored) {
            return "";
        }
    }

    private static final class ProtoReader {
        final byte[] buf;
        int pos;
        final int end;

        ProtoReader(byte[] buf, int start, int end) {
            this.buf = buf;
            this.pos = start;
            this.end = Math.min(end, buf.length);
        }

        boolean hasMore() {
            return pos < end;
        }

        long readVarint() {
            long val = 0;
            int shift = 0;
            while (pos < end && shift < 64) {
                int b = buf[pos++] & 0xFF;
                val |= (long) (b & 0x7F) << shift;
                if ((b & 0x80) == 0) return val;
                shift += 7;
            }
            pos = end;
            return val;
        }

        int readLengthDelimitedEnd() {
            long len = readVarint();
            if (len < 0 || len > (end - pos)) {
                pos = end;
                return end;
            }
            return pos + (int) len;
        }

        String readCleanString() {
            int start = pos;
            int subEnd = readLengthDelimitedEnd();
            pos = subEnd;
            String raw = new String(buf, start, Math.max(0, subEnd - start), StandardCharsets.UTF_8);
            StringBuilder clean = new StringBuilder(raw.length());
            for (int i = 0; i < raw.length(); i++) {
                char c = raw.charAt(i);
                if (c >= 0x20 && c != 0x7F) {
                    clean.append(c);
                } else if (c == '\t') {
                    clean.append(' ');
                }
            }
            return clean.toString().trim();
        }

        void skipField(int wireType) {
            switch (wireType) {
                case 0: readVarint(); break;
                case 1: pos = Math.min(end, pos + 8); break;
                case 2: pos = readLengthDelimitedEnd(); break;
                case 5: pos = Math.min(end, pos + 4); break;
                default: pos = end; break;
            }
        }
    }

    // Minimal wire-format parser for AOSP tombstone.proto (Tombstone / Signal / Thread / BacktraceFrame).
    private static String parseAospTombstoneProto(byte[] data) {
        try {
            ProtoReader r = new ProtoReader(data, 0, data.length);
            String buildFp = "";
            long crashTid = -1;
            String sigName = "";
            long sigNum = 0;
            String sigCodeName = "";
            long sigCode = 0;
            boolean hasFault = false;
            long faultAddr = 0;
            String abortMsg = "";
            List<String> causes = new ArrayList<>();
            String crashThreadName = "";
            List<String> crashFrames = new ArrayList<>();
            // Fallback first thread with backtrace if tid match appears before field 6
            String firstThreadName = "";
            long firstThreadTid = -1;
            List<String> firstFrames = new ArrayList<>();

            while (r.hasMore()) {
                long tag = r.readVarint();
                int field = (int) (tag >>> 3);
                int wt = (int) (tag & 7);
                if (field == 0) break;

                if (field == 2 && wt == 2) {
                    buildFp = r.readCleanString();
                } else if (field == 6 && wt == 0) {
                    crashTid = r.readVarint();
                } else if (field == 10 && wt == 2) {
                    int subEnd = r.readLengthDelimitedEnd();
                    ProtoReader sr = new ProtoReader(data, r.pos, subEnd);
                    while (sr.hasMore()) {
                        long stag = sr.readVarint();
                        int sf = (int) (stag >>> 3);
                        int swt = (int) (stag & 7);
                        if (sf == 1 && swt == 0) sigNum = sr.readVarint();
                        else if (sf == 2 && swt == 2) sigName = sr.readCleanString();
                        else if (sf == 3 && swt == 0) sigCode = sr.readVarint();
                        else if (sf == 4 && swt == 2) sigCodeName = sr.readCleanString();
                        else if (sf == 8 && swt == 0) hasFault = sr.readVarint() != 0;
                        else if (sf == 9 && swt == 0) faultAddr = sr.readVarint();
                        else sr.skipField(swt);
                    }
                    r.pos = subEnd;
                } else if (field == 14 && wt == 2) {
                    abortMsg = r.readCleanString();
                } else if (field == 15 && wt == 2) {
                    int subEnd = r.readLengthDelimitedEnd();
                    ProtoReader cr = new ProtoReader(data, r.pos, subEnd);
                    while (cr.hasMore()) {
                        long ctag = cr.readVarint();
                        int cf = (int) (ctag >>> 3);
                        int cwt = (int) (ctag & 7);
                        if (cf == 1 && cwt == 2) {
                            String c = cr.readCleanString();
                            if (!c.isEmpty() && causes.size() < 4) causes.add(c);
                        } else {
                            cr.skipField(cwt);
                        }
                    }
                    r.pos = subEnd;
                } else if (field == 16 && wt == 2) {
                    // map<uint32, Thread> entry: 1=key(tid), 2=Thread
                    int entryEnd = r.readLengthDelimitedEnd();
                    ProtoReader er = new ProtoReader(data, r.pos, entryEnd);
                    long entryTid = -1;
                    int threadStart = -1;
                    int threadEnd = -1;
                    while (er.hasMore()) {
                        long etag = er.readVarint();
                        int ef = (int) (etag >>> 3);
                        int ewt = (int) (etag & 7);
                        if (ef == 1 && ewt == 0) {
                            entryTid = er.readVarint();
                        } else if (ef == 2 && ewt == 2) {
                            threadEnd = er.readLengthDelimitedEnd();
                            threadStart = er.pos;
                            er.pos = threadEnd;
                        } else {
                            er.skipField(ewt);
                        }
                    }
                    r.pos = entryEnd;
                    if (threadStart >= 0 && threadEnd > threadStart) {
                        boolean isTarget = (crashTid > 0 && entryTid == crashTid);
                        if (isTarget || firstFrames.isEmpty()) {
                            List<String> frames = new ArrayList<>();
                            String tName = parseTombstoneThread(data, threadStart, threadEnd, frames);
                            if (isTarget) {
                                crashThreadName = tName;
                                crashFrames = frames;
                            } else if (firstFrames.isEmpty() && !frames.isEmpty()) {
                                firstThreadName = tName;
                                firstThreadTid = entryTid;
                                firstFrames = frames;
                            }
                        }
                    }
                } else {
                    r.skipField(wt);
                }
            }

            if (sigName.isEmpty() && sigNum == 0 && abortMsg.isEmpty() && crashFrames.isEmpty() && firstFrames.isEmpty()) {
                return "";
            }
            StringBuilder sb = new StringBuilder();
            if (!buildFp.isEmpty()) {
                sb.append("build: ").append(buildFp).append('\n');
            }
            if (sigNum != 0 || !sigName.isEmpty()) {
                sb.append(String.format(Locale.US, "signal: %d (%s) code: %d (%s)",
                        sigNum, sigName.isEmpty() ? signalName((int) sigNum) : sigName,
                        sigCode, sigCodeName));
                if (hasFault) {
                    sb.append(String.format(Locale.US, " fault_addr: 0x%x", faultAddr));
                }
                sb.append('\n');
            }
            if (!abortMsg.isEmpty()) {
                sb.append("abort: ").append(abortMsg).append('\n');
            }
            for (String cause : causes) {
                sb.append("cause: ").append(cause).append('\n');
            }
            List<String> useFrames = !crashFrames.isEmpty() ? crashFrames : firstFrames;
            String useThread = !crashFrames.isEmpty() ? crashThreadName : firstThreadName;
            long useTid = !crashFrames.isEmpty() ? crashTid : firstThreadTid;
            if (!useFrames.isEmpty()) {
                sb.append(String.format(Locale.US, "thread: tid=%d name=%s\n", useTid, useThread));
                for (int i = 0; i < useFrames.size(); i++) {
                    sb.append(String.format(Locale.US, "  #%02d %s\n", i, useFrames.get(i)));
                }
            }
            return sb.toString();
        } catch (Throwable ignored) {
            return "";
        }
    }

    private static String parseTombstoneThread(byte[] data, int start, int end, List<String> outFrames) {
        ProtoReader tr = new ProtoReader(data, start, end);
        String name = "";
        while (tr.hasMore()) {
            long tag = tr.readVarint();
            int f = (int) (tag >>> 3);
            int wt = (int) (tag & 7);
            if (f == 2 && wt == 2) {
                name = tr.readCleanString();
            } else if (f == 4 && wt == 2 && outFrames.size() < 24) {
                int fEnd = tr.readLengthDelimitedEnd();
                ProtoReader fr = new ProtoReader(data, tr.pos, fEnd);
                long relPc = 0;
                String fnName = "";
                long fnOff = 0;
                String fileName = "";
                while (fr.hasMore()) {
                    long ftag = fr.readVarint();
                    int ff = (int) (ftag >>> 3);
                    int fwt = (int) (ftag & 7);
                    if (ff == 1 && fwt == 0) relPc = fr.readVarint();
                    else if (ff == 4 && fwt == 2) fnName = fr.readCleanString();
                    else if (ff == 5 && fwt == 0) fnOff = fr.readVarint();
                    else if (ff == 6 && fwt == 2) fileName = fr.readCleanString();
                    else fr.skipField(fwt);
                }
                tr.pos = fEnd;
                String shortFile = fileName;
                int slash = shortFile.lastIndexOf('/');
                if (slash >= 0 && slash + 1 < shortFile.length()) {
                    shortFile = shortFile.substring(slash + 1);
                }
                StringBuilder frame = new StringBuilder();
                frame.append(String.format(Locale.US, "pc 0x%x %s",
                        relPc, shortFile.isEmpty() ? "<unknown>" : shortFile));
                if (!fnName.isEmpty()) {
                    frame.append(" (").append(fnName);
                    if (fnOff != 0) frame.append("+0x").append(Long.toHexString(fnOff));
                    frame.append(')');
                }
                outFrames.add(frame.toString());
            } else {
                tr.skipField(wt);
            }
        }
        return name;
    }

    private static void collectInterestingTombstoneToken(StringBuilder cur, Set<String> out) {
        if (cur.length() < 6 || cur.length() > 240) return;
        String s = cur.toString().trim();
        if (s.length() < 6) return;
        String lower = s.toLowerCase(Locale.US);
        boolean interesting = s.contains(".so")
                || s.contains("_Z")
                || lower.contains("sig")
                || lower.contains("abort")
                || lower.contains("fatal")
                || lower.contains("fortify")
                || lower.contains("scudo")
                || lower.contains("linker")
                || lower.contains("dlopen")
                || lower.contains("jni")
                || lower.contains("seccomp")
                || lower.contains("samp")
                || lower.contains("gtasa")
                || lower.contains("crash")
                || lower.contains("nvevent")
                || lower.contains("exception")
                || lower.contains("error")
                || lower.contains("null")
                || lower.contains("stack");
        if (interesting) {
            out.add(s);
        }
    }
}
