package com.solarbms.setup.v2

import android.content.Context
import android.content.SharedPreferences

/**
 * Môi trường mà gateway sẽ trỏ tới sau khi setup.
 *
 * Bốn giá trị này đi thành một bộ. Trước đây `mqttPort` và `mqttUseTls` bị khoá lúc biên dịch
 * firmware (`MQTT_BROKER_PORT` / `MQTT_USE_TLS`) nên đổi môi trường bắt buộc phải nạp lại
 * firmware. Firmware nay chọn lớp vận chuyển lúc chạy, nên switch trong app đổi được cả bốn.
 */
enum class Environment(
    val label: String,
    /** Nhãn ngắn cho chip trên thanh tiêu đề — "PRODUCTION" dài quá làm tiêu đề xuống dòng. */
    val shortLabel: String,
    val defaultBackendUrl: String,
    val defaultMqttHost: String,
    val mqttPort: Int,
    val useTls: Boolean,
) {
    DEV(
        label = "DEV",
        shortLabel = "DEV",
        defaultBackendUrl = "http://172.16.1.4:4001",
        defaultMqttHost = "172.16.1.4",
        mqttPort = 21883,
        useTls = false,
    ),
    PRODUCTION(
        label = "PRODUCTION",
        shortLabel = "PROD",
        defaultBackendUrl = "https://api.solaris.io.vn",
        defaultMqttHost = "mqtt.solaris.io.vn",
        mqttPort = 8883,
        useTls = true,
    );

    /** True khi gateway đang chạy đúng lớp vận chuyển của môi trường này. */
    fun matches(mqttPort: Int, useTls: Boolean): Boolean =
        mqttPort == this.mqttPort && useTls == this.useTls

    companion object {
        /**
         * Môi trường sở hữu broker này, hoặc null nếu là host lạ.
         *
         * Port và TLS là thuộc tính CỦA BROKER, không phải của lựa chọn trong app. QR do Admin
         * sinh có thể ghi đè host; ghép host ấy với port/TLS của env đang chọn sẽ tạo ra tổ hợp
         * không tồn tại — ví dụ broker production `mqtt.solaris.io.vn` cộng port dev `21883`,
         * một cấu hình mà không có ai lắng nghe nên MQTT vĩnh viễn không nối được.
         */
        fun forMqttHost(host: String): Environment? =
            entries.firstOrNull { it.defaultMqttHost.equals(host.trim(), ignoreCase = true) }
    }
}

/** Env đang chọn cộng với host có thể đã bị người dùng sửa tay (chỉ cho DEV). */
data class EnvSettings(
    val environment: Environment,
    val backendUrl: String,
    val mqttHost: String,
) {
    val mqttPort: Int get() = environment.mqttPort
    val useTls: Boolean get() = environment.useTls
}

/**
 * Lưu lựa chọn môi trường. SharedPreferences là đủ cho ba giá trị — DataStore ở đây chỉ là
 * thêm một dependency và một coroutine scope để làm đúng việc mà thứ này đã làm được.
 */
class EnvStore(context: Context) {
    private val prefs: SharedPreferences =
        context.getSharedPreferences("solar_setup_v2", Context.MODE_PRIVATE)

    fun load(): EnvSettings {
        val env = runCatching {
            Environment.valueOf(prefs.getString(KEY_ENV, null) ?: Environment.DEV.name)
        }.getOrDefault(Environment.DEV)
        return EnvSettings(
            environment = env,
            // Host chỉ sửa được ở DEV. PRODUCTION luôn đọc từ hằng số, kể cả khi prefs có
            // giá trị cũ sót lại — không để bấm nhầm rồi đẩy thiết bị tới một host lạ.
            backendUrl = if (env == Environment.DEV) {
                prefs.getString(KEY_BACKEND, null)?.takeIf { it.isNotBlank() }
                    ?: env.defaultBackendUrl
            } else {
                env.defaultBackendUrl
            },
            mqttHost = if (env == Environment.DEV) {
                prefs.getString(KEY_MQTT_HOST, null)?.takeIf { it.isNotBlank() }
                    ?: env.defaultMqttHost
            } else {
                env.defaultMqttHost
            },
        )
    }

    fun save(settings: EnvSettings) {
        prefs.edit()
            .putString(KEY_ENV, settings.environment.name)
            .putString(KEY_BACKEND, settings.backendUrl.trim())
            .putString(KEY_MQTT_HOST, settings.mqttHost.trim())
            .apply()
    }

    private companion object {
        const val KEY_ENV = "env"
        const val KEY_BACKEND = "backendUrl"
        const val KEY_MQTT_HOST = "mqttHost"
    }
}
