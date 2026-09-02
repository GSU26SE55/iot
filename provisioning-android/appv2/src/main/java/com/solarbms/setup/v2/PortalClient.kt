package com.solarbms.setup.v2

import android.net.Network
import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

/** Một mạng Wi-Fi mà gateway quét thấy. */
data class WifiNetwork(val ssid: String, val rssi: Int, val secure: Boolean)

/**
 * Gộp các bản ghi trùng SSID, giữ bản sóng khoẻ nhất, rồi xếp mạnh trước.
 *
 * Một lượt quét THƯỜNG trả trùng: cùng một tên mạng phát từ nhiều AP (mesh, repeater, hoặc
 * hai băng tần chung SSID). Trang web trong firmware lọc bằng `Set` trước khi render; bản
 * Kotlin đầu tiên bỏ sót việc đó và `LazyColumn` với `key = ssid` ném
 * `IllegalArgumentException: Key "..." was already used` làm sập app ngay khi quét ra kết quả.
 */
fun dedupeNetworks(items: List<WifiNetwork>): List<WifiNetwork> {
    val strongest = LinkedHashMap<String, WifiNetwork>()
    for (item in items) {
        if (item.ssid.isEmpty()) continue
        val existing = strongest[item.ssid]
        if (existing == null || item.rssi > existing.rssi) strongest[item.ssid] = item
    }
    return strongest.values.sortedByDescending { it.rssi }
}

/** Ảnh chụp `GET /api/config` của gateway. */
data class PortalConfig(
    val wifiSsid: String,
    val backendUrl: String,
    val deviceCode: String,
    val hasApiKey: Boolean,
    val mqttHost: String,
    val mqttPort: Int,
    val mqttUseTls: Boolean,
    val stationConnected: Boolean,
    val stationIp: String,
    val apActive: Boolean,
    val fwVersion: String,
)

/** Trạng thái thử kết nối Wi-Fi của gateway. */
data class WifiCheck(
    val connected: Boolean,
    val connecting: Boolean,
    val ssid: String,
    val error: String?,
)

class PortalException(message: String) : IOException(message)

/**
 * Nói chuyện với portal của ESP32.
 *
 * MỌI request đi qua [network] chứ không qua route mặc định của máy. SoftAP của gateway không
 * có internet nên Android sẽ không tự chọn nó — để hệ thống quyết thì request bay ra 4G và
 * không bao giờ tới được thiết bị.
 *
 * Bản Java cũ giải quyết bằng `bindProcessToNetwork()`: bind CẢ TIẾN TRÌNH vào mạng Wi-Fi ĐẦU
 * TIÊN tìm thấy, không kiểm tra đó có phải gateway không. Máy đang bắt Wi-Fi nhà là bind nhầm,
 * và mọi traffic khác của app cũng bị kéo theo. Ở đây không đoán — xem [discover].
 */
class PortalClient(private val network: Network, val host: String) {

    fun config(): PortalConfig {
        val json = getJson("/api/config")
        return PortalConfig(
            wifiSsid = json.optString("wifiSsid"),
            backendUrl = json.optString("backendUrl"),
            deviceCode = json.optString("deviceCode"),
            hasApiKey = json.optBoolean("hasApiKey"),
            mqttHost = json.optString("mqttHost"),
            mqttPort = json.optInt("mqttPort"),
            mqttUseTls = json.optBoolean("mqttUseTls"),
            stationConnected = json.optBoolean("stationConnected"),
            stationIp = json.optString("stationIp"),
            apActive = json.optBoolean("apActive"),
            fwVersion = json.optString("fwVersion"),
        )
    }

