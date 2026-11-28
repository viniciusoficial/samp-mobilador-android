package com.sampmobile.launcher.ui.adapter;

import android.app.Activity;
import android.content.Context;
import android.content.res.ColorStateList;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.joom.paranoid.Obfuscate;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.LauncherFonts;
import com.sampmobile.launcher.utils.LauncherThemeManager;
import com.sampmobile.launcher.utils.SampQuery;
import com.sampmobile.launcher.utils.ServerModel;
import com.sampmobile.launcher.utils.Utils;

@Obfuscate
public class ServerAdapter extends RecyclerView.Adapter<ServerAdapter.ViewHolder> {

    public static final int MODE_FAVORITES = 0;
    public static final int MODE_INTERNET = 1;
    public static final int MODE_RECENT = 2;

    private static final int INTERNET_LIMIT = 120;
    private static final int QUERY_POOL_SIZE = 8;
    private static final int LOCAL_QUERY_ATTEMPTS = 2;

    public interface OnServerSelectListener {
        void onServerSelected(ServerModel server);
    }

    public interface OnListLoadedListener {
        void onListLoaded();
    }

    static class ViewHolder extends RecyclerView.ViewHolder {
        LinearLayout server_item;
        TextView server_name;
        TextView server_ping;
        ImageView server_password;
        TextView server_gamemode;
        TextView server_players;
        View server_status;
        ProgressBar server_players_bar;

        public ViewHolder(View itemView) {
            super(itemView);
            server_item = itemView.findViewById(R.id.server_item);
            server_name = itemView.findViewById(R.id.server_name);
            server_ping = itemView.findViewById(R.id.server_ping);
            server_password = itemView.findViewById(R.id.server_password);
            server_gamemode = itemView.findViewById(R.id.server_gamemode);
            server_players = itemView.findViewById(R.id.server_players);
            server_status = itemView.findViewById(R.id.server_status);
            server_players_bar = itemView.findViewById(R.id.server_players_bar);
            LauncherFonts.applyCondensed(server_ping, server_players);
        }
    }

    /** Ping quality colour: green up to 120 ms, amber up to 250 ms, red above, faint when unknown. */
    public static int pingColor(Context ctx, String ping) {
        int ms;
        try {
            ms = Integer.parseInt(ping.trim());
        } catch (Exception e) {
            return ctx.getColor(R.color.ui_text_faint);
        }
        if (ms < 0) return ctx.getColor(R.color.ui_text_faint);
        if (ms <= 120) return ctx.getColor(R.color.ui_good);
        if (ms <= 250) return ctx.getColor(R.color.ui_warn);
        return ctx.getColor(R.color.ui_bad);
    }

    /** Server-reported text without a single letter or digit ("-", "..", blank) counts as absent. */
    public static String meaningful(String s) {
        if (s == null) return "";
        String t = s.trim();
        for (int i = 0; i < t.length(); i++) {
            if (Character.isLetterOrDigit(t.charAt(i))) return t;
        }
        return "";
    }

    private static int parseIntSafe(String s) {
        try {
            return Integer.parseInt(s.trim());
        } catch (Exception e) {
            return 0;
        }
    }

    Activity activity;
    public List<ServerModel> serverModel;
    private final List<ServerModel> allServers = new ArrayList<>();
    private final Map<Integer, List<ServerModel>> listCache = new HashMap<>();
    private int mode = MODE_FAVORITES;
    private String selectedAddress = null;
    private String filterQuery = "";
    private volatile int generation = 0;
    private OnServerSelectListener selectListener;
    private OnListLoadedListener listLoadedListener;

    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();
    private final ExecutorService queryPool = Executors.newFixedThreadPool(QUERY_POOL_SIZE);

    public ServerAdapter(Activity activity, List<ServerModel> serverModel, int mode) {
        this.activity = activity;
        this.serverModel = serverModel;
        this.mode = mode;
    }

    public void setOnServerSelectListener(OnServerSelectListener listener) {
        this.selectListener = listener;
    }

