package com.sampmobile.launcher.ui.fragment;


import com.sampmobile.launcher.utils.I18n;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.graphics.BitmapFactory;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.FragmentActivity;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.bumptech.glide.Glide;
import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.LauncherThemeManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Manages the Screenshots gallery tab inside {@link SettingsPageFragment}.
 */
final class SettingsScreenshotController {
    private final SettingsPageFragment host;
    private final List<File> screenshotFiles = new ArrayList<>();
    private ScreenshotAdapter screenshotAdapter;
    private TextView tvSummary;
    private TextView tvEmpty;

    SettingsScreenshotController(SettingsPageFragment host) {
        this.host = host;
    }

    void bindViews(View root) {
        tvSummary = root.findViewById(R.id.tv_screenshots_summary);
        tvEmpty = root.findViewById(R.id.tv_screenshots_empty);
        RecyclerView rv = root.findViewById(R.id.rv_screenshots);
        rv.setLayoutManager(new GridLayoutManager(host.requireContext(), 2));
        screenshotAdapter = new ScreenshotAdapter();
        rv.setAdapter(screenshotAdapter);
    }

    void notifyThemeChanged() {
        if (screenshotAdapter != null) {
            screenshotAdapter.notifyDataSetChanged();
        }
    }

    void refresh() {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.ioExecutor.execute(() -> {
            File extDir = appCtx.getExternalFilesDir(null);
            if (extDir == null) return;
            File dir = new File(extDir, "SAMP/screens");
            if (!dir.exists()) dir.mkdirs();
            File[] files = dir.listFiles((d, name) -> {
                String lower = name.toLowerCase(Locale.US);
                return lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg");
            });

            List<File> list = new ArrayList<>();
            long totalBytes = 0;
            if (files != null) {
                Arrays.sort(files, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
                for (File f : files) {
                    list.add(f);
                    totalBytes += f.length();
                }
            }
            final long finalBytes = totalBytes;
            FragmentActivity act = host.getActivity();
            if (act != null && host.isAdded()) {
                act.runOnUiThread(() -> {
                    if (!host.isAdded() || host.getView() == null) return;
                    screenshotFiles.clear();
                    screenshotFiles.addAll(list);
                    screenshotAdapter.notifyDataSetChanged();
                    tvEmpty.setVisibility(list.isEmpty() ? View.VISIBLE : View.GONE);
                    tvSummary.setText(String.format(Locale.US,
                            I18n.tr("共 %d 张截图 (%.2f MB) · 点击图片可放大预览或保存到手机相册"),
                            list.size(), finalBytes / (1024.0 * 1024.0)));
                });
            }
        });
    }

    void confirmClear() {
        if (screenshotFiles.isEmpty()) {
            Toast.makeText(host.requireContext(), I18n.tr("截图库已经是空的"), Toast.LENGTH_SHORT).show();
            return;
        }
        host.showAlertDialog(I18n.tr("确定要清空截图库中的所有 ") + screenshotFiles.size() + I18n.tr(" 张截图吗？"), (d, w) -> {
            List<File> toDelete = new ArrayList<>(screenshotFiles);
            host.ioExecutor.execute(() -> {
                for (File f : toDelete) {
                    f.delete();
                }
                FragmentActivity act = host.getActivity();
                if (act != null && host.isAdded()) {
                    act.runOnUiThread(() -> {
                        if (!host.isAdded() || host.getContext() == null) return;
                        refresh();
                        Toast.makeText(host.getContext(), I18n.tr("已清空全部截图"), Toast.LENGTH_SHORT).show();
                    });
                }
            });
            d.dismiss();
        }, (d, w) -> d.dismiss());
    }

    private void previewScreenshot(File file) {
        Context ctx = host.requireContext();
        LinearLayout box = new LinearLayout(ctx);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(24, 20, 24, 12);
        box.setBackgroundResource(R.color.launcher_surface);

        ImageView iv = new ImageView(ctx);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                (int) (ctx.getResources().getDisplayMetrics().heightPixels * 0.62f));
        iv.setLayoutParams(lp);
        iv.setScaleType(ImageView.ScaleType.FIT_CENTER);
        Glide.with(host).load(file).into(iv);
        box.addView(iv);

        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(file.getAbsolutePath(), opts);

        TextView meta = new TextView(ctx);
        meta.setPadding(0, 12, 0, 4);
        meta.setGravity(Gravity.CENTER);
        meta.setTextColor(ctx.getResources().getColor(R.color.launcher_text_dim));
        meta.setTextSize(12);
        boolean isClean = file.getName().contains("_game");
        meta.setText(String.format(Locale.US, "%dx%d  ·  %.0f KB  ·  %s",
                opts.outWidth, opts.outHeight, file.length() / 1024.0,
                isClean ? I18n.tr("仅游戏画面") : I18n.tr("含界面与聊天")));
        box.addView(meta);

        new AlertDialog.Builder(ctx)
                .setView(box)
                .setPositiveButton(I18n.tr("保存到手机相册"), (d, w) -> saveScreenshotToSystemGallery(file))
                .setNeutralButton(I18n.tr("分享截图"), (d, w) -> host.shareFile(file, "image/png", I18n.tr("分享游戏截图")))
                .setNegativeButton(I18n.tr("关闭"), (d, w) -> d.dismiss())
                .show();
    }

