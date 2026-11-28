package com.sampmobile.game.ui.widgets;

import android.app.Activity;
import android.view.View;

import androidx.constraintlayout.widget.ConstraintLayout;

import com.google.android.material.textview.MaterialTextView;
import com.joom.paranoid.Obfuscate;

import com.sampmobile.launcher.BuildConfig;
import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.Utils;

@Obfuscate
public class LoadingScreen {
    ConstraintLayout layout;

    public LoadingScreen(Activity activity) {
        layout = (ConstraintLayout) activity.getLayoutInflater().inflate(R.layout.activity_game_loadingscreen, null);
        activity.addContentView(layout, new ConstraintLayout.LayoutParams(-1, -1));

        MaterialTextView version = layout.findViewById(R.id.loading_version);
        version.setText("v" + BuildConfig.VERSION_NAME);

        MaterialTextView copyright = layout.findViewById(R.id.loading_copyright);
        copyright.setText(Utils.copyright);

        show(false);
    }

    public void show(boolean z) {
        layout.setVisibility(z ? View.VISIBLE : View.GONE);
    }
}
