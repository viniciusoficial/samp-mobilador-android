package com.gta.launcher.activity;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import com.gta.game.R;
import com.gta.game.SAMP;
import com.gta.launcher.ui.PingGraphView;

import org.ini4j.Wini;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileWriter;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends AppCompatActivity {
    private static final String TAG = "SAMP_Launcher";
    private static final String PREFS_NAME = "samp_pc_launcher_prefs";
    private static final String KEY_NICKNAME = "nickname";
    private static final String KEY_FAVORITES = "favorites_json";
    private static final String KEY_FPS_LIMIT = "fps_limit";

    private static class PlayerEntry {
        final String name;
        final int score;

        PlayerEntry(String name, int score) {
            this.name = name;
            this.score = score;
        }
    }

    private static class RuleEntry {
        final String rule;
        final String value;

        RuleEntry(String rule, String value) {
            this.rule = rule;
            this.value = value;
        }
    }

    private static class ServerEntry {
        String name;
        String ip;
        int port;
        String mode;
        String language;
        String tab;
        boolean passworded;
        int players;
        int maxPlayers;
        int ping;
        final List<PlayerEntry> playerList = new ArrayList<>();
        final List<RuleEntry> ruleList = new ArrayList<>();

        ServerEntry(String name, String ip, int port, String mode, String language,
                    String tab, boolean passworded, int players, int maxPlayers, int ping) {
            this.name = name;
            this.ip = ip;
            this.port = port;
            this.mode = mode;
            this.language = language;
            this.tab = tab;
            this.passworded = passworded;
            this.players = players;
            this.maxPlayers = maxPlayers;
            this.ping = ping;
            seedDefaultDetails();
        }

        void seedDefaultDetails() {
            ruleList.clear();
            ruleList.add(new RuleEntry("lagcomp", "On"));
            ruleList.add(new RuleEntry("mapname", "San Andreas"));
            ruleList.add(new RuleEntry("version", "0.3.7-R5"));
            ruleList.add(new RuleEntry("weather", "10"));
            ruleList.add(new RuleEntry("weburl", "www.sa-mp.mp"));
            ruleList.add(new RuleEntry("worldtime", "12:00"));
        }
    }

    private EditText etNickname;
    private EditText etFilterMode;
    private EditText etFilterLanguage;
    private CheckBox cbNotFull;
    private CheckBox cbNotEmpty;
    private CheckBox cbNoPassword;

    private ListView lvServers;
    private ListView lvPlayers;
    private ListView lvRules;

    private TextView tvServerInfoTitle;
    private TextView tvServerInfo;
    private TextView tvStatusBar;
    private TextView tvPermStatus;
    private PingGraphView pingGraphView;

    private Button startButton;
    private Button btnAddServer;
    private Button btnDeleteServer;
    private Button btnRefreshServer;
    private Button btnPermissions;
    private Button tabFavorites;
    private Button tabInternet;
    private Button tabHosted;

    private TextView menuFile;
    private TextView menuView;
    private TextView menuServers;
    private TextView menuTools;
    private TextView menuHelp;
    private TextView btnWindowMinimize;
    private TextView btnWindowMaximize;
    private TextView btnWindowClose;

    private final List<ServerEntry> allServers = new ArrayList<>();
    private final List<ServerEntry> visibleServers = new ArrayList<>();
    private final List<PlayerEntry> visiblePlayers = new ArrayList<>();
    private final List<RuleEntry> visibleRules = new ArrayList<>();

    private ArrayAdapter<ServerEntry> serverAdapter;
    private ArrayAdapter<PlayerEntry> playerAdapter;
    private ArrayAdapter<RuleEntry> ruleAdapter;

    private String currentTab = "favorites";
    private int selectedPosition = 0;
    private int lastClickedPos = -1;
    private long lastClickMs = 0L;
    private int fpsLimit = 120;
    private boolean storagePermissionGranted = false;
    private boolean gameLaunchRequested = false;

    private final ExecutorService queryExecutor = Executors.newSingleThreadExecutor();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private final ActivityResultLauncher<String[]> requestRuntimePermissionsLauncher =
            registerForActivityResult(new ActivityResultContracts.RequestMultiplePermissions(), permissions -> {
                refreshPermissionStatus();
            });

    private final ActivityResultLauncher<Intent> manageStorageLauncher =
            registerForActivityResult(new ActivityResultContracts.StartActivityForResult(), result -> {
                refreshPermissionStatus();
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setFullScreenMode();
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        setContentView(R.layout.main_activity);

        initViews();
        loadSavedPreferences();
        initAdapters();
        setupClickListeners();
        refreshPermissionStatus();
        requestInitialPermissionsIfNeeded();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshPermissionStatus();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        queryExecutor.shutdownNow();
    }

    private void initViews() {
        etNickname = findViewById(R.id.etNickname);
        etFilterMode = findViewById(R.id.etFilterMode);
        etFilterLanguage = findViewById(R.id.etFilterLanguage);
        cbNotFull = findViewById(R.id.cbNotFull);
        cbNotEmpty = findViewById(R.id.cbNotEmpty);
        cbNoPassword = findViewById(R.id.cbNoPassword);

        lvServers = findViewById(R.id.lvServers);
        lvPlayers = findViewById(R.id.lvPlayers);
        lvRules = findViewById(R.id.lvRules);

        tvServerInfoTitle = findViewById(R.id.tvServerInfoTitle);
        tvServerInfo = findViewById(R.id.tvServerInfo);
        tvStatusBar = findViewById(R.id.tvStatusBar);
        tvPermStatus = findViewById(R.id.tvPermStatus);
        pingGraphView = findViewById(R.id.pingGraphView);

        startButton = findViewById(R.id.startButton);
        btnAddServer = findViewById(R.id.btnAddServer);
        btnDeleteServer = findViewById(R.id.btnDeleteServer);
        btnRefreshServer = findViewById(R.id.btnRefreshServer);
        btnPermissions = findViewById(R.id.btnPermissions);
        tabFavorites = findViewById(R.id.tabFavorites);
        tabInternet = findViewById(R.id.tabInternet);
        tabHosted = findViewById(R.id.tabHosted);

        menuFile = findViewById(R.id.menuFile);
        menuView = findViewById(R.id.menuView);
        menuServers = findViewById(R.id.menuServers);
        menuTools = findViewById(R.id.menuTools);
        menuHelp = findViewById(R.id.menuHelp);
        btnWindowMinimize = findViewById(R.id.btnWindowMinimize);
        btnWindowMaximize = findViewById(R.id.btnWindowMaximize);
        btnWindowClose = findViewById(R.id.btnWindowClose);
    }

    private void loadSavedPreferences() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        etNickname.setText(prefs.getString(KEY_NICKNAME, "Mobilador_PC"));
        fpsLimit = prefs.getInt(KEY_FPS_LIMIT, 120);
        allServers.clear();

        String favJson = prefs.getString(KEY_FAVORITES, null);
        if (favJson != null) {
            try {
                JSONArray arr = new JSONArray(favJson);
                for (int i = 0; i < arr.length(); i++) {
                    JSONObject o = arr.getJSONObject(i);
                    allServers.add(new ServerEntry(
                            o.optString("name", "Servidor Favorito"),
                            o.optString("ip", "132.243.253.240"),
                            o.optInt("port", 2467),
                            o.optString("mode", "SA-MP 0.3.7"),
                            o.optString("language", "Português | Brasil"),
                            "favorites",
                            o.optBoolean("passworded", false),
                            o.optInt("players", 0),
                            o.optInt("maxPlayers", 100),
                            o.optInt("ping", 30)
                    ));
                }
            } catch (Exception e) {
                Log.w(TAG, "Failed parsing favorites: " + e.getMessage());
            }
        }

        if (allServers.isEmpty()) {
            allServers.add(new ServerEntry("Brasil Play Shox | RPG [0.3.7]", "132.243.253.240", 2467, "Brasil RPG", "Português | Brasil", "favorites", false, 284, 500, 28));
            allServers.add(new ServerEntry("Arena DM / Mobilador [BR]", "149.56.112.88", 7777, "TDM / C-Bug", "Português | BR", "favorites", false, 112, 200, 19));
        }
        allServers.add(new ServerEntry("Cidade Vida Real | Server 1", "177.54.150.15", 7777, "Vida Real RP", "Português | Brasil", "internet", false, 468, 600, 34));
        allServers.add(new ServerEntry("FZ Roleplay [S1] Español", "51.77.68.34", 7777, "FZ:RP v5.06", "Español - Spanish", "internet", false, 897, 899, 148));
        allServers.add(new ServerEntry("Horizon Roleplay Brasil", "198.50.214.12", 7777, "Horizon RP", "Português | Brasil", "hosted", false, 342, 500, 31));
    }

    private void saveFavorites() {
        try {
            JSONArray arr = new JSONArray();
            for (ServerEntry s : allServers) {
                if ("favorites".equals(s.tab)) {
                    JSONObject o = new JSONObject();
                    o.put("name", s.name);
                    o.put("ip", s.ip);
                    o.put("port", s.port);
                    o.put("mode", s.mode);
                    o.put("language", s.language);
                    o.put("passworded", s.passworded);
                    o.put("players", s.players);
                    o.put("maxPlayers", s.maxPlayers);
                    o.put("ping", s.ping);
                    arr.put(o);
                }
            }
            getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                    .edit()
                    .putString(KEY_FAVORITES, arr.toString())
                    .putString(KEY_NICKNAME, etNickname.getText().toString().trim())
                    .putInt(KEY_FPS_LIMIT, fpsLimit)
                    .apply();
        } catch (Exception e) {
            Log.e(TAG, "Error saving favorites: " + e.getMessage());
        }
    }

    private int dp(int value) {
        return Math.round(TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics()));
    }

    private TextView makeCell(float weight, int fixedWidthDp, int gravity) {
        TextView tv = new TextView(this);
        LinearLayout.LayoutParams lp = fixedWidthDp > 0
                ? new LinearLayout.LayoutParams(dp(fixedWidthDp), ViewGroup.LayoutParams.MATCH_PARENT)
                : new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, weight);
        tv.setLayoutParams(lp);
        tv.setGravity(gravity | Gravity.CENTER_VERTICAL);
        tv.setSingleLine(true);
        tv.setEllipsize(TextUtils.TruncateAt.END);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f);
        tv.setPadding(dp(4), 0, dp(4), 0);
        return tv;
    }

    private void initAdapters() {
        serverAdapter = new ArrayAdapter<>(this, 0, visibleServers) {
            @NonNull
            @Override
            public View getView(int position, @Nullable View convertView, @NonNull ViewGroup parent) {
                LinearLayout row;
                if (convertView instanceof LinearLayout) {
                    row = (LinearLayout) convertView;
                } else {
                    row = new LinearLayout(MainActivity.this);
                    row.setOrientation(LinearLayout.HORIZONTAL);
                    row.setLayoutParams(new ListView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(21)));
                    row.addView(makeCell(0f, 20, Gravity.CENTER));
                    row.addView(makeCell(3.0f, 0, Gravity.START));
                    row.addView(makeCell(0.95f, 0, Gravity.START));
                    row.addView(makeCell(0.65f, 0, Gravity.START));
                    row.addView(makeCell(1.75f, 0, Gravity.START));
                    row.addView(makeCell(1.35f, 0, Gravity.START));
                }

                ServerEntry s = visibleServers.get(position);
                boolean selected = (position == selectedPosition);
                row.setBackgroundColor(selected ? Color.parseColor("#316AC5") : Color.WHITE);

                TextView cLock = (TextView) row.getChildAt(0);
                TextView cHost = (TextView) row.getChildAt(1);
                TextView cPlayers = (TextView) row.getChildAt(2);
                TextView cPing = (TextView) row.getChildAt(3);
                TextView cMode = (TextView) row.getChildAt(4);
                TextView cLang = (TextView) row.getChildAt(5);

                cLock.setText(s.passworded ? "🔒" : "🔓");
                cHost.setText(s.name);
                cPlayers.setText(s.players + " / " + s.maxPlayers);
                cPing.setText(String.valueOf(s.ping));
                cMode.setText(s.mode);
                cLang.setText(s.language);

                int fg = selected ? Color.WHITE : Color.parseColor("#111111");
                cLock.setTextColor(selected ? Color.WHITE : (s.passworded ? Color.parseColor("#B45309") : Color.parseColor("#15803D")));
                cHost.setTextColor(fg);
                cPlayers.setTextColor(fg);
                cPing.setTextColor(fg);
                cMode.setTextColor(fg);
                cLang.setTextColor(fg);
                return row;
            }
        };
        lvServers.setAdapter(serverAdapter);

        playerAdapter = new ArrayAdapter<>(this, 0, visiblePlayers) {
            @NonNull
            @Override
            public View getView(int position, @Nullable View convertView, @NonNull ViewGroup parent) {
                LinearLayout row;
                if (convertView instanceof LinearLayout) {
                    row = (LinearLayout) convertView;
                } else {
                    row = new LinearLayout(MainActivity.this);
                    row.setOrientation(LinearLayout.HORIZONTAL);
                    row.setLayoutParams(new ListView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(19)));
                    row.addView(makeCell(1.6f, 0, Gravity.START));
                    row.addView(makeCell(1.0f, 0, Gravity.START));
                }
                PlayerEntry p = visiblePlayers.get(position);
                TextView cName = (TextView) row.getChildAt(0);
                TextView cScore = (TextView) row.getChildAt(1);
                cName.setText(p.name);
                cScore.setText(String.valueOf(p.score));
                cName.setTextColor(Color.parseColor("#111111"));
                cScore.setTextColor(Color.parseColor("#111111"));
                return row;
            }
        };
        lvPlayers.setAdapter(playerAdapter);

        ruleAdapter = new ArrayAdapter<>(this, 0, visibleRules) {
            @NonNull
            @Override
            public View getView(int position, @Nullable View convertView, @NonNull ViewGroup parent) {
                LinearLayout row;
                if (convertView instanceof LinearLayout) {
                    row = (LinearLayout) convertView;
                } else {
                    row = new LinearLayout(MainActivity.this);
                    row.setOrientation(LinearLayout.HORIZONTAL);
                    row.setLayoutParams(new ListView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(19)));
                    row.addView(makeCell(1.0f, 0, Gravity.START));
                    row.addView(makeCell(1.3f, 0, Gravity.START));
                }
                RuleEntry r = visibleRules.get(position);
                TextView cRule = (TextView) row.getChildAt(0);
                TextView cVal = (TextView) row.getChildAt(1);
                cRule.setText(r.rule);
                cVal.setText(r.value);
                cRule.setTextColor(Color.parseColor("#111111"));
                cVal.setTextColor(Color.parseColor("#111111"));
                return row;
            }
        };
        lvRules.setAdapter(ruleAdapter);

        filterServers();
    }

    private void filterServers() {
        String modeQ = etFilterMode.getText().toString().trim().toLowerCase(Locale.ROOT);
        String langQ = etFilterLanguage.getText().toString().trim().toLowerCase(Locale.ROOT);
        boolean notFull = cbNotFull.isChecked();
        boolean notEmpty = cbNotEmpty.isChecked();
        boolean noPassword = cbNoPassword.isChecked();

        visibleServers.clear();
        int totalPlayers = 0;
        int totalSlots = 0;

        for (ServerEntry s : allServers) {
            if (!currentTab.equals(s.tab)) continue;
            if (!modeQ.isEmpty() && !s.mode.toLowerCase(Locale.ROOT).contains(modeQ)) continue;
            if (!langQ.isEmpty() && !s.language.toLowerCase(Locale.ROOT).contains(langQ)) continue;
            if (notFull && s.players >= s.maxPlayers) continue;
            if (notEmpty && s.players <= 0) continue;
            if (noPassword && s.passworded) continue;

            visibleServers.add(s);
            totalPlayers += s.players;
            totalSlots += s.maxPlayers;
        }

        if (selectedPosition >= visibleServers.size()) {
            selectedPosition = 0;
        }
        serverAdapter.notifyDataSetChanged();
        updateTabButtons();
        updateSelectedServerDetails(true);
        tvStatusBar.setText(String.format(
                Locale.US,
                "Servers: %d players, playing on %d servers. (%d player slots available)",
                totalPlayers, visibleServers.size(), totalSlots
        ));
    }

    private void updateTabButtons() {
        tabFavorites.setBackgroundResource("favorites".equals(currentTab) ? R.drawable.win32_tab_active : R.drawable.win32_tab_inactive);
        tabInternet.setBackgroundResource("internet".equals(currentTab) ? R.drawable.win32_tab_active : R.drawable.win32_tab_inactive);
        tabHosted.setBackgroundResource("hosted".equals(currentTab) ? R.drawable.win32_tab_active : R.drawable.win32_tab_inactive);
    }

    private void updateSelectedServerDetails(boolean resetGraph) {
        visiblePlayers.clear();
        visibleRules.clear();

        if (visibleServers.isEmpty() || selectedPosition < 0 || selectedPosition >= visibleServers.size()) {
            tvServerInfoTitle.setText("Server Info: -");
            tvServerInfo.setText("Address:  -\nPlayers:  0 / 0\nPing:     0\nMode:     -\nLanguage: -");
            playerAdapter.notifyDataSetChanged();
            ruleAdapter.notifyDataSetChanged();
            return;
        }

        ServerEntry s = visibleServers.get(selectedPosition);
        tvServerInfoTitle.setText("Server Info: " + s.name);
        tvServerInfo.setText(
                "Address:  " + s.ip + ":" + s.port + "\n" +
                "Players:  " + s.players + " / " + s.maxPlayers + "\n" +
                "Ping:     " + s.ping + "\n" +
                "Mode:     " + s.mode + "\n" +
                "Language: " + s.language
        );

        visiblePlayers.addAll(s.playerList);
        visibleRules.addAll(s.ruleList);
        playerAdapter.notifyDataSetChanged();
        ruleAdapter.notifyDataSetChanged();

        if (resetGraph) {
            pingGraphView.setPingSeed(s.ping);
        } else {
            pingGraphView.pushPing(s.ping);
        }
    }

    private void queryServerAsync(final ServerEntry server) {
        queryExecutor.execute(() -> {
            try {
                InetAddress addr = InetAddress.getByName(server.ip);
                byte[] ipBytes = addr.getAddress();
                if (ipBytes.length != 4) return;

                try (DatagramSocket socket = new DatagramSocket()) {
                    socket.setSoTimeout(1200);

                    // 1. Ping packet ('p')
                    long t0 = System.currentTimeMillis();
                    byte[] pingPkt = buildSampQueryPacket(ipBytes, server.port, 'p', new byte[]{0x31, 0x32, 0x33, 0x34});
                    socket.send(new DatagramPacket(pingPkt, pingPkt.length, addr, server.port));
                    byte[] buf = new byte[2048];
                    DatagramPacket resp = new DatagramPacket(buf, buf.length);
                    socket.receive(resp);
                    int rtt = (int) Math.max(1L, System.currentTimeMillis() - t0);
                    server.ping = rtt;

                    // 2. Server info packet ('i')
                    byte[] infoPkt = buildSampQueryPacket(ipBytes, server.port, 'i', null);
                    socket.send(new DatagramPacket(infoPkt, infoPkt.length, addr, server.port));
                    resp = new DatagramPacket(buf, buf.length);
                    socket.receive(resp);
                    parseSampInfoResponse(server, buf, resp.getLength());

                    // 3. Rules packet ('r')
                    byte[] rulesPkt = buildSampQueryPacket(ipBytes, server.port, 'r', null);
                    socket.send(new DatagramPacket(rulesPkt, rulesPkt.length, addr, server.port));
                    resp = new DatagramPacket(buf, buf.length);
                    socket.receive(resp);
                    parseSampRulesResponse(server, buf, resp.getLength());

                    // 4. Client list packet ('c')
                    if (server.players > 0 && server.players <= 100) {
                        byte[] cliPkt = buildSampQueryPacket(ipBytes, server.port, 'c', null);
                        socket.send(new DatagramPacket(cliPkt, cliPkt.length, addr, server.port));
                        resp = new DatagramPacket(buf, buf.length);
                        socket.receive(resp);
                        parseSampClientsResponse(server, buf, resp.getLength());
                    }

                    mainHandler.post(() -> {
                        saveFavorites();
                        serverAdapter.notifyDataSetChanged();
                        updateSelectedServerDetails(false);
                    });
                }
            } catch (Exception ignored) {
                mainHandler.post(() -> {
                    if (!visibleServers.isEmpty() && selectedPosition >= 0 && selectedPosition < visibleServers.size()
                            && visibleServers.get(selectedPosition) == server) {
                        pingGraphView.pushPing(server.ping);
                    }
                });
            }
        });
    }

    private static byte[] buildSampQueryPacket(byte[] ip, int port, char opcode, @Nullable byte[] extra) {
        int extraLen = (extra != null) ? extra.length : 0;
        byte[] out = new byte[11 + extraLen];
        out[0] = 'S';
        out[1] = 'A';
        out[2] = 'M';
        out[3] = 'P';
        out[4] = ip[0];
        out[5] = ip[1];
        out[6] = ip[2];
        out[7] = ip[3];
        out[8] = (byte) (port & 0xFF);
        out[9] = (byte) ((port >> 8) & 0xFF);
        out[10] = (byte) opcode;
        if (extra != null) {
            System.arraycopy(extra, 0, out, 11, extraLen);
        }
        return out;
    }

    private static void parseSampInfoResponse(ServerEntry s, byte[] buf, int len) {
        if (len < 16) return;
        ByteBuffer bb = ByteBuffer.wrap(buf, 11, len - 11).order(ByteOrder.LITTLE_ENDIAN);
        s.passworded = (bb.get() != 0);
        s.players = bb.getShort() & 0xFFFF;
        s.maxPlayers = bb.getShort() & 0xFFFF;
        s.name = readInt32String(bb);
        s.mode = readInt32String(bb);
        s.language = readInt32String(bb);
    }

    private static void parseSampRulesResponse(ServerEntry s, byte[] buf, int len) {
        if (len < 13) return;
        ByteBuffer bb = ByteBuffer.wrap(buf, 11, len - 11).order(ByteOrder.LITTLE_ENDIAN);
        int count = bb.getShort() & 0xFFFF;
        List<RuleEntry> parsed = new ArrayList<>();
        for (int i = 0; i < count && bb.remaining() >= 2; i++) {
            String key = readByteString(bb);
            String val = readByteString(bb);
            parsed.add(new RuleEntry(key, val));
        }
        if (!parsed.isEmpty()) {
            s.ruleList.clear();
            s.ruleList.addAll(parsed);
        }
    }

    private static void parseSampClientsResponse(ServerEntry s, byte[] buf, int len) {
        if (len < 13) return;
        ByteBuffer bb = ByteBuffer.wrap(buf, 11, len - 11).order(ByteOrder.LITTLE_ENDIAN);
        int count = bb.getShort() & 0xFFFF;
        List<PlayerEntry> parsed = new ArrayList<>();
        for (int i = 0; i < count && bb.remaining() >= 5; i++) {
            String name = readByteString(bb);
            if (bb.remaining() < 4) break;
            int score = bb.getInt();
            parsed.add(new PlayerEntry(name, score));
        }
        s.playerList.clear();
        s.playerList.addAll(parsed);
    }

    private static String readInt32String(ByteBuffer bb) {
        if (bb.remaining() < 4) return "";
        int strLen = bb.getInt();
        if (strLen <= 0 || strLen > bb.remaining()) return "";
        byte[] bytes = new byte[strLen];
        bb.get(bytes);
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private static String readByteString(ByteBuffer bb) {
        if (bb.remaining() < 1) return "";
        int strLen = bb.get() & 0xFF;
        if (strLen <= 0 || strLen > bb.remaining()) return "";
        byte[] bytes = new byte[strLen];
        bb.get(bytes);
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private void setupClickListeners() {
        lvServers.setOnItemClickListener((parent, view, position, id) -> {
            long now = System.currentTimeMillis();
            boolean doubleTap = (position == lastClickedPos && (now - lastClickMs) < 380L);
            lastClickedPos = position;
            lastClickMs = now;

            selectedPosition = position;
            serverAdapter.notifyDataSetChanged();
            updateSelectedServerDetails(true);
            if (position >= 0 && position < visibleServers.size()) {
                queryServerAsync(visibleServers.get(position));
            }
            if (doubleTap) {
                connectSelectedServer();
            }
        });

        TextWatcher filterWatcher = new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override public void afterTextChanged(Editable s) { filterServers(); }
        };
        etFilterMode.addTextChangedListener(filterWatcher);
        etFilterLanguage.addTextChangedListener(filterWatcher);
        cbNotFull.setOnCheckedChangeListener((b, checked) -> filterServers());
        cbNotEmpty.setOnCheckedChangeListener((b, checked) -> filterServers());
        cbNoPassword.setOnCheckedChangeListener((b, checked) -> filterServers());

        tabFavorites.setOnClickListener(v -> { currentTab = "favorites"; filterServers(); });
        tabInternet.setOnClickListener(v -> { currentTab = "internet"; filterServers(); });
        tabHosted.setOnClickListener(v -> { currentTab = "hosted"; filterServers(); });

        startButton.setOnClickListener(v -> connectSelectedServer());
        btnAddServer.setOnClickListener(v -> showAddServerDialog());
        btnDeleteServer.setOnClickListener(v -> deleteSelectedFavorite());
        btnRefreshServer.setOnClickListener(v -> {
            if (!visibleServers.isEmpty() && selectedPosition >= 0 && selectedPosition < visibleServers.size()) {
                queryServerAsync(visibleServers.get(selectedPosition));
                Toast.makeText(this, "Atualizando query UDP do servidor...", Toast.LENGTH_SHORT).show();
            }
        });
        btnPermissions.setOnClickListener(v -> showPermissionsAndOptimizationsDialog());
        tvPermStatus.setOnClickListener(v -> showPermissionsAndOptimizationsDialog());

        menuFile.setOnClickListener(v -> connectSelectedServer());
        menuView.setOnClickListener(v -> {
            etFilterMode.setText("");
            etFilterLanguage.setText("");
            cbNotFull.setChecked(false);
            cbNotEmpty.setChecked(false);
            cbNoPassword.setChecked(false);
            filterServers();
        });
        menuServers.setOnClickListener(v -> showAddServerDialog());
        menuTools.setOnClickListener(v -> showPermissionsAndOptimizationsDialog());
        menuHelp.setOnClickListener(v -> new AlertDialog.Builder(this)
                .setTitle("San Andreas Multiplayer 0.3.7-R5-2-MP")
                .setMessage("Porta Mobilador PC 2.10\n- Câmera direta via Mouse (horizontal e vertical)\n- Mira alinhada à direção da câmera\n- Chat e diálogos estilo PC desktop")
                .setPositiveButton("OK", null)
                .show());

        btnWindowClose.setOnClickListener(v -> finish());
        btnWindowMinimize.setOnClickListener(v -> moveTaskToBack(true));
        btnWindowMaximize.setOnClickListener(v -> setFullScreenMode());
    }

    private void deleteSelectedFavorite() {
        if (!"favorites".equals(currentTab) || visibleServers.isEmpty()) return;
        ServerEntry target = visibleServers.get(selectedPosition);
        allServers.remove(target);
        saveFavorites();
        filterServers();
    }

    private void connectSelectedServer() {
        if (visibleServers.isEmpty() || selectedPosition < 0 || selectedPosition >= visibleServers.size()) {
            Toast.makeText(this, "Adicione ou selecione um servidor primeiro.", Toast.LENGTH_SHORT).show();
            return;
        }
        String nick = etNickname.getText().toString().trim();
        if (nick.length() < 3 || nick.length() > 24) {
            Toast.makeText(this, "O Nickname deve ter entre 3 e 24 caracteres.", Toast.LENGTH_SHORT).show();
            return;
        }
        if (!hasStorageAccess()) {
            requestInitialPermissionsIfNeeded();
            return;
        }
        ServerEntry srv = visibleServers.get(selectedPosition);
        saveFavorites();
        writeSampSettingsIni(nick, srv.ip, srv.port);
        startGame();
    }

    private void showAddServerDialog() {
        final EditText input = new EditText(this);
        input.setHint("IP:Porta (ex: 132.243.253.240:2467)");
        input.setSingleLine(true);

        new AlertDialog.Builder(this)
                .setTitle("Add Server")
                .setMessage("Enter new server IP:Port below:")
                .setView(input)
                .setPositiveButton("OK", (dialog, which) -> {
                    String raw = input.getText().toString().trim();
                    String[] parts = raw.split(":");
                    String ip = parts.length > 0 ? parts[0].trim() : "";
                    int port = 7777;
                    if (parts.length > 1) {
                        try {
                            port = Integer.parseInt(parts[1].trim());
                        } catch (NumberFormatException ignored) {
                            port = -1;
                        }
                    }
                    if (ip.length() < 4 || port < 1 || port > 65535) {
                        Toast.makeText(this, "Formato inválido. Use IP:Porta.", Toast.LENGTH_SHORT).show();
                        return;
                    }
                    ServerEntry added = new ServerEntry("SA-MP Server (" + ip + ":" + port + ")", ip, port, "SA-MP 0.3.7", "-", "favorites", false, 0, 100, 30);
                    allServers.add(0, added);
                    currentTab = "favorites";
                    selectedPosition = 0;
                    saveFavorites();
                    filterServers();
                    queryServerAsync(added);
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void showPermissionsAndOptimizationsDialog() {
        String[] options = {
                "Solicitar Acesso ao Armazenamento (settings.ini / cache)",
                "Solicitar Microfone + Bluetooth (OTG/BT + Voice)",
                "FPS Máximo: 60 FPS",
                "FPS Máximo: 90 FPS",
                "FPS Máximo: 120 FPS (Recomendado Mobilador)",
                "FPS Máximo: 144 FPS"
        };
        new AlertDialog.Builder(this)
                .setTitle("Tools · Permissões & Otimização Mobilador")
                .setItems(options, (dialog, which) -> {
                    if (which == 0) {
                        requestManageStoragePermission();
                    } else if (which == 1) {
                        requestRuntimePermissionsIfNeeded();
                    } else if (which == 2) {
                        fpsLimit = 60;
                    } else if (which == 3) {
                        fpsLimit = 90;
                    } else if (which == 4) {
                        fpsLimit = 120;
                    } else if (which == 5) {
                        fpsLimit = 144;
                    }
                    saveFavorites();
                    refreshPermissionStatus();
                })
                .setNegativeButton("Fechar", null)
                .show();
    }

    private boolean hasStorageAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void refreshPermissionStatus() {
        storagePermissionGranted = hasStorageAccess();
        tvPermStatus.setText(storagePermissionGranted
                ? ("Storage: OK | " + fpsLimit + " FPS")
                : ("Storage: Pendente (Clique) | " + fpsLimit + " FPS"));
        tvPermStatus.setTextColor(storagePermissionGranted
                ? Color.parseColor("#1E5631")
                : Color.parseColor("#B91C1C"));
    }

    private void requestInitialPermissionsIfNeeded() {
        requestRuntimePermissionsIfNeeded();
        if (!hasStorageAccess() && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            requestManageStoragePermission();
        }
    }

    private void requestRuntimePermissionsIfNeeded() {
        List<String> needed = new ArrayList<>();
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            needed.add(Manifest.permission.RECORD_AUDIO);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            needed.add(Manifest.permission.BLUETOOTH_CONNECT);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                needed.add(Manifest.permission.POST_NOTIFICATIONS);
            }
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_MEDIA_IMAGES) != PackageManager.PERMISSION_GRANTED) {
                needed.add(Manifest.permission.READ_MEDIA_IMAGES);
            }
        } else if (!hasStorageAccess()) {
            needed.add(Manifest.permission.READ_EXTERNAL_STORAGE);
            needed.add(Manifest.permission.WRITE_EXTERNAL_STORAGE);
        }
        if (!needed.isEmpty()) {
            requestRuntimePermissionsLauncher.launch(needed.toArray(new String[0]));
        }
    }

    private void requestManageStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                manageStorageLauncher.launch(intent);
            } catch (Exception e) {
                manageStorageLauncher.launch(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        }
    }

    private void writeSampSettingsIni(String nick, String host, int port) {
        try {
            File sampDir = new File(Environment.getExternalStorageDirectory(), "GTA/SAMP");
            if (!sampDir.exists()) sampDir.mkdirs();
            File iniFile = new File(sampDir, "settings.ini");

            if (iniFile.exists()) {
                Wini ini = new Wini(iniFile);
                ini.put("client", "name", nick);
                ini.put("client", "host", host);
                ini.put("client", "port", String.valueOf(port));
                ini.put("gui", "FPSLimit", String.valueOf(fpsLimit));
                ini.put("gui", "fps", "true");
                ini.store();
            } else {
                try (FileWriter writer = new FileWriter(iniFile, false)) {
                    writer.write("[client]\n");
                    writer.write("name = " + nick + "\n");
                    writer.write("host = " + host + "\n");
                    writer.write("port = " + port + "\n");
                    writer.write("password = \n");
                    writer.write("version = 0.3.7\n");
                    writer.write("[debug]\n");
                    writer.write("debug = true\n");
                    writer.write("online = true\n");
                    writer.write("[gui]\n");
                    writer.write("Font = arial.ttf\n");
                    writer.write("FontSize = 26.0\n");
                    writer.write("FontOutline = 2\n");
                    writer.write("ChatPosX = 325.0\n");
                    writer.write("ChatPosY = 25.0\n");
                    writer.write("ChatSizeX = 1150.0\n");
                    writer.write("ChatSizeY = 220.0\n");
                    writer.write("ChatMaxMessages = 8\n");
                    writer.write("fps = true\n");
                    writer.write("FPSLimit = " + fpsLimit + "\n");
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error writing settings.ini: " + e.getMessage());
        }
    }

    private void startGame() {
        if (gameLaunchRequested) return;
        try {
            gameLaunchRequested = true;
            startActivity(new Intent(MainActivity.this, SAMP.class));
            finish();
        } catch (Exception e) {
            gameLaunchRequested = false;
            Log.e(TAG, "Error starting game: " + e.getMessage());
        }
    }

    private void setFullScreenMode() {
        try {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            );
            getWindow().setStatusBarColor(Color.TRANSPARENT);
            getWindow().setNavigationBarColor(Color.TRANSPARENT);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                WindowManager.LayoutParams params = getWindow().getAttributes();
                params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                getWindow().setAttributes(params);
            }
            if (getSupportActionBar() != null) getSupportActionBar().hide();
        } catch (Exception ignored) {
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            setFullScreenMode();
        }
    }

    public static void hideKeyboard(Activity activity) {
        try {
            InputMethodManager inputManager = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            View currentFocusedView = activity.getCurrentFocus();
            if (currentFocusedView != null && inputManager != null) {
                inputManager.hideSoftInputFromWindow(currentFocusedView.getWindowToken(), InputMethodManager.HIDE_NOT_ALWAYS);
            }
        } catch (Exception ignored) {
        }
    }
}