    /** Null = gateway báo đang quét (HTTP 202); gọi lại sau một nhịp. */
    fun scanResults(): List<WifiNetwork>? {
        val connection = open("/api/networks")
        try {
            val code = connection.responseCode
            val body = readBody(connection, code)
            if (code == 202) return null
            val json = parse(code, body)
            if (json.optBoolean("scanning")) return null
            val items = json.optJSONArray("networks") ?: JSONArray()
            val out = ArrayList<WifiNetwork>(items.length())
            for (index in 0 until items.length()) {
                val item = items.optJSONObject(index) ?: continue
                val ssid = item.optString("ssid")
                if (ssid.isEmpty()) continue
                out.add(WifiNetwork(ssid, item.optInt("rssi"), item.optBoolean("secure")))
            }
            // Lọc trùng NGAY TẠI ĐÂY chứ không ở UI: mọi nơi đọc kết quả quét đều cần danh
            // sách đã sạch, và SSID phải là khoá duy nhất dùng được.
            return dedupeNetworks(out)
        } finally {
            connection.disconnect()
        }
    }

    fun startWifiCheck(ssid: String, password: String) {
        postJson(
            "/api/wifi/check",
            JSONObject().put("wifiSsid", ssid).put("wifiPassword", password),
        )
    }

    fun wifiCheck(): WifiCheck {
        val json = getJson("/api/wifi/check")
        return WifiCheck(
            connected = json.optBoolean("connected"),
            connecting = json.optBoolean("connecting"),
            ssid = json.optString("ssid"),
            error = json.optString("error").takeIf { it.isNotEmpty() },
        )
    }

    fun save(body: JSONObject) {
        postJson("/api/config", body)
    }

    /**
     * Đồng bộ lại cấu hình pin: xoá cờ provision rồi khởi động lại, gateway sẽ tải danh sách
     * pin mới từ backend. Wi-Fi và mã IoT giữ nguyên.
     */
    fun reprovision() {
        postJson("/api/reprovision", JSONObject())
    }

    /**
     * Xoá liên kết IoT hiện tại (mã thiết bị, API key, Site, mapping pin, MQTT) để ghép sang
     * một IoT khác. Wi-Fi hiện tại được giữ lại.
     */
    fun newPairing() {
        postJson("/api/new-pairing", JSONObject())
    }

    /**
     * Đẩy firmware .bin qua SoftAP. Chặn luồng gọi — chạy trên Dispatchers.IO.
     *
     * Thao tác THỦ CÔNG cục bộ, không dính backend. Firmware xác thực ngay ở chunk đầu và
     * kiểm magic byte 0xE9, nên file sai bị từ chối trước khi ghi được gì vào flash.
     */
    fun uploadFirmware(
        input: InputStream,
        size: Long,
        fileName: String,
        onProgress: (Long) -> Unit,
    ): Long {
        val boundary = "----SolarSetupV2Boundary"
        val head = (
            "--$boundary$CRLF" +
                "Content-Disposition: form-data; name=\"firmware\"; filename=\"$fileName\"$CRLF" +
                "Content-Type: application/octet-stream$CRLF$CRLF"
            ).toByteArray()
        val tail = "$CRLF--$boundary--$CRLF".toByteArray()

        val connection = open("/api/ota")
        connection.requestMethod = "POST"
        connection.doOutput = true
        connection.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
        // Hơn một megabyte: không gom vào RAM. Cũng không dùng chunked — WebServer của ESP32
        // không xử lý transfer-encoding chunked cho upload.
        connection.setFixedLengthStreamingMode(head.size + size + tail.size)
        connection.readTimeout = UPLOAD_READ_TIMEOUT_MS

        var sent = 0L
        try {
            BufferedOutputStream(connection.outputStream, BUFFER_SIZE).use { out ->
                out.write(head)
                val buffer = ByteArray(BUFFER_SIZE)
                while (true) {
                    val read = input.read(buffer)
                    if (read <= 0) break
                    out.write(buffer, 0, read)
                    sent += read
                    onProgress(sent)
                }
                out.write(tail)
                out.flush()
            }
            val code = connection.responseCode
            parse(code, readBody(connection, code))
        } finally {
            connection.disconnect()
        }
        return sent
    }

    // ---- HTTP ----

