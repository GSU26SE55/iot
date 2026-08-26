package com.solarbms.setup;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.SystemClock;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.ComponentActivity;
import androidx.activity.OnBackPressedCallback;
import androidx.annotation.OptIn;
import androidx.camera.core.Camera;
import androidx.camera.core.CameraSelector;
import androidx.camera.core.ExperimentalGetImage;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
import androidx.camera.core.Preview;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.camera.view.PreviewView;
import androidx.core.content.ContextCompat;

import com.google.common.util.concurrent.ListenableFuture;
import com.google.mlkit.vision.barcode.BarcodeScanner;
import com.google.mlkit.vision.barcode.BarcodeScannerOptions;
import com.google.mlkit.vision.barcode.BarcodeScanning;
import com.google.mlkit.vision.barcode.common.Barcode;
import com.google.mlkit.vision.common.InputImage;

import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

@OptIn(markerClass = ExperimentalGetImage.class)
public final class QrScannerActivity extends ComponentActivity {
    public static final String EXTRA_QR_VALUE = "com.solarbms.setup.QR_VALUE";

    private final AtomicBoolean analysisInFlight = new AtomicBoolean(false);
    private final AtomicBoolean resultDelivered = new AtomicBoolean(false);
    private ExecutorService cameraExecutor;
    private BarcodeScanner barcodeScanner;
    private PreviewView previewView;
    private ImageButton flashButton;
    private TextView scannerStatus;
    private Camera camera;
    private boolean torchEnabled;
    private long lastInvalidNoticeMs;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_qr_scanner);
        getWindow().getDecorView().setSystemUiVisibility(0);
        getOnBackPressedDispatcher().addCallback(this, new OnBackPressedCallback(true) {
            @Override
            public void handleOnBackPressed() {
                cancelScan();
            }
        });
        previewView = findViewById(R.id.cameraPreview);
        flashButton = findViewById(R.id.flashScannerButton);
        scannerStatus = findViewById(R.id.scannerStatusText);
        findViewById(R.id.closeScannerButton).setOnClickListener(view -> cancelScan());
        flashButton.setOnClickListener(view -> toggleTorch());

        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            Toast.makeText(this, R.string.camera_required, Toast.LENGTH_LONG).show();
            cancelScan();
            return;
        }

        cameraExecutor = Executors.newSingleThreadExecutor();
        BarcodeScannerOptions options = new BarcodeScannerOptions.Builder()
                .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
                .build();
        barcodeScanner = BarcodeScanning.getClient(options);
        startCamera();
    }

    private void startCamera() {
        ListenableFuture<ProcessCameraProvider> future =
                ProcessCameraProvider.getInstance(this);
        future.addListener(() -> {
            try {
                ProcessCameraProvider provider = future.get();
                Preview preview = new Preview.Builder().build();
                preview.setSurfaceProvider(previewView.getSurfaceProvider());

                ImageAnalysis analysis = new ImageAnalysis.Builder()
                        .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                        .build();
                analysis.setAnalyzer(cameraExecutor, this::analyzeFrame);

                provider.unbindAll();
                camera = provider.bindToLifecycle(
                        this,
                        CameraSelector.DEFAULT_BACK_CAMERA,
                        preview,
                        analysis
                );
                boolean hasFlash = camera.getCameraInfo().hasFlashUnit();
                flashButton.setVisibility(hasFlash ? View.VISIBLE : View.INVISIBLE);
            } catch (Exception error) {
                Toast.makeText(
                        this,
                        getString(R.string.scanner_start_failed, error.getMessage()),
                        Toast.LENGTH_LONG
                ).show();
                cancelScan();
            }
        }, ContextCompat.getMainExecutor(this));
    }

    private void analyzeFrame(ImageProxy imageProxy) {
        if (resultDelivered.get() || !analysisInFlight.compareAndSet(false, true)) {
            imageProxy.close();
            return;
        }

        android.media.Image mediaImage = imageProxy.getImage();
        if (mediaImage == null) {
            analysisInFlight.set(false);
            imageProxy.close();
            return;
        }

        InputImage image = InputImage.fromMediaImage(
                mediaImage,
                imageProxy.getImageInfo().getRotationDegrees()
        );
        barcodeScanner.process(image)
                .addOnSuccessListener(this::handleBarcodes)
                .addOnCompleteListener(task -> {
                    analysisInFlight.set(false);
                    imageProxy.close();
                });
    }

    private void handleBarcodes(List<Barcode> barcodes) {
        for (Barcode barcode : barcodes) {
            String rawValue = barcode.getRawValue();
            if (rawValue == null || rawValue.trim().isEmpty()) continue;
            if (!isProvisioningQr(rawValue)) {
                showInvalidQrHint();
                continue;
            }
            if (!resultDelivered.compareAndSet(false, true)) return;

            previewView.performHapticFeedback(
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                            ? HapticFeedbackConstants.CONFIRM
                            : HapticFeedbackConstants.LONG_PRESS
            );
            Intent data = new Intent().putExtra(EXTRA_QR_VALUE, rawValue);
            setResult(RESULT_OK, data);
            finish();
            return;
        }
    }

    private boolean isProvisioningQr(String rawValue) {
        try {
            Uri uri = Uri.parse(rawValue.trim());
            boolean supportedScheme = "iot".equalsIgnoreCase(uri.getScheme())
                    || "https".equalsIgnoreCase(uri.getScheme())
                    || "http".equalsIgnoreCase(uri.getScheme());
            if (!supportedScheme) return false;
            if ("iot".equalsIgnoreCase(uri.getScheme())
                    && !"provision".equalsIgnoreCase(uri.getHost())) return false;

            String deviceCode = uri.getQueryParameter("dc");
            if (deviceCode == null) deviceCode = uri.getQueryParameter("deviceCode");
            String apiKey = uri.getQueryParameter("key");
            if (apiKey == null) apiKey = uri.getQueryParameter("apiKey");
            return deviceCode != null && !deviceCode.trim().isEmpty()
                    && apiKey != null && !apiKey.trim().isEmpty();
        } catch (RuntimeException ignored) {
            return false;
        }
    }

    private void showInvalidQrHint() {
        long now = SystemClock.elapsedRealtime();
        if (now - lastInvalidNoticeMs < 1800L) return;
        lastInvalidNoticeMs = now;
        runOnUiThread(() -> {
            scannerStatus.setText(R.string.scanner_wrong_qr);
            scannerStatus.setTextColor(0xFFFFCDD4);
            scannerStatus.postDelayed(() -> {
                if (!resultDelivered.get()) {
                    scannerStatus.setText(R.string.scanner_privacy_hint);
                    scannerStatus.setTextColor(0xFFFFFFFF);
                }
            }, 1600L);
        });
    }

    private void toggleTorch() {
        if (camera == null || !camera.getCameraInfo().hasFlashUnit()) return;
        torchEnabled = !torchEnabled;
        camera.getCameraControl().enableTorch(torchEnabled);
        flashButton.setAlpha(torchEnabled ? 1f : 0.72f);
    }

    private void cancelScan() {
        if (resultDelivered.compareAndSet(false, true)) setResult(RESULT_CANCELED);
        finish();
    }

    @Override
    protected void onDestroy() {
        if (camera != null && torchEnabled) camera.getCameraControl().enableTorch(false);
        if (barcodeScanner != null) barcodeScanner.close();
        if (cameraExecutor != null) cameraExecutor.shutdown();
        super.onDestroy();
    }
}
