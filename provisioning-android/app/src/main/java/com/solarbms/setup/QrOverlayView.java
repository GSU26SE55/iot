package com.solarbms.setup;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.View;

public final class QrOverlayView extends View {
    private final Paint dimPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint cornerPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF scanRect = new RectF();

    public QrOverlayView(Context context, AttributeSet attrs) {
        super(context, attrs);
        dimPaint.setColor(0x88040A14);
        cornerPaint.setColor(Color.WHITE);
        cornerPaint.setStyle(Paint.Style.STROKE);
        cornerPaint.setStrokeWidth(dp(5));
        cornerPaint.setStrokeCap(Paint.Cap.ROUND);
        linePaint.setColor(0xFF82A8FF);
        linePaint.setStrokeWidth(dp(2));
        linePaint.setStrokeCap(Paint.Cap.ROUND);
        setLayerType(LAYER_TYPE_SOFTWARE, null);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float size = Math.min(getWidth() * 0.82f, getHeight() * 0.55f);
        float left = (getWidth() - size) / 2f;
        float top = (getHeight() - size) / 2f;
        scanRect.set(left, top, left + size, top + size);

        canvas.drawRect(0, 0, getWidth(), scanRect.top, dimPaint);
        canvas.drawRect(0, scanRect.bottom, getWidth(), getHeight(), dimPaint);
        canvas.drawRect(0, scanRect.top, scanRect.left, scanRect.bottom, dimPaint);
        canvas.drawRect(scanRect.right, scanRect.top, getWidth(), scanRect.bottom, dimPaint);

        float length = dp(42);
        drawCorner(canvas, scanRect.left, scanRect.top, length, 1, 1);
        drawCorner(canvas, scanRect.right, scanRect.top, length, -1, 1);
        drawCorner(canvas, scanRect.left, scanRect.bottom, length, 1, -1);
        drawCorner(canvas, scanRect.right, scanRect.bottom, length, -1, -1);

        float phase = (SystemClock.uptimeMillis() % 1800L) / 1800f;
        float wave = phase < 0.5f ? phase * 2f : (1f - phase) * 2f;
        float y = scanRect.top + dp(28) + wave * (scanRect.height() - dp(56));
        linePaint.setShadowLayer(dp(9), 0, 0, 0xFF82A8FF);
        canvas.drawLine(scanRect.left + dp(22), y, scanRect.right - dp(22), y, linePaint);
        postInvalidateOnAnimation();
    }

    private void drawCorner(
            Canvas canvas,
            float x,
            float y,
            float length,
            int horizontal,
            int vertical
    ) {
        canvas.drawLine(x, y, x + length * horizontal, y, cornerPaint);
        canvas.drawLine(x, y, x, y + length * vertical, cornerPaint);
    }

    private float dp(float value) {
        return value * getResources().getDisplayMetrics().density;
    }
}
