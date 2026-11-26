package com.gta.launcher.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

public class PingGraphView extends View {
    private static final int MAX_SAMPLES = 48;
    private static final float MAX_PING = 400.0f;

    private final List<Integer> samples = new ArrayList<>();
    private final Paint bgPaint = new Paint();
    private final Paint gridPaint = new Paint();
    private final Paint borderPaint = new Paint();
    private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path linePath = new Path();

    public PingGraphView(Context context) {
        super(context);
        init();
    }

    public PingGraphView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public PingGraphView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        bgPaint.setColor(Color.BLACK);
        bgPaint.setStyle(Paint.Style.FILL);

        gridPaint.setColor(Color.parseColor("#242424"));
        gridPaint.setStrokeWidth(1.0f);

        borderPaint.setColor(Color.parseColor("#828790"));
        borderPaint.setStyle(Paint.Style.STROKE);
        borderPaint.setStrokeWidth(2.0f);

        linePaint.setColor(Color.parseColor("#FF7F00"));
        linePaint.setStyle(Paint.Style.STROKE);
        linePaint.setStrokeWidth(2.2f);
    }

    public void setPingSeed(int basePing) {
        samples.clear();
        int clamped = Math.max(8, Math.min(390, basePing));
        for (int i = 0; i < 24; i++) {
            int jitter = ((i * 17) % 15) - 7;
            samples.add(Math.max(4, Math.min(395, clamped + jitter)));
        }
        invalidate();
    }

    public void pushPing(int pingMs) {
        int clamped = Math.max(4, Math.min(395, pingMs));
        if (samples.size() >= MAX_SAMPLES) {
            samples.remove(0);
        }
        samples.add(clamped);
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth();
        float h = getHeight();
        if (w <= 2f || h <= 2f) return;

        canvas.drawRect(0, 0, w, h, bgPaint);

        // Grid matching SA-MP PC ping graph (horizontal 200ms line + vertical 16px ticks)
        canvas.drawLine(0, h * 0.5f, w, h * 0.5f, gridPaint);
        canvas.drawLine(0, h * 0.25f, w, h * 0.25f, gridPaint);
        canvas.drawLine(0, h * 0.75f, w, h * 0.75f, gridPaint);
        for (float x = 24f; x < w; x += 24f) {
            canvas.drawLine(x, 0, x, h, gridPaint);
        }

        if (samples.size() >= 2) {
            linePath.reset();
            float stepX = w / (float) (MAX_SAMPLES - 1);
            int startIdx = Math.max(0, samples.size() - MAX_SAMPLES);
            for (int i = 0; i < samples.size() - startIdx; i++) {
                float px = i * stepX;
                float norm = Math.max(0.0f, Math.min(1.0f, samples.get(startIdx + i) / MAX_PING));
                float py = h - 2f - norm * (h - 4f);
                if (i == 0) linePath.moveTo(px, py);
                else linePath.lineTo(px, py);
            }
            canvas.drawPath(linePath, linePaint);
        }

        canvas.drawRect(0, 0, w - 1f, h - 1f, borderPaint);
    }
}
