package com.sampmobile.launcher;


import com.sampmobile.launcher.utils.I18n;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.animation.DecelerateInterpolator;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.app.AppCompatDelegate;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentManager;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.recyclerview.widget.SimpleItemAnimator;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import com.joom.paranoid.Obfuscate;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import com.sampmobile.launcher.ui.GameDataImportDialog;
import com.sampmobile.launcher.ui.LauncherServerDialogs;
import com.sampmobile.launcher.ui.adapter.PlayersAdapter;
import com.sampmobile.launcher.ui.adapter.RulesAdapter;
import com.sampmobile.launcher.ui.adapter.ServerAdapter;
import com.sampmobile.launcher.ui.fragment.ModsManagerFragment;
import com.sampmobile.launcher.ui.fragment.SettingsPageFragment;
import com.sampmobile.launcher.ui.widget.GtaTabStrip;
import com.sampmobile.launcher.utils.BundledGamedata;
import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.GameDataImporter;
import com.sampmobile.launcher.utils.LauncherFonts;
import com.sampmobile.launcher.utils.LauncherThemeManager;
import com.sampmobile.launcher.utils.LocaleManager;
import com.sampmobile.launcher.utils.SampQuery;
import com.sampmobile.launcher.utils.ServerModel;
import com.sampmobile.launcher.utils.Utils;

@Obfuscate
public class MainActivity extends AppCompatActivity implements LauncherThemeManager.ThemeListener {

    private static final int PAGE_SERVERS = 0;
    private static final int PAGE_MODS = 1;
    private static final int PAGE_SETTINGS = 2;
    private static final String STATE_CURRENT_PAGE = "state_current_page";

    private static final String TAG_MODS = "frag_mods";
    private static final String TAG_SETTINGS = "frag_settings";

    private static final int[] SERVER_TAB_MODES = {
            ServerAdapter.MODE_FAVORITES,
            ServerAdapter.MODE_INTERNET,
            ServerAdapter.MODE_RECENT
    };

    private ServerAdapter serverAdapter;
    private List<ServerModel> serverModel;
    private SwipeRefreshLayout swipeRefreshLayout;

    private EditText searchEditText;
    private TextView listCount;
    private TextView listEmpty;
    private boolean listLoading = true;

    private int currentTab = ServerAdapter.MODE_FAVORITES;

    private int currentPage = PAGE_SERVERS;
    private SettingsPageFragment.ViewBuilder settingsBuilder;
    private View[] pageViews;
    private ViewGroup[] navItems;
    private View navIndicator;

    private View infoEmpty;
    private View infoBody;
    private TextView infoServerName;
    private TextView infoServerMeta;
    private TextView infoAddress;
    private View infoLock;
    private TextView statPlayers;
    private TextView statPing;
    private TextView statLang;
    private View playersHeader;
    private View linksContainer;
    private View discordRow;
    private TextView discordText;
    private View rulesHeader;
    private ImageView btnFavorite;
    private View btnDelete;
    private RecyclerView playersList;
    private TextView playersEmpty;
    private PlayersAdapter playersAdapter;
    private RecyclerView rulesList;
    private RulesAdapter rulesAdapter;
    private final List<String[]> playersData = new ArrayList<>();
    private final List<String[]> rulesData = new ArrayList<>();
    private String detailAddress;

    private volatile int detailsGeneration = 0;
    private final ExecutorService detailExecutor = Executors.newSingleThreadExecutor();

