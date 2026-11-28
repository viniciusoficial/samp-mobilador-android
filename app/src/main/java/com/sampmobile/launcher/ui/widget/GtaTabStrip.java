package com.sampmobile.launcher.ui.widget;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.animation.DecelerateInterpolator;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.LauncherThemeManager;

/**
 * Second-level tab row: text tabs with an accent underline that slides between them.
 * Scrolls horizontally when the tabs do not fit.
 */
public class GtaTabStrip extends HorizontalScrollView {

    public interface OnTabSelectedListener {
        void onTabSelected(int index, boolean reselected);
    }

    private final Strip strip;
    private int selected = -1;
    private int accentColor;
    private final int textColor;
    private final int dimColor;
    private OnTabSelectedListener listener;
    private ValueAnimator indicatorAnim;

    public GtaTabStrip(Context context) {
        this(context, null);
    }

    public GtaTabStrip(Context context, AttributeSet attrs) {
        super(context, attrs);
        setHorizontalScrollBarEnabled(false);
        setOverScrollMode(OVER_SCROLL_NEVER);
        setFillViewport(false);
        accentColor = LauncherThemeManager.getAccentColor(context);
        textColor = context.getColor(R.color.launcher_text);
        dimColor = context.getColor(R.color.launcher_text_dim);
        strip = new Strip(context);
        addView(strip, new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.MATCH_PARENT));
    }

    public void setOnTabSelectedListener(OnTabSelectedListener l) {
        this.listener = l;
    }

    public void setTabs(CharSequence... titles) {
        strip.removeAllViews();
        Context ctx = getContext();
        int padH = getResources().getDimensionPixelSize(com.intuit.sdp.R.dimen._9sdp);
        float textPx = getResources().getDimension(com.intuit.ssp.R.dimen._9ssp);
        for (int i = 0; i < titles.length; i++) {
            final int index = i;
            TextView tv = new TextView(ctx);
            tv.setText(titles[i]);
            tv.setTextSize(TypedValue.COMPLEX_UNIT_PX, textPx);
            tv.setTypeface(Typeface.DEFAULT_BOLD);
            tv.setLetterSpacing(0.03f);
            tv.setSingleLine(true);
            tv.setGravity(Gravity.CENTER);
            tv.setPadding(padH, 0, padH, 0);
            tv.setTextColor(dimColor);
            tv.setBackgroundResource(R.drawable.ui_row_bg);
            tv.setOnClickListener(v -> select(index, true, true));
            strip.addView(tv, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.MATCH_PARENT));
        }
        selected = -1;
        if (titles.length > 0) select(0, false, false);
    }

    public int getSelectedTab() {
        return selected;
    }

    public void setSelectedTab(int index, boolean animate) {
        select(index, animate, false);
    }

    public void setAccentColor(int color) {
        accentColor = color;
        strip.paint.setColor(color);
        strip.invalidate();
    }

    private void select(int index, boolean animate, boolean fromUser) {
        if (index < 0 || index >= strip.getChildCount()) return;
        boolean reselected = index == selected;
        if (!reselected) {
            selected = index;
            for (int i = 0; i < strip.getChildCount(); i++) {
                ((TextView) strip.getChildAt(i)).setTextColor(i == index ? textColor : dimColor);
            }
            moveIndicator(animate && strip.isLaidOut());
            View child = strip.getChildAt(index);
            if (isLaidOut()) {
                smoothScrollTo(child.getLeft() - (getWidth() - child.getWidth()) / 2, 0);
            }
        }
        if (fromUser && listener != null) listener.onTabSelected(index, reselected);
    }

    private void moveIndicator(boolean animate) {
        if (indicatorAnim != null) {
            indicatorAnim.cancel();
            indicatorAnim = null;
        }
        View child = strip.getChildAt(selected);
        if (child == null) return;
        final float toL = child.getLeft() + child.getPaddingLeft();
        final float toR = child.getRight() - child.getPaddingRight();
        if (!animate || strip.indRight <= strip.indLeft) {
            strip.indLeft = toL;
            strip.indRight = toR;
            strip.invalidate();
            return;
        }
        final float fromL = strip.indLeft;
        final float fromR = strip.indRight;
        indicatorAnim = ValueAnimator.ofFloat(0f, 1f);
        indicatorAnim.setDuration(220);
        indicatorAnim.setInterpolator(new DecelerateInterpolator(1.6f));
        indicatorAnim.addUpdateListener(a -> {
            float t = (float) a.getAnimatedValue();
            strip.indLeft = fromL + (toL - fromL) * t;
            strip.indRight = fromR + (toR - fromR) * t;
            strip.invalidate();
        });
        indicatorAnim.start();
    }

    private final class Strip extends LinearLayout {
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        final RectF rect = new RectF();
        float indLeft;
        float indRight;

        Strip(Context context) {
            super(context);
            setOrientation(HORIZONTAL);
            setWillNotDraw(false);
            paint.setColor(accentColor);
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            super.onLayout(changed, l, t, r, b);
            if (selected >= 0 && (indicatorAnim == null || !indicatorAnim.isRunning())) {
                View child = getChildAt(selected);
                if (child != null) {
                    indLeft = child.getLeft() + child.getPaddingLeft();
                    indRight = child.getRight() - child.getPaddingRight();
                }
            }
        }

        @Override
        protected void dispatchDraw(Canvas canvas) {
            super.dispatchDraw(canvas);
            if (indRight <= indLeft) return;
            float dp = getResources().getDisplayMetrics().density;
            float h = 2.5f * dp;
            rect.set(indLeft, getHeight() - h, indRight, getHeight());
            canvas.drawRoundRect(rect, h, h, paint);
        }
    }
}
