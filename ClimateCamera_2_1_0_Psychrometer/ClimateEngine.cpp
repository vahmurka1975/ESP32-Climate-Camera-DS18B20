#include "ClimateEngine.h"
#include <cmath>

static Ds18b20Psychrometer sensor(PIN_DS_DRY, PIN_DS_WET);
static volatile uint32_t tachPulses = 0;

static void IRAM_ATTR onTachPulseISR() {
  tachPulses++;
}

void ClimateEngine::init(const ClimateConfig &cfg) {
  pinMode(PIN_RELAY_IN1, OUTPUT);
  pinMode(PIN_RELAY_IN2, OUTPUT);
  pinMode(PIN_RELAY_IN3, OUTPUT);
  pinMode(PIN_RELAY_IN4, OUTPUT);
  allRelaysOff();

  // PWM вентилятора 25 кГц
  ledcSetup(FAN_PWM_CH, FAN_PWM_FREQ, FAN_PWM_RES);
  ledcAttachPin(PIN_FAN_PWM, FAN_PWM_CH);
  ledcWrite(FAN_PWM_CH, 255); // Инверсный: 255 = 0% вращения

  // Тахометр
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), onTachPulseISR, FALLING);

  // Два DS18B20: неблокирующая 12-bit конверсия на независимых шинах.
  sensor.begin();
  _sensorError = sensor.errorText();

  _fridgeChangedAt = millis();
  _humidCycleChangedAt = millis();
  _lastSensorPoll = millis() - SENSOR_PERIOD_MS;
}

void ClimateEngine::allRelaysOff() {
  setRelayFridge(false);
  setRelayHumid(false);
  setRelayFan(false);
  // Явно устанавливаем безопасные уровни даже до первого изменения флагов.
  digitalWrite(PIN_RELAY_IN1, RELAY_OFF);
  digitalWrite(PIN_RELAY_IN2, RELAY_OFF);
  digitalWrite(PIN_RELAY_IN3, RELAY_OFF);
  digitalWrite(PIN_RELAY_IN4, RELAY_OFF);
  _humidDemand = false;
  if (_humidCycleOn) {
    _humidCycleOn = false;
    _humidCycleChangedAt = millis();
  }
}

void ClimateEngine::setRelayFridge(bool state) {
  if (_relayFridge == state) return;
  digitalWrite(PIN_RELAY_IN4, state ? RELAY_ON : RELAY_OFF);
  _relayFridge = state;
  _fridgeChangedAt = millis();
}

void ClimateEngine::setRelayHumid(bool state) {
  if (_relayHumid == state) return;
  digitalWrite(PIN_RELAY_IN2, state ? RELAY_ON : RELAY_OFF);
  _relayHumid = state;
}

void ClimateEngine::setRelayFan(bool state) {
  if (_relayFan == state) return;
  digitalWrite(PIN_RELAY_IN1, state ? RELAY_ON : RELAY_OFF);
  _relayFan = state;
}

uint32_t ClimateEngine::getCompressorProtectionRemaining() const {
  if (_relayFridge) return 0;
  uint32_t elapsed = millis() - _fridgeChangedAt;
  if (elapsed < FRIDGE_MIN_OFF_MS) {
    return (FRIDGE_MIN_OFF_MS - elapsed + 999) / 1000;
  }
  return 0;
}

float ClimateEngine::getDewPoint() const {
  if (!_sensorValid) return 0.0f;
  float a = 17.27f;
  float b = 237.7f;
  float alpha = ((a * _curTemp) / (b + _curTemp)) + logf(_curHum / 100.0f);
  return (b * alpha) / (a - alpha);
}

void ClimateEngine::update(const ClimateConfig &cfg) {
  updateSensor(cfg);
  if (!cfg.enabled || !_sensorValid) {
    allRelaysOff();
    ledcWrite(FAN_PWM_CH, 255);
    return;
  }
  updateLogic(cfg);
  updateFan(cfg);
}

