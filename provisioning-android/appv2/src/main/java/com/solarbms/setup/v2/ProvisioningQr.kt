package com.solarbms.setup.v2

import java.net.URI
import java.net.URLDecoder

/**
 * Nội dung QR ghép thiết bị do web Admin sinh ra.
 *
 * Dùng `java.net.URI` chứ KHÔNG dùng `android.net.Uri`: lớp của Android không chạy được
 * trong unit test JVM, mà đây đúng là phần cần test nhất.
 *
 * Bản Java cũ chỉ đọc `dc` và `key`, bỏ qua `api`/`mh` — trong khi trang web trong firmware
 * (`setup_portal.cpp`, readProvisioningQr) lại đọc cả bốn. Hai bên hiểu khác nhau về cùng
 * một mã QR. Ở đây đọc đủ bốn cho khớp portal.
 */
data class ProvisioningQr(
    val deviceCode: String,
    val apiKey: String,
    /** Ghi đè backend URL cho riêng thiết bị này. Null = dùng env đang chọn trong app. */
    val backendUrl: String?,
    /** Ghi đè MQTT host. Null = dùng env đang chọn trong app. */
    val mqttHost: String?,
) {
    companion object {
        /**
         * Trả null nếu chuỗi không phải QR ghép thiết bị hợp lệ.
         *
         * Hợp lệ = URL phân tích được, scheme thuộc {iot://provision, http, https}, có cả
         * mã thiết bị lẫn API key, và nếu có `api` thì nó phải là http(s).
         */
        fun parse(raw: String?): ProvisioningQr? {
            val text = raw?.trim().orEmpty()
            if (text.isEmpty()) return null

            val uri = try {
                URI(text)
            } catch (ignored: Exception) {
                return null
            }

            val scheme = uri.scheme?.lowercase() ?: return null
            when (scheme) {
                "iot" -> if (!"provision".equals(uri.authority, ignoreCase = true)) return null
                "http", "https" -> Unit
                else -> return null
            }

            val params = parseQuery(uri.rawQuery)
            val deviceCode = params.firstNotBlank("dc", "deviceCode") ?: return null
            val apiKey = params.firstNotBlank("key", "apiKey") ?: return null

            val backendUrl = params.firstNotBlank("api", "backendUrl")
            if (backendUrl != null && !isHttpUrl(backendUrl)) return null

            return ProvisioningQr(
                deviceCode = deviceCode,
                apiKey = apiKey,
                backendUrl = backendUrl,
                mqttHost = params.firstNotBlank("mh", "mqttHost"),
            )
        }

        fun isHttpUrl(value: String): Boolean {
            val lower = value.lowercase()
            return lower.startsWith("http://") || lower.startsWith("https://")
        }

        private fun parseQuery(rawQuery: String?): Map<String, String> {
            if (rawQuery.isNullOrEmpty()) return emptyMap()
            val out = LinkedHashMap<String, String>()
            for (pair in rawQuery.split("&")) {
                if (pair.isEmpty()) continue
                val idx = pair.indexOf('=')
                if (idx <= 0) continue
                val key = decode(pair.substring(0, idx))
                // Tham số lặp: giữ giá trị ĐẦU TIÊN, giống cách URLSearchParams.get() của trình
                // duyệt hành xử — để app và trang web đọc cùng một QR ra cùng một kết quả.
                if (!out.containsKey(key)) out[key] = decode(pair.substring(idx + 1))
            }
            return out
        }

        private fun decode(value: String): String = try {
            URLDecoder.decode(value, "UTF-8")
        } catch (ignored: Exception) {
            value
        }

        private fun Map<String, String>.firstNotBlank(vararg keys: String): String? {
            for (key in keys) {
                val value = this[key]?.trim()
                if (!value.isNullOrEmpty()) return value
            }
            return null
        }
    }
}