    private fun open(path: String): HttpURLConnection {
        // openConnection() TRÊN Network là cả điểm mấu chốt: nó ghim socket vào đúng mạng của
        // gateway mà không đụng gì tới route của phần còn lại trong máy.
        val connection = network.openConnection(URL("http://$host:$PORT$path")) as HttpURLConnection
        connection.setRequestProperty("Authorization", BASIC_AUTH)
        connection.setRequestProperty("Cache-Control", "no-store")
        connection.connectTimeout = CONNECT_TIMEOUT_MS
        connection.readTimeout = READ_TIMEOUT_MS
        return connection
    }

    /** Đọc body dù mã trả về là gì — lỗi của portal cũng là JSON và có thông điệp dùng được. */
    private fun readBody(connection: HttpURLConnection, code: Int): String {
        val stream = if (code in 200..299) connection.inputStream else connection.errorStream
        return stream?.bufferedReader()?.use { it.readText() }.orEmpty()
    }

    private fun getJson(path: String): JSONObject {
        val connection = open(path)
        return try {
            val code = connection.responseCode
            parse(code, readBody(connection, code))
        } finally {
            connection.disconnect()
        }
    }

    private fun postJson(path: String, body: JSONObject): JSONObject {
        val connection = open(path)
        connection.requestMethod = "POST"
        connection.doOutput = true
        connection.setRequestProperty("Content-Type", "application/json")
        val payload = body.toString().toByteArray()
        connection.setFixedLengthStreamingMode(payload.size)
        return try {
            connection.outputStream.use { it.write(payload) }
            val code = connection.responseCode
            parse(code, readBody(connection, code))
        } finally {
            connection.disconnect()
        }
    }

    private fun parse(code: Int, body: String): JSONObject {
        val json = try {
            if (body.isBlank()) JSONObject() else JSONObject(body)
        } catch (error: Exception) {
            if (code in 200..299) JSONObject() else throw PortalException("Gateway returned HTTP $code")
        }
        if (code !in 200..299) {
            throw PortalException(
                json.optString("error").takeIf { it.isNotEmpty() } ?: "Gateway returned HTTP $code",
            )
        }
        return json
    }

    companion object {
        const val PORT = 8080

        /**
         * Một danh sách host DUY NHẤT. Bản Java cũ chép nó ra năm chỗ — `PORTAL_URLS`, vòng lặp
         * gán basic-auth, `isPortalHost()`, `network_security_config.xml` và một chuỗi trong
         * `strings.xml` — nên thêm một host là phải sửa năm nơi.
         */
        val HOSTS = listOf("192.168.4.1", "solar-gateway.local", "solargw.local")

        private const val USER = "admin"
        private const val PASSWORD = "12345678"
        private const val CONNECT_TIMEOUT_MS = 4000
        private const val READ_TIMEOUT_MS = 8000

        // Ghi 1.25 MB qua SoftAP mất 30-60 giây, rộng tay hơn timeout thường.
        private const val UPLOAD_READ_TIMEOUT_MS = 90_000
        private const val BUFFER_SIZE = 8 * 1024
        private const val CRLF = "\r\n"

        private val BASIC_AUTH: String by lazy {
            "Basic " + Base64.encodeToString("$USER:$PASSWORD".toByteArray(), Base64.NO_WRAP)
        }

        /**
         * Tìm mạng nào trong [networks] thật sự nói chuyện được với gateway.
         *
         * Không suy đoán từ SSID hay capability: gọi thử `/api/config` qua từng mạng, mạng nào
         * trả JSON hợp lệ thì đó là gateway. Kiểm tra thật, và nó loại luôn trường hợp máy đang
         * bắt Wi-Fi nhà mà app tưởng đã nối được thiết bị.
         */
        fun discover(networks: Collection<Network>): PortalClient? {
            for (network in networks) {
                for (host in HOSTS) {
                    val candidate = PortalClient(network, host)
                    if (runCatching { candidate.config() }.isSuccess) return candidate
                }
            }
            return null
        }
    }
}
