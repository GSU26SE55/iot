package com.solarbms.setup.v2

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class ProvisioningQrTest {

    @Test
    fun `chap nhan ca ba scheme`() {
        assertNotNull(ProvisioningQr.parse("iot://provision?dc=GW-001&key=abc"))
        assertNotNull(ProvisioningQr.parse("https://admin.solaris.io.vn/p?dc=GW-001&key=abc"))
        assertNotNull(ProvisioningQr.parse("http://192.168.1.9/p?dc=GW-001&key=abc"))
    }

    @Test
    fun `chap nhan ca hai kieu ten tham so`() {
        val short = ProvisioningQr.parse("iot://provision?dc=GW-001&key=abc")
        val long = ProvisioningQr.parse("https://x.io/p?deviceCode=GW-001&apiKey=abc")
        assertEquals("GW-001", short?.deviceCode)
        assertEquals("GW-001", long?.deviceCode)
        assertEquals("abc", long?.apiKey)
    }

    @Test
    fun `tu choi khi thieu deviceCode hoac apiKey`() {
        assertNull(ProvisioningQr.parse("https://x.io/p?dc=GW-001"))
        assertNull(ProvisioningQr.parse("https://x.io/p?key=abc"))
        assertNull(ProvisioningQr.parse("https://x.io/p?dc=&key=abc"))
    }

    @Test
    fun `tu choi scheme la`() {
        assertNull(ProvisioningQr.parse("ftp://x.io/p?dc=A&key=B"))
        // iot:// phải trỏ đúng host "provision"
        assertNull(ProvisioningQr.parse("iot://something?dc=A&key=B"))
        assertNull(ProvisioningQr.parse("chi la mot chuoi thuong"))
        assertNull(ProvisioningQr.parse(""))
        assertNull(ProvisioningQr.parse(null))
    }

    /**
     * Bản Java cũ bỏ qua `api` và `mh` trong khi trang web của firmware lại đọc — cùng một mã
     * QR ra hai kết quả khác nhau. Test này chốt việc app đọc đủ như portal.
     */
    @Test
    fun `doc override backend va mqtt host giong portal`() {
        val qr = ProvisioningQr.parse(
            "iot://provision?dc=A&key=B&api=https://api.x.io&mh=mqtt.x.io",
        )
        assertEquals("https://api.x.io", qr?.backendUrl)
        assertEquals("mqtt.x.io", qr?.mqttHost)

        val longNames = ProvisioningQr.parse(
            "https://x.io/p?dc=A&key=B&backendUrl=http://10.0.0.2:4001&mqttHost=10.0.0.2",
        )
        assertEquals("http://10.0.0.2:4001", longNames?.backendUrl)
        assertEquals("10.0.0.2", longNames?.mqttHost)
    }

    @Test
    fun `khong co override thi tra null de app dung env dang chon`() {
        val qr = ProvisioningQr.parse("iot://provision?dc=A&key=B")
        assertNull(qr?.backendUrl)
        assertNull(qr?.mqttHost)
    }

    /** Portal từ chối backend URL không phải http(s); app phải từ chối y hệt. */
    @Test
    fun `tu choi backend url khong phai http`() {
        assertNull(ProvisioningQr.parse("iot://provision?dc=A&key=B&api=ftp://api.x.io"))
        assertNull(ProvisioningQr.parse("iot://provision?dc=A&key=B&api=api.x.io"))
    }
}
