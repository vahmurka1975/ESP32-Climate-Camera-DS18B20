#pragma once
#include "Config.h"
#include "Ds18b20Psychrometer.h"
#include "PsychrometricMath.h"
#include <cmath>

class ClimateEngine {
public:
  void init(const ClimateConfig &cfg);
  void update(const ClimateConfig &cfg);

  float getTemp() const { return _curTemp; }
  float getHum() const { return _curHum; }
  float getWetTemp() const { return _wetTemp; }
  const char* getSensorError() const { return _sensorError; }
  float getDewPoint() const;
  uint16_t getFanRPM() const { return _fanRPM; }

  bool isCoolOn() const { return _relayFridge; }
  bool isHeatOn() const { return false; }
  bool isHumidOn() const { return _relayHumid; }
  bool isDehumidOn() const { return false; }
  bool isFanOn() const { return _relayFan; }
  bool isSensorValid() const { return _sensorValid; }
  uint32_t getCompressorProtectionRemaining() const;

  void allRelaysOff();

private:
  float _curTemp = NAN;
  float _curHum = NAN;
  float _wetTemp = NAN;
  const char* _sensorError = "WAITING_FOR_PAIR";
  bool  _sensorValid = false;

  bool _relayFridge = false;
  bool _relayHumid  = false;
  bool _relayFan    = false;

  uint32_t _fridgeChangedAt = 0;
  uint32_t _humidCycleChangedAt = 0;
  bool     _humidCycleOn = false;
  bool     _humidDemand = false;

  uint32_t _lastSensorPoll = 0;
  uint32_t _lastSensorGood = 0;

  uint16_t _fanRPM = 0;
  uint32_t _lastFanCalc = 0;
  float    _fanIntegral = 0.0f;
  uint8_t  _fanDuty = 0;

  void updateSensor(const ClimateConfig &cfg);
  void updateLogic(const ClimateConfig &cfg);
  void updateFan(const ClimateConfig &cfg);
  void setRelayFridge(bool state);
  void setRelayHumid(bool state);
  void setRelayFan(bool state);
};
