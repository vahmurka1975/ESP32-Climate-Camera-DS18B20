#pragma once
#include <Arduino.h>
#include <Preferences.h>

#define FW_VERSION "2.1.0"
#define REPO_NAME  "ESP32-2,4TFT-WiFI-Climate-Camera"

// ==================== АППАРАТНАЯ РАСПИНОВКА ====================
// Дисплей 2.4" ILI9341 SPI (240x320)
static constexpr uint8_t PIN_TFT_MOSI = 23;
static constexpr uint8_t PIN_TFT_SCLK = 18;
static constexpr uint8_t PIN_TFT_CS   = 19; // Новый выделенный пин CS
static constexpr uint8_t PIN_TFT_DC   = 25;
static constexpr uint8_t PIN_TFT_RST  = 13;

// Две независимые шины 1-Wire: роли не зависят от порядка ROM-адресов.
static constexpr uint8_t PIN_DS_DRY = 21; // Сухой DS18B20
static constexpr uint8_t PIN_DS_WET = 22; // Влажный DS18B20 с фитилём

// Формула для аспирируемого психрометра над жидкой водой.
// Обязателен постоянный внешний обдув обоих зондов (примерно 2–4 м/с).
static constexpr float PSYCH_PRESSURE_HPA = 1013.25f; // Местное абсолютное давление
static constexpr float PSYCH_COEFFICIENT = 0.00066f; // A0, 1/°C; уточняется калибровкой
static constexpr float PSYCH_DRY_OFFSET_C = 0.0f;
static constexpr float PSYCH_WET_OFFSET_C = 0.0f;
static constexpr float PSYCH_PAIR_TOLERANCE_C = 0.5f;

// Энкодер с кнопкой
static constexpr uint8_t PIN_ENC_A    = 32;
static constexpr uint8_t PIN_ENC_B    = 33;
static constexpr uint8_t PIN_ENC_SW   = 4;

// Вентилятор обдува (4-pin PWM 25 кГц + тахометр)
static constexpr uint8_t PIN_FAN_PWM  = 16;
static constexpr uint8_t PIN_FAN_TACH = 27;

// Релейный модуль (активный HIGH)
static constexpr uint8_t PIN_RELAY_IN1 = 15; // Питание вентилятора
static constexpr uint8_t PIN_RELAY_IN2 = 17; // Увлажнитель
static constexpr uint8_t PIN_RELAY_IN3 = 14; // Резерв (всегда LOW)
static constexpr uint8_t PIN_RELAY_IN4 = 26; // Компрессор холодильника

static constexpr uint8_t RELAY_ON  = HIGH;
static constexpr uint8_t RELAY_OFF = LOW;

// ==================== ПАРАМЕТРЫ ЗАЩИТЫ ОБОРУДОВАНИЯ ====================
static constexpr uint32_t WDT_TIMEOUT_SEC        = 8;
static constexpr uint32_t SENSOR_PERIOD_MS       = 2000;
static constexpr uint32_t SENSOR_STALE_MS        = 7000;

// Защита компрессора
static constexpr uint32_t FRIDGE_START_DELAY_MS  = 300000; // 5 минут паузы при старте питания
static constexpr uint32_t FRIDGE_MIN_OFF_MS      = 300000; // 5 минут отдыха между выключением и включением
static constexpr uint32_t COMPRESSOR_MIN_ON_MS   = 120000; // 2 минуты минимальный рабочий цикл
static constexpr uint32_t COMPRESSOR_MAX_ON_MS   = 2400000;// 40 минут лимит непрерывного охлаждения

// Импульсное увлажнение
static constexpr uint32_t HUMID_WORK_MS          = 20000;  // 20 секунд работы пара
static constexpr uint32_t HUMID_PAUSE_MS         = 120000; // 120 секунд паузы для распределения

// ШИМ вентилятора (25 кГц, инверсный для 4-pin серверных кулеров)
static constexpr uint32_t FAN_PWM_FREQ           = 25000;
static constexpr uint8_t  FAN_PWM_RES            = 8;
static constexpr uint8_t  FAN_PWM_CH             = 0;
static constexpr float    TACH_PULSES_PER_REV    = 2.0f;

// ==================== СЕТЕВЫЕ НАСТРОЙКИ ПО УМОЛЧАНИЮ ====================
static const char DEFAULT_WIFI_SSID[] = "Svetlana";
static const char DEFAULT_WIFI_PASS[] = "89776381197";
static const char AP_FALLBACK_SSID[]  = "ClimateCamera-AP";
static const char AP_FALLBACK_PASS[]  = "12345678";
static const char HOSTNAME_MDNS[]     = "climate-camera";

// ==================== СТРУКТУРЫ ДАННЫХ И ХРАНЕНИЕ ====================
struct ClimateConfig {
  bool enabled;
  float tempTarget;
  float tempHyst;
  float humTarget;
  float humHyst;
  uint16_t fanWorkMin;
  uint16_t fanPauseMin;
  uint16_t fanTargetRPM;
  float tempOffset;
  float humOffset;
  float pidKp;
  float pidKi;
};

struct WebTelemetry {
  float currentTemp;
  float currentHum;
  float wetTemp;
  bool relayFan;
  char sensorError[48];
  float dewPoint;
  uint16_t fanRPM;
  bool relayCool;
  bool relayHeat;
  bool relayHumid;
  bool relayDehumid;
  uint32_t compressorWaitSec;
  bool sensorOk;
  uint32_t uptimeSec;
  bool wifiConnected;
  char ipStr[16];
};

class ConfigManager {
public:
  ClimateConfig data;

  void begin() {
    setDefaults();
    _prefs.begin("clim_cfg", false);
    if (_prefs.isKey("cfg_init")) {
      load();
    } else {
      saveNow();
      _prefs.putBool("cfg_init", true);
    }
    // Не переносим поправки SHT/HTU на новую психрометрическую пару.
    if (!_prefs.getBool("psych_v1", false)) {
      data.tempOffset = 0.0f;
      data.humOffset = 0.0f;
      saveNow();
      _prefs.putBool("psych_v1", true);
    }
  }

  void scheduleSave() {
    _needSave = true;
    _saveTimer = millis();
  }

  void update() {
    if (_needSave && (millis() - _saveTimer >= 2000)) {
      saveNow();
      _needSave = false;
    }
  }

  void saveNow() {
    _prefs.putBytes("cfg", &data, sizeof(ClimateConfig));
  }

  void load() {
    _prefs.getBytes("cfg", &data, sizeof(ClimateConfig));
  }

  void setDefaults() {
    data.enabled = true;
    data.tempTarget = 12.0f;
    data.tempHyst = 1.0f;
    data.humTarget = 75.0f;
    data.humHyst = 3.0f;
    data.fanWorkMin = 10;
    data.fanPauseMin = 30;
    data.fanTargetRPM = 600;
    data.tempOffset = 0.0f;
    data.humOffset = 0.0f;
    data.pidKp = 0.08f;
    data.pidKi = 0.02f;
  }

private:
  Preferences _prefs;
  bool _needSave = false;
  uint32_t _saveTimer = 0;
};