    public void setOnListLoadedListener(OnListLoadedListener listener) {
        this.listLoadedListener = listener;
    }

    public int getMode() {
        return mode;
    }

    public ServerModel getSelected() {
        if (selectedAddress == null) return null;
        for (ServerModel s : allServers) {
            if (s.address().equals(selectedAddress)) return s;
        }
        return null;
    }

    public void clearSelection() {
        selectedAddress = null;
        notifyDataSetChanged();
        if (selectListener != null) selectListener.onServerSelected(null);
    }

    public void filter(String query) {
        filterQuery = query == null ? "" : query.toLowerCase();
        serverModel.clear();
        for (ServerModel s : allServers) {
            if (filterQuery.isEmpty()
                    || s.server_name.toLowerCase().contains(filterQuery)
                    || s.server_host.toLowerCase().contains(filterQuery)
                    || s.server_gamemode.toLowerCase().contains(filterQuery)) {
                serverModel.add(s);
            }
        }
        notifyDataSetChanged();
    }

    public boolean deleteSelected() {
        ServerModel selected = getSelected();
        if (selected == null) return false;

        String address = selected.address();
        try {
            if (mode == MODE_FAVORITES) {
                Utils.deleteServerFromFavorites(activity, address);
            } else if (mode == MODE_RECENT) {
                Utils.deleteServerFromRecent(activity, address);
            } else {
                return false;
            }
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        }

        int pos = serverModel.indexOf(selected);
        allServers.remove(selected);
        List<ServerModel> cached = listCache.get(mode);
        if (cached != null) cached.remove(selected);
        if (pos >= 0) {
            serverModel.remove(pos);
            notifyItemRemoved(pos);
        }
        clearSelection();
        return true;
    }

