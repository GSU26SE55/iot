#include "portal/setup_portal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>

#include "config/device_identity.h"
#include "config/runtime_config.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#ifndef CONFIG_PORTAL_PORT
  #define CONFIG_PORTAL_PORT 8080
#endif
#ifndef CONFIG_PORTAL_USER
  #define CONFIG_PORTAL_USER "admin"
#endif
#ifndef CONFIG_PORTAL_PASSWORD
  #define CONFIG_PORTAL_PASSWORD "solar-setup"
#endif
#ifndef CONFIG_PORTAL_AP_PREFIX
  #define CONFIG_PORTAL_AP_PREFIX "SolarBMS"
#endif
#ifndef CONFIG_PORTAL_HOSTNAME
  #define CONFIG_PORTAL_HOSTNAME "solar-gateway"
#endif
#ifndef CONFIG_PORTAL_AP_FALLBACK_MS
  #define CONFIG_PORTAL_AP_FALLBACK_MS 30000UL
#endif

namespace portal {
namespace {

WebServer s_server(CONFIG_PORTAL_PORT);
DNSServer s_dns;
bool s_started = false;
bool s_apActive = false;
bool s_mdnsStarted = false;
bool s_restartPending = false;
uint32_t s_restartAtMs = 0;
uint32_t s_offlineSinceMs = 0;

const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="vi"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Solar BMS Setup</title><style>
:root{color-scheme:dark;--bg:#08111f;--card:#101d30;--line:#29405d;--text:#edf5ff;--muted:#9eb2c9;--accent:#36d399;--danger:#ff7b72}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#15304c,var(--bg) 55%);color:var(--text);font:15px system-ui,sans-serif;min-height:100vh}
main{max-width:760px;margin:auto;padding:28px 16px 48px}h1{margin:0 0 6px;font-size:28px}p{color:var(--muted);line-height:1.5}.status{display:flex;gap:8px;flex-wrap:wrap;margin:18px 0}.pill{padding:7px 11px;border:1px solid var(--line);border-radius:999px;background:#0c1828}
form{display:grid;gap:16px}.card{background:rgba(16,29,48,.95);border:1px solid var(--line);border-radius:16px;padding:18px;box-shadow:0 16px 40px #0005}.card h2{font-size:18px;margin:0 0 14px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}label{display:grid;gap:6px;color:var(--muted);font-size:13px}.full{grid-column:1/-1}input{width:100%;border:1px solid var(--line);border-radius:9px;background:#091525;color:var(--text);padding:11px;font:inherit}input:focus{outline:2px solid #36d39966;border-color:var(--accent)}.check{display:flex;align-items:center;gap:8px}.check input{width:auto}
button{border:0;border-radius:10px;padding:13px 18px;background:var(--accent);color:#032319;font-weight:750;font-size:15px;cursor:pointer}button:disabled{opacity:.55}.note{font-size:12px;margin:8px 0 0}.message{min-height:22px;font-weight:650}.ok{color:var(--accent)}.err{color:var(--danger)}@media(max-width:600px){.grid{grid-template-columns:1fr}.full{grid-column:auto}}
</style></head><body><main>
<h1>Solar BMS Gateway</h1><p>Cấu hình mạng và địa chỉ dịch vụ trực tiếp trên ESP32. Mật khẩu đang lưu không bao giờ được gửi lại ra trình duyệt.</p>
<div class="status"><span class="pill" id="network">Đang đọc trạng thái…</span><span class="pill" id="address"></span></div>
<form id="setupForm">
<section class="card"><h2>Wi‑Fi 2.4 GHz</h2><div class="grid">
<label class="full">Tên Wi‑Fi (SSID)<input id="wifiSsid" maxlength="32" required></label>
<label class="full">Mật khẩu Wi‑Fi<input id="wifiPassword" type="password" maxlength="64" placeholder="Để trống để giữ nguyên"></label>
</div></section>
<section class="card"><h2>Backend</h2><div class="grid">
<label class="full">Backend URL<input id="backendUrl" maxlength="159" placeholder="http://192.168.1.10:4006" required></label>
<label>Device code<input id="deviceCode" maxlength="63" required></label>
<label>API key<input id="apiKey" type="password" maxlength="95" placeholder="Để trống để giữ nguyên"></label>
</div></section>
<section class="card"><h2>MQTT</h2><div class="grid">
<label>Broker host<input id="mqttHost" maxlength="95" required></label>
<label>Port<input id="mqttPort" type="number" min="1" max="65535" required></label>
<label>Username<input id="mqttUsername" maxlength="64"></label>
<label>Password<input id="mqttPassword" type="password" maxlength="96" placeholder="Để trống để giữ nguyên"></label>
<label class="check full"><input id="mqttUseTls" type="checkbox"> Dùng TLS</label>
</div></section>
<button id="saveButton" type="submit">Lưu và khởi động lại ESP32</button><div id="message" class="message"></div>
</form><p class="note">Trang này chỉ nên dùng trong mạng nội bộ. Sau khi đổi Wi‑Fi, hãy kết nối điện thoại/máy tính vào mạng mới rồi mở lại địa chỉ của ESP32.</p>
<script>
const $=id=>document.getElementById(id),msg=$('message');
async function load(){try{const r=await fetch('/api/config',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const c=await r.json();
for(const k of ['wifiSsid','backendUrl','deviceCode','mqttHost','mqttPort','mqttUsername'])$(k).value=c[k]??'';$('mqttUseTls').checked=!!c.mqttUseTls;
$('network').textContent=c.stationConnected?'Wi‑Fi: đã kết nối':'Wi‑Fi: chưa kết nối';$('address').textContent=c.stationConnected?c.stationIp:(c.apActive?'AP: '+c.apIp:'');
}catch(e){msg.className='message err';msg.textContent='Không đọc được cấu hình: '+e.message}}
$('setupForm').addEventListener('submit',async e=>{e.preventDefault();$('saveButton').disabled=true;msg.className='message';msg.textContent='Đang lưu…';
const body={wifiSsid:$('wifiSsid').value.trim(),wifiPassword:$('wifiPassword').value,backendUrl:$('backendUrl').value.trim(),deviceCode:$('deviceCode').value.trim(),apiKey:$('apiKey').value,mqttHost:$('mqttHost').value.trim(),mqttPort:Number($('mqttPort').value),mqttUseTls:$('mqttUseTls').checked,mqttUsername:$('mqttUsername').value.trim(),mqttPassword:$('mqttPassword').value};
try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const out=await r.json();if(!r.ok)throw Error(out.error||('HTTP '+r.status));msg.className='message ok';msg.textContent='Đã lưu. ESP32 đang khởi động lại…';}catch(err){msg.className='message err';msg.textContent='Lưu thất bại: '+err.message;$('saveButton').disabled=false;}});load();
</script></main></body></html>)HTML";

bool authenticated() {
  if (s_server.authenticate(CONFIG_PORTAL_USER, CONFIG_PORTAL_PASSWORD)) return true;
  s_server.requestAuthentication(BASIC_AUTH, "Solar BMS Setup", "Login required");
  return false;
}

void sendJson(int statusCode, const JsonDocument& document) {
  String response;
  serializeJson(document, response);
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(statusCode, "application/json; charset=utf-8", response);
}

void sendError(int statusCode, const char* message) {
  JsonDocument document;
  document["ok"] = false;
  document["error"] = message;
  sendJson(statusCode, document);
}

bool tooLong(const char* value, size_t capacity) {
  return value == nullptr || strlen(value) >= capacity;
}

void copySafe(char* destination, size_t destinationLen, const char* source) {
  strncpy(destination, source, destinationLen - 1);
  destination[destinationLen - 1] = '\0';
}

void handleGetConfig() {
  if (!authenticated()) return;
  const runtimecfg::RuntimeConfig& config = runtimecfg::runtimeConfig();
  JsonDocument document;
  document["wifiSsid"] = config.wifiSsid;
  document["hasWifiPassword"] = config.wifiPassword[0] != '\0';
  document["backendUrl"] = config.backendUrl;
  document["deviceCode"] = identity::deviceCode();
  document["hasApiKey"] = identity::apiKey()[0] != '\0';
  document["mqttHost"] = config.mqttHost;
  document["mqttPort"] = config.mqttPort;
  document["mqttUseTls"] = config.mqttUseTls;
  document["mqttUsername"] = config.mqttUsername;
  document["hasMqttPassword"] = config.mqttPassword[0] != '\0';
  document["stationConnected"] = WiFi.status() == WL_CONNECTED;
  document["stationIp"] = WiFi.localIP().toString();
  document["apActive"] = s_apActive;
  document["apIp"] = s_apActive ? WiFi.softAPIP().toString() : "";
  document["setupPort"] = CONFIG_PORTAL_PORT;
  sendJson(200, document);
}

void handleSaveConfig() {
  if (!authenticated()) return;
  JsonDocument document;
  DeserializationError error = deserializeJson(document, s_server.arg("plain"));
  if (error) {
    sendError(400, "JSON không hợp lệ");
    return;
  }

  const char* wifiSsid = document["wifiSsid"] | "";
  const char* wifiPassword = document["wifiPassword"] | "";
  const char* backendUrl = document["backendUrl"] | "";
  const char* deviceCode = document["deviceCode"] | "";
  const char* apiKey = document["apiKey"] | "";
  const char* mqttHost = document["mqttHost"] | "";
  const char* mqttUsername = document["mqttUsername"] | "";
  const char* mqttPassword = document["mqttPassword"] | "";
  const int mqttPort = document["mqttPort"] | 0;

  if (wifiSsid[0] == '\0' || backendUrl[0] == '\0' || deviceCode[0] == '\0' ||
      mqttHost[0] == '\0') {
    sendError(400, "Thiếu SSID, Backend URL, Device code hoặc MQTT host");
    return;
  }
  if (tooLong(wifiSsid, runtimecfg::kMaxWifiSsidLen) ||
      tooLong(wifiPassword, runtimecfg::kMaxWifiPasswordLen) ||
      tooLong(backendUrl, runtimecfg::kMaxBackendUrlLen) ||
      tooLong(deviceCode, identity::kMaxDeviceCodeLen) ||
      tooLong(apiKey, identity::kMaxApiKeyLen) ||
      tooLong(mqttHost, runtimecfg::kMaxMqttHostLen) ||
      tooLong(mqttUsername, runtimecfg::kMaxMqttUsernameLen) ||
      tooLong(mqttPassword, runtimecfg::kMaxMqttPasswordLen)) {
    sendError(400, "Một trường cấu hình vượt quá độ dài cho phép");
    return;
  }
  if (strncmp(backendUrl, "http://", 7) != 0 &&
      strncmp(backendUrl, "https://", 8) != 0) {
    sendError(400, "Backend URL phải bắt đầu bằng http:// hoặc https://");
    return;
  }
  if (mqttPort < 1 || mqttPort > 65535) {
    sendError(400, "MQTT port phải nằm trong khoảng 1-65535");
    return;
  }

  runtimecfg::RuntimeConfig next = runtimecfg::runtimeConfig();
  copySafe(next.wifiSsid, sizeof(next.wifiSsid), wifiSsid);
  if (wifiPassword[0] != '\0') {
    copySafe(next.wifiPassword, sizeof(next.wifiPassword), wifiPassword);
  }
  copySafe(next.backendUrl, sizeof(next.backendUrl), backendUrl);
  copySafe(next.mqttHost, sizeof(next.mqttHost), mqttHost);
  next.mqttPort = static_cast<uint16_t>(mqttPort);
  next.mqttUseTls = document["mqttUseTls"] | false;
  copySafe(next.mqttUsername, sizeof(next.mqttUsername), mqttUsername);
  if (mqttPassword[0] != '\0') {
    copySafe(next.mqttPassword, sizeof(next.mqttPassword), mqttPassword);
  }

  if (!runtimecfg::saveRuntimeConfig(next) ||
      !identity::setDeviceCode(deviceCode) ||
      (apiKey[0] != '\0' && !identity::setApiKey(apiKey))) {
    sendError(500, "Không ghi được cấu hình vào NVS");
    return;
  }

  JsonDocument response;
  response["ok"] = true;
  response["restarting"] = true;
  sendJson(200, response);
  s_restartPending = true;
  s_restartAtMs = millis() + 1500;
  Serial.println("[portal] configuration saved; restart scheduled");
}

void startAccessPoint() {
  if (s_apActive) return;
  uint64_t chipId = ESP.getEfuseMac();
  char ssid[40];
  snprintf(ssid, sizeof(ssid), "%s-%06llX", CONFIG_PORTAL_AP_PREFIX,
           static_cast<unsigned long long>(chipId & 0xFFFFFFULL));

  WiFi.mode(WIFI_AP_STA);
  const IPAddress ip(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(ip, ip, subnet);
  const char* apPassword = strlen(CONFIG_PORTAL_PASSWORD) >= 8
                               ? CONFIG_PORTAL_PASSWORD
                               : "SolarSetup1";
  if (!WiFi.softAP(ssid, apPassword)) {
    Serial.println("[portal] fallback AP start FAILED");
    return;
  }
  s_dns.start(53, "*", ip);
  s_apActive = true;
  Serial.printf("[portal] fallback AP=%s url=http://%s:%u\n", ssid,
                ip.toString().c_str(), static_cast<unsigned>(CONFIG_PORTAL_PORT));
  Serial.printf("[portal] login user=%s (password is CONFIG_PORTAL_PASSWORD)\n",
                CONFIG_PORTAL_USER);
}

void startMdnsIfPossible() {
  if (s_mdnsStarted || WiFi.status() != WL_CONNECTED) return;
  if (MDNS.begin(CONFIG_PORTAL_HOSTNAME)) {
    MDNS.addService("http", "tcp", CONFIG_PORTAL_PORT);
    s_mdnsStarted = true;
    Serial.printf("[portal] LAN url=http://%s.local:%u or http://%s:%u\n",
                  CONFIG_PORTAL_HOSTNAME, static_cast<unsigned>(CONFIG_PORTAL_PORT),
                  WiFi.localIP().toString().c_str(),
                  static_cast<unsigned>(CONFIG_PORTAL_PORT));
  } else {
    Serial.printf("[portal] LAN url=http://%s:%u (mDNS unavailable)\n",
                  WiFi.localIP().toString().c_str(),
                  static_cast<unsigned>(CONFIG_PORTAL_PORT));
  }
}

}  // namespace

void setupPortalBegin() {
  if (s_started) return;
  s_server.on("/", HTTP_GET, []() {
    if (!authenticated()) return;
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send_P(200, "text/html; charset=utf-8", kIndexHtml);
  });
  s_server.on("/api/config", HTTP_GET, handleGetConfig);
  s_server.on("/api/config", HTTP_POST, handleSaveConfig);
  s_server.onNotFound([]() {
    if (!authenticated()) return;
    s_server.sendHeader("Location", "/", true);
    s_server.send(302, "text/plain", "");
  });
  s_server.begin();
  s_started = true;
  s_offlineSinceMs = millis();
  Serial.printf("[portal] web setup listening on port %u\n",
                static_cast<unsigned>(CONFIG_PORTAL_PORT));

  if (WiFi.status() == WL_CONNECTED) startMdnsIfPossible();
  else startAccessPoint();
}

void setupPortalTick() {
  if (!s_started) return;
  s_server.handleClient();
  if (s_apActive) s_dns.processNextRequest();

  if (WiFi.status() == WL_CONNECTED) {
    s_offlineSinceMs = 0;
    startMdnsIfPossible();
  } else {
    if (s_offlineSinceMs == 0) s_offlineSinceMs = millis();
    if (!s_apActive && millis() - s_offlineSinceMs >= CONFIG_PORTAL_AP_FALLBACK_MS) {
      startAccessPoint();
    }
  }

  if (s_restartPending && static_cast<int32_t>(millis() - s_restartAtMs) >= 0) {
    Serial.println("[portal] restarting now");
    delay(50);
    ESP.restart();
  }
}

bool setupPortalApActive() {
  return s_apActive;
}

}  // namespace portal
