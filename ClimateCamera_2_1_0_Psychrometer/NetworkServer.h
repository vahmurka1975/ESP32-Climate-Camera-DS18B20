#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "Config.h"
#include <atomic>
#include <cmath>

class NetworkServer {
public:
  void begin(ConfigManager* cfgMgr) {
    _cfgMgr = cfgMgr;
    _cfgSnapshot = cfgMgr->data;

    // Режим AP + STA: одновременная доступность резервной точки доступа
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_FALLBACK_SSID, AP_FALLBACK_PASS);

    // Подключение к указанной домашней сети
    WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);

    setupRoutes();

    // Запуск сетевого стека на Core 0 в независимой задаче FreeRTOS
    xTaskCreatePinnedToCore(
      networkTaskRoutine,
      "NetServerCore0",
      8192,
      this,
      1,
      &_taskHandle,
      0
    );
  }

  void updateTelemetry(const WebTelemetry &t, const ClimateConfig &cfg) {
    portENTER_CRITICAL(&_mux);
    _telemetry = t;
    _cfgSnapshot = cfg;
    portEXIT_CRITICAL(&_mux);
    _outputsSafe.store(!t.relayCool && !t.relayHumid && !t.relayFan);
  }
  bool isOtaActive() const { return _otaActive.load(); }
  // Изменения конфигурации и запись NVS выполняются только в основном цикле.
  void applyPendingSettings() {
    ClimateConfig cfg;
    bool pending;
    portENTER_CRITICAL(&_mux);
    pending = _settingsPending;
    cfg = _pendingConfig;
    _settingsPending = false;
    portEXIT_CRITICAL(&_mux);
    if (pending) { _cfgMgr->data = cfg; _cfgMgr->scheduleSave(); }
  }

  bool isConnected() const { return _wifiConnected; }
  void getIPString(char *out, size_t maxLen) const {
    if (_wifiConnected) {
      strncpy(out, WiFi.localIP().toString().c_str(), maxLen);
    } else {
      strncpy(out, WiFi.softAPIP().toString().c_str(), maxLen);
    }
  }

