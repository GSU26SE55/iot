package com.solarbms.setup.v2

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.HapticFeedbackConstants
import android.view.View
import android.widget.ImageButton
import android.widget.TextView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.annotation.OptIn
import androidx.camera.core.Camera
import androidx.camera.core.CameraSelector
import androidx.camera.core.ExperimentalGetImage
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import com.google.mlkit.vision.barcode.BarcodeScanner
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Viewfinder quét QR ghép thiết bị. Port từ bản Java của `:app` — CameraX + ML Kit đang chạy
 * tốt nên giữ nguyên cách làm, chỉ đổi ngôn ngữ và dùng chung [ProvisioningQr] để kiểm tra
 * tính hợp lệ (trước đây logic ấy chép riêng ở đây và hiểu QR khác với trang web).
 */
@OptIn(ExperimentalGetImage::class)
class QrScannerActivity : ComponentActivity() {

    private val analysisInFlight = AtomicBoolean(false)
    private val resultDelivered = AtomicBoolean(false)
    private lateinit var previewView: PreviewView
    private lateinit var flashButton: ImageButton
    private lateinit var scannerStatus: TextView
    private var cameraExecutor: ExecutorService? = null
    private var barcodeScanner: BarcodeScanner? = null
    private var camera: Camera? = null
    private var torchEnabled = false
    private var lastInvalidNoticeMs = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_qr_scanner)
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() = cancelScan()
        })

        previewView = findViewById(R.id.cameraPreview)
        flashButton = findViewById(R.id.flashScannerButton)
        scannerStatus = findViewById(R.id.scannerStatusText)
        findViewById<View>(R.id.closeScannerButton).setOnClickListener { cancelScan() }
        flashButton.setOnClickListener { toggleTorch() }

        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            Toast.makeText(this, R.string.camera_required, Toast.LENGTH_LONG).show()
            cancelScan()
            return
        }

        cameraExecutor = Executors.newSingleThreadExecutor()
        barcodeScanner = BarcodeScanning.getClient(
            BarcodeScannerOptions.Builder()
                .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
                .build(),
        )
        startCamera()
    }

    private fun startCamera() {
        val future = ProcessCameraProvider.getInstance(this)
        future.addListener({
            try {
                val provider = future.get()
                val preview = Preview.Builder().build()
                preview.setSurfaceProvider(previewView.surfaceProvider)

                val analysis = ImageAnalysis.Builder()
                    .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                    .build()
                analysis.setAnalyzer(cameraExecutor!!, ::analyzeFrame)

                provider.unbindAll()
                camera = provider.bindToLifecycle(
                    this, CameraSelector.DEFAULT_BACK_CAMERA, preview, analysis,
                )
                flashButton.visibility =
                    if (camera?.cameraInfo?.hasFlashUnit() == true) View.VISIBLE else View.INVISIBLE
            } catch (error: Exception) {
                Toast.makeText(
                    this,
                    getString(R.string.scanner_start_failed, error.message),
                    Toast.LENGTH_LONG,
                ).show()
                cancelScan()
            }
        }, ContextCompat.getMainExecutor(this))
    }

    private fun analyzeFrame(imageProxy: ImageProxy) {
        if (resultDelivered.get() || !analysisInFlight.compareAndSet(false, true)) {
            imageProxy.close()
            return
        }
        val mediaImage = imageProxy.image
        if (mediaImage == null) {
            analysisInFlight.set(false)
            imageProxy.close()
            return
        }
        val image = InputImage.fromMediaImage(mediaImage, imageProxy.imageInfo.rotationDegrees)
        barcodeScanner?.process(image)
            ?.addOnSuccessListener(::handleBarcodes)
            ?.addOnCompleteListener {
                analysisInFlight.set(false)
                imageProxy.close()
            } ?: imageProxy.close()
    }

    private fun handleBarcodes(barcodes: List<Barcode>) {
        for (barcode in barcodes) {
            val rawValue = barcode.rawValue
            if (rawValue.isNullOrBlank()) continue
            if (ProvisioningQr.parse(rawValue) == null) {
                showInvalidQrHint()
                continue
            }
            if (!resultDelivered.compareAndSet(false, true)) return

            previewView.performHapticFeedback(
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) HapticFeedbackConstants.CONFIRM
                else HapticFeedbackConstants.LONG_PRESS,
            )
            setResult(RESULT_OK, Intent().putExtra(EXTRA_QR_VALUE, rawValue))
            finish()
            return
        }
    }

    private fun showInvalidQrHint() {
        val now = SystemClock.elapsedRealtime()
        if (now - lastInvalidNoticeMs < INVALID_NOTICE_INTERVAL_MS) return
        lastInvalidNoticeMs = now
        runOnUiThread {
            scannerStatus.setText(R.string.scanner_wrong_qr)
            scannerStatus.setTextColor(ContextCompat.getColor(this, R.color.scanner_warning))
            scannerStatus.postDelayed({
                if (!resultDelivered.get()) {
                    scannerStatus.setText(R.string.scanner_privacy_hint)
                    scannerStatus.setTextColor(ContextCompat.getColor(this, R.color.white))
                }
            }, INVALID_NOTICE_RESET_MS)
        }
    }

    private fun toggleTorch() {
        val current = camera ?: return
        if (!current.cameraInfo.hasFlashUnit()) return
        torchEnabled = !torchEnabled
        current.cameraControl.enableTorch(torchEnabled)
        flashButton.alpha = if (torchEnabled) 1f else 0.72f
    }

    private fun cancelScan() {
        if (resultDelivered.compareAndSet(false, true)) setResult(RESULT_CANCELED)
        finish()
    }

    override fun onDestroy() {
        if (torchEnabled) camera?.cameraControl?.enableTorch(false)
        barcodeScanner?.close()
        cameraExecutor?.shutdown()
        super.onDestroy()
    }

    companion object {
        const val EXTRA_QR_VALUE = "com.solarbms.setup.v2.QR_VALUE"
        private const val INVALID_NOTICE_INTERVAL_MS = 1800L
        private const val INVALID_NOTICE_RESET_MS = 1600L
    }
}
