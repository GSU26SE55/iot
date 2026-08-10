package com.solarbms.setup;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.HapticFeedbackConstants;
import android.widget.Toast;

import androidx.activity.ComponentActivity;
import androidx.camera.core.CameraSelector;
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

public final class QrScannerActivity extends ComponentActivity {
    public static final String EXTRA_QR_VALUE = "com.solarbms.setup.QR_VALUE";

    private final AtomicBoolean analysisInFlight = new AtomicBoolean(false);
    private final AtomicBoolean resultDelivered = new AtomicBoolean(false);
    private ExecutorService cameraExecutor;
    private BarcodeScanner barcodeScanner;
    private PreviewView previewView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_qr_scanner);
        previewView = findViewById(R.id.cameraPreview);
        findViewById(R.id.closeScannerButton).setOnClickListener(view -> cancelScan());

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
                provider.bindToLifecycle(
                        this,
                        CameraSelector.DEFAULT_BACK_CAMERA,
                        preview,
                        analysis
                );
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
            if (!resultDelivered.compareAndSet(false, true)) return;

            previewView.performHapticFeedback(HapticFeedbackConstants.CONFIRM);
            Intent data = new Intent().putExtra(EXTRA_QR_VALUE, rawValue);
            setResult(RESULT_OK, data);
            finish();
            return;
        }
    }

    private void cancelScan() {
        if (resultDelivered.compareAndSet(false, true)) setResult(RESULT_CANCELED);
        finish();
    }

    @Override
    public void onBackPressed() {
        cancelScan();
    }

    @Override
    protected void onDestroy() {
        if (barcodeScanner != null) barcodeScanner.close();
        if (cameraExecutor != null) cameraExecutor.shutdown();
        super.onDestroy();
    }
}

