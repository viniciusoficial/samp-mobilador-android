package com.sampmobile.game;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.app.Activity;
import android.graphics.Color;
import android.graphics.Point;
import android.text.Layout;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.text.style.ForegroundColorSpan;
import android.view.Display;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.recyclerview.widget.RecyclerView;

import com.joom.paranoid.Obfuscate;

@Obfuscate
public class Utils {
    private static boolean isHexDigit(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    private static boolean isColorTagAt(String str, int idx) {
        if (idx + 7 >= str.length() || str.charAt(idx) != '{' || str.charAt(idx + 7) != '}') {
            return false;
        }
        for (int k = 1; k <= 6; k++) {
            if (!isHexDigit(str.charAt(idx + k))) {
                return false;
            }
        }
        return true;
    }

    public static CharSequence getColoredString(String str) {
        if (str == null || str.isEmpty()) {
            return "";
        }
        SpannableStringBuilder ssb = new SpannableStringBuilder();
        int len = str.length();
        int i = 0;
        int currentColor = 0;
        boolean hasColor = false;
        int spanStart = 0;

        while (i < len) {
            if (isColorTagAt(str, i)) {
                if (hasColor && ssb.length() > spanStart) {
                    ssb.setSpan(new ForegroundColorSpan(currentColor), spanStart, ssb.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
                }
                int rgb = Integer.parseInt(str.substring(i + 1, i + 7), 16);
                currentColor = 0xFF000000 | rgb;
                hasColor = true;
                spanStart = ssb.length();
                i += 8;
            } else {
                ssb.append(str.charAt(i));
                i++;
            }
        }
        if (hasColor && ssb.length() > spanStart) {
            ssb.setSpan(new ForegroundColorSpan(currentColor), spanStart, ssb.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        return ssb;
    }

    private static final float MULT_X = 5.2083336E-4f;
    private static final float MULT_Y = 9.259259E-4f;

    public static void scaleViewAndChildren(Activity activity, View view) {
        Display defaultDisplay = activity.getWindowManager().getDefaultDisplay();
        Point point = new Point();
        defaultDisplay.getSize(point);
        float min = Math.min(((float) point.x) * MULT_X, ((float) point.y) * MULT_Y);
        ViewGroup.LayoutParams layoutParams = view.getLayoutParams();
        if (!(layoutParams.width == -1 || layoutParams.width == -2 || ((int) (((float) layoutParams.width) * min)) == 0)) {
            layoutParams.width = (int) (((float) layoutParams.width) * min);
        }
        if (!(layoutParams.height == -1 || layoutParams.height == -2 || ((int) (((float) layoutParams.height) * min)) == 0)) {
            layoutParams.height = (int) (((float) layoutParams.height) * min);
        }
        if (layoutParams instanceof ViewGroup.MarginLayoutParams) {
            ViewGroup.MarginLayoutParams marginLayoutParams = (ViewGroup.MarginLayoutParams) layoutParams;
            marginLayoutParams.leftMargin = (int) (((float) marginLayoutParams.leftMargin) * min);
            marginLayoutParams.rightMargin = (int) (((float) marginLayoutParams.rightMargin) * min);
            marginLayoutParams.topMargin = (int) (((float) marginLayoutParams.topMargin) * min);
            marginLayoutParams.bottomMargin = (int) (((float) marginLayoutParams.bottomMargin) * min);
        }
        if (layoutParams instanceof ConstraintLayout.LayoutParams) {
            ConstraintLayout.LayoutParams layoutParams2 = (ConstraintLayout.LayoutParams) layoutParams;
            layoutParams2.matchConstraintMinHeight = (int) (((float) layoutParams2.matchConstraintMinHeight) * min);
            layoutParams2.matchConstraintMinWidth = (int) (((float) layoutParams2.matchConstraintMinWidth) * min);
            layoutParams2.matchConstraintMaxHeight = (int) (((float) layoutParams2.matchConstraintMaxHeight) * min);
            layoutParams2.matchConstraintMaxWidth = (int) (((float) layoutParams2.matchConstraintMaxWidth) * min);
        }
        view.setLayoutParams(layoutParams);
        view.setPadding((int) (((float) view.getPaddingLeft()) * min), (int) (((float) view.getPaddingTop()) * min), (int) (((float) view.getPaddingRight()) * min), (int) (((float) view.getPaddingBottom()) * min));
        view.setMinimumHeight((int) (((float) view.getMinimumHeight()) * min));
        view.setMinimumWidth((int) (((float) view.getMinimumWidth()) * min));
        if (view instanceof TextView) {
            TextView textView = (TextView) view;
            textView.setTextSize(0, textView.getTextSize() * min);
        }

        if (view instanceof RecyclerView) {
            RecyclerView customRecyclerView = (RecyclerView) view;
            customRecyclerView.setScrollBarSize((int) (((float) customRecyclerView.getScrollBarSize()) * min));
        }

        if (view instanceof ViewGroup) {
            ViewGroup viewGroup = (ViewGroup) view;
            for (int i = 0; i < viewGroup.getChildCount(); i++) {
                scaleViewAndChildren(activity, viewGroup.getChildAt(i));
            }
        }
    }

    public static float scale(Activity activity, float f) {
        Display defaultDisplay = activity.getWindowManager().getDefaultDisplay();
        Point point = new Point();
        defaultDisplay.getSize(point);
        return f * Math.min(((float) point.x) * MULT_X, ((float) point.y) * MULT_Y);
    }

    public static int getTextWidth(String str, TextPaint textPaint) {
        return (int) new StaticLayout(str, textPaint, Integer.MAX_VALUE, Layout.Alignment.ALIGN_NORMAL, 1.0f, 0.0f, false).getLineWidth(0);
    }

    public static String getStringWithoutColors(String str) {
        if (str == null || str.isEmpty()) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        int i2 = 0;
        while (i2 < str.length()) {
            if (isColorTagAt(str, i2)) {
                i2 += 8;
            } else {
                sb.append(str.charAt(i2));
                i2++;
            }
        }
        return sb.toString();
    }
}
