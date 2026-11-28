package com.sampmobile.launcher.ui.widget;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListAdapter;
import android.widget.ListPopupWindow;
import android.widget.TextView;

import androidx.annotation.NonNull;

import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.LauncherThemeManager;

import java.util.ArrayList;
import java.util.List;

/**
 * GTA menu style value switcher: "‹ value ›". The arrows step through the options,
 * tapping the value opens the full list. Mirrors the small slice of the
 * exposed-dropdown API the settings page uses, so bindings stay untouched.
 */
public class GtaOptionView extends LinearLayout {

    private static final int MAX_VISIBLE_ROWS = 4;

    private final ImageView prev;
    private final ImageView next;
    private final TextView label;
    private final List<String> items = new ArrayList<>();
    private int index = -1;
    private int accentColor;
    private AdapterView.OnItemClickListener itemListener;

    public GtaOptionView(Context context) {
        this(context, null);
    }

    public GtaOptionView(Context context, AttributeSet attrs) {
        super(context, attrs);
        setOrientation(HORIZONTAL);
        setGravity(Gravity.CENTER_VERTICAL);
        setBackgroundResource(R.drawable.launcher_input_bg);
        accentColor = LauncherThemeManager.getAccentColor(context);

        prev = makeArrow(R.drawable.ic_chevron_left);
        next = makeArrow(R.drawable.ic_chevron_right);

        label = new TextView(context);
        label.setGravity(Gravity.CENTER);
        label.setSingleLine(true);
        label.setEllipsize(android.text.TextUtils.TruncateAt.END);
        label.setTextColor(context.getColor(R.color.launcher_text));
        label.setTextSize(TypedValue.COMPLEX_UNIT_PX, getResources().getDimension(com.intuit.ssp.R.dimen._8ssp));
        label.setTypeface(Typeface.DEFAULT_BOLD);

        addView(prev);
        addView(label, new LayoutParams(0, LayoutParams.MATCH_PARENT, 1f));
        addView(next);

        prev.setOnClickListener(v -> step(-1));
        next.setOnClickListener(v -> step(1));
        setOnClickListener(v -> showDropDown());
        updateArrows();
    }

    private ImageView makeArrow(int iconRes) {
        ImageView iv = new ImageView(getContext());
        int size = getResources().getDimensionPixelSize(com.intuit.sdp.R.dimen._24sdp);
        int pad = getResources().getDimensionPixelSize(com.intuit.sdp.R.dimen._5sdp);
        iv.setLayoutParams(new LayoutParams(size, LayoutParams.MATCH_PARENT));
        iv.setPadding(pad, 0, pad, 0);
        iv.setImageResource(iconRes);
        iv.setBackgroundResource(R.drawable.ui_row_bg);
        iv.setImageTintList(ColorStateList.valueOf(accentColor));
        return iv;
    }

    public void setAccentColor(int color) {
        accentColor = color;
        prev.setImageTintList(ColorStateList.valueOf(color));
        next.setImageTintList(ColorStateList.valueOf(color));
    }

    public void setAdapter(ListAdapter adapter) {
        items.clear();
        if (adapter != null) {
            for (int i = 0; i < adapter.getCount(); i++) {
                items.add(String.valueOf(adapter.getItem(i)));
            }
        }
        index = items.indexOf(label.getText().toString());
        updateArrows();
    }

    public void setThreshold(int threshold) {
    }

    public void setText(CharSequence text, boolean filter) {
        label.setText(text);
        index = text == null ? -1 : items.indexOf(text.toString());
        updateArrows();
    }

    public CharSequence getText() {
        return label.getText();
    }

    public void setOnItemClickListener(AdapterView.OnItemClickListener l) {
        itemListener = l;
    }

    public void showDropDown() {
        if (items.isEmpty()) return;
        Context ctx = getContext();
        ListPopupWindow popup = new ListPopupWindow(ctx);
        popup.setAnchorView(this);
        popup.setModal(true);
        popup.setWidth(Math.max(getWidth(), getResources().getDimensionPixelSize(com.intuit.sdp.R.dimen._120sdp)));
        popup.setBackgroundDrawable(ctx.getDrawable(R.drawable.ui_popup_bg));
        popup.setVerticalOffset(getResources().getDimensionPixelSize(com.intuit.sdp.R.dimen._3sdp));
        if (items.size() > MAX_VISIBLE_ROWS) {
            // long lists scroll inside a compact sheet instead of running off the landscape screen
            int rowHeight = 2 * getResources().getDimensionPixelSize(com.intuit.sdp.R.dimen._7sdp)
                    + Math.round(getResources().getDimension(com.intuit.ssp.R.dimen._9ssp) * 1.35f);
            popup.setHeight(rowHeight * MAX_VISIBLE_ROWS + rowHeight / 2);
        }
        popup.setAdapter(new ArrayAdapter<String>(ctx, R.layout.layout_setting_version, items) {
            @NonNull
            @Override
            public View getView(int position, View convertView, @NonNull ViewGroup parent) {
                TextView tv = (TextView) super.getView(position, convertView, parent);
                boolean current = position == index;
                tv.setTextColor(current ? accentColor : ctx.getColor(R.color.launcher_text));
                tv.setTypeface(current ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
                return tv;
            }
        });
        popup.setOnItemClickListener((parent, view, position, id) -> {
            popup.dismiss();
            if (position != index) choose(position, 0);
        });
        popup.show();
        if (index >= 0) popup.setSelection(index);
    }

    private void step(int dir) {
        int target = index + dir;
        if (target < 0 || target >= items.size()) return;
        choose(target, dir);
    }

    private void choose(int target, int dir) {
        index = target;
        label.setText(items.get(target));
        updateArrows();
        float shift = 10f * getResources().getDisplayMetrics().density * dir;
        label.animate().cancel();
        label.setTranslationX(shift);
        label.setAlpha(0.25f);
        label.animate().translationX(0f).alpha(1f).setDuration(150).start();
        if (itemListener != null) itemListener.onItemClick(null, this, target, target);
    }

    private void updateArrows() {
        boolean canPrev = index > 0;
        boolean canNext = index < items.size() - 1 && !items.isEmpty();
        prev.setAlpha(canPrev ? 1f : 0.25f);
        next.setAlpha(canNext ? 1f : 0.25f);
    }
}
