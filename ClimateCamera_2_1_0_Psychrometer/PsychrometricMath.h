#pragma once
#include <cmath>

namespace PsychrometricMath {
enum class Error {
  None, InvalidPressure, InvalidCoefficient, NonFiniteTemperature,
  DryTemperatureRange, WetFrozenOrRange, WetAboveDry, VapourPressureRange
};
struct Result {
  bool ok;
  float humidity;
  float dry;
  float wet;
  Error error;
};
inline const char* errorText(Error e) {
  switch (e) {
    case Error::None: return "OK";
    case Error::InvalidPressure: return "INVALID_PRESSURE";
    case Error::InvalidCoefficient: return "INVALID_PSYCH_COEFFICIENT";
    case Error::NonFiniteTemperature: return "NONFINITE_TEMPERATURE";
    case Error::DryTemperatureRange: return "DRY_OUTSIDE_2_TO_50_C";
    case Error::WetFrozenOrRange: return "WET_OUTSIDE_0_5_TO_50_C";
    case Error::WetAboveDry: return "WET_ABOVE_DRY_CHECK_PROBES";
    case Error::VapourPressureRange: return "INVALID_VAPOUR_PRESSURE";
  }
  return "PSYCH_ERROR";
}
// Buck: давление насыщения над жидкой водой, hPa.
inline float saturationHpa(float t) {
  return 6.1121f * std::exp((18.678f - t / 234.5f) * t / (257.14f + t));
}
inline Result calculate(float dry, float wet, float pressureHpa = 1013.25f,
                        float coefficient = 0.00066f, float pairTolerance = 0.5f) {
  Result r{false, 0.0f, dry, wet, Error::None};
  if (!std::isfinite(pressureHpa) || pressureHpa < 500.0f || pressureHpa > 1100.0f)
    r.error = Error::InvalidPressure;
  else if (!std::isfinite(coefficient) || coefficient < 0.0003f || coefficient > 0.0015f ||
           !std::isfinite(pairTolerance) || pairTolerance < 0.0f || pairTolerance > 1.0f)
    r.error = Error::InvalidCoefficient;
  else if (!std::isfinite(dry) || !std::isfinite(wet))
    r.error = Error::NonFiniteTemperature;
  else if (dry < 2.0f || dry > 50.0f)
    r.error = Error::DryTemperatureRange;
  else if (wet < 0.5f || wet > 50.0f)
    r.error = Error::WetFrozenOrRange;
  else if (wet > dry + pairTolerance)
    r.error = Error::WetAboveDry;
  else {
    // В пределах допуска пары отрицательная депрессия соответствует 100% RH.
    const float effectiveWet = (wet > dry) ? dry : wet;
    const float ew = saturationHpa(effectiveWet);
    const float ed = saturationHpa(dry);
    const float a = coefficient * (1.0f + 0.00115f * effectiveWet);
    const float vapour = ew - a * pressureHpa * (dry - effectiveWet);
    if (!std::isfinite(vapour) || vapour <= 0.0f || vapour > ed * 1.001f)
      r.error = Error::VapourPressureRange;
    else {
      r.humidity = 100.0f * vapour / ed;
      if (r.humidity > 100.0f) r.humidity = 100.0f;
      r.ok = true;
    }
  }
  return r;
}
} // namespace PsychrometricMath