    private void saveScreenshotToSystemGallery(File file) {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.ioExecutor.execute(() -> {
            try {
                ContentResolver resolver = appCtx.getContentResolver();
                ContentValues values = new ContentValues();
                values.put(MediaStore.Images.Media.DISPLAY_NAME, file.getName());
                values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    values.put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/SAMP-Mobile");
                    values.put(MediaStore.Images.Media.IS_PENDING, 1);
                }
                Uri uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
                if (uri == null) throw new Exception(I18n.tr("相册写入失败"));

                try (InputStream in = new FileInputStream(file);
                     OutputStream out = resolver.openOutputStream(uri)) {
                    if (out == null) throw new Exception(I18n.tr("相册输出流打开失败"));
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        out.write(buf, 0, n);
                    }
                }
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    values.clear();
                    values.put(MediaStore.Images.Media.IS_PENDING, 0);
                    resolver.update(uri, values, null, null);
                }
                FragmentActivity act = host.getActivity();
                if (act != null && host.isAdded()) {
                    act.runOnUiThread(() -> {
                        if (!host.isAdded() || host.getContext() == null) return;
                        Toast.makeText(host.getContext(), I18n.tr("已保存到手机相册"), Toast.LENGTH_LONG).show();
                    });
                }
            } catch (Exception e) {
                FragmentActivity act = host.getActivity();
                if (act != null && host.isAdded()) {
                    act.runOnUiThread(() -> {
                        if (!host.isAdded() || host.getContext() == null) return;
                        Toast.makeText(host.getContext(), I18n.tr("保存相册失败: ") + e.getMessage(), Toast.LENGTH_SHORT).show();
                    });
                }
            }
        });
    }

    private class ScreenshotAdapter extends RecyclerView.Adapter<ScreenshotAdapter.VH> {
        class VH extends RecyclerView.ViewHolder {
            ImageView ivThumb;
            TextView tvTitle, tvBadge, tvMeta, btnSave, btnShare, btnDelete;

            VH(View itemView, ImageView ivThumb, TextView tvTitle, TextView tvBadge,
               TextView tvMeta, TextView btnSave, TextView btnShare, TextView btnDelete) {
                super(itemView);
                this.ivThumb = ivThumb;
                this.tvTitle = tvTitle;
                this.tvBadge = tvBadge;
                this.tvMeta = tvMeta;
                this.btnSave = btnSave;
                this.btnShare = btnShare;
                this.btnDelete = btnDelete;
            }
        }

        @NonNull
        @Override
        public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            Context ctx = parent.getContext();
            float dp = ctx.getResources().getDisplayMetrics().density;

            LinearLayout card = new LinearLayout(ctx);
            card.setOrientation(LinearLayout.VERTICAL);
            card.setBackgroundResource(R.drawable.launcher_row_bg);
            int pad = (int) (8 * dp);
            card.setPadding(pad, pad, pad, pad);
            RecyclerView.LayoutParams lp = new RecyclerView.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            int m = (int) (4 * dp);
            lp.setMargins(m, m, m, m);
            card.setLayoutParams(lp);

            ImageView iv = new ImageView(ctx);
            iv.setLayoutParams(new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, (int) (105 * dp)));
            iv.setScaleType(ImageView.ScaleType.CENTER_CROP);
            iv.setBackgroundResource(R.drawable.launcher_img_clip);
            iv.setClipToOutline(true);
            card.addView(iv);

            LinearLayout titleRow = new LinearLayout(ctx);
            titleRow.setOrientation(LinearLayout.HORIZONTAL);
            titleRow.setGravity(Gravity.CENTER_VERTICAL);
            titleRow.setPadding(0, (int) (6 * dp), 0, (int) (2 * dp));

            TextView tvTitle = new TextView(ctx);
            tvTitle.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            tvTitle.setSingleLine(true);
            tvTitle.setTextColor(ctx.getResources().getColor(R.color.launcher_text));
            tvTitle.setTextSize(11);
            tvTitle.setTypeface(Typeface.DEFAULT_BOLD);
            titleRow.addView(tvTitle);

            TextView tvBadge = new TextView(ctx);
            tvBadge.setBackgroundResource(R.drawable.launcher_btn_outline);
            tvBadge.setPadding((int) (6 * dp), (int) (2 * dp), (int) (6 * dp), (int) (2 * dp));
            tvBadge.setTextColor(LauncherThemeManager.getAccentColor(ctx));
            tvBadge.setTextSize(9);
            tvBadge.setTypeface(Typeface.DEFAULT_BOLD);
            titleRow.addView(tvBadge);
            card.addView(titleRow);

            TextView tvMeta = new TextView(ctx);
            tvMeta.setTextColor(ctx.getResources().getColor(R.color.launcher_text_dim));
            tvMeta.setTextSize(10);
            card.addView(tvMeta);

            LinearLayout btnRow = new LinearLayout(ctx);
            btnRow.setOrientation(LinearLayout.HORIZONTAL);
            btnRow.setPadding(0, (int) (6 * dp), 0, 0);

            TextView btnSave = makeMiniButton(ctx, I18n.tr("存相册"), R.drawable.launcher_btn_outline, R.color.launcher_accent);
            TextView btnShare = makeMiniButton(ctx, I18n.tr("分享"), R.drawable.launcher_btn_outline, R.color.launcher_text);
            TextView btnDel = makeMiniButton(ctx, I18n.tr("删除"), R.drawable.launcher_btn_danger, R.color.launcher_danger);

            btnRow.addView(btnSave);
            btnRow.addView(btnShare);
            btnRow.addView(btnDel);
            card.addView(btnRow);

            return new VH(card, iv, tvTitle, tvBadge, tvMeta, btnSave, btnShare, btnDel);
        }

        private TextView makeMiniButton(Context ctx, String text, int bgRes, int colorRes) {
            float dp = ctx.getResources().getDisplayMetrics().density;
            TextView tv = new TextView(ctx);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, (int) (26 * dp), 1f);
            lp.setMargins((int) (2 * dp), 0, (int) (2 * dp), 0);
            tv.setLayoutParams(lp);
            tv.setGravity(Gravity.CENTER);
            tv.setBackgroundResource(bgRes);
            tv.setTextColor(ctx.getResources().getColor(colorRes));
            tv.setTextSize(10);
            tv.setTypeface(Typeface.DEFAULT_BOLD);
            tv.setText(text);
            return tv;
        }

        @Override
        public void onBindViewHolder(@NonNull VH h, int position) {
            File file = screenshotFiles.get(position);
            int accentColor = LauncherThemeManager.getAccentColor(h.itemView.getContext());
            Glide.with(host).load(file).centerCrop().into(h.ivThumb);
            String dateStr = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date(file.lastModified()));
            h.tvTitle.setText(dateStr);
            boolean isClean = file.getName().contains("_game");
            h.tvBadge.setText(isClean ? I18n.tr("仅游戏画面") : I18n.tr("含界面与聊天"));
            h.tvBadge.setTextColor(accentColor);
            h.btnSave.setTextColor(accentColor);
            h.tvMeta.setText(String.format(Locale.US, I18n.tr("文件大小：%.0f KB"), file.length() / 1024.0));

            h.itemView.setOnClickListener(v -> previewScreenshot(file));
            h.ivThumb.setOnClickListener(v -> previewScreenshot(file));
            h.btnSave.setOnClickListener(v -> saveScreenshotToSystemGallery(file));
            h.btnShare.setOnClickListener(v -> host.shareFile(file, "image/png", I18n.tr("分享游戏截图")));
            h.btnDelete.setOnClickListener(v -> {
                file.delete();
                refresh();
                Toast.makeText(host.requireContext(), I18n.tr("截图已删除"), Toast.LENGTH_SHORT).show();
            });
        }

        @Override
        public int getItemCount() {
            return screenshotFiles.size();
        }
    }
}
