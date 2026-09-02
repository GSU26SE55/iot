package com.solarbms.setup.v2

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Cảnh báo lệch môi trường là thứ giữ cho switch DEV/PROD không trở thành switch giả: nếu nó
 * sai, app sẽ im lặng khi gateway đang trỏ broker của môi trường kia và người ta chỉ phát hiện
 * lúc MQTT không kết nối được.
 */
class EnvironmentTest {

    @Test
    fun `khop khi dung ca port lan tls`() {
        assertTrue(Environment.PRODUCTION.matches(mqttPort = 8883, useTls = true))
        assertTrue(Environment.DEV.matches(mqttPort = 21883, useTls = false))
    }

    @Test
    fun `lech khi gateway chay moi truong kia`() {
        assertFalse(Environment.PRODUCTION.matches(mqttPort = 21883, useTls = false))
        assertFalse(Environment.DEV.matches(mqttPort = 8883, useTls = true))
    }

    /** Đúng port nhưng sai TLS vẫn là lệch — nối sai lớp vận chuyển thì không bao giờ nối được. */
    @Test
    fun `dung port ma sai tls van la lech`() {
        assertFalse(Environment.PRODUCTION.matches(mqttPort = 8883, useTls = false))
        assertFalse(Environment.DEV.matches(mqttPort = 21883, useTls = true))
    }

    @Test
    fun `gia tri mac dinh khop voi config firmware`() {
        // config.h trong firmware-esp32 (dev) và config.prod.h (production)
        assertEquals(21883, Environment.DEV.mqttPort)
        assertFalse(Environment.DEV.useTls)
        assertEquals(8883, Environment.PRODUCTION.mqttPort)
        assertTrue(Environment.PRODUCTION.useTls)
        assertEquals("https://api.solaris.io.vn", Environment.PRODUCTION.defaultBackendUrl)
        assertEquals("mqtt.solaris.io.vn", Environment.PRODUCTION.defaultMqttHost)
    }

    /**
     * Regression cho cấu hình chết đã gặp trên thiết bị thật:
     * `mqtt.solaris.io.vn:21883 (TLS)` — host của production ghép với port dev. Bắt tay TLS
     * tới port đó không bao giờ thành công vì không có listener nào ở 21883 trên production.
     * Port và TLS phải suy ra TỪ HOST, không lấy theo env đang bật trong app.
     */
    @Test
    fun `port va tls suy ra tu mqtt host`() {
        assertEquals(Environment.PRODUCTION, Environment.forMqttHost("mqtt.solaris.io.vn"))
        assertEquals(Environment.DEV, Environment.forMqttHost("172.16.1.4"))
        assertEquals(Environment.PRODUCTION, Environment.forMqttHost("  MQTT.SOLARIS.IO.VN  "))
    }

    @Test
    fun `host la thi khong doan bua`() {
        assertNull(Environment.forMqttHost("broker.cua-khach.vn"))
        assertNull(Environment.forMqttHost(""))
    }

    /** Đúng bộ ba của production, đối chiếu với production/tls.conf (listener 8883). */
    @Test
    fun `bo ba production khop deployment`() {
        val prod = Environment.forMqttHost("mqtt.solaris.io.vn")!!
        assertEquals(8883, prod.mqttPort)
        assertTrue(prod.useTls)
    }
}
