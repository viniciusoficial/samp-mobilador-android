package com.sampmobile.launcher.ui.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewParent;

import com.sampmobile.launcher.utils.LauncherThemeManager;

import java.util.ArrayList;
import java.util.List;

/**
 * GTA:SA stat-bar style slider: a slim segmented gauge with a pill thumb.
 * Lightweight custom Canvas view that replaces heavy Material Slider (20x faster inflation).
 * A drag only starts after a clearly horizontal move, so scrolling the page never changes a value.
 */
public class GtaSaSliderView extends View {

    public interface OnChangeListener {
        void onValueChange(GtaSaSliderView slider, float value, boolean fromUser);
    }

    private static final String ANDROID_NS = "http://schemas.android.com/apk/res/android";
    private static final int SEGMENT_COUNT = 16;
    private static final float TRACK_PAD_DP = 9f;

    private float valueFrom = 0f;
    private float valueTo = 100f;
    private float stepSize = 1f;
    private float value = 50f;

    private int accentColor = 0xFFF5A623;
    private boolean rainbowMode = false;
    private boolean isDragging = false;
    private float downX;
    private float downY;
    private int touchSlop;
    private Shader rainbowShader;
    private int rainbowShaderWidth = -1;

    private final Paint emptyBarPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint filledBarPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint haloPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint thumbShadowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint thumbPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    private final RectF segRect = new RectF();

    private final List<OnChangeListener> listeners = new ArrayList<>();

    public GtaSaSliderView(Context context) {
        super(context);
        init(context, null);
    }

