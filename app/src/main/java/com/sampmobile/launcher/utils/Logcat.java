package com.sampmobile.launcher.utils;

import android.content.Context;

import com.joom.paranoid.Obfuscate;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.List;

import kotlin.collections.CollectionsKt;
import kotlin.io.FilesKt;
import kotlin.text.Charsets;

@Obfuscate
public final class Logcat {
    private static volatile String PATH_LOGCAT;
    private LogDumper mLogDumper;
    private final int mPId;

    public Logcat(Context context) {
        init(context);
        this.mPId = android.os.Process.myPid();
    }

    public void init(Context context) {
        if (context == null) return;
        File externalFilesDir = context.getExternalFilesDir(null);
        if (externalFilesDir == null) {
            PATH_LOGCAT = null;
            return;
        }
        File dir = new File(externalFilesDir, "logcat");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        PATH_LOGCAT = dir.getAbsolutePath();
    }

    public void start() {
        String dir = PATH_LOGCAT;
        if (dir == null || dir.isEmpty()) {
            return;
        }
        if (this.mLogDumper == null) {
            this.mLogDumper = new LogDumper(String.valueOf(this.mPId), dir);
        }
        try {
            LogDumper logDumper = this.mLogDumper;
            if (logDumper != null) {
                logDumper.start();
            }
        } catch (IllegalThreadStateException e) {
            e.printStackTrace();
        }
    }

    public void stop() {
        LogDumper logDumper = this.mLogDumper;
        if (logDumper != null) {
            logDumper.stopLogs();
            this.mLogDumper = null;
        }
    }

    private static final class LogDumper extends Thread {
        private volatile Process logcatProc;
        private final String mPID;
        private final String mLogDir;
        private volatile boolean mRunning;

        public LogDumper(String pid, String logDir) {
            super("SampLogcatDumper");
            this.mPID = pid;
            this.mLogDir = logDir;
        }

        @Override
        public synchronized void start() {
            this.mRunning = true;
            super.start();
        }

        public void stopLogs() {
            this.mRunning = false;
            Process proc = this.logcatProc;
            if (proc != null) {
                proc.destroy();
            }
        }

        public boolean isRunning() {
            return this.mRunning;
        }

        @Override
        public void run() {
            FileOutputStream out = null;
            BufferedReader reader = null;
            try {
                File dir = new File(this.mLogDir);
                if (!dir.exists()) {
                    dir.mkdirs();
                }
                File file = new File(dir, "SAMPMOBILE.log");
                if (file.exists()) {
                    if (file.length() > 33554432L) {
                        file.delete();
                    } else if (file.length() > 16777216L) {
                        try {
                            List<String> lines = FilesKt.readLines(file, Charsets.UTF_8);
                            List<String> subList = lines.subList(lines.size() / 2, lines.size());
                            String joinedText = CollectionsKt.joinToString(subList, "\n", "", "", -1, "...", null);
                            FilesKt.writeText(file, joinedText, Charsets.UTF_8);
                        } catch (Exception e) {
                            e.printStackTrace();
                        }
                    }
                }
                out = new FileOutputStream(file, true);

                Process proc = new ProcessBuilder("logcat", "--pid=" + this.mPID).start();
                this.logcatProc = proc;
                reader = new BufferedReader(new InputStreamReader(proc.getInputStream(), StandardCharsets.UTF_8), 2048);

                String line;
                while (this.mRunning && (line = reader.readLine()) != null) {
                    if (!this.mRunning) break;
                    if (line.isEmpty() || line.contains("AudioTrack")) {
                        continue;
                    }
                    out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
                }
            } catch (IOException e) {
                if (this.mRunning) {
                    e.printStackTrace();
                }
            } finally {
                Process proc = this.logcatProc;
                this.logcatProc = null;
                if (proc != null) {
                    proc.destroy();
                }
                if (reader != null) {
                    try {
                        reader.close();
                    } catch (IOException ignored) {
                    }
                }
                if (out != null) {
                    try {
                        out.close();
                    } catch (IOException ignored) {
                    }
                }
            }
        }
    }
}