    private final ActivityResultLauncher<String[]> gameDataZipPicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null) {
                    GameDataImportDialog.startImport(this, uri, null);
                }
            });

    @Override
    protected void attachBaseContext(Context newBase) {
        super.attachBaseContext(LocaleManager.wrap(newBase));
    }

    private boolean ensureGameDataOrPrompt() {
        List<String> missing = GameDataImporter.findMissingCoreFiles(getExternalFilesDir(null));
        if (missing.isEmpty()) {
            return true;
        }
        if (isFinishing() || isDestroyed()) {
            return false;
        }
        boolean hasBundle = BundledGamedata.isPresent(this);
        StringBuilder msg = new StringBuilder(I18n.tr("未检测到完整游戏数据包（缺少 "));
        msg.append(String.join("、", missing));
        if (hasBundle) {
            msg.append(I18n.tr("）。\n\n本安装包已内置游戏资源，点击下方「解压内置资源」即可自动安装，无需联网下载。"));
        } else {
            msg.append(I18n.tr("）。\n\n可前往「设置 → 文件验证」查看详细文件状态并选择本地导入或启动器下载精简版资源。"));
        }
        AlertDialog.Builder builder = new AlertDialog.Builder(this)
                .setTitle(I18n.tr("缺少游戏数据包"))
                .setMessage(msg.toString())
                .setNeutralButton(I18n.tr("一键导入 .zip"), (d, w) ->
                        gameDataZipPicker.launch(new String[]{"application/zip", "application/octet-stream", "*/*"}))
                .setNegativeButton(I18n.tr("取消"), null);
        if (hasBundle) {
            builder.setPositiveButton(I18n.tr("解压内置资源"), (d, w) ->
                    GameDataImportDialog.startBundledImport(this, null));
        } else {
            builder.setPositiveButton(I18n.tr("前往文件验证"), (d, w) -> openSettingsGameDataTab());
        }
        builder.show();
        return false;
    }

    /**
     * Friend-test packages embed the full game data inside the APK: the whole point
     * is install-and-play, so when core files are missing we offer extraction right
     * on the server list instead of making the user dig through settings.
     * Shown on every launch while data is incomplete — declining (「稍后」) leaves
     * all manual entry points (server-join dialog / zip import) available.
     */
    private void maybePromptBundledExtraction() {
        if (!BundledGamedata.isPresent(this)) {
            return;
        }
        try {
            detailExecutor.execute(() -> checkDataAndPromptBundled());
        } catch (java.util.concurrent.RejectedExecutionException ignored) {
            // Activity already destroyed within the post-delay window — nothing to prompt on.
        }
    }

    private void checkDataAndPromptBundled() {
        boolean missing = !GameDataImporter.findMissingCoreFiles(getExternalFilesDir(null)).isEmpty();
        if (!missing) {
            return;
        }
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) {
                return;
            }
            BundledGamedata.Info info = BundledGamedata.inspect(this);
            String sizeText = info.uncompressedBytes > 0
                    ? String.format(java.util.Locale.US, "%.1f GB", info.uncompressedBytes / (1024.0 * 1024.0 * 1024.0))
                    : I18n.tr("大量");
            new AlertDialog.Builder(this)
                    .setTitle(I18n.tr("首次运行需要解压游戏资源"))
                    .setMessage(String.format(java.util.Locale.US,
                            I18n.tr("本安装包已内置%s游戏资源，无需联网下载。\n\n首次运行需解压约 %s 数据到本机存储（约需几分钟，请保持屏幕常亮），解压完成后即可进服游玩。"),
                            info.displayLabel(), sizeText))
                    .setPositiveButton(I18n.tr("立即解压"), (d, w) ->
                            GameDataImportDialog.startBundledImport(this, null))
                    .setNegativeButton(I18n.tr("稍后"), null)
                    .show();
        });
    }

    private void openSettingsGameDataTab() {
        showPage(PAGE_SETTINGS);
        Fragment frag = getSupportFragmentManager().findFragmentByTag(TAG_SETTINGS);
        if (frag instanceof SettingsPageFragment) {
            ((SettingsPageFragment) frag).openGameDataTab();
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_YES);
        super.onCreate(savedInstanceState);

        Intent launchIntent = getIntent();
        if (launchIntent == null || !"sampmobile1337".equals(launchIntent.getStringExtra("extra_check"))) {
            Log.e("MainActivity", "Not joined from launcher!");
            finish();
            return;
        }

        // draw under the display cutout: the rail and backdrop run edge to edge instead of leaving a black bar
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }

        setContentView(R.layout.activity_main);

        androidx.core.view.WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        androidx.core.view.WindowInsetsControllerCompat insets =
                new androidx.core.view.WindowInsetsControllerCompat(getWindow(), getWindow().getDecorView());
        insets.setSystemBarsBehavior(
                androidx.core.view.WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        insets.hide(androidx.core.view.WindowInsetsCompat.Type.systemBars());

        View navRail = findViewById(R.id.nav_rail);
        View pageHost = findViewById(R.id.page_host);
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.root), (v, windowInsets) -> {
            Insets cutout = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout());
            navRail.setPadding(cutout.left, 0, 0, 0);
            pageHost.setPadding(0, 0, cutout.right, 0);
            return windowInsets;
        });

        ClientSettings.setAsync(this, "offline_mode", false);
        com.sampmobile.launcher.utils.ModManager.ensureDirectories(this);

        initViews();
        initServerList();
        initNavigation();
        initButtons();
        if (savedInstanceState != null) {
            int page = savedInstanceState.getInt(STATE_CURRENT_PAGE, PAGE_SERVERS);
            if (page == PAGE_MODS || page == PAGE_SETTINGS) {
                showPage(page);
            }
        }
        LauncherThemeManager.addListener(this);
        onThemeColorChanged(LauncherThemeManager.getAccentColor(this));

        scheduleSettingsPrewarm();

        // Let the server list settle first; the prompt itself is cheap and gated
        // by both bundle presence and missing core data.
        getWindow().getDecorView().postDelayed(this::maybePromptBundledExtraction, 900);
    }

    /**
     * Pre-warms SettingsPageFragment so opening Settings is instant (0ms), one panel per idle slot:
     * the page holds 600+ views, built in one go it froze the launcher for ~0.6 s (debug build, Redmi K50).
     */
    private void scheduleSettingsPrewarm() {
        Looper.myQueue().addIdleHandler(() -> {
            if (isFinishing() || isDestroyed()
                    || getSupportFragmentManager().findFragmentByTag(TAG_SETTINGS) != null) {
                return false;
            }
            if (settingsBuilder == null) {
                settingsBuilder = new SettingsPageFragment.ViewBuilder(getLayoutInflater(), findViewById(R.id.page_settings));
            } else if (!settingsBuilder.step()) {
                ensurePageFragment(PAGE_SETTINGS);
                return false;
            }
            // an idle handler only fires once per idle period: queue a message so the next slot comes around
            getWindow().getDecorView().post(this::scheduleSettingsPrewarm);
            return false;
        });
    }

    private void initViews() {
        swipeRefreshLayout = findViewById(R.id.refresh);
        swipeRefreshLayout.setColorSchemeColors(getColor(R.color.launcher_accent));
        swipeRefreshLayout.setProgressBackgroundColorSchemeColor(getColor(R.color.launcher_surface_2));
        swipeRefreshLayout.setOnRefreshListener(this::refreshServers);

        listCount = findViewById(R.id.list_count);
        listEmpty = findViewById(R.id.list_empty);

        searchEditText = findViewById(R.id.search_edit_text);
        searchEditText.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                if (serverAdapter != null) serverAdapter.filter(s.toString());
            }

            @Override
            public void afterTextChanged(Editable s) {
            }
        });

        infoEmpty = findViewById(R.id.info_empty);
        infoBody = findViewById(R.id.info_body);
        infoServerName = findViewById(R.id.info_server_name);
        infoServerMeta = findViewById(R.id.info_server_meta);
        infoAddress = findViewById(R.id.info_address);
        infoLock = findViewById(R.id.info_lock);
        statPlayers = findViewById(R.id.stat_players);
        statPing = findViewById(R.id.stat_ping);
        statLang = findViewById(R.id.stat_lang);
        LauncherFonts.applyCondensed(statPlayers, statPing);
        playersHeader = findViewById(R.id.players_header);
        linksContainer = findViewById(R.id.links_container);
        discordRow = findViewById(R.id.discord_row);
        discordText = findViewById(R.id.discord_text);
        rulesHeader = findViewById(R.id.rules_header);
        btnFavorite = findViewById(R.id.btn_favorite);
        btnDelete = findViewById(R.id.btn_delete);
        playersList = findViewById(R.id.players_list);
        playersEmpty = findViewById(R.id.players_empty);
        rulesList = findViewById(R.id.rules_list);

        playersAdapter = new PlayersAdapter(playersData);
        playersList.setLayoutManager(new LinearLayoutManager(this));
        playersList.setAdapter(playersAdapter);

        rulesAdapter = new RulesAdapter(rulesData);
        rulesList.setLayoutManager(new LinearLayoutManager(this));
        rulesList.setAdapter(rulesAdapter);

        findViewById(R.id.info_address_row).setOnClickListener(v -> {
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm != null && detailAddress != null) {
                cm.setPrimaryClip(ClipData.newPlainText("server", detailAddress));
                Toast.makeText(this, R.string.address_copied, Toast.LENGTH_SHORT).show();
            }
        });
    }

    private void initServerList() {
        serverModel = new ArrayList<>();

        RecyclerView recyclerView = findViewById(R.id.servers_list);
        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        recyclerView.setHasFixedSize(true);
        // ping results land row by row; the default change animation would make every row blink
        if (recyclerView.getItemAnimator() instanceof SimpleItemAnimator) {
            ((SimpleItemAnimator) recyclerView.getItemAnimator()).setSupportsChangeAnimations(false);
        }

        serverAdapter = new ServerAdapter(this, serverModel, ServerAdapter.MODE_FAVORITES);
        serverAdapter.setOnServerSelectListener(this::onServerSelected);
        serverAdapter.setOnListLoadedListener(() -> {
            swipeRefreshLayout.setRefreshing(false);
            listLoading = false;
            updateListMeta();
        });
        serverAdapter.registerAdapterDataObserver(new RecyclerView.AdapterDataObserver() {
            @Override
            public void onChanged() {
                updateListMeta();
            }

            @Override
            public void onItemRangeInserted(int positionStart, int itemCount) {
                updateListMeta();
            }

            @Override
            public void onItemRangeRemoved(int positionStart, int itemCount) {
                updateListMeta();
            }
        });
        recyclerView.setAdapter(serverAdapter);

        serverAdapter.refresh();
        updateListMeta();
    }

    private void updateListMeta() {
        int count = serverAdapter.getItemCount();
        listCount.setText(getString(R.string.servers_count, count));
        if (count > 0) {
            listEmpty.setVisibility(View.GONE);
            return;
        }
        int message;
        if (listLoading) {
            message = R.string.loading;
        } else if (searchEditText.getText().length() > 0) {
            message = R.string.servers_empty_search;
        } else if (currentTab == ServerAdapter.MODE_FAVORITES) {
            message = R.string.servers_empty_favorites;
        } else if (currentTab == ServerAdapter.MODE_RECENT) {
            message = R.string.servers_empty_recent;
        } else {
            message = R.string.servers_empty_internet;
        }
        listEmpty.setText(message);
        listEmpty.setVisibility(View.VISIBLE);
    }

    private void initNavigation() {
        GtaTabStrip serverTabs = findViewById(R.id.server_tabs);
        serverTabs.setTabs(getString(R.string.tab_favorites), getString(R.string.tab_internet), getString(R.string.tab_recent));
        serverTabs.setOnTabSelectedListener((index, reselected) -> selectTab(SERVER_TAB_MODES[index]));

        pageViews = new View[]{
                findViewById(R.id.page_servers),
                findViewById(R.id.page_mods),
                findViewById(R.id.page_settings)
        };
        navItems = new ViewGroup[]{
                findViewById(R.id.nav_servers),
                findViewById(R.id.nav_mods),
                findViewById(R.id.nav_settings)
        };
        navIndicator = findViewById(R.id.nav_indicator);
        for (int i = 0; i < navItems.length; i++) {
            final int page = i;
            navItems[i].setOnClickListener(v -> showPage(page));
        }

        findViewById(R.id.nav_offline).setOnClickListener(v -> showOfflineDialog());
        findViewById(R.id.nav_exit).setOnClickListener(v -> quitApp());

        updateNavStyles(false);
    }

    private void selectTab(int mode) {
        if (mode == currentTab) {
            refreshServers();
            return;
        }
        currentTab = mode;
        listLoading = true;
        serverAdapter.selectMode(mode);
        updateDeleteVisibility();
    }

    private void updateNavStyles(boolean animate) {
        int accentColor = LauncherThemeManager.getAccentColor(this);
        int dimColor = getColor(R.color.launcher_text_dim);

        for (int i = 0; i < navItems.length; i++) {
            int color = i == currentPage ? accentColor : dimColor;
            ((ImageView) navItems[i].getChildAt(0)).setImageTintList(ColorStateList.valueOf(color));
            ((TextView) navItems[i].getChildAt(1)).setTextColor(color);
        }

        View target = navItems[currentPage];
        if (!target.isLaidOut()) {
            target.post(() -> updateNavStyles(false));
            return;
        }
        navIndicator.setVisibility(View.VISIBLE);
        navIndicator.animate().cancel();
        if (animate) {
            navIndicator.animate().translationY(target.getTop()).setDuration(240)
                    .setInterpolator(new DecelerateInterpolator(1.8f)).start();
        } else {
            navIndicator.setTranslationY(target.getTop());
        }
    }

    private Fragment ensurePageFragment(int page) {
        String tag = page == PAGE_MODS ? TAG_MODS : TAG_SETTINGS;
        FragmentManager fm = getSupportFragmentManager();
        Fragment frag = fm.findFragmentByTag(tag);
        if (frag == null && !fm.isStateSaved()) {
            frag = page == PAGE_MODS ? new ModsManagerFragment() : new SettingsPageFragment();
            if (page == PAGE_SETTINGS && settingsBuilder != null) {
                ((SettingsPageFragment) frag).setPrebuiltView(settingsBuilder.finish());
                settingsBuilder = null;
            }
            fm.beginTransaction()
                    .setReorderingAllowed(true)
                    .add(page == PAGE_MODS ? R.id.page_mods : R.id.page_settings, frag, tag)
                    .commitNowAllowingStateLoss();
        }
        return frag;
    }

    private void showPage(int page) {
        if (page == currentPage) return;

        if (page != PAGE_SERVERS) {
            Fragment frag = ensurePageFragment(page);
            if (frag instanceof SettingsPageFragment) {
                ((SettingsPageFragment) frag).onPageShown();
            }
        }

        View current = getCurrentFocus();
        if (current != null) {
            current.clearFocus();
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(current.getWindowToken(), 0);
        }

        View out = pageViews[currentPage];
        View in = pageViews[page];
        currentPage = page;

        out.animate().cancel();
        out.setVisibility(View.GONE);

        in.animate().cancel();
        in.setAlpha(0f);
        in.setTranslationY(10f * getResources().getDisplayMetrics().density);
        in.setVisibility(View.VISIBLE);
        in.animate().alpha(1f).translationY(0f).setDuration(200)
                .setInterpolator(new DecelerateInterpolator(1.6f)).start();

        updateNavStyles(true);
    }

    @Override
    public void onThemeColorChanged(int accentColor) {
        LauncherThemeManager.applyThemeToTree(getWindow().getDecorView(), accentColor);
        if (swipeRefreshLayout != null) {
            swipeRefreshLayout.setColorSchemeColors(accentColor);
        }
        if (navIndicator != null) {
            navIndicator.setBackground(LauncherThemeManager.createNavIndicatorDrawable(this, accentColor));
            updateNavStyles(false);
        }
        View btnJoin = findViewById(R.id.btn_join);
        if (btnJoin != null) {
            btnJoin.setBackgroundTintList(ColorStateList.valueOf(accentColor));
        }
        if (serverAdapter != null) {
            serverAdapter.notifyDataSetChanged();
            if (serverAdapter.getSelected() != null) {
                updateFavoriteButton(serverAdapter.getSelected());
            }
        }
    }

    private void initButtons() {
        findViewById(R.id.btn_join).setOnClickListener(v -> connectToSelectedServer());
        findViewById(R.id.btn_add).setOnClickListener(v -> showAddServerDialog());
        findViewById(R.id.btn_refresh).setOnClickListener(v -> refreshServers());
        findViewById(R.id.btn_delete).setOnClickListener(v -> deleteSelectedServer());
    }

    public void refreshServers() {
        swipeRefreshLayout.setRefreshing(true);
        listLoading = true;
        serverAdapter.refresh();
    }

    private void updateDeleteVisibility() {
        boolean deletable = serverAdapter.getSelected() != null
                && currentTab == ServerAdapter.MODE_RECENT;
        btnDelete.setVisibility(deletable ? View.VISIBLE : View.GONE);
    }

    private void deleteSelectedServer() {
        if (serverAdapter.deleteSelected()) {
            Toast.makeText(this, R.string.server_removed, Toast.LENGTH_SHORT).show();
        } else {
            Toast.makeText(this, R.string.delete_not_supported, Toast.LENGTH_SHORT).show();
        }
        updateDeleteVisibility();
    }

    private void onServerSelected(ServerModel server) {
        int gen = ++detailsGeneration;
        updateDeleteVisibility();

        if (server == null) {
            detailAddress = null;
            infoEmpty.setVisibility(View.VISIBLE);
            infoBody.setVisibility(View.GONE);
            playersData.clear();
            playersAdapter.notifyDataSetChanged();
            rulesData.clear();
            rulesAdapter.notifyDataSetChanged();
            return;
        }

        // the same server is re-announced whenever its query refreshes: keep the lists on screen then
        boolean sameServer = server.address().equals(detailAddress);
        detailAddress = server.address();

        infoEmpty.setVisibility(View.GONE);
        infoBody.setVisibility(View.VISIBLE);
        infoServerName.setText(server.server_name);
        infoAddress.setText(server.address());
        infoLock.setVisibility("1".equals(server.server_password) ? View.VISIBLE : View.GONE);

        String ping = server.server_ping;
        boolean hasPing = !("-".equals(ping) || ping.isEmpty());
        statPlayers.setText(server.server_players + "/" + server.server_maxplayers);
        statPing.setText(hasPing ? ping + " ms" : "—");
        statPing.setTextColor(hasPing ? ServerAdapter.pingColor(this, ping) : getColor(R.color.launcher_text_dim));
        String lang = ServerAdapter.meaningful(server.server_language);
        statLang.setText(lang.isEmpty() ? "—" : lang);

        updateFavoriteButton(server);
        btnFavorite.setOnClickListener(v -> toggleFavorite(server));

        if (!sameServer) {
            String website = sanitizeUrl(server.server_website);
            infoServerMeta.setText(website);
            infoServerMeta.setVisibility(website.isEmpty() ? View.GONE : View.VISIBLE);
            infoServerMeta.setOnClickListener(null);

            playersData.clear();
            playersAdapter.notifyDataSetChanged();
            rulesData.clear();
            rulesAdapter.notifyDataSetChanged();
            playersList.setVisibility(View.GONE);
            playersEmpty.setVisibility(View.VISIBLE);
            playersEmpty.setText(R.string.loading);
            rulesHeader.setVisibility(View.GONE);
            rulesList.setVisibility(View.GONE);
            updateLinkButtons(server, "", "");

            infoBody.animate().cancel();
            infoBody.setAlpha(0f);
            infoBody.animate().alpha(1f).setDuration(180).start();
        }

        // fetch player list & rules in background
        String address = server.address();
        String charset = ClientSettings.getCharsetForLauncher(this);
        detailExecutor.execute(() -> {
            List<String[]> players = new ArrayList<>();
            List<String[]> rules = new ArrayList<>();
            SampQuery query = null;
            try {
                query = new SampQuery(server.server_host, Integer.parseInt(server.server_port), charset);
                players = query.getPlayers();
                rules = query.getRules();
            } catch (Exception e) {
                e.printStackTrace();
            } finally {
                if (query != null) {
                    query.close();
                }
            }

            String weburl = "", discordFromRules = "";
            List<String[]> visibleRules = new ArrayList<>();
            for (String[] rule : rules) {
                String key = rule[0].toLowerCase();
                if ((key.equals("weburl") || key.equals("url") || key.equals("website")) && weburl.isEmpty()) {
                    weburl = rule[1];
                }
                if (key.contains("discord") && discordFromRules.isEmpty()) {
                    discordFromRules = rule[1];
                }
                if (!sanitizeUrl(rule[1]).isEmpty()) {
                    visibleRules.add(rule);
                }
            }

            List<String[]> finalPlayers = players;
            List<String[]> finalRules = visibleRules;
            String finalWeburl = weburl;
            String finalDiscord = discordFromRules;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                if (gen != detailsGeneration || !address.equals(serverAdapter.getSelected() != null ? serverAdapter.getSelected().address() : null)) {
                    return;
                }
                playersData.clear();
                playersData.addAll(finalPlayers);
                playersAdapter.notifyDataSetChanged();
                playersList.setVisibility(finalPlayers.isEmpty() ? View.GONE : View.VISIBLE);
                playersEmpty.setVisibility(finalPlayers.isEmpty() ? View.VISIBLE : View.GONE);
                playersEmpty.setText(R.string.no_players);

                rulesData.clear();
                rulesData.addAll(finalRules);
                rulesAdapter.notifyDataSetChanged();
                rulesHeader.setVisibility(finalRules.isEmpty() ? View.GONE : View.VISIBLE);
                rulesList.setVisibility(finalRules.isEmpty() ? View.GONE : View.VISIBLE);

                updateLinkButtons(server, finalWeburl, finalDiscord);
            });
        });
    }

    private void updateLinkButtons(ServerModel server, String weburl, String discordFromRules) {
        String website = sanitizeUrl(!server.server_website.isEmpty() ? server.server_website : weburl);
        String discord = sanitizeUrl(!server.server_discord.isEmpty() ? server.server_discord : discordFromRules);

        if (!website.isEmpty()) {
            infoServerMeta.setText(website);
            infoServerMeta.setVisibility(View.VISIBLE);
            infoServerMeta.setOnClickListener(v -> openLink(website));
        } else {
            infoServerMeta.setVisibility(View.GONE);
        }

        if (discord.isEmpty()) {
            linksContainer.setVisibility(View.GONE);
            return;
        }
        linksContainer.setVisibility(View.VISIBLE);
        discordRow.setVisibility(View.VISIBLE);
        discordText.setText(discord);
        discordRow.setOnClickListener(v -> openLink(discord));
    }

    private static String sanitizeUrl(String url) {
        if (url == null) return "";
        String trimmed = url.trim();
        return trimmed.equalsIgnoreCase("null") || trimmed.length() < 4 ? "" : trimmed;
    }

    private void updateFavoriteButton(ServerModel server) {
        boolean fav = Utils.isServerInFavorites(this, server.address());
        btnFavorite.setColorFilter(fav ? LauncherThemeManager.getAccentColor(this) : getColor(R.color.launcher_text_dim));
        btnFavorite.setContentDescription(getString(fav ? R.string.remove_favorite : R.string.add_to_favorites));
    }

    private void toggleFavorite(ServerModel server) {
        String address = server.address();
        if (Utils.isServerInFavorites(this, address)) {
            try {
                Utils.deleteServerFromFavorites(this, address);
                Toast.makeText(this, R.string.removed_from_favorites, Toast.LENGTH_LONG).show();
            } catch (Exception e) {
                e.printStackTrace();
            }
        } else if (Utils.addServerToFavorites(this, address)) {
            Toast.makeText(this, R.string.server_added, Toast.LENGTH_LONG).show();
        }
        updateFavoriteButton(server);
        if (currentTab == ServerAdapter.MODE_FAVORITES) {
            refreshServers();
        }
    }

    private void openLink(String url) {
        try {
            if (!url.startsWith("http://") && !url.startsWith("https://")) {
                url = "https://" + url;
            }
            startActivity(new Intent(Intent.ACTION_VIEW, android.net.Uri.parse(url)));
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private void connectToSelectedServer() {
        ServerModel server = serverAdapter.getSelected();
        if (server == null) {
            Toast.makeText(this, R.string.no_server_selected, Toast.LENGTH_LONG).show();
            return;
        }

        if (!Utils.isOnline(this)) {
            Toast.makeText(this, R.string.no_internet, Toast.LENGTH_LONG).show();
            return;
        }

        showJoinDialog(server);
    }

    private final LauncherServerDialogs.Host serverDialogHost = new LauncherServerDialogs.Host() {
        @Override
        public boolean ensureGameDataOrPrompt() {
            return MainActivity.this.ensureGameDataOrPrompt();
        }

        @Override
        public void launchGameProcess() {
            MainActivity.this.launchGameProcess();
        }

        @Override
        public void onFavoriteServerAdded() {
            if (currentTab == ServerAdapter.MODE_FAVORITES) {
                refreshServers();
            }
        }
    };

    private void showJoinDialog(ServerModel server) {
        LauncherServerDialogs.showJoinDialog(this, server, serverDialogHost);
    }

    private void showAddServerDialog() {
        LauncherServerDialogs.showAddServerDialog(this, serverDialogHost);
    }

    private void showOfflineDialog() {
        LauncherServerDialogs.showOfflineDialog(this, serverDialogHost);
    }

    private void launchGameProcess() {
        try {
            android.app.ActivityManager am = (android.app.ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
            if (am != null) {
                String targetProc = getPackageName() + ":game";
                java.util.List<android.app.ActivityManager.RunningAppProcessInfo> procs = am.getRunningAppProcesses();
                boolean killed = false;
                if (procs != null) {
                    for (android.app.ActivityManager.RunningAppProcessInfo info : procs) {
                        if (targetProc.equals(info.processName) && info.pid != android.os.Process.myPid()) {
                            // The launcher is the intentional killer here (e.g. user
                            // backgrounded the game and rejoins) — clear the session-alive
                            // marker first so the next session doesn't misread this as a crash.
                            try {
                                java.io.File extDir = getExternalFilesDir(null);
                                if (extDir != null) {
                                    new java.io.File(extDir, "logcat/session_alive").delete();
                                }
                            } catch (Throwable ignored) {
                            }
                            android.os.Process.killProcess(info.pid);
                            killed = true;
                        }
                    }
                }
                if (killed) {
                    // Wait briefly for AMS to process AppDeathRecipient so startActivity spawns a fresh :game process.
                    for (int attempt = 0; attempt < 20; ++attempt) {
                        boolean stillAlive = false;
                        java.util.List<android.app.ActivityManager.RunningAppProcessInfo> cur = am.getRunningAppProcesses();
                        if (cur != null) {
                            for (android.app.ActivityManager.RunningAppProcessInfo info : cur) {
                                if (targetProc.equals(info.processName)) {
                                    stillAlive = true;
                                    break;
                                }
                            }
                        }
                        if (!stillAlive) break;
                        try {
                            Thread.sleep(15);
                        } catch (InterruptedException ignored) {
                            Thread.currentThread().interrupt();
                            break;
                        }
                    }
                }
            }

            Intent intent = new Intent(this, com.sampmobile.game.SAMP.class);
            intent.putExtra("extra_check", "sampmobile1337");
            startActivity(intent);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        outState.putInt(STATE_CURRENT_PAGE, currentPage);
        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onResume() {
        super.onResume();
        ClientSettings.reloadLauncher(this);
        if (currentPage == PAGE_SETTINGS) {
            Fragment f = getSupportFragmentManager().findFragmentByTag(TAG_SETTINGS);
            if (f instanceof SettingsPageFragment) {
                ((SettingsPageFragment) f).onPageShown();
            }
        }
    }

    @Override
    protected void onDestroy() {
        LauncherThemeManager.removeListener(this);
        detailExecutor.shutdownNow();
        if (serverAdapter != null) serverAdapter.shutdown();
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        if (currentPage != PAGE_SERVERS) {
            showPage(PAGE_SERVERS);
            return;
        }
        quitApp();
    }

    private long quit_time = 0;

    public void quitApp() {
        if ((System.currentTimeMillis() - quit_time) > 2000) {
            Toast.makeText(this, R.string.press_again_quit, Toast.LENGTH_LONG).show();
            quit_time = System.currentTimeMillis();
        } else {
            finish();
        }
    }
}
