#include "WebPortalManager.h"

namespace {
const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>翻转番茄钟 控制台</title>
<style>
  :root{--bg:#0b0f14;--card:#151c24;--bd:#263241;--txt:#eef4fb;--mut:#8b9bb0;--acc:#00d8de;--gold:#fecc2f;}
  *{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;}
  body{background:var(--bg);color:var(--txt);padding:16px;max-width:540px;margin:0 auto;line-height:1.5;}
  h1{font-size:20px;margin-bottom:4px;color:var(--acc);}
  .sub{font-size:12px;color:var(--mut);margin-bottom:16px;}
  .card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin-bottom:14px;}
  .card h2{font-size:15px;margin-bottom:10px;color:#fff;}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
  .stat{background:#0f151c;border-radius:8px;padding:10px;text-align:center;}
  .stat .v{font-size:22px;font-weight:700;color:var(--acc);}
  .stat .l{font-size:11px;color:var(--mut);}
  .bars{display:flex;align-items:flex-end;gap:6px;height:80px;margin-top:12px;padding-top:10px;}
  .bar-col{flex:1;display:flex;flex-direction:column;align-items:center;height:100%;justify-content:flex-end;}
  .bar{width:100%;background:linear-gradient(180deg,var(--acc),#00838f);border-radius:4px 4px 0 0;min-height:3px;}
  .bar-lbl{font-size:10px;color:var(--mut);margin-top:4px;}
  label{display:block;font-size:12px;color:var(--mut);margin:8px 0 4px;}
  input[type=text],input[type=password],input[type=number],select{width:100%;padding:8px 10px;border-radius:8px;border:1px solid var(--bd);background:#0f151c;color:#fff;font-size:14px;}
  input[type=range]{width:100%;accent-color:var(--acc);}
  .row{display:flex;align-items:center;justify-content:space-between;margin-top:10px;}
  .btn{display:inline-block;width:100%;padding:10px;border:none;border-radius:8px;font-size:14px;font-weight:600;cursor:pointer;margin-top:10px;}
  .btn-p{background:var(--acc);color:#061014;}
  .btn-s{background:#233040;color:#fff;}
  .btn-d{background:#3a1e24;color:#ff8a93;}
  .msg{font-size:13px;color:var(--gold);margin-top:8px;text-align:center;min-height:18px;}
</style>
</head>
<body>
  <h1>翻转番茄钟 Web 控制台</h1>
  <div class="sub">免重编译配网 · 习惯设置 · 专注统计</div>

  <div class="card">
    <h2>专注数据统计</h2>
    <div class="grid">
      <div class="stat"><div class="v" id="todayCnt">-</div><div class="l">今日番茄 (个)</div></div>
      <div class="stat"><div class="v" id="todayMin">-</div><div class="l">今日专注 (分钟)</div></div>
      <div class="stat"><div class="v" id="totCnt">-</div><div class="l">累计番茄 (个)</div></div>
      <div class="stat"><div class="v" id="totMin">-</div><div class="l">累计专注 (分钟)</div></div>
    </div>
    <div class="bars" id="bars"></div>
  </div>

  <form class="card" id="cfgForm" onsubmit="return saveCfg(event)">
    <h2>Wi-Fi 与时区</h2>
    <label>Wi-Fi 名称 (SSID)</label>
    <input type="text" id="ssid" name="ssid" maxlength="63" placeholder="留空则离线使用硬件 RTC">
    <label>Wi-Fi 密码</label>
    <input type="password" id="pass" name="pass" maxlength="63" placeholder="输入新密码（留空或保持不变）">
    <label>时区偏移 (小时，北京时间为 8)</label>
    <input type="number" id="tz" name="tz" min="-12" max="14">

    <h2 style="margin-top:14px;">翻转时长与工作流</h2>
    <div class="grid">
      <div><label>面1 上边 (分钟)</label><input type="number" id="f1" name="f1" min="1" max="180"></div>
      <div><label>面2 左边 (分钟)</label><input type="number" id="f2" name="f2" min="1" max="180"></div>
      <div><label>面3 下边 (分钟)</label><input type="number" id="f3" name="f3" min="1" max="180"></div>
      <div><label>面4 右边 (分钟)</label><input type="number" id="f4" name="f4" min="1" max="180"></div>
    </div>
    <div class="grid">
      <div><label>休息时长 (分钟)</label><input type="number" id="brk" name="brk" min="1" max="60"></div>
      <div><label>完成后自动息屏 (秒,0关)</label><input type="number" id="aoff" name="aoff" min="0" max="600"></div>
    </div>
    <div class="row">
      <span style="font-size:13px;">专注完成后点击自动进入休息</span>
      <input type="checkbox" id="abrk" name="abrk" style="width:18px;height:18px;">
    </div>

    <h2 style="margin-top:14px;">声音、显示与表盘主题</h2>
    <label>番茄钟主题风格</label>
    <select id="thm" name="thm">
      <option value="0">默认科技环 (Cyber Ring)</option>
      <option value="1">经典翻页时钟 (Fliqlo)</option>
      <option value="2">复古辉光管 (IN-18 Nixie)</option>
      <option value="3">像素风小番茄 (Pixel)</option>
      <option value="4">流沙沙漏 (Hourglass)</option>
    </select>
    <label>时钟页主题风格</label>
    <select id="cthm" name="cthm">
      <option value="0">极简天象时钟 (24h 昼夜渐变 + 流星)</option>
      <option value="1">像素小猫 · 时间小世界</option>
    </select>
    <div class="row">
      <span style="font-size:13px;">时钟页整点报时 (22:00-07:00 免打扰)</span>
      <input type="checkbox" id="hchm" name="hchm" style="width:18px;height:18px;">
    </div>
    <label>提示音音量: <span id="volVal">75</span> (0=静音)</label>
    <input type="range" id="vol" name="vol" min="0" max="85" oninput="volVal.innerText=this.value">
    <label>屏幕亮度: <span id="briVal">255</span></label>
    <input type="range" id="bri" name="bri" min="30" max="255" oninput="briVal.innerText=this.value">

    <button class="btn btn-p" type="submit">保存配置并立即生效</button>
    <div class="msg" id="msg"></div>
  </form>

  <div class="card">
    <button class="btn btn-s" type="button" onclick="postAct('/api/sync','正在关闭热点并后台校时...')">关闭热点并同步 NTP 时间</button>
    <button class="btn btn-s" type="button" onclick="postAct('/api/close','热点已关闭')">仅关闭配置热点</button>
    <button class="btn btn-d" type="button" onclick="if(confirm('确定清空全部专注统计记录吗？')) postAct('/api/stats/reset','统计已清零')">清空专注统计</button>
  </div>

<script>
async function loadData(){
  const r = await fetch('/api/data'); const d = await r.json();
  ssid.value=d.ssid||''; pass.value=d.pass||''; tz.value=d.tz;
  f1.value=d.f1; f2.value=d.f2; f3.value=d.f3; f4.value=d.f4;
  brk.value=d.brk; aoff.value=d.aoff; abrk.checked=!!d.abrk; thm.value=d.thm||0;
  cthm.value=d.cthm||0; hchm.checked=(d.hchm!==0);
  vol.value=d.vol; volVal.innerText=d.vol;
  bri.value=d.bri; briVal.innerText=d.bri;
  todayCnt.innerText=d.todayCnt; todayMin.innerText=d.todayMin;
  totCnt.innerText=d.totCnt; totMin.innerText=d.totMin;
  const maxM = Math.max(10, ...d.days.map(x=>x.m));
  bars.innerHTML = d.days.map(x=>{
    const h = Math.max(4, Math.round(x.m * 60 / maxM));
    const lbl = x.d ? String(x.d).slice(4,6)+'/'+String(x.d).slice(6,8) : '--';
    return `<div class="bar-col"><div class="bar" style="height:${h}px" title="${x.c}个/${x.m}分"></div><div class="bar-lbl">${lbl}</div></div>`;
  }).join('');
}
async function saveCfg(e){
  e.preventDefault();
  const fd = new FormData(cfgForm);
  if(!abrk.checked) fd.append('abrk','0'); else fd.set('abrk','1');
  if(!hchm.checked) fd.append('hchm','0'); else fd.set('hchm','1');
  const r = await fetch('/api/config',{method:'POST',body:new URLSearchParams(fd)});
  msg.innerText = r.ok ? '已保存并生效！' : '保存失败';
  return false;
}
async function postAct(url, tip){
  await fetch(url,{method:'POST'});
  msg.innerText = tip;
  if(url==='/api/stats/reset') loadData();
}
loadData();
</script>
</body>
</html>
)rawliteral";
}

void WebPortalManager::begin(ConfigManager &cfg, StatsManager &st, RTCManager &rtcMgr) {
  config = &cfg;
  stats = &st;
  rtc = &rtcMgr;
}

void WebPortalManager::start() {
  if (mode != PORTAL_OFF) return;
  if (!server) server = new WebServer(80);
  if (!dns) dns = new DNSServer();

  if (config && strlen(config->data.wifiSsid) > 0) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(config->data.wifiSsid, config->data.wifiPass);
    mode = PORTAL_CONNECTING_STA;
    connectStartMs = millis();
    lastActivityMs = connectStartMs;
    strcpy(ipStr, "0.0.0.0");
    Serial.printf("[PORTAL] Connecting to saved WiFi '%s'...\n", config->data.wifiSsid);
  } else {
    startApFallback();
  }
}

void WebPortalManager::startServerOnly() {
  setupRoutes();
  server->begin();
  lastActivityMs = millis();
}

void WebPortalManager::startApFallback() {
  if (!server) server = new WebServer(80);
  if (!dns) dns = new DNSServer();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  delay(100);

  dns->start(53, "*", WiFi.softAPIP());
  startServerOnly();
  mode = PORTAL_AP;
  strncpy(ipStr, WiFi.softAPIP().toString().c_str(), sizeof(ipStr) - 1);
  ipStr[sizeof(ipStr) - 1] = '\0';
  Serial.printf("[PORTAL] AP fallback started: SSID=%s, IP=%s\n", AP_SSID, ipStr);
}

void WebPortalManager::stop() {
  if (mode == PORTAL_OFF) return;
  mode = PORTAL_OFF;
  strcpy(ipStr, "0.0.0.0");
  if (server) server->stop();
  if (dns) dns->stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[PORTAL] Portal stopped");
}

void WebPortalManager::handleClient() {
  if (mode == PORTAL_OFF) return;

  if (mode == PORTAL_CONNECTING_STA) {
    if (WiFi.status() == WL_CONNECTED) {
      mode = PORTAL_STA;
      strncpy(ipStr, WiFi.localIP().toString().c_str(), sizeof(ipStr) - 1);
      ipStr[sizeof(ipStr) - 1] = '\0';
      startServerOnly();
      Serial.printf("[PORTAL] STA connected! Visit http://%s\n", ipStr);
    } else if (millis() - connectStartMs >= STA_CONNECT_TIMEOUT_MS) {
      Serial.println("[PORTAL] STA connect timeout -> switching to AP fallback");
      startApFallback();
    }
    return;
  }

  if (mode == PORTAL_AP && dns) dns->processNextRequest();
  if (server) server->handleClient();

  if (millis() - lastActivityMs >= IDLE_TIMEOUT_MS) {
    Serial.println("[PORTAL] Idle timeout -> stopping portal");
    stop();
  }
}

void WebPortalManager::setupRoutes() {
  server->on("/", HTTP_GET, [this]() { handleRoot(); });
  server->on("/api/data", HTTP_GET, [this]() { handleApiData(); });
  server->on("/api/config", HTTP_POST, [this]() { handleSaveConfig(); });
  server->on("/api/sync", HTTP_POST, [this]() { handleSyncNow(); });
  server->on("/api/stats/reset", HTTP_POST, [this]() { handleResetStats(); });
  server->on("/api/close", HTTP_POST, [this]() { handleClosePortal(); });
  server->onNotFound([this]() {
    lastActivityMs = millis();
    server->sendHeader("Location", String("http://") + AP_SSID + ".local/", true);
    server->sendHeader("Location", "http://192.168.4.1/", true);
    server->send(302, "text/plain", "");
  });
}

void WebPortalManager::handleRoot() {
  lastActivityMs = millis();
  server->send_P(200, "text/html", PORTAL_HTML);
}

void WebPortalManager::handleApiData() {
  lastActivityMs = millis();
  uint32_t today = rtc ? rtc->getDateYyyyMmDd() : 0;
  uint16_t tCnt = stats ? stats->getTodayCount(today) : 0;
  uint16_t tMin = stats ? stats->getTodayMinutes(today) : 0;

  String json = "{";
  json += "\"ssid\":\"" + String(config->data.wifiSsid) + "\",";
  json += "\"pass\":\"" + String(config->data.wifiPass) + "\",";
  json += "\"tz\":" + String(config->data.timezoneOffsetHours) + ",";
  json += "\"f1\":" + String(config->data.faceMinutes[0]) + ",";
  json += "\"f2\":" + String(config->data.faceMinutes[1]) + ",";
  json += "\"f3\":" + String(config->data.faceMinutes[2]) + ",";
  json += "\"f4\":" + String(config->data.faceMinutes[3]) + ",";
  json += "\"brk\":" + String(config->data.breakMinutes) + ",";
  json += "\"abrk\":" + String(config->data.autoBreak ? 1 : 0) + ",";
  json += "\"vol\":" + String(config->data.audioVolume) + ",";
  json += "\"bri\":" + String(config->data.brightness) + ",";
  json += "\"aoff\":" + String(config->data.autoScreenOffSec) + ",";
  json += "\"thm\":" + String(config->data.theme) + ",";
  json += "\"cthm\":" + String(config->data.clockTheme) + ",";
  json += "\"hchm\":" + String(config->data.hourlyChime ? 1 : 0) + ",";
  json += "\"todayCnt\":" + String(tCnt) + ",";
  json += "\"todayMin\":" + String(tMin) + ",";
  json += "\"totCnt\":" + String(stats ? stats->totalPomodoros : 0) + ",";
  json += "\"totMin\":" + String(stats ? stats->totalFocusMinutes : 0) + ",";
  json += "\"days\":[";
  for (int i = 0; i < StatsManager::MAX_DAYS; ++i) {
    if (i > 0) json += ",";
    uint32_t d = stats ? stats->days[i].yyyymmdd : 0;
    uint16_t c = stats ? stats->days[i].count : 0;
    uint16_t m = stats ? stats->days[i].minutes : 0;
    json += "{\"d\":" + String(d) + ",\"c\":" + String(c) + ",\"m\":" + String(m) + "}";
  }
  json += "]}";
  server->send(200, "application/json", json);
}

void WebPortalManager::handleSaveConfig() {
  lastActivityMs = millis();
  if (!config) {
    server->send(500, "text/plain", "NO_CFG");
    return;
  }
  if (server->hasArg("ssid")) {
    String s = server->arg("ssid");
    s.trim();
    strncpy(config->data.wifiSsid, s.c_str(), sizeof(config->data.wifiSsid) - 1);
    config->data.wifiSsid[sizeof(config->data.wifiSsid) - 1] = '\0';
  }
  if (server->hasArg("pass")) {
    String p = server->arg("pass");
    strncpy(config->data.wifiPass, p.c_str(), sizeof(config->data.wifiPass) - 1);
    config->data.wifiPass[sizeof(config->data.wifiPass) - 1] = '\0';
  }
  if (server->hasArg("tz")) config->data.timezoneOffsetHours = constrain(server->arg("tz").toInt(), -12, 14);
  if (server->hasArg("f1")) config->data.faceMinutes[0] = constrain(server->arg("f1").toInt(), 1, 180);
  if (server->hasArg("f2")) config->data.faceMinutes[1] = constrain(server->arg("f2").toInt(), 1, 180);
  if (server->hasArg("f3")) config->data.faceMinutes[2] = constrain(server->arg("f3").toInt(), 1, 180);
  if (server->hasArg("f4")) config->data.faceMinutes[3] = constrain(server->arg("f4").toInt(), 1, 180);
  if (server->hasArg("brk")) config->data.breakMinutes = constrain(server->arg("brk").toInt(), 1, 60);
  if (server->hasArg("aoff")) config->data.autoScreenOffSec = constrain(server->arg("aoff").toInt(), 0, 600);
  if (server->hasArg("abrk")) config->data.autoBreak = (server->arg("abrk") == "1");
  if (server->hasArg("vol")) config->data.audioVolume = constrain(server->arg("vol").toInt(), 0, 85);
  if (server->hasArg("bri")) config->data.brightness = constrain(server->arg("bri").toInt(), 30, 255);
  if (server->hasArg("thm")) config->data.theme = constrain(server->arg("thm").toInt(), 0, 4);
  if (server->hasArg("cthm")) config->data.clockTheme = constrain(server->arg("cthm").toInt(), 0, 1);
  if (server->hasArg("hchm")) config->data.hourlyChime = (server->arg("hchm") == "1");

  config->save();
  configChanged = true;
  server->send(200, "application/json", "{\"ok\":true}");
}

void WebPortalManager::handleSyncNow() {
  lastActivityMs = millis();
  server->send(200, "application/json", "{\"ok\":true}");
  delay(150);
  stop();
  syncRequested = true;
}

void WebPortalManager::handleResetStats() {
  lastActivityMs = millis();
  if (stats) stats->resetAll();
  server->send(200, "application/json", "{\"ok\":true}");
}

void WebPortalManager::handleClosePortal() {
  lastActivityMs = millis();
  server->send(200, "application/json", "{\"ok\":true}");
  delay(150);
  stop();
}
