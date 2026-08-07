// ==================================================================
// CA cert nhúng thẳng vào firmware, không đọc từ LittleFS.
//
// Lý do: local_queue.cpp và mqtt_client.cpp đều gọi LittleFS.begin(true)
// tức format-nếu-mount-lỗi. Ảnh do `mklittlefs` tạo không mount được với
// thư viện LittleFS trong firmware, nên phân vùng bị xoá sạch mỗi lần boot
// và cuốn theo ca_cert.pem. Nhúng vào flash chương trình thì hết phụ thuộc.
//
// Nguồn: iot/infra/mqtt/mosquitto/certs/ca.crt
// Sinh lại cert thì phải cập nhật file này, hoặc chạy:
//   cd iot/infra/mqtt/mosquitto/certs && \
//     awk 'BEGIN{print "static const char kMqttCaCert[] PROGMEM = R\"CERT("} \
//          {print} END{print ")CERT\";"}' ca.crt
//
// Cert này tự ký, CHỈ dùng cho dev. Production thì thay bằng CA thật.
// ==================================================================
#pragma once

// Không dùng PROGMEM: header này được include trước <Arduino.h> nên macro
// đó chưa tồn tại. Trên ESP32 hằng const nằm sẵn ở flash, không tốn RAM.
static const char kMqttCaCert[] = R"CERT(
-----BEGIN CERTIFICATE-----
MIIDqDCCApCgAwIBAgIUW9EyIvycObDAYx7nEvgp3XgLPYgwDQYJKoZIhvcNAQEL
BQAwbDELMAkGA1UEBhMCVk4xDjAMBgNVBAgMBUhhbm9pMQ4wDAYDVQQHDAVIYW5v
aTEdMBsGA1UECgwUR1NVMjZTRTU1IElvVCBEZXYgQ0ExHjAcBgNVBAMMFUdTVTI2
U0U1NSBJb1QgUm9vdCBDQTAeFw0yNjA4MDUxNDM1MDZaFw0zNjA4MDIxNDM1MDZa
MGwxCzAJBgNVBAYTAlZOMQ4wDAYDVQQIDAVIYW5vaTEOMAwGA1UEBwwFSGFub2kx
HTAbBgNVBAoMFEdTVTI2U0U1NSBJb1QgRGV2IENBMR4wHAYDVQQDDBVHU1UyNlNF
NTUgSW9UIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDB
LiQRle2fJ4YNNNzdToTBBcIyrM12dFZTjzOqrImEhtQdz/6PTANn+TJJhJRuTID0
5NMT58UwDYJYDSkrHAWhCDBqVJF4MBqUhBKE5mbMZSmLagIaH83N5LgGr9QggunQ
9OJF9g4o5l2o5IhyThx+3mtsrN/+3HDdFmlPB+1eGVFSg69hNfMimqRtizudKrMp
R+owKnYveRNDPoH1gD7DUoG8TYWTYQc60PYsKDrCX9Vwnx/c+U5q7DU1N/n9iQ/J
Q9jwJ2TBHmsrKShaWmUbexWwhbaNgPWwwLz8YrDYeixjUbNyaO7qO+V7qbpLwTXB
1hpifSesy3CTTonRKvNbAgMBAAGjQjBAMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0P
AQH/BAQDAgEGMB0GA1UdDgQWBBSF8l385g67v/GVg3+up8Drq7qSCzANBgkqhkiG
9w0BAQsFAAOCAQEADuBNfNt5P2Z3i+LsLAgUMin5ssO0kJbLfg2a9oosImzki+F0
SAgYn+vI7gBDxZLVYtrYOiw19Ns8WbeuZRIgRjdA3nAhEhfVQmda/B6pTnEpxby9
6//G8r/Jxiu4Hobiz/jxoFEov6LXex5obps+U5IS5pKjBw8eZqNlR6/KmtazMdc2
zJNbhN6vmbSK1dZBJsh8VbFABCjukGx2Xj9uz3WAMJUwfrKsAwlfw9ZNqDuK1svu
0DUuF0JGyeZZtkzLLLrugK0QLq/j/zmJXUJ2cW3nqhHM/EBLTJ8t1UrySarp6zRO
OUDjSr8oWDYALDyghUnKJ+EqHVnHjFgC+OFKRA==
-----END CERTIFICATE-----
)CERT";
