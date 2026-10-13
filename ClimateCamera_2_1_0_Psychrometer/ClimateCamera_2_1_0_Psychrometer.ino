/*
 * =========================================================================
 * Проект: ESP32-2,4TFT-WiFI-Climate-Camera
 * Версия: 2.1.0
 * Контроллер: ESP32 Dev Module (WROOM-32)
 * Дисплей: 2.4" TFT ILI9341 SPI (240x320), пин CS = GPIO 19
 * Датчики: психрометрическая пара DS18B20 (сухой GPIO21, влажный GPIO22)
 * Wi-Fi: Домашняя сеть Svetlana (Пароль: 89776381197) + AP ClimateCamera-AP
 * Среда: Arduino IDE 1.8.19 / 2.x
 * =========================================================================
 */

#include <esp_task_wdt.h>
#include "Config.h"
#include "ClimateEngine.h"
#include "DisplayUI.h"
#include "NetworkServer.h"

static ConfigManager cfgManager;
static ClimateEngine engine;
static DisplayUI     ui;
static NetworkServer netServer;

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n[INIT] Запуск ESP32-2,4TFT-WiFI-Climate-Camera v" FW_VERSION));

  // 1. Аппаратный сторожевой таймер Task WDT на 8 секунд
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);

  // 2. Инициализация энергонезависимой памяти настроек NVS
  cfgManager.begin();

  // 3. Запуск климатического ядра и дисплея 2.4" ILI9341
  engine.init(cfgManager.data);
  ui.init();

  // 4. Запуск сетевой службы на Core 0 (Wi-Fi, Web, OTA)
  netServer.begin(&cfgManager);

  Serial.println(F("[INIT] Система инициализирована и работает."));
}

void loop() {
  // Сброс сторожевого таймера WDT в основном цикле
  esp_task_wdt_reset();

  // Отложенная запись уставок в NVS Flash
  netServer.applyPendingSettings();
  cfgManager.update();

  // Работа климатического цикла на Core 1
  ClimateConfig controlCfg = cfgManager.data;
  if (netServer.isOtaActive()) controlCfg.enabled = false;
  engine.update(controlCfg);

  // Получение актуального IP адреса
  char ipStr[16];
  netServer.getIPString(ipStr, sizeof(ipStr));
  bool wifiOk = netServer.isConnected();

  // Передача снимка телеметрии сетевому веб-серверу
  WebTelemetry t{};
  t.currentTemp = engine.getTemp();
  t.currentHum  = engine.getHum();
  t.wetTemp     = engine.getWetTemp();
  t.relayFan    = engine.isFanOn();
  snprintf(t.sensorError, sizeof(t.sensorError), "%s", engine.getSensorError());
  t.dewPoint    = engine.getDewPoint();
  t.fanRPM      = engine.getFanRPM();
  t.relayCool   = engine.isCoolOn();
  t.relayHeat   = engine.isHeatOn();
  t.relayHumid  = engine.isHumidOn();
  t.relayDehumid= engine.isDehumidOn();
  t.compressorWaitSec = engine.getCompressorProtectionRemaining();
  t.sensorOk    = engine.isSensorValid();
  t.uptimeSec   = millis() / 1000;
  t.wifiConnected = wifiOk;
  strncpy(t.ipStr, ipStr, sizeof(t.ipStr));
  netServer.updateTelemetry(t, cfgManager.data);

  // Отрисовка дисплея ILI9341 240x320
  ui.update(engine, cfgManager.data, ipStr, wifiOk);

  yield();
}
