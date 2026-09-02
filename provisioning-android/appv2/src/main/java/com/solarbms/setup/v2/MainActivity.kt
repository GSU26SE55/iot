package com.solarbms.setup.v2

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.solarbms.setup.v2.ui.SetupApp
import com.solarbms.setup.v2.ui.SolarSetupTheme

class MainActivity : ComponentActivity() {

    private val viewModel: SetupViewModel by viewModels()

    /** Sau khi rời panel Wi-Fi, người dùng có thể vừa nối SolarGW — dò lại. */
    private var wifiPanelOpened = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            SolarSetupTheme {
                val state by viewModel.state.collectAsStateWithLifecycle()

                // Nạp firmware mà màn hình tắt giữa chừng là đứt luồng và hỏng lượt nạp.
                LaunchedEffect(state.firmware.uploading) {
                    if (state.firmware.uploading) {
                        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    } else {
                        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    }
                }

                SetupApp(
                    viewModel = viewModel,
                    state = state,
                    onOpenWifiPanel = ::openWifiPanel,
                    onScanQr = ::requestQrScan,
                    onPickFirmware = { firmwarePicker.launch(arrayOf("*/*")) },
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        if (wifiPanelOpened) {
            wifiPanelOpened = false
            viewModel.discover()
        }
    }

    private fun openWifiPanel() {
        wifiPanelOpened = true
        val intent = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            Intent(Settings.Panel.ACTION_WIFI)
        } else {
            Intent(Settings.ACTION_WIFI_SETTINGS)
        }
        startActivity(intent)
    }

    // ---- Quét QR ----

    private val qrScanner = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult(),
    ) { result ->
        if (result.resultCode != RESULT_OK) return@registerForActivityResult
        viewModel.applyQr(result.data?.getStringExtra(QrScannerActivity.EXTRA_QR_VALUE))
    }

    private val cameraPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (granted) launchScanner()
        else Toast.makeText(this, R.string.camera_required, Toast.LENGTH_LONG).show()
    }

    private fun requestQrScan() {
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            launchScanner()
        } else {
            cameraPermission.launch(Manifest.permission.CAMERA)
        }
    }

    private fun launchScanner() {
        qrScanner.launch(Intent(this, QrScannerActivity::class.java))
    }

    // ---- Chọn file firmware ----

    // Nhiều máy không gán MIME cho .bin nên lọc theo type sẽ ẩn mất file cần chọn; mở rộng
    // rồi để firmware từ chối bằng magic byte 0xE9 — đó mới là kiểm tra đáng tin.
    private val firmwarePicker = registerForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri -> uri?.let(viewModel::pickFirmware) }
}