    public GtaSaSliderView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context, attrs);
    }

    public GtaSaSliderView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(context, attrs);
    }

    private static float readAttrFloat(AttributeSet attrs, String name, float def) {
        if (attrs == null) return def;
        String raw = attrs.getAttributeValue(ANDROID_NS, name);
        if (raw == null || raw.isEmpty()) return def;
        try {
            return Float.parseFloat(raw);
        } catch (NumberFormatException e) {
            return def;
        }
    }

    private void init(Context context, AttributeSet attrs) {
        accentColor = LauncherThemeManager.getAccentColor(context);
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();

        if (attrs != null) {
            valueFrom = readAttrFloat(attrs, "valueFrom", 0f);
            valueTo = readAttrFloat(attrs, "valueTo", 100f);
            stepSize = readAttrFloat(attrs, "stepSize", 1f);
            value = readAttrFloat(attrs, "value", valueFrom);
        }

        emptyBarPaint.setStyle(Paint.Style.FILL);
        emptyBarPaint.setColor(0x2EFFFFFF);

        filledBarPaint.setStyle(Paint.Style.FILL);
        filledBarPaint.setColor(accentColor);

        haloPaint.setStyle(Paint.Style.FILL);

        thumbShadowPaint.setStyle(Paint.Style.FILL);
        thumbShadowPaint.setColor(0x73000000);

        thumbPaint.setStyle(Paint.Style.FILL);
        thumbPaint.setColor(0xFFFFFFFF);

        setClickable(true);
    }

    public void setRainbowMode(boolean rainbow) {
        this.rainbowMode = rainbow;
        invalidate();
    }

    public void setAccentColor(int color) {
        this.accentColor = color;
        filledBarPaint.setColor(color);
        invalidate();
    }

    public float getValue() {
        return value;
    }

    public void setValue(float newValue) {
        setValueInternal(newValue, false);
    }

    public float getValueFrom() {
        return valueFrom;
    }

    public void setValueFrom(float valueFrom) {
        this.valueFrom = valueFrom;
        setValueInternal(this.value, false);
    }

    public float getValueTo() {
        return valueTo;
    }

    public void setValueTo(float valueTo) {
        this.valueTo = valueTo;
        setValueInternal(this.value, false);
    }

    public float getStepSize() {
        return stepSize;
    }

    public void setStepSize(float stepSize) {
        this.stepSize = stepSize;
    }

    public void addOnChangeListener(OnChangeListener listener) {
        if (listener != null && !listeners.contains(listener)) {
            listeners.add(listener);
        }
    }

    public void clearOnChangeListeners() {
        listeners.clear();
    }

    private void setValueInternal(float rawValue, boolean fromUser) {
        float clamped = Math.max(valueFrom, Math.min(valueTo, rawValue));
        if (stepSize > 0f) {
            int steps = Math.round((clamped - valueFrom) / stepSize);
            clamped = valueFrom + steps * stepSize;
            clamped = Math.max(valueFrom, Math.min(valueTo, clamped));
        }
        if (Math.abs(clamped - this.value) > 0.0001f || !fromUser) {
            boolean changed = Math.abs(clamped - this.value) > 0.0001f;
            this.value = clamped;
            invalidate();
            if (changed) {
                for (OnChangeListener l : listeners) {
                    l.onValueChange(this, this.value, fromUser);
                }
            }
        }
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        float density = getResources().getDisplayMetrics().density;
        int defaultW = (int) (220 * density);
        int defaultH = (int) (34 * density);
        int w = resolveSize(defaultW, widthMeasureSpec);
        int h = resolveSize(defaultH, heightMeasureSpec);
        setMeasuredDimension(w, h);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float density = getResources().getDisplayMetrics().density;
        int w = getWidth();
        int h = getHeight();
        if (w <= 0 || h <= 0) return;

        float left = TRACK_PAD_DP * density;
        float right = Math.max(left + 1f, w - TRACK_PAD_DP * density);
        float trackW = right - left;
        float cy = h * 0.5f;
        float barH = (rainbowMode ? 8f : 7f) * density;
        float top = cy - barH * 0.5f;
        float bottom = cy + barH * 0.5f;

        float range = Math.max(0.0001f, valueTo - valueFrom);
        float fraction = Math.max(0f, Math.min(1f, (value - valueFrom) / range));
        float thumbX = left + fraction * trackW;

        if (rainbowMode) {
            if (rainbowShader == null || rainbowShaderWidth != w) {
                int[] colors = new int[7];
                for (int i = 0; i < 7; i++) {
                    colors[i] = Color.HSVToColor(new float[]{i * 60f, 0.84f, 0.96f});
                }
                rainbowShader = new LinearGradient(left, 0, right, 0, colors, null, Shader.TileMode.CLAMP);
                rainbowShaderWidth = w;
            }
            filledBarPaint.setShader(rainbowShader);
            segRect.set(left, top, right, bottom);
            canvas.drawRoundRect(segRect, barH * 0.5f, barH * 0.5f, filledBarPaint);
            filledBarPaint.setShader(null);
        } else {
            float gap = 2f * density;
            float segW = Math.max(2f, (trackW - gap * (SEGMENT_COUNT - 1)) / SEGMENT_COUNT);
            float radius = 1.6f * density;
            float exactFill = fraction * SEGMENT_COUNT;
            filledBarPaint.setColor(accentColor);

            for (int i = 0; i < SEGMENT_COUNT; i++) {
                float sLeft = left + i * (segW + gap);
                segRect.set(sLeft, top, sLeft + segW, bottom);
                canvas.drawRoundRect(segRect, radius, radius, emptyBarPaint);
                if (exactFill > i) {
                    float segFill = Math.min(1f, exactFill - i);
                    segRect.set(sLeft, top, sLeft + segW * segFill, bottom);
                    canvas.drawRoundRect(segRect, radius, radius, filledBarPaint);
                }
            }
        }

        if (isDragging) {
            haloPaint.setColor((accentColor & 0x00FFFFFF) | 0x38000000);
            canvas.drawCircle(thumbX, cy, 13f * density, haloPaint);
        }
        float thumbHalfW = (isDragging ? 3.2f : 2.6f) * density;
        float thumbHalfH = Math.min(h * 0.5f - density, (isDragging ? 10.5f : 8.5f) * density);
        segRect.set(thumbX - thumbHalfW - density, cy - thumbHalfH - density,
                thumbX + thumbHalfW + density, cy + thumbHalfH + density);
        canvas.drawRoundRect(segRect, thumbHalfW + density, thumbHalfW + density, thumbShadowPaint);
        segRect.set(thumbX - thumbHalfW, cy - thumbHalfH, thumbX + thumbHalfW, cy + thumbHalfH);
        canvas.drawRoundRect(segRect, thumbHalfW, thumbHalfW, thumbPaint);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!isEnabled()) return false;
        int action = event.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
                downX = event.getX();
                downY = event.getY();
                isDragging = false;
                return true;
            case MotionEvent.ACTION_MOVE:
                if (!isDragging) {
                    float dx = Math.abs(event.getX() - downX);
                    float dy = Math.abs(event.getY() - downY);
                    if (dx > touchSlop && dx > dy) {
                        isDragging = true;
                        ViewParent parent = getParent();
                        if (parent != null) {
                            parent.requestDisallowInterceptTouchEvent(true);
                        }
                    }
                }
                if (isDragging) {
                    updateFromTouchX(event.getX());
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP:
                boolean tap = Math.abs(event.getX() - downX) <= touchSlop
                        && Math.abs(event.getY() - downY) <= touchSlop;
                if (isDragging || tap) {
                    updateFromTouchX(event.getX());
                }
                isDragging = false;
                performClick();
                invalidate();
                return true;
            case MotionEvent.ACTION_CANCEL:
                isDragging = false;
                invalidate();
                return true;
        }
        return super.onTouchEvent(event);
    }

    @Override
    public boolean performClick() {
        return super.performClick();
    }

    private void updateFromTouchX(float touchX) {
        float density = getResources().getDisplayMetrics().density;
        float pad = TRACK_PAD_DP * density;
        float left = pad;
        float right = Math.max(left + 1f, getWidth() - pad);
        float fraction = Math.max(0f, Math.min(1f, (touchX - left) / (right - left)));
        float raw = valueFrom + fraction * (valueTo - valueFrom);
        setValueInternal(raw, true);
    }
}
