#include "portal/setup_portal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>

#include "config/device_identity.h"
#include "config/mqtt_config.h"
#include "config/nvs_store.h"
#include "config/runtime_config.h"
#include "config/wifi_config.h"
#include "core/net_config_rules.h"
#include "net/setup_portal.h"
#include "net/wifi_manager.h"
#include "provision/provision.h"
#include "queue/local_queue.h"

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
#ifndef CONFIG_PORTAL_HOSTNAME
  #define CONFIG_PORTAL_HOSTNAME "solar-gateway"
#endif

namespace portal {
namespace {

WebServer s_server(CONFIG_PORTAL_PORT);
bool s_started = false;
bool s_mdnsStarted = false;
bool s_restartPending = false;
uint32_t s_restartAtMs = 0;
bool s_wifiCheckActive = false;
uint32_t s_wifiCheckStartedAtMs = 0;
char s_wifiCheckSsid[wificfg::kSsidBufLen]{};

constexpr uint32_t kWifiCheckTimeoutMs = 30000UL;

extern const uint8_t kJsQrGzipStart[] asm("_binary_data_jsqr_1_4_0_js_gz_start");
extern const uint8_t kJsQrGzipEnd[] asm("_binary_data_jsqr_1_4_0_js_gz_end");

const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="vi"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="referrer" content="no-referrer">
<title>Solar Gateway Setup</title><style>
:root{color-scheme:light;--bg:#eef1f6;--panel:#fff;--border:#e2e5ec;--text:#1b2233;--muted:#6b7280;--brand:#3f5df0;--brand-dark:#3348c9;--soft:#eef1ff;--ok:#1a9d6c;--danger:#d64550;--shadow:0 12px 32px rgba(30,41,74,.08)}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);font:14px/1.5 Inter,ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:440px;margin:0 auto;padding:32px 16px 40px}
.panel{background:var(--panel);border:1px solid var(--border);border-radius:16px;box-shadow:var(--shadow);overflow:hidden}
.panel-head{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:16px 20px;border-bottom:1px solid var(--border)}
.brand{display:flex;align-items:center;gap:10px}
.brand-mark{width:34px;height:34px;flex:0 0 auto;border-radius:10px;background:var(--soft);color:var(--brand);display:grid;place-items:center;font-weight:800;font-size:16px}
.brand strong{display:block;font-size:14px;line-height:1.3}
.brand span{display:block;font-size:11px;color:var(--muted)}
.status-group{display:flex;flex-direction:column;align-items:flex-end;gap:4px}
.pill{font-size:11px;font-weight:700;color:var(--muted);display:inline-flex;align-items:center;gap:5px}
.pill:before{content:"";width:6px;height:6px;border-radius:50%;background:var(--ok);flex:0 0 auto}
.pill:empty{display:none}
.pill-muted{color:#9aa3b5}.pill-muted:before{display:none}
.steps{display:flex;align-items:center;gap:8px;padding:14px 20px 0}
.step{display:flex;align-items:center;gap:7px;color:#aab1c2;white-space:nowrap}
.step-number{width:20px;height:20px;flex:0 0 auto;border-radius:6px;background:#eef0f5;color:#9aa3b5;display:grid;place-items:center;font-size:11px;font-weight:800;transition:.2s}
.step b{display:block;font-size:11px;font-weight:800;color:inherit}
.step small{display:block;font-size:9.5px;font-weight:600;color:#c1c6d3;margin-top:1px}
.step.active{color:var(--brand)}.step.active .step-number{background:var(--brand);color:#fff}.step.active small{color:#9fb0f5}
.step-sep{flex:1;height:1px;background:var(--border)}
.lead{margin:10px 20px 0;font-size:12px;color:var(--muted)}
form,.step-block{padding:16px 20px 20px}
.section-title{font-size:11px;font-weight:800;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);margin-bottom:10px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
label{display:grid;gap:5px;color:#4b5468;font-size:11.5px;font-weight:700}
.full{grid-column:1/-1}
input{width:100%;border:1px solid var(--border);border-radius:9px;background:#f9fafc;color:var(--text);padding:10px 11px;font:inherit;transition:.15s}
input:hover{border-color:#ccd2e0}input:focus{outline:0;border-color:var(--brand);background:#fff;box-shadow:0 0 0 3px #3f5df01a}
.check{display:flex;align-items:center;gap:7px}.check input{width:auto}
button{border:0;border-radius:10px;padding:11px 16px;background:var(--brand);color:#fff;font-weight:700;font-size:13px;cursor:pointer;transition:.15s}
button:hover{background:var(--brand-dark)}
button.secondary{background:#f3f4f8;color:#4b5468;border:1px solid var(--border)}
button.secondary:hover{background:#eceef4}
button.danger{background:#fff5f5;color:var(--danger);border:1px solid #f3c7cb}
button.danger:hover{background:#ffe9eb}
button:disabled{opacity:.55;cursor:wait}
.full-btn{width:100%}
.message{min-height:18px;font-size:11.5px;font-weight:700;padding-top:8px}
.ok{color:var(--ok)}.err{color:var(--danger)}
.network-list{display:grid;gap:6px;max-height:180px;overflow:auto;margin-top:8px}
.network-option{display:flex;justify-content:space-between;align-items:center;width:100%;padding:9px 11px;text-align:left;border-radius:9px}
.network-option span:first-child{color:#374056;font-weight:700;font-size:12px}
.network-option span:last-child{color:#9aa3b5;font-size:11px}
.network-option.selected{border-color:#9cb0ff;background:var(--soft);color:var(--brand);box-shadow:0 0 0 2px #3f5df01a}
.network-option.selected span:first-child{color:var(--brand)}
.step-block h2{font-size:14px;margin:0 0 3px}
.step-block>p{margin:0 0 4px;color:var(--muted);font-size:12px}
.qr-scanner{position:relative;width:100%;max-width:260px;aspect-ratio:1;margin:14px auto;display:grid;place-items:center;background:#101728;border-radius:14px;overflow:hidden}
.qr-scanner img{width:100%;height:100%;object-fit:contain;background:#fff}
.qr-scanner.idle img{display:none}
.qr-icon{width:56px;height:56px;border:2px dashed #5f7be0;border-radius:10px;display:grid;place-items:center;color:#7d97ee;font-weight:800;letter-spacing:.06em;font-size:12px}
.qr-scanner:not(.idle) .qr-icon{display:none}
.corner{position:absolute;width:22px;height:22px;border-color:#fff;border-style:solid}
.corner.tl{left:10px;top:10px;border-width:2px 0 0 2px;border-radius:6px 0 0}
.corner.tr{right:10px;top:10px;border-width:2px 2px 0 0;border-radius:0 6px 0 0}
.corner.bl{left:10px;bottom:10px;border-width:0 0 2px 2px;border-radius:0 0 0 6px}
.corner.br{right:10px;bottom:10px;border-width:0 2px 2px 0;border-radius:0 0 6px}
.camera-button{width:100%;margin:4px 0}
.helper{font-size:11px;color:var(--muted);margin:8px 0 0}
.external-help{margin-top:14px;padding:12px 13px;border:1px solid var(--border);border-radius:10px;background:#f9fafc;font-size:11.5px;color:#4b5468;line-height:1.5}
.external-help b{display:block;color:var(--text);font-size:12px;margin-bottom:4px}
.external-help .manual{display:grid;grid-template-columns:1fr auto;gap:7px;margin-top:10px}
.external-help .manual button{padding:9px 12px}
.saving-block{text-align:center}
.loader{width:52px;height:52px;margin:6px auto 14px;border-radius:16px;background:var(--soft);display:grid;place-items:center}
.loader:before{content:"";width:22px;height:22px;border:3px solid #d2d9f7;border-top-color:var(--brand);border-radius:50%;animation:spin .8s linear infinite}
.saving-dots{display:flex;justify-content:center;gap:5px;margin-top:12px}
.saving-dots i{width:5px;height:5px;border-radius:50%;background:#c3cbe6;animation:dots 1.1s ease-in-out infinite}
.saving-dots i:nth-child(2){animation-delay:.15s}.saving-dots i:nth-child(3){animation-delay:.3s}
.hidden{display:none!important}
details{border-top:1px solid var(--border);padding-top:12px;margin-top:2px}
summary{cursor:pointer;color:var(--muted);margin-bottom:10px;font-size:11.5px;font-weight:700}
.note{font-size:11px;color:var(--muted);text-align:center;margin:0;padding:14px 20px 20px;border-top:1px solid var(--border)}
@keyframes spin{to{transform:rotate(360deg)}}
@keyframes dots{0%,100%{transform:scale(.6);opacity:.4}50%{transform:scale(1);opacity:1}}
@media(max-width:420px){.grid{grid-template-columns:1fr}}
@media(prefers-reduced-motion:reduce){*{animation:none!important}}
</style></head><body><main>
<div class="panel" id="panel">
<div class="panel-head"><div class="brand"><span class="brand-mark">⌁</span><div><strong>Solar Gateway</strong><span>Thiết lập thiết bị</span></div></div>
<div class="status-group"><span class="pill" id="network">Đang đọc trạng thái…</span><span class="pill pill-muted" id="address"></span></div></div>

<div class="steps"><div class="step active" id="stepOne"><span class="step-number">1</span><span><b>Kết nối Wi‑Fi</b><small>Chọn mạng 2.4 GHz</small></span></div><div class="step-sep"></div><div class="step" id="stepTwo"><span class="step-number">2</span><span><b>Ghép thiết bị</b><small>Quét QR từ Admin</small></span></div></div>
<p class="lead" id="flowLead">Cài đặt lần đầu hoàn toàn trên web, không cần cài APK.</p>

<form id="setupForm"><div class="section-title">Wi‑Fi</div><div class="grid">
<label class="full">Tên Wi‑Fi (SSID)<input id="wifiSsid" list="wifiNetworks" maxlength="32" required></label><datalist id="wifiNetworks"></datalist>
<button class="secondary full" id="scanButton" type="button">Quét lại mạng Wi‑Fi</button>
<div class="network-list full" id="wifiList"></div>
<label class="full">Mật khẩu Wi‑Fi<input id="wifiPassword" type="password" maxlength="64" placeholder="Để trống nếu mạng mới không có mật khẩu"></label>
</div><input id="deviceCode" type="hidden"><input id="apiKey" type="hidden">
<details><summary>Cấu hình nâng cao</summary><div class="grid">
<label class="full">Backend URL<input id="backendUrl" maxlength="159" placeholder="https://api.solaris.io.vn" required></label>
<label>Broker host<input id="mqttHost" maxlength="95" required></label><label>Port (cố định theo firmware)<input id="mqttPort" type="number" disabled></label>
<label>Username<input id="mqttUsername" maxlength="64"></label><label>Password<input id="mqttPassword" type="password" maxlength="96" placeholder="Để trống để giữ nguyên"></label>
<label class="check full"><input id="mqttUseTls" type="checkbox" disabled> Dùng TLS (cố định theo firmware)</label>
</div></details><button class="full-btn" id="continueButton" type="submit" style="margin-top:14px">Tiếp tục chụp QR →</button><div id="message" class="message"></div></form>

<section class="step-block hidden" id="qrStep"><h2>Quét QR thiết bị</h2><p>Đưa camera vào mã QR của IoT mới trên web Admin.</p><div class="qr-scanner idle" id="qrScanner"><img id="qrPreview" alt="Ảnh QR vừa chọn"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span><div class="qr-icon">QR</div></div><canvas id="qrCanvas" class="hidden"></canvas><input id="qrImageInput" class="hidden" type="file" accept="image/*" capture="environment"><div class="message" id="qrMessage">Wi‑Fi đã kết nối. Hãy quét QR để tiếp tục.</div><button class="camera-button" id="cameraButton" type="button">Mở camera / chọn ảnh QR</button><div class="external-help"><b>Web chạy trực tiếp trên ESP32 qua HTTP</b><span>Trình duyệt sẽ mở camera hệ thống, sau đó trang thử nhiều độ phân giải và mức tương phản để đọc QR. Ảnh không được gửi đi và không lưu trong ESP32. Bạn cũng có thể dán mã bằng tay bên dưới.</span><div class="manual"><input id="qrText" autocomplete="off" placeholder="Hoặc dán URL/chuỗi QR"><button id="applyQrButton" type="button">Ghép</button></div></div><p class="helper">Lấy trọn 4 góc QR, giữ gần và tránh ánh sáng phản chiếu từ màn hình.</p><button class="secondary full-btn" id="backButton" type="button" style="margin-top:6px">← Quay lại chọn Wi‑Fi</button></section>

<section class="step-block saving-block hidden" id="savingStep"><div class="loader"></div><h2 id="savingTitle">Đang hoàn tất thiết lập…</h2><p id="savingText">Giữ nguồn ESP32. Gateway đang lưu cấu hình và chuẩn bị kết nối hệ thống.</p><div class="saving-dots"><i></i><i></i><i></i></div></section>

<section class="step-block hidden" id="syncStep"><div class="section-title">Đồng bộ cấu hình pin</div><p class="helper">Dùng sau khi Admin thêm hoặc thay đổi pin trong Site. Gateway sẽ giữ nguyên Wi‑Fi và mã IoT, khởi động lại rồi tải danh sách pin mới từ backend.</p><button class="secondary camera-button" id="syncButton" type="button">Đồng bộ và khởi động lại</button><div class="message" id="syncMessage"></div></section>

<section class="step-block hidden" id="rebindStep"><div class="section-title">Ghép IoT mới</div><p class="helper">Xóa mã IoT, API key, Site, mapping pin và MQTT cũ. Wi‑Fi hiện tại được giữ lại; sau khi khởi động, hãy chụp QR của IoT mới.</p><button class="danger camera-button" id="newPairingButton" type="button">Đưa về chế độ quét QR mới</button><div class="message" id="newPairingMessage"></div></section>

<p class="note">SolarGW-xxxx luôn sẵn sàng. Khi chuyển nhà, chỉ cần kết nối lại AP này và vào trang để đổi Wi‑Fi. Thiết bị đã ghép sẽ giữ nguyên mã IoT và không yêu cầu QR.</p>
</div>
<script src="/jsqr.js"></script><script>
const $=id=>document.getElementById(id),msg=$('message'),pendingKey='solarSetupPendingV2',pendingMaxAge=15*60*1000;
const params=new URLSearchParams(location.search);let qrDeviceCode=params.get('dc')||'',qrApiKey=params.get('key')||'',qrBackendUrl=params.get('api')||params.get('backendUrl')||'',qrMqttHost=params.get('mh')||params.get('mqttHost')||'',apActive=false,hasIdentity=false,canReprovision=false,qrBusy=false;
if(qrDeviceCode||qrApiKey||qrBackendUrl||qrMqttHost)history.replaceState({},document.title,'/');
function showStep(name){$('setupForm').classList.toggle('hidden',name!=='wifi');$('qrStep').classList.toggle('hidden',name!=='qr');$('savingStep').classList.toggle('hidden',name!=='saving');$('syncStep').classList.toggle('hidden',name!=='wifi'||!canReprovision);$('rebindStep').classList.toggle('hidden',name!=='wifi'||!canReprovision);$('stepOne').classList.toggle('active',name==='wifi');$('stepTwo').classList.toggle('active',name!=='wifi');if(apActive&&name==='qr')$('network').textContent='Wi‑Fi đã chọn: '+$('wifiSsid').value;if(apActive&&name==='saving')$('network').textContent='Wi‑Fi: đang lưu và kết nối'}
function collect(){return{wifiSsid:$('wifiSsid').value.trim(),wifiPassword:$('wifiPassword').value,backendUrl:$('backendUrl').value.trim(),deviceCode:$('deviceCode').value.trim(),apiKey:$('apiKey').value,mqttHost:$('mqttHost').value.trim(),mqttPort:Number($('mqttPort').value),mqttUseTls:$('mqttUseTls').checked,mqttUsername:$('mqttUsername').value.trim(),mqttPassword:$('mqttPassword').value}}
function storePending(body){try{localStorage.setItem(pendingKey,JSON.stringify({savedAt:Date.now(),config:body}));return true}catch{return false}}
function readPending(){try{const raw=localStorage.getItem(pendingKey);if(!raw)return null;const value=JSON.parse(raw);if(Date.now()-Number(value.savedAt)<=pendingMaxAge)return value.config;localStorage.removeItem(pendingKey);return null}catch{try{localStorage.removeItem(pendingKey)}catch{}return null}}
function clearPending(){try{localStorage.removeItem(pendingKey)}catch{}}
function readProvisioningQr(raw){try{const url=new URL(raw.trim()),dc=url.searchParams.get('dc')||url.searchParams.get('deviceCode')||'',key=url.searchParams.get('key')||url.searchParams.get('apiKey')||'',api=url.searchParams.get('api')||url.searchParams.get('backendUrl')||'',mh=url.searchParams.get('mh')||url.searchParams.get('mqttHost')||'';if(!dc||!key)throw Error();if(api&&!/^https?:\/\//i.test(api))throw Error();return{dc,key,api,mh}}catch{throw Error('QR này không phải mã ghép thiết bị Solar BMS.')}}
async function applyQr(raw){if(qrBusy)return;qrBusy=true;try{const code=readProvisioningQr(raw);$('deviceCode').value=code.dc;$('apiKey').value=code.key;const pending=readPending()||(hasIdentity?collect():null);if(!pending)throw Error('Thông tin Wi‑Fi tạm đã hết hạn. Hãy quay lại bước 1.');pending.deviceCode=code.dc;pending.apiKey=code.key;if(code.api)pending.backendUrl=code.api;if(code.mh)pending.mqttHost=code.mh;$('qrMessage').className='message ok';$('qrMessage').textContent='Đã nhận QR '+code.dc+'. Đang lưu cấu hình…';if(await saveConfig(pending))clearPending();else qrBusy=false}catch(err){$('qrMessage').className='message err';$('qrMessage').textContent=err.message;qrBusy=false}}
function loadPreview(url){return new Promise((resolve,reject)=>{const image=$('qrPreview');image.onload=()=>resolve(image);image.onerror=()=>reject(Error('Không đọc được ảnh đã chọn'));image.src=url})}
async function findQrInImage(image){const canvas=$('qrCanvas'),ctx=canvas.getContext('2d',{willReadFrequently:true}),maxSides=[2400,1800,1200];for(let pass=0;pass<maxSides.length;pass++){const scale=Math.min(1,maxSides[pass]/Math.max(image.naturalWidth,image.naturalHeight)),width=Math.max(1,Math.round(image.naturalWidth*scale)),height=Math.max(1,Math.round(image.naturalHeight*scale));canvas.width=width;canvas.height=height;ctx.drawImage(image,0,0,width,height);if(pass===0&&window.BarcodeDetector){try{const detector=new window.BarcodeDetector({formats:['qr_code']}),codes=await detector.detect(canvas);if(codes[0]&&codes[0].rawValue)return codes[0].rawValue}catch{}}const pixels=ctx.getImageData(0,0,width,height),normal=window.jsQR(pixels.data,width,height,{inversionAttempts:'attemptBoth'});if(normal)return normal.data;if(pass===1){for(const threshold of [88,120,152,184]){const bw=ctx.getImageData(0,0,width,height),data=bw.data;for(let i=0;i<data.length;i+=4){const light=(data[i]*77+data[i+1]*150+data[i+2]*29)>>8,value=light>=threshold?255:0;data[i]=value;data[i+1]=value;data[i+2]=value}const result=window.jsQR(data,width,height,{inversionAttempts:'attemptBoth'});if(result)return result.data}}}return''}
async function decodeQrFile(file){if(!file||qrBusy)return;const status=$('qrMessage'),button=$('cameraButton'),url=URL.createObjectURL(file);qrBusy=true;button.disabled=true;status.className='message';status.textContent='Đang tìm mã QR trong ảnh…';try{if(file.size>12*1024*1024)throw Error('Ảnh lớn hơn 12 MB. Hãy chụp lại ở độ phân giải thấp hơn.');const image=await loadPreview(url);$('qrScanner').classList.remove('idle');const result=await findQrInImage(image);if(!result)throw Error('Không tìm thấy QR. Hãy đưa mã gần hơn, lấy trọn 4 góc và tránh phản chiếu màn hình.');status.className='message ok';status.textContent='Đã nhận diện QR. Đang kiểm tra…';qrBusy=false;await applyQr(result)}catch(err){status.className='message err';status.textContent=err.message;qrBusy=false;button.disabled=false}finally{setTimeout(()=>URL.revokeObjectURL(url),1000);$('qrImageInput').value=''}}
function prepareQrCapture(){$('qrScanner').classList.add('idle');$('qrPreview').removeAttribute('src');$('qrMessage').className='message ok';$('qrMessage').textContent='Wi‑Fi đã kết nối thành công. Bây giờ có thể quét hoặc nhập QR.';$('cameraButton').disabled=false}
async function verifyWifi(body){msg.className='message';msg.textContent='ESP32 đang kết nối và kiểm tra Wi‑Fi…';const started=await fetch('/api/wifi/check',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({wifiSsid:body.wifiSsid,wifiPassword:body.wifiPassword})}),startBody=await started.json();if(!started.ok)throw Error(startBody.error||('HTTP '+started.status));for(let attempt=0;attempt<45;attempt++){await new Promise(resolve=>setTimeout(resolve,800));try{const response=await fetch('/api/wifi/check',{cache:'no-store'}),status=await response.json();if(status.connected){msg.className='message ok';msg.textContent='Đã kết nối Wi‑Fi '+status.ssid+'. Có thể tiếp tục ghép thiết bị.';return}if(!status.connecting){const failure=Error(status.error||'ESP32 không kết nối được Wi‑Fi đã chọn.');failure.name='WifiRejected';throw failure}}catch(error){if(error.name==='WifiRejected')throw error;/* AP có thể đổi kênh theo STA; chờ điện thoại nối lại rồi poll tiếp. */}}throw Error('Kiểm tra Wi‑Fi quá thời gian. Hãy kiểm tra mật khẩu rồi thử lại.')}
async function saveConfig(body){showStep('saving');try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const out=await r.json();if(!r.ok)throw Error(out.error||('HTTP '+r.status));$('savingTitle').textContent='Đã lưu thành công';$('savingText').textContent='ESP32 đang khởi động lại, kết nối Wi‑Fi và tự provision với backend.';return true}catch(err){showStep('wifi');msg.className='message err';msg.textContent='Lưu thất bại: '+err.message;$('continueButton').disabled=false;return false}}
async function reprovision(){if(!confirm('Đồng bộ lại cấu hình pin và khởi động ESP32?'))return;const button=$('syncButton');button.disabled=true;$('syncMessage').className='message';$('syncMessage').textContent='Đang gửi yêu cầu…';try{const r=await fetch('/api/reprovision',{method:'POST'}),out=await r.json();if(!r.ok)throw Error(out.error||('HTTP '+r.status));$('savingTitle').textContent='Đang đồng bộ cấu hình pin…';$('savingText').textContent='ESP32 đang khởi động lại. Wi‑Fi và mã IoT vẫn được giữ nguyên.';showStep('saving')}catch(err){$('syncMessage').className='message err';$('syncMessage').textContent='Không thể đồng bộ: '+err.message;button.disabled=false}}
async function prepareNewPairing(){if(!confirm('Xóa liên kết IoT hiện tại và quét QR của IoT mới? Wi‑Fi sẽ được giữ lại.'))return;const button=$('newPairingButton');button.disabled=true;$('newPairingMessage').className='message';$('newPairingMessage').textContent='Đang xóa dữ liệu IoT cũ…';try{const r=await fetch('/api/new-pairing',{method:'POST'}),out=await r.json();if(!r.ok)throw Error(out.error||('HTTP '+r.status));$('savingTitle').textContent='Đã đưa về chế độ ghép mới';$('savingText').textContent='ESP32 đang khởi động lại. Kết nối lại SolarGW-xxxx, chọn Wi‑Fi rồi chụp QR của IoT mới.';showStep('saving')}catch(err){$('newPairingMessage').className='message err';$('newPairingMessage').textContent='Không thể đưa về ban đầu: '+err.message;button.disabled=false}}
async function load(){try{const r=await fetch('/api/config',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const c=await r.json();
for(const k of ['wifiSsid','backendUrl','deviceCode','mqttHost','mqttPort','mqttUsername'])$(k).value=c[k]??'';$('mqttUseTls').checked=!!c.mqttUseTls;apActive=!!c.apActive;hasIdentity=!!(c.hasApiKey&&c.deviceCode);$('continueButton').textContent=hasIdentity?'Kiểm tra và lưu Wi‑Fi mới':((qrDeviceCode&&qrApiKey)?'Kiểm tra Wi‑Fi và lưu':'Kiểm tra Wi‑Fi rồi quét QR →');
if(qrBackendUrl)$('backendUrl').value=qrBackendUrl;if(qrMqttHost)$('mqttHost').value=qrMqttHost;
$('network').textContent=c.stationConnected?'Wi‑Fi: đã kết nối':'Wi‑Fi: chưa kết nối';$('address').textContent=c.stationConnected?c.stationIp:(c.apActive?'AP: '+c.apIp:'');
canReprovision=hasIdentity;$('syncStep').classList.toggle('hidden',!canReprovision);$('rebindStep').classList.toggle('hidden',!canReprovision);if(hasIdentity){$('flowLead').textContent='Thiết bị đã ghép. Bạn có thể đổi Wi‑Fi mà không cần quét QR lại.';$('stepTwo').querySelector('b').textContent='Giữ mã IoT';$('stepTwo').querySelector('small').textContent='Không cần QR';msg.className='message ok';msg.textContent='Chế độ đổi Wi‑Fi: mã thiết bị và API key sẽ được giữ nguyên.'}
if(qrDeviceCode&&qrApiKey){$('deviceCode').value=qrDeviceCode;$('apiKey').value=qrApiKey;const pending=readPending();if(pending){pending.deviceCode=qrDeviceCode;pending.apiKey=qrApiKey;if(qrBackendUrl)pending.backendUrl=qrBackendUrl;if(qrMqttHost)pending.mqttHost=qrMqttHost;if(await saveConfig(pending))clearPending();return}msg.className='message ok';msg.textContent='Đã nhận QR '+qrDeviceCode+'. Chọn Wi‑Fi rồi tiếp tục để lưu.';}
}catch(e){msg.className='message err';msg.textContent='Không đọc được cấu hình: '+e.message}}
async function loadNetworks(){const button=$('scanButton');button.disabled=true;button.textContent='Đang quét…';try{const r=await fetch('/api/networks',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const out=await r.json(),seen=new Set(),dataList=$('wifiNetworks'),visibleList=$('wifiList');dataList.replaceChildren();visibleList.replaceChildren();for(const n of out.networks||[]){if(!n.ssid||seen.has(n.ssid))continue;seen.add(n.ssid);const option=document.createElement('option');option.value=n.ssid;option.label=(n.secure?'🔒 ':'')+n.rssi+' dBm';dataList.appendChild(option);const choice=document.createElement('button'),name=document.createElement('span'),signal=document.createElement('span');choice.type='button';choice.className='secondary network-option';name.textContent=(n.secure?'🔒 ':'')+n.ssid;signal.textContent=n.rssi+' dBm';choice.appendChild(name);choice.appendChild(signal);choice.addEventListener('click',()=>{$('wifiSsid').value=n.ssid;for(const item of visibleList.children)item.classList.remove('selected');choice.classList.add('selected')});visibleList.appendChild(choice);}}catch(e){msg.className='message err';msg.textContent='Không quét được Wi‑Fi: '+e.message}finally{button.disabled=false;button.textContent='Quét lại mạng Wi‑Fi'}}
$('setupForm').addEventListener('submit',async e=>{e.preventDefault();const body=collect(),button=$('continueButton');button.disabled=true;try{await verifyWifi(body);if(hasIdentity||(qrDeviceCode&&qrApiKey)){await saveConfig(body);return}if(!storePending(body))throw Error('Trình duyệt không cho lưu tạm Wi‑Fi. Hãy bật lưu trữ trang web rồi thử lại.');showStep('qr');button.disabled=false;prepareQrCapture()}catch(error){msg.className='message err';msg.textContent='Không thể tiếp tục: '+error.message;button.disabled=false}});
const scanWait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function renderNetworkResults(items){const seen=new Set(),dataList=$('wifiNetworks'),visibleList=$('wifiList');dataList.replaceChildren();visibleList.replaceChildren();for(const n of items||[]){if(!n.ssid||seen.has(n.ssid))continue;seen.add(n.ssid);const option=document.createElement('option');option.value=n.ssid;option.label=(n.secure?'🔒 ':'')+n.rssi+' dBm';dataList.appendChild(option);const choice=document.createElement('button'),name=document.createElement('span'),signal=document.createElement('span');choice.type='button';choice.className='secondary network-option';name.textContent=(n.secure?'🔒 ':'')+n.ssid;signal.textContent=n.rssi+' dBm';choice.appendChild(name);choice.appendChild(signal);choice.addEventListener('click',()=>{$('wifiSsid').value=n.ssid;for(const item of visibleList.children)item.classList.remove('selected');choice.classList.add('selected')});visibleList.appendChild(choice)}return seen.size}
async function loadNetworksRobust(){const button=$('scanButton');button.disabled=true;button.textContent='Đang quét…';try{for(let attempt=0;attempt<20;attempt++){const response=await fetch('/api/networks',{cache:'no-store'}),out=await response.json();if(response.status===202||out.scanning){await scanWait(700);continue}if(!response.ok)throw Error(out.error||('HTTP '+response.status));const count=renderNetworkResults(out.networks);if(count===0){msg.className='message';msg.textContent='Chưa tìm thấy mạng 2.4 GHz. Bạn vẫn có thể nhập SSID thủ công.'}return}throw Error('Quét Wi‑Fi quá thời gian. Hãy thử lại hoặc nhập SSID thủ công.')}catch(e){msg.className='message err';msg.textContent='Không quét được Wi‑Fi: '+e.message+' Bạn vẫn có thể nhập SSID thủ công.'}finally{button.disabled=false;button.textContent='Quét lại mạng Wi‑Fi'}}
$('scanButton').addEventListener('click',loadNetworksRobust);$('cameraButton').addEventListener('click',()=>$('qrImageInput').click());$('qrImageInput').addEventListener('change',event=>decodeQrFile(event.target.files&&event.target.files[0]));$('applyQrButton').addEventListener('click',()=>applyQr($('qrText').value));$('backButton').addEventListener('click',()=>showStep('wifi'));$('syncButton').addEventListener('click',reprovision);$('newPairingButton').addEventListener('click',prepareNewPairing);load().then(loadNetworksRobust);
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

void handleGetConfig() {
  if (!authenticated()) return;
  JsonDocument document;
  document["wifiSsid"] = wificfg::ssid();
  document["hasWifiPassword"] = wificfg::password()[0] != '\0';
  document["backendUrl"] = runtimecfg::backendUrl();
  document["deviceCode"] = identity::deviceCode();
  document["hasApiKey"] = identity::apiKey()[0] != '\0';
  document["mqttHost"] = mqttcfg::host();
  document["mqttPort"] = MQTT_BROKER_PORT;
  document["mqttUseTls"] = MQTT_USE_TLS != 0;
  document["mqttUsername"] = mqttcfg::username();
  document["hasMqttPassword"] = mqttcfg::password()[0] != '\0';
  document["stationConnected"] = WiFi.status() == WL_CONNECTED;
  document["stationIp"] = WiFi.localIP().toString();
  document["apActive"] = net::portalIsActive();
  document["apIp"] = net::portalIsActive() ? WiFi.softAPIP().toString() : "";
  document["setupPort"] = CONFIG_PORTAL_PORT;
  sendJson(200, document);
}

void sendNetworkScanResults(int count) {
  JsonDocument document;
  document["ok"] = true;
  document["scanning"] = false;
  JsonArray networks = document["networks"].to<JsonArray>();
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    JsonObject item = networks.add<JsonObject>();
    item["ssid"] = ssid;
    item["rssi"] = WiFi.RSSI(i);
    item["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  sendJson(200, document);
}

void sendNetworkScanPending() {
  JsonDocument document;
  document["ok"] = true;
  document["scanning"] = true;
  sendJson(202, document);
}

void handleGetNetworks() {
  if (!authenticated()) return;

  const int scanState = WiFi.scanComplete();
  if (scanState >= 0) {
    sendNetworkScanResults(scanState);
    return;
  }
  if (scanState == WIFI_SCAN_RUNNING) {
    sendNetworkScanPending();
    return;
  }

  // No result is available yet (WIFI_SCAN_FAILED is also the initial state).
  // Start an asynchronous scan so the HTTP handler does not block the AP
  // client while the radio visits the 2.4 GHz channels.
  WiFi.scanDelete();
  const int startState = WiFi.scanNetworks(true, true);
  if (startState == WIFI_SCAN_FAILED) {
    // The ESP32 driver rejects scans briefly while STA is associating or
    // changing channel. This is transient: report pending so the browser
    // retries instead of surfacing a misleading HTTP 503 to the user.
    Serial.println("[portal] Wi-Fi scan busy; browser will retry");
    sendNetworkScanPending();
    return;
  }
  if (startState >= 0) {
    sendNetworkScanResults(startState);
    return;
  }

  Serial.println("[portal] asynchronous Wi-Fi scan started");
  sendNetworkScanPending();
}

void handleStartWifiCheck() {
  if (!authenticated()) return;

  JsonDocument document;
  const DeserializationError error =
      deserializeJson(document, s_server.arg("plain"));
  if (error) {
    sendError(400, "JSON khong hop le");
    return;
  }

  const char* wifiSsid = document["wifiSsid"] | "";
  const char* wifiPassword = document["wifiPassword"] | "";
  if (wifiSsid[0] == '\0') {
    sendError(400, "Chua chon Wi-Fi");
    return;
  }
  if (tooLong(wifiSsid, wificfg::kSsidBufLen) ||
      tooLong(wifiPassword, wificfg::kPassBufLen)) {
    sendError(400, "SSID hoac mat khau Wi-Fi qua dai");
    return;
  }

  // Password inputs are write-only. An empty password keeps the current one
  // only when the selected SSID is unchanged; for a new SSID it means an open
  // network. This is the same rule used by the final config save.
  char nextPassword[wificfg::kPassBufLen];
  const bool sameSsid = strcmp(wifiSsid, wificfg::ssid()) == 0;
  snprintf(nextPassword, sizeof(nextPassword), "%s",
           wifiPassword[0] != '\0' ? wifiPassword
                                    : (sameSsid ? wificfg::password() : ""));

  if (!net::wifiReconfigure(wifiSsid, nextPassword)) {
    sendError(500, "Khong luu duoc Wi-Fi de kiem tra");
    return;
  }

  snprintf(s_wifiCheckSsid, sizeof(s_wifiCheckSsid), "%s", wifiSsid);
  s_wifiCheckStartedAtMs = millis();
  s_wifiCheckActive = true;

  JsonDocument response;
  response["ok"] = true;
  response["connecting"] = true;
  response["ssid"] = s_wifiCheckSsid;
  sendJson(202, response);
  Serial.printf("[portal] checking Wi-Fi association for \"%s\"\n",
                s_wifiCheckSsid);
}

void handleGetWifiCheck() {
  if (!authenticated()) return;

  const bool connected =
      s_wifiCheckSsid[0] != '\0' && WiFi.status() == WL_CONNECTED &&
      WiFi.SSID() == String(s_wifiCheckSsid);
  const bool timedOut =
      s_wifiCheckActive &&
      static_cast<uint32_t>(millis() - s_wifiCheckStartedAtMs) >=
          kWifiCheckTimeoutMs;

  if (connected || timedOut) s_wifiCheckActive = false;

  JsonDocument response;
  response["ok"] = true;
  response["connected"] = connected;
  response["connecting"] = s_wifiCheckActive;
  response["ssid"] = s_wifiCheckSsid;
  if (connected) {
    response["stationIp"] = WiFi.localIP().toString();
    response["rssi"] = WiFi.RSSI();
  } else if (timedOut) {
    response["error"] =
        "ESP32 khong ket noi duoc Wi-Fi. Kiem tra SSID va mat khau.";
  }
  sendJson(s_wifiCheckActive ? 202 : 200, response);
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
  const int mqttPort = MQTT_BROKER_PORT;
  const bool receivedApiKey = apiKey[0] != '\0';

  if (wifiSsid[0] == '\0' || backendUrl[0] == '\0' || deviceCode[0] == '\0' ||
      mqttHost[0] == '\0') {
    sendError(400, "Thiếu SSID, Backend URL, Device code hoặc MQTT host");
    return;
  }
  if (tooLong(wifiSsid, wificfg::kSsidBufLen) ||
      tooLong(wifiPassword, wificfg::kPassBufLen) ||
      tooLong(backendUrl, runtimecfg::kMaxBackendUrlLen) ||
      tooLong(deviceCode, identity::kMaxDeviceCodeLen) ||
      tooLong(apiKey, identity::kMaxApiKeyLen) ||
      tooLong(mqttHost, mqttcfg::kHostBufLen) ||
      tooLong(mqttUsername, mqttcfg::kUserBufLen) ||
      tooLong(mqttPassword, mqttcfg::kPassBufLen)) {
    sendError(400, "Một trường cấu hình vượt quá độ dài cho phép");
    return;
  }
  if (strncmp(backendUrl, "http://", 7) != 0 &&
      strncmp(backendUrl, "https://", 8) != 0) {
    sendError(400, "Backend URL phải bắt đầu bằng http:// hoặc https://");
    return;
  }
  // TLS and its public broker port are compile-time properties of this
  // production transport. Ignore stale values from cached pages/legacy QR;
  // accepting mp=1883 while MQTT_USE_TLS=1 makes WiFiClientSecure attempt a
  // TLS handshake against the plain listener and can never succeed.

  // Password fields are write-only. Với Wi-Fi, chỉ giữ mật khẩu cũ khi SSID
  // không đổi; SSID mới + password rỗng phải thực sự là mạng mở, không được
  // vô tình lấy mật khẩu của nhà cũ đem thử với mạng mới.
  char nextWifiPassword[wificfg::kPassBufLen];
  char nextMqttUsername[mqttcfg::kUserBufLen];
  char nextMqttPassword[mqttcfg::kPassBufLen];
  const bool sameWifiSsid = strcmp(wifiSsid, wificfg::ssid()) == 0;
  snprintf(nextWifiPassword, sizeof(nextWifiPassword), "%s",
           wifiPassword[0] != '\0' ? wifiPassword
                                    : (sameWifiSsid ? wificfg::password() : ""));
  snprintf(nextMqttUsername, sizeof(nextMqttUsername), "%s",
           mqttUsername[0] != '\0' ? mqttUsername : mqttcfg::username());
  snprintf(nextMqttPassword, sizeof(nextMqttPassword), "%s",
           mqttPassword[0] != '\0' ? mqttPassword : mqttcfg::password());

  char topicPrefix[mqttcfg::kPrefixBufLen];
  if (core::deriveTopicPrefix(deviceCode, topicPrefix, sizeof(topicPrefix)) == 0) {
    sendError(400, "Device code cannot form a valid MQTT topic");
    return;
  }

  if (!wificfg::save(wifiSsid, nextWifiPassword) ||
      !runtimecfg::saveBackendUrl(backendUrl) ||
      !identity::setDeviceCode(deviceCode) ||
      (apiKey[0] != '\0' && !identity::setApiKey(apiKey)) ||
      !mqttcfg::setBroker(mqttHost, mqttPort) ||
      !mqttcfg::setCredential(nextMqttUsername, nextMqttPassword) ||
      !mqttcfg::setTopicPrefix(topicPrefix) ||
      (receivedApiKey && !provision::clearProvisionFlag())) {
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
  if (receivedApiKey) {
    Serial.println("[portal] QR credential updated; backend provision will refresh MQTT after reboot");
  }
}

void handleReprovision() {
  if (!authenticated()) return;
  if (identity::deviceCode()[0] == '\0' || identity::apiKey()[0] == '\0') {
    sendError(409, "Thiet bi chua co Device code hoac API key");
    return;
  }
  if (!provision::clearProvisionFlag()) {
    sendError(500, "Khong xoa duoc co provision trong NVS");
    return;
  }

  JsonDocument response;
  response["ok"] = true;
  response["restarting"] = true;
  response["message"] = "ESP32 se tai lai cau hinh pin tu backend";
  sendJson(200, response);
  s_restartPending = true;
  s_restartAtMs = millis() + 1500;
  Serial.println("[portal] reprovision requested; restart scheduled");
}

void handlePrepareNewPairing() {
  if (!authenticated()) return;

  // Keep only the current home Wi-Fi. Device identity, provision/site data,
  // battery mappings, MQTT credentials and every other runtime NVS value
  // belong to the old IoT registration and must not leak into the new one.
  char savedSsid[wificfg::kSsidBufLen];
  char savedPassword[wificfg::kPassBufLen];
  const bool hadWifi = wificfg::isConfigured();
  snprintf(savedSsid, sizeof(savedSsid), "%s", wificfg::ssid());
  snprintf(savedPassword, sizeof(savedPassword), "%s", wificfg::password());

  if (!storage::nvsErase()) {
    sendError(500, "Khong xoa duoc cau hinh IoT cu");
    return;
  }
  if (!identity::prepareForPairing()) {
    sendError(500, "Khong danh dau duoc trang thai cho QR moi");
    return;
  }

  bool wifiKept = !hadWifi || wificfg::save(savedSsid, savedPassword);
  const bool queueCleared = queue::queueClear();
  if (!wifiKept || !queueCleared) {
    Serial.printf("[portal] prepare pairing partial: wifi=%s queue=%s\n",
                  wifiKept ? "OK" : "FAIL", queueCleared ? "OK" : "FAIL");
  }

  JsonDocument response;
  response["ok"] = true;
  response["restarting"] = true;
  response["wifiKept"] = wifiKept;
  response["message"] = "ESP32 se khoi dong lai va cho quet QR cua IoT moi";
  sendJson(200, response);
  s_restartPending = true;
  s_restartAtMs = millis() + 1500;
  Serial.println("[portal] old IoT data cleared; new QR pairing scheduled");
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
  const auto sendIndex = []() {
    if (!authenticated()) return;
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.sendHeader("Referrer-Policy", "no-referrer");
    s_server.sendHeader("X-Content-Type-Options", "nosniff");
    s_server.sendHeader("Permissions-Policy", "camera=(self)");
    s_server.send_P(200, "text/html; charset=utf-8", kIndexHtml);
  };
  s_server.on("/", HTTP_GET, sendIndex);
  s_server.on("/import", HTTP_GET, sendIndex);
  s_server.on("/jsqr.js", HTTP_GET, []() {
    if (!authenticated()) return;
    const size_t length = static_cast<size_t>(kJsQrGzipEnd - kJsQrGzipStart);
    s_server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
    s_server.sendHeader("Content-Encoding", "gzip");
    s_server.send_P(200, "application/javascript; charset=utf-8",
                    reinterpret_cast<const char*>(kJsQrGzipStart), length);
  });
  s_server.on("/api/config", HTTP_GET, handleGetConfig);
  s_server.on("/api/networks", HTTP_GET, handleGetNetworks);
  s_server.on("/api/wifi/check", HTTP_POST, handleStartWifiCheck);
  s_server.on("/api/wifi/check", HTTP_GET, handleGetWifiCheck);
  s_server.on("/api/config", HTTP_POST, handleSaveConfig);
  s_server.on("/api/reprovision", HTTP_POST, handleReprovision);
  s_server.on("/api/new-pairing", HTTP_POST, handlePrepareNewPairing);
  s_server.onNotFound([]() {
    if (!authenticated()) return;
    s_server.sendHeader("Location", "/", true);
    s_server.send(302, "text/plain", "");
  });
  s_server.begin();
  s_started = true;
  Serial.printf("[portal] web setup listening on port %u\n",
                static_cast<unsigned>(CONFIG_PORTAL_PORT));

  if (WiFi.status() == WL_CONNECTED) startMdnsIfPossible();
}

void setupPortalTick() {
  if (!s_started) return;
  s_server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    startMdnsIfPossible();
  }

  if (s_restartPending && static_cast<int32_t>(millis() - s_restartAtMs) >= 0) {
    Serial.println("[portal] restarting now");
    delay(50);
    ESP.restart();
  }
}

bool setupPortalApActive() {
  return net::portalIsActive();
}

}  // namespace portal
