package com.solarbms.setup.v2

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.os.SystemClock
import android.util.AttributeSet
import android.view.View
import androidx.core.content.ContextCompat

/**
 * Khung ngắm QR: làm mờ xung quanh, vẽ bốn góc bo và một vạch quét chạy qua lại.
 *
 * Port từ bản Java của `:app`, giữ nguyên hình vẽ. Khác một điểm: màu lấy từ `colors.xml`
 * thay vì hằng số hex nằm rải trong code.
 */
class QrOverlayView(context: Context, attrs: AttributeSet?) : View(context, attrs) {

    private val dimPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val cornerPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val scanRect = RectF()
    private val sweepColor = ContextCompat.getColor(context, R.color.scanner_corner)

    init {
        dimPaint.color = ContextCompat.getColor(context, R.color.scanner_scrim)
        cornerPaint.color = Color.WHITE
        cornerPaint.style = Paint.Style.STROKE
        cornerPaint.strokeWidth = dp(5f)
        cornerPaint.strokeCap = Paint.Cap.ROUND
        linePaint.color = sweepColor
        linePaint.strokeWidth = dp(2f)
        linePaint.strokeCap = Paint.Cap.ROUND
        // setShadowLayer cần software layer, hardware canvas bỏ qua nó.
        setLayerType(LAYER_TYPE_SOFTWARE, null)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val size = minOf(width * 0.82f, height * 0.55f)
        val left = (width - size) / 2f
        val top = (height - size) / 2f
        scanRect.set(left, top, left + size, top + size)

        canvas.drawRect(0f, 0f, width.toFloat(), scanRect.top, dimPaint)
        canvas.drawRect(0f, scanRect.bottom, width.toFloat(), height.toFloat(), dimPaint)
        canvas.drawRect(0f, scanRect.top, scanRect.left, scanRect.bottom, dimPaint)
        canvas.drawRect(scanRect.right, scanRect.top, width.toFloat(), scanRect.bottom, dimPaint)

        val length = dp(42f)
        drawCorner(canvas, scanRect.left, scanRect.top, length, 1, 1)
        drawCorner(canvas, scanRect.right, scanRect.top, length, -1, 1)
        drawCorner(canvas, scanRect.left, scanRect.bottom, length, 1, -1)
        drawCorner(canvas, scanRect.right, scanRect.bottom, length, -1, -1)

        val phase = (SystemClock.uptimeMillis() % SWEEP_PERIOD_MS) / SWEEP_PERIOD_MS.toFloat()
        val wave = if (phase < 0.5f) phase * 2f else (1f - phase) * 2f
        val y = scanRect.top + dp(28f) + wave * (scanRect.height() - dp(56f))
        linePaint.setShadowLayer(dp(9f), 0f, 0f, sweepColor)
        canvas.drawLine(scanRect.left + dp(22f), y, scanRect.right - dp(22f), y, linePaint)
        postInvalidateOnAnimation()
    }

    private fun drawCorner(
        canvas: Canvas,
        x: Float,
        y: Float,
        length: Float,
        horizontal: Int,
        vertical: Int,
    ) {
        canvas.drawLine(x, y, x + length * horizontal, y, cornerPaint)
        canvas.drawLine(x, y, x, y + length * vertical, cornerPaint)
    }

    private fun dp(value: Float): Float = value * resources.displayMetrics.density

    private companion object {
        const val SWEEP_PERIOD_MS = 1800L
    }
}