void ClimateEngine::updateSensor(const ClimateConfig &cfg) {
  const uint32_t now = millis();
  if (_sensorValid && uint32_t(now - _lastSensorGood) >= SENSOR_STALE_MS) {
    _sensorValid = false;
    _sensorError = "PAIR_DATA_STALE";
    _curTemp = _curHum = _wetTemp = NAN;
    allRelaysOff();
  }
  if (!sensor.isMeasuring() && uint32_t(now - _lastSensorPoll) >= SENSOR_PERIOD_MS) {
    _lastSensorPoll = now;
    if (!sensor.startMeasurement()) {
      _sensorValid = false;
      _sensorError = sensor.errorText();
      _curTemp = _curHum = _wetTemp = NAN;
      allRelaysOff();
    }
  }
  if (!sensor.measurementReady()) return;
  float dry, wet;
  if (!sensor.readMeasurement(dry, wet)) {
    _sensorValid = false;
    _sensorError = sensor.errorText();
    _curTemp = _curHum = _wetTemp = NAN;
    allRelaysOff();
    return;
  }
  // Поправки температур применяются ДО вычисления RH.
  dry += PSYCH_DRY_OFFSET_C + cfg.tempOffset;
  wet += PSYCH_WET_OFFSET_C;
  auto result = PsychrometricMath::calculate(dry, wet, PSYCH_PRESSURE_HPA,
                                            PSYCH_COEFFICIENT, PSYCH_PAIR_TOLERANCE_C);
  if (!result.ok || !std::isfinite(cfg.humOffset)) {
    _sensorValid = false;
    _sensorError = result.ok ? "INVALID_HUMIDITY_OFFSET" : PsychrometricMath::errorText(result.error);
    _curTemp = _curHum = _wetTemp = NAN;
    allRelaysOff();
    return;
  }
  _curTemp = dry;
  _wetTemp = wet;
  _curHum = constrain(result.humidity + cfg.humOffset, 0.0f, 100.0f);
  _sensorValid = true;
  _sensorError = "OK";
  _lastSensorGood = now;
}

void ClimateEngine::updateLogic(const ClimateConfig &cfg) {
  uint32_t now = millis();

  // 1. Управление компрессором (Холодильник IN4)
  if (_relayFridge) {
    if (now - _fridgeChangedAt >= COMPRESSOR_MAX_ON_MS) {
      setRelayFridge(false); // Защита: отсечка 40 минут
    } else if (_curTemp <= cfg.tempTarget && (now - _fridgeChangedAt >= COMPRESSOR_MIN_ON_MS)) {
      setRelayFridge(false);
    }
  } else {
    if (getCompressorProtectionRemaining() == 0 && (_curTemp > cfg.tempTarget + cfg.tempHyst)) {
      setRelayFridge(true);
    }
  }

  // 2. Управление увлажнителем (IN2): 20с работа / 120с пауза
  if (_curHum >= cfg.humTarget) {
    _humidDemand = false;
  } else if (_curHum < cfg.humTarget - cfg.humHyst) {
    _humidDemand = true;
  }

  if (!_humidDemand) {
    if (_relayHumid) setRelayHumid(false);
    _humidCycleOn = true;
    _humidCycleChangedAt = now;
  } else {
    if (_humidCycleOn) {
      if (now - _humidCycleChangedAt >= HUMID_WORK_MS) {
        setRelayHumid(false);
        _humidCycleOn = false;
        _humidCycleChangedAt = now;
      } else if (!_relayHumid) {
        setRelayHumid(true);
      }
    } else {
      if (now - _humidCycleChangedAt >= HUMID_PAUSE_MS) {
        _humidCycleOn = true;
        _humidCycleChangedAt = now;
        setRelayHumid(true);
      }
    }
  }
}

void ClimateEngine::updateFan(const ClimateConfig &cfg) {
  uint32_t now = millis();
  if (now - _lastFanCalc < 1000) return;

  uint32_t dt = now - _lastFanCalc;
  _lastFanCalc = now;

  noInterrupts();
  uint32_t p = tachPulses;
  tachPulses = 0;
  interrupts();

  _fanRPM = (uint16_t)((p / TACH_PULSES_PER_REV) * (60000UL / dt));

  // ПИ-регулятор оборотов вентилятора
  float err = (float)cfg.fanTargetRPM - (float)_fanRPM;
  _fanIntegral = constrain(_fanIntegral + err * (dt / 1000.0f) * cfg.pidKi, 0.0f, 255.0f);
  float out = (cfg.fanTargetRPM / 15.0f) + (err * cfg.pidKp) + _fanIntegral;
  _fanDuty = (uint8_t)constrain((int)out, 20, 255);

  // Инверсный ШИМ: 0 -> 255, 255 -> 0
  ledcWrite(FAN_PWM_CH, 255 - _fanDuty);
  setRelayFan(_fanDuty > 10);
}
