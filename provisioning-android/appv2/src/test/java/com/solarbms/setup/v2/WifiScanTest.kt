package com.solarbms.setup.v2

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Regression cho crash đã gặp trên Mi 11:
 * `IllegalArgumentException: Key "Trung" was already used` — LazyColumn dùng SSID làm key,
 * mà một lượt quét thật thường trả trùng SSID (mesh, repeater, hai băng tần chung tên).
 */
class WifiScanTest {

    @Test
    fun `gop ssid trung va giu ban song khoe nhat`() {
        val result = dedupeNetworks(
            listOf(
                WifiNetwork("Trung", -70, true),
                WifiNetwork("Mint", -55, true),
                WifiNetwork("Trung", -42, true),
                WifiNetwork("Mint", -80, true),
            ),
        )
        assertEquals(2, result.size)
        assertEquals(listOf("Trung", "Mint"), result.map { it.ssid })
        assertEquals(-42, result.first { it.ssid == "Trung" }.rssi)
        assertEquals(-55, result.first { it.ssid == "Mint" }.rssi)
    }

    /** Đây chính là điều kiện mà LazyColumn cần: SSID phải là khoá duy nhất. */
    @Test
    fun `ssid la duy nhat sau khi loc`() {
        val result = dedupeNetworks(List(5) { WifiNetwork("Cung-Mot-Ten", -50 - it, true) })
        assertEquals(1, result.size)
        assertEquals(result.size, result.map { it.ssid }.toSet().size)
    }

    @Test
    fun `xep song manh truoc`() {
        val result = dedupeNetworks(
            listOf(
                WifiNetwork("Yeu", -88, false),
                WifiNetwork("Manh", -35, true),
                WifiNetwork("Vua", -60, true),
            ),
        )
        assertEquals(listOf("Manh", "Vua", "Yeu"), result.map { it.ssid })
    }

    @Test
    fun `bo ssid rong va danh sach rong`() {
        assertEquals(emptyList<WifiNetwork>(), dedupeNetworks(emptyList()))
        assertEquals(
            listOf("Co-Ten"),
            dedupeNetworks(
                listOf(WifiNetwork("", -40, false), WifiNetwork("Co-Ten", -50, true)),
            ).map { it.ssid },
        )
    }
}
