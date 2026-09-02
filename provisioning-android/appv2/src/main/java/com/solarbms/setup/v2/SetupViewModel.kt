package com.solarbms.setup.v2

import android.app.Application
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.Uri
import android.provider.OpenableColumns
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

enum class Step { CONNECTING, WIFI, QR, SAVING, SUCCESS }

data class FirmwareState(
    val uri: Uri? = null,
    val fileName: String = "",
    val fileSize: Long = 0L,
    val uploading: Boolean = false,
    val sent: Long = 0L,
    val done: Boolean = false,
    val error: String? = null,
) {
    val progress: Float
        get() = if (fileSize <= 0L) 0f else (sent.toFloat() / fileSize).coerceIn(0f, 1f)
}

data class UiState(
    val step: Step = Step.CONNECTING,
    val env: EnvSettings,
    val gateway: PortalConfig? = null,
    val searching: Boolean = false,
    val scanning: Boolean = false,
    val networks: List<WifiNetwork> = emptyList(),
    val ssid: String = "",
    val password: String = "",
    val busy: Boolean = false,
    val status: String? = null,
    val error: String? = null,
    val savedDeviceCode: String = "",
    val successTitle: String = "Saved successfully",
    val successText: String =
        "The ESP32 is restarting, joining Wi-Fi and provisioning with the backend.",
    /** Ẩn bảng tóm tắt cấu hình ở màn kết quả cho các thao tác bảo trì. */
    val successShowSummary: Boolean = true,
    val firmware: FirmwareState = FirmwareState(),
) {
    val connected: Boolean get() = gateway != null

    /** Thiết bị đã ghép rồi thì đổi Wi-Fi không cần quét QR lại. */
    val alreadyPaired: Boolean
        get() = gateway?.let { it.hasApiKey && it.deviceCode.isNotEmpty() } == true

    /**
     * Gateway ĐÃ GHÉP mà đang chạy lớp vận chuyển khác môi trường đang chọn trong app.
     *
     * Điều kiện "đã ghép" là bắt buộc. Thiết bị vừa xoá sạch hoặc chưa ghép bao giờ luôn nằm ở
     * mặc định compile-time (172.16.1.4:21883 plain) — kêu "lệch môi trường" lúc đó là báo nhầm,
     * vì kỹ thuật viên đang trên đường ghi đè đúng cấu hình ấy.
     *
     * Từ khi firmware chọn TLS lúc chạy, đây là cảnh báo CẤU HÌNH chứ không còn là "phải build
     * lại firmware" — bấm áp dụng env là xong.
     */
    val envMismatch: Boolean
        get() = alreadyPaired &&
            gateway?.let { !env.environment.matches(it.mqttPort, it.mqttUseTls) } == true
}

class SetupViewModel(application: Application) : AndroidViewModel(application) {

    private val envStore = EnvStore(application)
    private val connectivityManager =
        application.getSystemService(ConnectivityManager::class.java)

    private val _state = MutableStateFlow(UiState(env = envStore.load()))
    val state: StateFlow<UiState> = _state.asStateFlow()

    private var client: PortalClient? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null

    /**
     * Các mạng Wi-Fi đang lên, do callback cấp.
     *
     * Không dùng `ConnectivityManager.allNetworks` (deprecated từ API 31) — callback vừa là
     * nguồn dữ liệu đúng, vừa là thứ đằng nào cũng phải đăng ký để biết lúc gateway xuất hiện.
     * Được đọc từ coroutine và ghi từ luồng của hệ thống, nên phải là tập đồng bộ.
     */
    private val wifiNetworks = java.util.concurrent.CopyOnWriteArrayList<Network>()

    init {
        watchWifi()
        discover()
    }

    // ---- Dò gateway ----

    /**
     * Chỉ dùng callback để BIẾT có mạng Wi-Fi mới, rồi mới đi dò. Không bind tiến trình, không
     * đoán mạng nào là gateway — việc đó để [PortalClient.discover] xác nhận bằng request thật.
     */
    private fun watchWifi() {
        val manager = connectivityManager ?: return
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                if (!wifiNetworks.contains(network)) wifiNetworks.add(network)
                if (_state.value.gateway == null) discover()
            }

