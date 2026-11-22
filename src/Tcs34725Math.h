#pragma once
#include <stdint.h>
#include <math.h>
#include <limits.h>

namespace Tcs34725Math {
// DN40, GA=1 (no glass attenuation calibration). See docs/prog_json_schema.md.
inline bool saturated(const uint16_t *v, int cycles) {
  uint32_t limit = cycles * 1024U;
  if (limit > 65535U) limit = 65535U;
  if (cycles < 64) limit = limit * 3 / 4;
  for (int i = 0; i < 4; ++i) if (v[i] >= limit) return true;
  return false;
}
inline int rounded(float value) {
  if (!isfinite(value) || value < 0 || value >= float(INT_MAX)) return -1;
  return int(value + 0.5f);
}
inline int estimate(const uint16_t *v, int cycles, int gain, bool cct) {
  if (saturated(v, cycles)) return -1;
  float ir = (float(v[0]) + v[1] + v[2] - v[3]) / 2.0f;
  if (ir < 0) ir = 0;
  const float r = fmaxf(0, v[0] - ir), g = fmaxf(0, v[1] - ir), b = fmaxf(0, v[2] - ir);
  if (cct) return r > 0 && v[3] > 0 ? rounded(3810.0f * b / r + 1391.0f) : -1;
  const float cpl = (cycles * 2.4f * gain) / 310.0f;
  return rounded(fmaxf(0, (0.136f * r + g - 0.444f * b) / cpl));
}
inline int color(const uint16_t *v, int cycles) {
  if (saturated(v, cycles)) return -1;
  if (!v[3]) return 0;
  // motorControl/src/hal/tcs34725_sensor.cpp defaults; no EMPTY calibration here.
  const float refs[4][3] = {{.55f,.25f,.20f},{.45f,.40f,.15f},{.20f,.30f,.50f},{.20f,.50f,.30f}};
  float best = .15f * .15f;
  int result = 0;
  for (int i = 0; i < 4; ++i) {
    float distance = 0;
    for (int j = 0; j < 3; ++j) {
      const float d = float(v[j]) / v[3] - refs[i][j];
      distance += d * d;
    }
    if (distance <= best) { best = distance; result = i + 1; }
  }
  return result;
}
}
