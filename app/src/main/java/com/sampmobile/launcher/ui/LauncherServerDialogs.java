package com.sampmobile.launcher.ui;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;

import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.LauncherThemeManager;
import com.sampmobile.launcher.utils.ServerModel;
import com.sampmobile.launcher.utils.Utils;

import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Join Server, Add Server, and Offline Mode dialogs for {@link com.sampmobile.launcher.MainActivity}.
 */
public final class LauncherServerDialogs {
    public interface Host {
        boolean ensureGameDataOrPrompt();
        void launchGameProcess();
        void onFavoriteServerAdded();
    }

    private LauncherServerDialogs() {}

    public static void showJoinDialog(Activity activity, ServerModel server, Host host) {
        View v = activity.getLayoutInflater().inflate(R.layout.layout_join_dialog, null);

        AlertDialog dialog = new AlertDialog.Builder(activity).setView(v).create();
        Objects.requireNonNull(dialog.getWindow()).setBackgroundDrawableResource(android.R.color.transparent);

        ClientSettings settings = ClientSettings.loadLauncher(activity);

        TextView serverName = v.findViewById(R.id.join_server_name);
        serverName.setText(server.server_name);

        EditText nickname = v.findViewById(R.id.join_nickname);
        nickname.setText(settings.getString("nick_name"));
        nickname.setSelection(Objects.requireNonNull(nickname.getText()).length());

        TextView[] versionButtons = {
                v.findViewById(R.id.join_version_0),
                v.findViewById(R.id.join_version_1),
                v.findViewById(R.id.join_version_2)};
        final String[] versionIds = {
                ClientSettings.SAMP_VER_037,
                ClientSettings.SAMP_VER_037_R5,
                ClientSettings.SAMP_VER_03DL_R1};
        final int[] version = {0};
        String savedVersion = settings.getString("samp_version");
        for (int i = 0; i < versionIds.length; i++) {
            if (versionIds[i].equalsIgnoreCase(savedVersion)) version[0] = i;
        }
        int accentColor = LauncherThemeManager.getAccentColor(activity);
        LauncherThemeManager.applyThemeToTree(v, accentColor);
        View joinConnectBtn = v.findViewById(R.id.join_connect);
        if (joinConnectBtn != null) {
            joinConnectBtn.setBackgroundTintList(ColorStateList.valueOf(accentColor));
        }

        Runnable applyVersionStyles = () -> {
            for (int i = 0; i < versionButtons.length; i++) {
                boolean selected = version[0] == i;
                versionButtons[i].setBackgroundResource(
                        selected ? R.drawable.launcher_btn_primary : R.drawable.launcher_btn_outline);
                versionButtons[i].setBackgroundTintList(
                        selected ? ColorStateList.valueOf(accentColor) : null);
                versionButtons[i].setTextColor(activity.getColor(
                        selected ? R.color.launcher_on_accent : R.color.launcher_text_dim));
            }
        };
        applyVersionStyles.run();
        for (int i = 0; i < versionButtons.length; i++) {
            final int index = i;
            versionButtons[i].setOnClickListener(view -> {
                version[0] = index;
                applyVersionStyles.run();
            });
        }

        boolean hasPassword = "1".equals(server.server_password);
        EditText password = v.findViewById(R.id.join_password);
        password.setVisibility(hasPassword ? View.VISIBLE : View.GONE);

        v.findViewById(R.id.join_cancel).setOnClickListener(view -> dialog.dismiss());

        v.findViewById(R.id.join_connect).setOnClickListener(view -> {
            String nick = Objects.requireNonNull(nickname.getText()).toString().trim();
            if (nick.isEmpty()) {
                Toast.makeText(activity, R.string.set_nickname, Toast.LENGTH_LONG).show();
                return;
            }
            if (nick.length() > 24) {
                Toast.makeText(activity, R.string.nickname_too_long, Toast.LENGTH_LONG).show();
                return;
            }

            int port = 7777;
            try {
                port = Integer.parseInt(server.server_port);
            } catch (NumberFormatException ignored) {
            }
            String pass = hasPassword ? Objects.requireNonNull(password.getText()).toString() : "";
            ClientSettings.prepareOnlineLaunch(activity, server.server_host, port, pass, nick, versionIds[version[0]]);
            Utils.addServerToRecent(activity, server.address());

            dialog.dismiss();
            if (!host.ensureGameDataOrPrompt()) {
                return;
            }
            host.launchGameProcess();
        });

        if (!activity.isFinishing() && !activity.isDestroyed()) {
            showFormDialog(activity, dialog);
        }
    }