            override fun onLost(network: Network) {
                wifiNetworks.remove(network)
                // Đang ở màn kết quả thì đừng kéo người dùng ngược lại: ESP32 rụng AP là
                // chuyện BÌNH THƯỜNG ngay sau khi lưu — nó đang khởi động lại.
                if (_state.value.step == Step.SUCCESS) return
                _state.update { it.copy(gateway = null, step = Step.CONNECTING) }
                client = null
            }
        }
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build()
        runCatching { manager.registerNetworkCallback(request, callback) }
            .onSuccess { networkCallback = callback }
    }

    fun discover() {
        if (_state.value.searching) return
        viewModelScope.launch {
            _state.update { it.copy(searching = true, error = null) }
            val candidates = wifiNetworks.toList()
            val found = withContext(Dispatchers.IO) { PortalClient.discover(candidates) }
            val config = found?.let { portal ->
                withContext(Dispatchers.IO) { runCatching { portal.config() }.getOrNull() }
            }
            client = found
            if (config == null) {
                _state.update { it.copy(searching = false, gateway = null, step = Step.CONNECTING) }
                return@launch
            }
            _state.update {
                it.copy(
                    searching = false,
                    gateway = config,
                    step = Step.WIFI,
                    ssid = it.ssid.ifEmpty { config.wifiSsid },
                )
            }
            refreshScan()
        }
    }

    // ---- Quét Wi-Fi ----

    fun refreshScan() {
        val portal = client ?: return
        if (_state.value.scanning) return
        viewModelScope.launch {
            _state.update { it.copy(scanning = true, error = null) }
            // Gateway quét bất đồng bộ và trả 202 tới khi có kết quả; hỏi lại tới khi xong.
            var attempt = 0
            var result: List<WifiNetwork>? = null
            while (attempt < SCAN_ATTEMPTS && result == null) {
                attempt++
                result = withContext(Dispatchers.IO) {
                    runCatching { portal.scanResults() }.getOrNull()
                }
                if (result == null) delay(SCAN_POLL_MS)
            }
            _state.update {
                it.copy(
                    scanning = false,
                    networks = result ?: it.networks,
                    // Trước đây quét hụt là im lặng, người dùng chỉ thấy danh sách trống mà
                    // không biết vì sao và cũng không biết là vẫn gõ tay được.
                    status = if (result.isNullOrEmpty()) {
                        "No 2.4 GHz network found. Tap Rescan, or type the network name below."
                    } else {
                        null
                    },
                )
            }
        }
    }

    /** Từ bước QR quay về chọn Wi-Fi — giống nút "← Quay lại chọn Wi-Fi" của trang web. */
    fun backToWifi() = _state.update { it.copy(step = Step.WIFI, error = null, status = null) }

    /** Tải lại: dò lại gateway, và nếu đang kết nối thì quét lại danh sách Wi-Fi. */
    fun reload() {
        if (client == null) discover() else refreshScan()
    }

    fun selectNetwork(ssid: String) = _state.update { it.copy(ssid = ssid, error = null) }
    fun setSsid(value: String) = _state.update { it.copy(ssid = value, error = null) }
    fun setPassword(value: String) = _state.update { it.copy(password = value, error = null) }
    fun dismissError() = _state.update { it.copy(error = null) }

    // ---- Kiểm tra Wi-Fi rồi sang bước sau ----

    fun continueFromWifi() {
        val portal = client ?: return
        val snapshot = _state.value
        if (snapshot.ssid.isBlank()) {
            _state.update { it.copy(error = "Pick a network or type its name.") }
            return
        }
        viewModelScope.launch {
            _state.update {
                it.copy(busy = true, error = null, status = "The ESP32 is joining the network…")
            }
            val outcome = runCatching {
                withContext(Dispatchers.IO) {
                    portal.startWifiCheck(snapshot.ssid, snapshot.password)
                }
                pollWifiCheck(portal)
            }
            outcome.onFailure { error ->
                _state.update {
                    it.copy(busy = false, status = null, error = error.message ?: "Could not verify the Wi-Fi connection.")
                }
                return@launch
            }
            // Thiết bị đã ghép thì giữ nguyên mã IoT, không bắt quét QR lại.
            if (_state.value.alreadyPaired) {
                saveConfiguration(qr = null)
            } else {
                _state.update {
                    it.copy(
                        busy = false,
                        step = Step.QR,
                        status = "Wi-Fi connected. Scan the device QR to pair.",
                    )
                }
            }
        }
    }

    private suspend fun pollWifiCheck(portal: PortalClient) {
        repeat(WIFI_CHECK_ATTEMPTS) {
            delay(WIFI_CHECK_POLL_MS)
            // AP có thể đổi kênh theo STA và rớt một nhịp; nuốt lỗi mạng rồi hỏi tiếp, chỉ
            // dừng khi chính gateway nói là hỏng.
            val check = withContext(Dispatchers.IO) {
                runCatching { portal.wifiCheck() }.getOrNull()
            } ?: return@repeat
            if (check.connected) return
            if (!check.connecting) {
                throw PortalException(check.error ?: "The ESP32 could not join the selected network.")
            }
        }
        throw PortalException("Wi-Fi check timed out. Double-check the password and try again.")
    }

    // ---- QR + lưu cấu hình ----

    fun applyQr(raw: String?) {
        val qr = ProvisioningQr.parse(raw)
        if (qr == null) {
            _state.update {
                it.copy(error = "Invalid QR — device code or API key is missing.")
            }
            return
        }
        saveConfiguration(qr)
    }

    private fun saveConfiguration(qr: ProvisioningQr?) {
        val portal = client ?: return
        val snapshot = _state.value
        val gateway = snapshot.gateway
        val deviceCode = qr?.deviceCode ?: gateway?.deviceCode.orEmpty()
        if (deviceCode.isEmpty()) {
            _state.update { it.copy(error = "No device code yet. Scan the QR first.") }
            return
        }

        // QR có quyền ghi đè env đang chọn: mã do Admin sinh cho đúng thiết bị đó.
        val backendUrl = qr?.backendUrl ?: snapshot.env.backendUrl
        val mqttHost = qr?.mqttHost ?: snapshot.env.mqttHost

        // Port và TLS phải đi LIỀN với host vừa chọn, không lấy mù theo env đang bật. QR trỏ
        // broker production trong khi app đang ở DEV mà vẫn gửi kèm port/TLS của DEV thì ra
        // một endpoint không tồn tại. Host lạ (không thuộc env nào) mới dùng env hiện tại.
        val transport = Environment.forMqttHost(mqttHost) ?: snapshot.env.environment

        val body = JSONObject()
            .put("wifiSsid", snapshot.ssid)
            .put("wifiPassword", snapshot.password)
            .put("backendUrl", backendUrl)
            .put("deviceCode", deviceCode)
            .put("apiKey", qr?.apiKey.orEmpty())
            .put("mqttHost", mqttHost)
            .put("mqttPort", transport.mqttPort)
            .put("mqttUseTls", transport.useTls)
            // Để trống = firmware giữ nguyên credential hiện có (trường write-only).
            .put("mqttUsername", "")
            .put("mqttPassword", "")

        viewModelScope.launch {
            _state.update { it.copy(step = Step.SAVING, busy = true, error = null, status = null) }
            val result = withContext(Dispatchers.IO) { runCatching { portal.save(body) } }
            result.fold(
                onSuccess = {
                    _state.update {
                        it.copy(step = Step.SUCCESS, busy = false, savedDeviceCode = deviceCode)
                    }
                },
                onFailure = { error ->
                    _state.update {
                        it.copy(
                            step = if (qr == null) Step.WIFI else Step.QR,
                            busy = false,
                            error = "Save failed: " + (error.message ?: "unknown error"),
                        )
                    }
                },
            )
        }
    }

    /** Ghi env đang chọn xuống gateway mà không đụng Wi-Fi hay mã IoT — dùng khi lệch env. */
    fun applyEnvironmentToGateway() {
        val portal = client ?: return
        val snapshot = _state.value
        val gateway = snapshot.gateway ?: return
        if (gateway.deviceCode.isEmpty()) {
            _state.update { it.copy(error = "Device is not paired — scan the QR first.") }
            return
        }
        val body = JSONObject()
            .put("wifiSsid", gateway.wifiSsid.ifEmpty { snapshot.ssid })
            .put("wifiPassword", "")
            .put("backendUrl", snapshot.env.backendUrl)
            .put("deviceCode", gateway.deviceCode)
            .put("apiKey", "")
            .put("mqttHost", snapshot.env.mqttHost)
            .put("mqttPort", snapshot.env.mqttPort)
            .put("mqttUseTls", snapshot.env.useTls)
            .put("mqttUsername", "")
            .put("mqttPassword", "")

        viewModelScope.launch {
            _state.update { it.copy(step = Step.SAVING, busy = true, error = null) }
            val result = withContext(Dispatchers.IO) { runCatching { portal.save(body) } }
            result.fold(
                onSuccess = {
                    _state.update {
                        it.copy(
                            step = Step.SUCCESS,
                            busy = false,
                            savedDeviceCode = gateway.deviceCode,
                        )
                    }
                },
                onFailure = { error ->
                    _state.update {
                        it.copy(
                            step = Step.WIFI,
                            busy = false,
                            error = "Could not apply the environment: " + (error.message ?: ""),
                        )
                    }
                },
            )
        }
    }

    // ---- Bảo trì ----

    /** Đồng bộ lại danh sách pin từ backend. Giữ Wi-Fi và mã IoT. */
    fun reprovision() = maintenance(
        title = "Resync requested",
        text = "The ESP32 is restarting and will reload the battery list from the backend. " +
            "Wi-Fi and IoT code are kept.",
    ) { it.reprovision() }

    /** Xoá liên kết IoT để ghép sang thiết bị khác. Giữ Wi-Fi. */
    fun newPairing() = maintenance(
        title = "Ready for a new pairing",
        text = "The ESP32 is restarting. Rejoin SolarGW-xxxx, then scan the QR of the new IoT.",
    ) { it.newPairing() }

    private fun maintenance(
        title: String,
        text: String,
        action: (PortalClient) -> Unit,
    ) {
        val portal = client ?: return
        viewModelScope.launch {
            _state.update { it.copy(step = Step.SAVING, busy = true, error = null) }
            val result = withContext(Dispatchers.IO) { runCatching { action(portal) } }
            result.fold(
                onSuccess = {
                    _state.update {
                        it.copy(
                            step = Step.SUCCESS,
                            busy = false,
                            successTitle = title,
                            successText = text,
                            successShowSummary = false,
                        )
                    }
                },
                onFailure = { error ->
                    _state.update {
                        it.copy(
                            step = Step.WIFI,
                            busy = false,
                            error = error.message ?: "Could not send the command to the gateway.",
                        )
                    }
                },
            )
        }
    }

    // ---- Cài đặt môi trường ----

    fun updateEnv(settings: EnvSettings) {
        envStore.save(settings)
        _state.update { it.copy(env = settings) }
    }

    fun selectEnvironment(environment: Environment) {
        val current = _state.value.env
        val next = if (environment == Environment.DEV && current.environment == Environment.DEV) {
            current
        } else {
            EnvSettings(environment, environment.defaultBackendUrl, environment.defaultMqttHost)
        }
        updateEnv(next)
    }

    // ---- Nạp firmware ----

    fun pickFirmware(uri: Uri) {
        val resolver = getApplication<Application>().contentResolver
        var name = "firmware.bin"
        var size = 0L
        runCatching {
            resolver.query(uri, null, null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (nameIndex >= 0) name = cursor.getString(nameIndex) ?: name
                    val sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE)
                    if (sizeIndex >= 0) size = cursor.getLong(sizeIndex)
                }
            }
        }
        _state.update {
            it.copy(firmware = FirmwareState(uri = uri, fileName = name, fileSize = size))
        }
    }

    fun uploadFirmware() {
        val portal = client ?: return
        val firmware = _state.value.firmware
        val uri = firmware.uri ?: return
        viewModelScope.launch {
            _state.update {
                it.copy(firmware = it.firmware.copy(uploading = true, sent = 0L, error = null))
            }
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    val resolver = getApplication<Application>().contentResolver
                    resolver.openInputStream(uri).use { stream ->
                        requireNotNull(stream) { "Could not open the selected file" }
                        portal.uploadFirmware(stream, firmware.fileSize, firmware.fileName) { sent ->
                            _state.update { it.copy(firmware = it.firmware.copy(sent = sent)) }
                        }
                    }
                }
            }
            result.fold(
                onSuccess = {
                    _state.update {
                        it.copy(
                            firmware = it.firmware.copy(uploading = false, done = true),
                            gateway = null,
                            step = Step.CONNECTING,
                        )
                    }
                    client = null
                    // ESP32 hẹn khởi động lại sau 1.5 s rồi mới dựng lại AP.
                    delay(REBOOT_WAIT_MS)
                    discover()
                },
                onFailure = { error ->
                    _state.update {
                        it.copy(
                            firmware = it.firmware.copy(
                                uploading = false,
                                error = error.message ?: "Firmware flash failed",
                            ),
                        )
                    }
                },
            )
        }
    }

    fun clearFirmware() = _state.update { it.copy(firmware = FirmwareState()) }

    fun restart() {
        client = null
        _state.update {
            UiState(env = it.env, step = Step.CONNECTING)
        }
        discover()
    }

    override fun onCleared() {
        networkCallback?.let { callback ->
            runCatching { connectivityManager?.unregisterNetworkCallback(callback) }
        }
        super.onCleared()
    }

    private companion object {
        const val SCAN_ATTEMPTS = 40
        const val SCAN_POLL_MS = 400L
        const val WIFI_CHECK_ATTEMPTS = 45
        const val WIFI_CHECK_POLL_MS = 800L
        const val REBOOT_WAIT_MS = 6000L
    }
}