private:
  ConfigManager* _cfgMgr = nullptr;
  WebServer _server{80};
  TaskHandle_t _taskHandle = nullptr;
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

  WebTelemetry _telemetry{};
  ClimateConfig _cfgSnapshot{};
  ClimateConfig _pendingConfig{};
  bool _settingsPending = false;
  std::atomic<bool> _otaActive{false};
  std::atomic<bool> _outputsSafe{false};
  bool _otaUploadFailed = false;
  static void numberOrNull(char* dst, size_t n, float value, bool valid) {
    if (valid && std::isfinite(value)) snprintf(dst, n, "%.2f", value);
    else snprintf(dst, n, "null");
  }

  bool _wifiConnected = false;
  uint32_t _lastWifiCheck = 0;

  static void networkTaskRoutine(void *param) {
    NetworkServer *self = static_cast<NetworkServer*>(param);
    
    // Регистрация mDNS
    if (MDNS.begin(HOSTNAME_MDNS)) {
      MDNS.addService("http", "tcp", 80);
    }

    self->_server.begin();

    while (true) {
      self->_server.handleClient();

      uint32_t now = millis();
      if (now - self->_lastWifiCheck >= 3000) {
        self->_lastWifiCheck = now;
        self->_wifiConnected = (WiFi.status() == WL_CONNECTED);
      }

      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  void setupRoutes() {
    _server.on("/", HTTP_GET, [this]() {
      _server.send(200, "text/html", getHTML());
    });

    _server.on("/api/telemetry", HTTP_GET, [this]() {
      WebTelemetry t;
      portENTER_CRITICAL(&_mux);
      t = _telemetry;
      portEXIT_CRITICAL(&_mux);

      ClimateConfig cfg;
      portENTER_CRITICAL(&_mux);
      cfg = _cfgSnapshot;
      portEXIT_CRITICAL(&_mux);
      char td[20], tw[20], rh[20], dew[20];
      numberOrNull(td, sizeof(td), t.currentTemp, t.sensorOk);
      numberOrNull(tw, sizeof(tw), t.wetTemp, t.sensorOk);
      numberOrNull(rh, sizeof(rh), t.currentHum, t.sensorOk);
      numberOrNull(dew, sizeof(dew), t.dewPoint, t.sensorOk);
      char buf[896];
      snprintf(buf, sizeof(buf),
        "{\"temp\":%s,\"dry_temp\":%s,\"wet_temp\":%s,\"hum\":%s,\"dew\":%s,\"rpm\":%u,"
        "\"r_cool\":%d,\"r_hum\":%d,\"r_fan\":%d,\"comp_wait\":%u,"
        "\"sensor_ok\":%d,\"sensor_error\":\"%s\",\"sensor_type\":\"DS18B20_PSYCHROMETER\","
        "\"fw_version\":\"" FW_VERSION "\",\"pressure_hpa\":%.2f,\"uptime\":%u,\"wifi_ok\":%d,"
        "\"t_tgt\":%.2f,\"t_hyst\":%.2f,\"h_tgt\":%.2f,\"h_hyst\":%.2f,\"fan_rpm\":%u,\"enabled\":%d}",
        td, td, tw, rh, dew, t.fanRPM,
        t.relayCool, t.relayHumid, t.relayFan, t.compressorWaitSec,
        t.sensorOk, t.sensorError, PSYCH_PRESSURE_HPA, t.uptimeSec, _wifiConnected,
        cfg.tempTarget, cfg.tempHyst, cfg.humTarget, cfg.humHyst, cfg.fanTargetRPM, cfg.enabled);
      _server.send(200, "application/json", buf);
    });

    _server.on("/api/settings", HTTP_POST, [this]() {
      if (_otaActive.load()) { _server.send(409, "text/plain", "OTA active"); return; }
      ClimateConfig cfg;
      portENTER_CRITICAL(&_mux); cfg = _cfgSnapshot; portEXIT_CRITICAL(&_mux);
      if (_server.hasArg("t_tgt")) cfg.tempTarget = _server.arg("t_tgt").toFloat();
      if (_server.hasArg("t_hyst")) cfg.tempHyst = _server.arg("t_hyst").toFloat();
      if (_server.hasArg("h_tgt")) cfg.humTarget = _server.arg("h_tgt").toFloat();
      if (_server.hasArg("h_hyst")) cfg.humHyst = _server.arg("h_hyst").toFloat();
      if (_server.hasArg("fan_rpm")) cfg.fanTargetRPM = _server.arg("fan_rpm").toInt();
      if (_server.hasArg("enabled")) cfg.enabled = _server.arg("enabled") == "1";
      if (!std::isfinite(cfg.tempTarget) || cfg.tempTarget < 2.0f || cfg.tempTarget > 50.0f ||
          !std::isfinite(cfg.tempHyst) || cfg.tempHyst < 0.1f || cfg.tempHyst > 10.0f ||
          !std::isfinite(cfg.humTarget) || cfg.humTarget < 10.0f || cfg.humTarget > 100.0f ||
          !std::isfinite(cfg.humHyst) || cfg.humHyst < 0.1f || cfg.humHyst > 30.0f ||
          cfg.fanTargetRPM < 100 || cfg.fanTargetRPM > 10000) {
        _server.send(400, "application/json", "{\"error\":\"settings range\"}"); return;
      }
      portENTER_CRITICAL(&_mux);
      _pendingConfig = cfg; _settingsPending = true;
      portEXIT_CRITICAL(&_mux);
      _server.send(200, "application/json", "{\"ok\":true}");
    });

    // Web OTA сервис обновления
    _server.on("/update", HTTP_GET, [this]() {
      _server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>OTA v2.1.0</title></head>"
        "<body style='font-family:sans-serif;padding:30px;background:#141619;color:#eee;'>"
        "<h2>Климат Камера: Web OTA Обновление</h2>"
        "<p>Выберите бинарный файл новой прошивки (.bin):</p>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='update' accept='.bin' style='margin:15px 0;'><br>"
        "<input type='submit' value='Прошить контроллер' style='padding:10px 24px;background:#0084ff;color:#fff;border:none;border-radius:6px;cursor:pointer;font-weight:bold;'>"
        "</form></body></html>"
      );
    });

    _server.on("/update", HTTP_POST, [this]() {
      const bool failed = _otaUploadFailed || Update.hasError();
      _server.send(200, "text/plain", failed ? "FAIL" : "OK");
      if (failed) _otaActive.store(false);
      else { delay(100); ESP.restart(); }
    }, [this]() {
      HTTPUpload& upload = _server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        _otaUploadFailed = false;
        _otaActive.store(true);
        // Обесточиваем выходы в Core 1 ДО записи Flash, а не после неё.
        const uint32_t started = millis();
        while (!_outputsSafe.load() && uint32_t(millis() - started) < 1000)
          vTaskDelay(pdMS_TO_TICKS(5));
        if (!_outputsSafe.load() || !Update.begin(UPDATE_SIZE_UNKNOWN)) _otaUploadFailed = true;
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!_otaUploadFailed && Update.write(upload.buf, upload.currentSize) != upload.currentSize)
          _otaUploadFailed = true;
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!_otaUploadFailed && !Update.end(true)) _otaUploadFailed = true;
        if (_otaUploadFailed) Update.abort();
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort(); _otaUploadFailed = true; _otaActive.store(false);
      }
    });
  }

  const char* getHTML() {
    return "<!doctype html><html lang='ru'><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Климат Камера v2.1.0</title>"
           "<style>"
           ":root{--bg:#101318;--card:#181d26;--border:#262d3d;--text:#f0f4f8;--accent:#0ea5e9;--green:#10b981;--warn:#f59e0b;--red:#ef4444;}"
           "*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;}"
           "body{background:var(--bg);color:var(--text);padding:16px;display:flex;justify-content:center;}"
           ".app{width:100%;max-width:480px;}"
           "header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;padding-bottom:12px;border-bottom:1px solid var(--border);}"
           "h1{font-size:18px;font-weight:700;color:var(--accent);}"
           ".badge{padding:4px 8px;border-radius:6px;font-size:11px;font-weight:bold;}"
           ".badge-on{background:rgba(16,185,129,0.2);color:var(--green);border:1px solid var(--green);}"
           ".badge-off{background:rgba(239,68,68,0.2);color:var(--red);border:1px solid var(--red);}"
           ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px;}"
           ".card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:14px;}"
           ".card-title{font-size:11px;color:#94a3b8;text-transform:uppercase;margin-bottom:6px;font-weight:600;}"
           ".card-val{font-size:32px;font-weight:bold;}"
           ".card-sub{font-size:12px;color:var(--accent);margin-top:4px;}"
           ".relays{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:12px;}"
           ".r-box{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:10px;text-align:center;}"
           ".r-lbl{font-size:11px;color:#94a3b8;margin-bottom:6px;}"
           ".r-stat{font-size:12px;font-weight:bold;padding:3px 6px;border-radius:4px;}"
           ".r-active{background:var(--green);color:#fff;}"
           ".r-wait{background:var(--warn);color:#fff;}"
           ".r-off{background:#334155;color:#cbd5e1;}"
           "label{display:block;font-size:13px;color:#cbd5e1;margin-top:10px;}"
           "input[type=number]{width:100%;padding:8px 12px;border-radius:8px;border:1px solid var(--border);background:#0d1117;color:#fff;font-size:15px;margin-top:4px;}"
           "button{width:100%;background:var(--accent);color:#fff;border:none;border-radius:8px;padding:12px;font-size:15px;font-weight:bold;margin-top:16px;cursor:pointer;}"
           "</style></head><body>"
           "<div class='app'>"
           "<header>"
           "<h1>Климат Камера v2.1.0</h1>"
           "<span id='sysBadge' class='badge badge-off'>ОТКЛ</span>"
           "</header>"
           "<div id='sensorStatus' style='margin-bottom:12px;font-size:13px'>DS18B20: ожидание пары</div>"
           "<div class='grid'>"
           "<div class='card'>"
           "<div class='card-title'>Температура</div>"
           "<div class='card-val'><span id='temp'>--.-</span><span style='font-size:16px;color:#94a3b8'> °C</span></div>"
           "<div class='card-sub'>Цель: <span id='t_tgt_lbl'>--</span> °C</div>"
           "</div>"
           "<div class='card'>"
           "<div class='card-title'>Влажность</div>"
           "<div class='card-val'><span id='hum'>--.-</span><span style='font-size:16px;color:#94a3b8'> %</span></div>"
           "<div class='card-sub'>Цель: <span id='h_tgt_lbl'>--</span> %</div>"
           "</div>"
           "</div>"
           "<div class='card' style='margin-bottom:12px;'>"
           "<div class='card-title'>Вентилятор камеры</div>"
           "<div style='display:flex;justify-content:space-between;align-items:center;'>"
           "<div class='card-val' style='font-size:24px;'><span id='rpm'>0</span><span style='font-size:14px;color:#94a3b8;'> RPM</span></div>"
           "<div style='font-size:13px;color:#94a3b8;'>Цель: <span id='rpm_lbl' style='color:#fff;'>--</span> RPM</div>"
           "</div>"
           "</div>"
           "<div class='relays'>"
           "<div class='r-box'><div class='r-lbl'>Холод</div><div id='rCool' class='r-stat r-off'>ОТКЛ</div></div>"
           "<div class='r-box'><div class='r-lbl'>Увлажн.</div><div id='rHum' class='r-stat r-off'>ОТКЛ</div></div>"
           "<div class='r-box'><div class='r-lbl'>Обдув</div><div id='rFan' class='r-stat r-off'>ОТКЛ</div></div>"
           "</div>"
           "<div class='card'>"
           "<div class='card-title'>Уставки климата</div>"
           "<label>Температура уставки (°C):<input type='number' step='0.1' id='t_tgt'></label>"
           "<label>Гистерезис температуры (°C):<input type='number' step='0.1' id='t_hyst'></label>"
           "<label>Влажность уставки (%):<input type='number' step='0.5' id='h_tgt'></label>"
           "<label>Гистерезис влажности (%):<input type='number' step='0.5' id='h_hyst'></label>"
           "<label>Обороты вентилятора (RPM):<input type='number' step='50' id='fan_rpm'></label>"
           "<button onclick='save()'>Сохранить в контроллер</button>"
           "</div>"
           "<div style='text-align:center;margin-top:16px;'><a href='/update' style='color:#64748b;font-size:12px;text-decoration:none;'>Web OTA Обновление прошивки</a></div>"
           "</div>"
           "<script>"
           "let loaded=false;"
           "function fetchTelem(){"
           "fetch('/api/telemetry').then(r=>r.json()).then(d=>{"
           "document.getElementById('temp').innerText=Number.isFinite(d.temp)?d.temp.toFixed(2):'--';"
           "document.getElementById('hum').innerText=Number.isFinite(d.hum)?d.hum.toFixed(1):'--';"
           "document.getElementById('rpm').innerText=d.rpm;"
           "let ss=document.getElementById('sensorStatus');ss.style.color=d.sensor_ok?'#10b981':'#ef4444';"
           "ss.innerText=d.sensor_ok?'DS18B20: сухой '+d.dry_temp.toFixed(2)+' C / влажный '+d.wet_temp.toFixed(2)+' C / P '+d.pressure_hpa.toFixed(1)+' hPa':'СБОЙ ПАРЫ: '+d.sensor_error;"
           "document.getElementById('t_tgt_lbl').innerText=d.t_tgt.toFixed(1);"
           "document.getElementById('h_tgt_lbl').innerText=d.h_tgt.toFixed(1);"
           "document.getElementById('rpm_lbl').innerText=d.fan_rpm;"
           "let sb=document.getElementById('sysBadge');"
           "sb.className=d.enabled?'badge badge-on':'badge badge-off';"
           "sb.innerText=d.enabled?'АКТИВЕН':'ОТКЛЮЧЕН';"
           "let c=document.getElementById('rCool');"
           "if(d.r_cool){c.className='r-stat r-active';c.innerText='РАБОТА';}"
           "else if(d.comp_wait>0){c.className='r-stat r-wait';c.innerText='ПАУЗА '+d.comp_wait+'с';}"
           "else{c.className='r-stat r-off';c.innerText='ОТКЛ';}"
           "let h=document.getElementById('rHum');"
           "h.className=d.r_hum?'r-stat r-active':'r-stat r-off';"
           "h.innerText=d.r_hum?'ПАР':'ОТКЛ';"
           "let f=document.getElementById('rFan');"
           "f.className=(d.rpm>50)?'r-stat r-active':'r-stat r-off';"
           "f.innerText=(d.rpm>50)?'ОБДУВ':'ОТКЛ';"
           "if(!loaded){"
           "document.getElementById('t_tgt').value=d.t_tgt;"
           "document.getElementById('t_hyst').value=d.t_hyst;"
           "document.getElementById('h_tgt').value=d.h_tgt;"
           "document.getElementById('h_hyst').value=d.h_hyst;"
           "document.getElementById('fan_rpm').value=d.fan_rpm;"
           "loaded=true;"
           "}"
           "});}"
           "function save(){"
           "let f=new FormData();"
           "f.append('t_tgt',document.getElementById('t_tgt').value);"
           "f.append('t_hyst',document.getElementById('t_hyst').value);"
           "f.append('h_tgt',document.getElementById('h_tgt').value);"
           "f.append('h_hyst',document.getElementById('h_hyst').value);"
           "f.append('fan_rpm',document.getElementById('fan_rpm').value);"
           "f.append('enabled','1');"
           "fetch('/api/settings',{method:'POST',body:f}).then(r=>{if(!r.ok)throw new Error('Ошибка настройки');alert('Настройки применены');}).catch(e=>alert(e.message));"
           "}"
           "setInterval(fetchTelem,1500);fetchTelem();"
           "</script></body></html>";
  }
};