    @NonNull
    public ViewHolder onCreateViewHolder(ViewGroup parent, int i) {
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.layout_server_item, parent, false);
        return new ViewHolder(view);
    }

    public void onBindViewHolder(ViewHolder viewHolder, int position) {
        if (position >= serverModel.size()) {
            return;
        }

        ServerModel server = serverModel.get(position);

        viewHolder.server_name.setText(server.server_name);
        String ping = server.server_ping;
        boolean hasPing = !("-".equals(ping) || ping.isEmpty());
        viewHolder.server_ping.setText(hasPing ? ping + " ms" : "—");
        int pingColor = pingColor(activity, ping);
        viewHolder.server_ping.setTextColor(pingColor);
        viewHolder.server_status.setBackgroundTintList(ColorStateList.valueOf(pingColor));

        String gamemode = meaningful(server.server_gamemode);
        String lang = meaningful(server.server_language);
        if (gamemode.isEmpty() && lang.isEmpty()) {
            viewHolder.server_gamemode.setText(server.address());
        } else if (gamemode.isEmpty() || lang.isEmpty()) {
            viewHolder.server_gamemode.setText(gamemode + lang);
        } else {
            viewHolder.server_gamemode.setText(gamemode + "  ·  " + lang);
        }

        int players = parseIntSafe(server.server_players);
        int maxPlayers = parseIntSafe(server.server_maxplayers);
        viewHolder.server_players.setText(server.server_players + "/" + server.server_maxplayers);
        viewHolder.server_players.setTextColor(activity.getColor(players > 0 ? R.color.launcher_text : R.color.launcher_text_dim));
        viewHolder.server_players_bar.setProgress(maxPlayers > 0 ? Math.min(100, players * 100 / maxPlayers) : 0);
        viewHolder.server_password.setVisibility(server.server_password.equals("1") ? View.VISIBLE : View.GONE);

        boolean selected = server.address().equals(selectedAddress);
        int accentColor = LauncherThemeManager.getAccentColor(activity);
        viewHolder.server_players_bar.setProgressTintList(ColorStateList.valueOf(accentColor));
        if (selected) {
            viewHolder.server_item.setBackground(LauncherThemeManager.createSelectedRowDrawable(activity, accentColor));
        } else {
            viewHolder.server_item.setBackgroundResource(R.drawable.ui_row_bg);
        }

        viewHolder.server_item.setOnClickListener(v -> {
            String address = server.address();
            if (address.equals(selectedAddress)) {
                return;
            }
            String old = selectedAddress;
            selectedAddress = address;
            int count = serverModel.size();
            for (int i = 0; i < count; i++) {
                String a = serverModel.get(i).address();
                if (a.equals(old) || a.equals(address)) {
                    notifyItemChanged(i);
                }
            }
            if (selectListener != null) selectListener.onServerSelected(server);
        });
    }

    /**
     * Switch tab. Restores the target tab's cached list immediately (stale
     * data visible instantly), then reloads the source and re-queries every
     * server on the bounded pool in the background.
     */
    public void selectMode(int newMode) {
        if (newMode == mode) {
            refresh();
            return;
        }

        generation++;
        listCache.put(mode, new ArrayList<>(allServers));
        mode = newMode;

        selectedAddress = null;
        if (selectListener != null) selectListener.onServerSelected(null);

        allServers.clear();
        List<ServerModel> cached = listCache.get(mode);
        if (cached != null) allServers.addAll(cached);
        filter(filterQuery);

        loadAndQuery();
    }

    public void setMode(int mode) {
        selectMode(mode);
    }

    /** Reload current mode's source and re-query all servers. Selection is kept. */
    public void refresh() {
        generation++;
        loadAndQuery();
    }

    public void refreshServers() {
        refresh();
    }

    public void shutdown() {
        ioExecutor.shutdownNow();
        queryPool.shutdownNow();
    }

    private void loadAndQuery() {
        final int gen = generation;
        final int listMode = mode;
        ioExecutor.execute(() -> {
            List<ServerModel> parsed = listMode == MODE_INTERNET ? loadInternet() : loadLocal(listMode);
            if (gen != generation) return;

            activity.runOnUiThread(() -> {
                if (activity.isFinishing() || activity.isDestroyed()) return;
                if (gen != generation) return;

                // merge with existing entries so re-querying doesn't flash
                // rows back to "Loading" — cached data stays until replaced
                Map<String, ServerModel> oldByAddr = new HashMap<>();
                for (ServerModel s : allServers) oldByAddr.put(s.address(), s);
                List<ServerModel> cached = listCache.get(listMode);
                if (cached != null) {
                    for (ServerModel s : cached) oldByAddr.putIfAbsent(s.address(), s);
                }

                allServers.clear();
                for (ServerModel s : parsed) {
                    ServerModel old = oldByAddr.get(s.address());
                    allServers.add(old != null ? old : s);
                }
                listCache.put(listMode, new ArrayList<>(allServers));
                filter(filterQuery);

                if (listLoadedListener != null) listLoadedListener.onListLoaded();

                boolean internet = listMode == MODE_INTERNET;
                for (ServerModel model : allServers) {
                    queryPool.execute(() -> queryServer(model, internet, gen));
                }
            });
        });
    }

    private List<ServerModel> loadLocal(int listMode) {
        List<ServerModel> parsed = new ArrayList<>();
        try {
            File extDir = activity.getExternalFilesDir(null);
            if (extDir == null) return parsed;
            File serversFile = new File(extDir,
                    listMode == MODE_RECENT ? "recent.json" : "servers.json");
            String jsonString = (serversFile.exists() && serversFile.length() > 0)
                    ? Utils.readJSONFromFile(serversFile) : "{\"servers\":[]}";
            JSONObject json = new JSONObject(jsonString);
            JSONArray serversArray = json.getJSONArray("servers");

            for (int i = 0; i < serversArray.length(); i++) {
                JSONObject serverJson = serversArray.getJSONObject(i);
                String host = serverJson.getString("host");
                String port = serverJson.getString("port");

                ServerModel model = new ServerModel(activity.getString(R.string.loading), host, port, "0", "-", "0", "0", "-");
                model.server_discord = optStr(serverJson, "discord");
                model.server_website = optStr(serverJson, "website");
                parsed.add(model);
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return parsed;
    }

    private List<ServerModel> loadInternet() {
        List<ServerModel> parsed = new ArrayList<>();
        try {
            String jsonString = Utils.getStringOutputByURL("https://api.open.mp/servers");
            JSONArray serversArray = new JSONArray(jsonString);

            int limit = Math.min(serversArray.length(), INTERNET_LIMIT);
            for (int i = 0; i < limit; i++) {
                JSONObject serverJson = serversArray.getJSONObject(i);
                String ip = serverJson.optString("ip", "");
                String[] parts = ip.split(":");
                if (parts.length < 2) continue;
                String host = parts[0];
                String port = parts[1];

                String hn = optStr(serverJson, "hn");
                ServerModel model = new ServerModel(
                        hn.isEmpty() ? host : hn,
                        host,
                        port,
                        serverJson.optBoolean("pa", false) ? "1" : "0",
                        emptyDash(optStr(serverJson, "gm")),
                        String.valueOf(serverJson.optInt("pc", 0)),
                        String.valueOf(serverJson.optInt("pm", 0)),
                        emptyDash(optStr(serverJson, "la")));
                parsed.add(model);
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return parsed;
    }

    private static String optStr(JSONObject j, String key) {
        String s = j.optString(key, "");
        return "null".equals(s) ? "" : s;
    }

    private static String emptyDash(String s) {
        return s.isEmpty() ? "-" : s;
    }

    private int indexOf(ServerModel model) {
        return serverModel.indexOf(model);
    }

    private void queryServer(ServerModel model, boolean internet, int gen) {
        int attempts = internet ? 1 : LOCAL_QUERY_ATTEMPTS;
        boolean done = false;
        for (int a = 0; a < attempts && !done; a++) {
            if (gen != generation) return;
            done = fetchServerInfo(model, gen);
            if (!done && a + 1 < attempts) {
                try {
                    Thread.sleep(800);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }

        if (!done && gen == generation) {
            if (!internet) {
                model.server_name = activity.getString(R.string.server_unreachable);
                model.server_players = "0";
                model.server_maxplayers = "0";
                model.server_gamemode = "-";
            }
            model.server_ping = "-";
            activity.runOnUiThread(() -> {
                if (activity.isFinishing() || activity.isDestroyed()) return;
                int idx = indexOf(model);
                if (idx >= 0) notifyItemChanged(idx);
            });
        }
    }

    private boolean fetchServerInfo(ServerModel model, int gen) {
        if (gen != generation) return true;
        SampQuery sampQuery = null;
        try {
            String charset = ClientSettings.getCharsetForLauncher(activity);
            sampQuery = new SampQuery(model.server_host, Integer.parseInt(model.server_port), charset);

            String[] infos = sampQuery.getInfo();
            if (infos == null) {
                return false;
            }

            String hostname = infos[3];
            if (hostname == null || hostname.replace(" ", "").equals("")) {
                return false;
            }

            long ping = sampQuery.getPing();

            if (gen != generation) return true;

            model.server_name = hostname;
            model.server_password = infos[0];
            model.server_gamemode = infos[4];
            model.server_players = infos[1];
            model.server_maxplayers = infos[2];
            model.server_language = infos[5];
            model.server_ping = ping >= 0 ? String.valueOf(ping) : "-";

            activity.runOnUiThread(() -> {
                if (activity.isFinishing() || activity.isDestroyed()) return;
                int idx = indexOf(model);
                if (idx >= 0) notifyItemChanged(idx);
                if (selectListener != null && model.address().equals(selectedAddress)) {
                    selectListener.onServerSelected(model);
                }
            });
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        } finally {
            if (sampQuery != null) {
                sampQuery.close();
            }
        }
        return true;
    }

    public int getItemCount() {
        return serverModel.size();
    }
}