    public static void showAddServerDialog(Activity activity, Host host) {
        View v = activity.getLayoutInflater().inflate(R.layout.layout_server_add, null);

        AlertDialog dialog = new AlertDialog.Builder(activity).setView(v).create();
        Objects.requireNonNull(dialog.getWindow()).setBackgroundDrawableResource(android.R.color.transparent);

        EditText address = v.findViewById(R.id.address_input_text);
        TextView addButton = v.findViewById(R.id.add_server);
        int accentColor = LauncherThemeManager.getAccentColor(activity);
        LauncherThemeManager.applyThemeToTree(v, accentColor);
        if (addButton != null) {
            addButton.setBackgroundTintList(ColorStateList.valueOf(accentColor));
        }

        address.setOnEditorActionListener((textView, i, keyEvent) ->
                i == EditorInfo.IME_ACTION_DONE || i == EditorInfo.IME_ACTION_NEXT || i == EditorInfo.IME_ACTION_UNSPECIFIED);

        addButton.setOnClickListener(view -> {
            address.setText(Objects.requireNonNull(address.getText()).toString().replace("\n", ""));

            Pattern pattern = Pattern.compile("^(((?!-)[A-Za-z0-9-]{1,63}(?<!-)\\.)+[A-Za-z]{2,6}|localhost|(([0-9]{1,3}\\.){3})[0-9]{1,3}):[0-9]{1,5}$");
            Pattern pattern2 = Pattern.compile("^(((?!-)[A-Za-z0-9-]{1,63}(?<!-)\\.)+[A-Za-z]{2,6}|localhost|(([0-9]{1,3}\\.){3})[0-9]{1,3})$");

            Matcher matcher = pattern.matcher(address.getText().toString());
            Matcher matcher2 = pattern2.matcher(address.getText().toString());

            if (matcher.find() || matcher2.find()) {
                if (!address.getText().toString().contains(":")) {
                    address.setText(address.getText().toString() + ":7777");
                }
                try {
                    if (Utils.addServerToFavorites(activity, address.getText().toString())) {
                        Toast.makeText(activity, R.string.server_added, Toast.LENGTH_LONG).show();
                        dialog.dismiss();
                        host.onFavoriteServerAdded();
                    } else {
                        Toast.makeText(activity, R.string.server_already_exists, Toast.LENGTH_LONG).show();
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                }
            } else {
                Toast.makeText(activity, R.string.invalid_host, Toast.LENGTH_LONG).show();
            }
        });

        if (!activity.isFinishing() && !activity.isDestroyed()) {
            showFormDialog(activity, dialog);
        }
    }

    public static void showOfflineDialog(Activity activity, Host host) {
        if (activity.isFinishing() || activity.isDestroyed()) return;
        new AlertDialog.Builder(activity)
                .setTitle(R.string.offline_mode)
                .setMessage(R.string.offline_mode_confirm)
                .setPositiveButton(R.string.yes, (dialog, which) -> {
                    ClientSettings.prepareOfflineLaunch(activity);
                    if (!host.ensureGameDataOrPrompt()) {
                        return;
                    }
                    host.launchGameProcess();
                })
                .setNegativeButton(R.string.no, null)
                .show();
    }

    /** Form dialogs keep a compact fixed width instead of the alert default (65% of the screen). */
    private static void showFormDialog(Activity activity, AlertDialog dialog) {
        dialog.show();
        if (dialog.getWindow() != null) {
            dialog.getWindow().setLayout(
                    activity.getResources().getDimensionPixelSize(com.intuit.sdp.R.dimen._300sdp),
                    ViewGroup.LayoutParams.WRAP_CONTENT);
        }
    }
}
