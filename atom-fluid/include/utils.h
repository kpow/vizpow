#ifndef UTILS_H
#define UTILS_H

#include "config.h"

namespace utils {

// clamp value between min and max, inlined for performance (float)
inline float __attribute__((always_inline)) clamp(float value, float min_val, float max_val) {
  return fminf(fmaxf(value, min_val), max_val);
}
// clamp (int)
inline int __attribute__((always_inline)) clamp(int value, int min_val, int max_val) {
  if (value < min_val) return min_val;
  if (value > max_val) return max_val;
  return value;
}

// quake III fast inverse square root, inlined for performance
inline float __attribute__((always_inline)) fastInvSqrt(float number) {
  long i;
  float x2, y;
  const float threehalfs = 1.5F;

  x2 = number * 0.5F;
  y = number;
  i = *(long*)&y;
  i = 0x5f3759df - (i >> 1);
  y = *(float*)&i;
  y = y * (threehalfs - (x2 * y * y));
  return y;
}

}  // namespace utils

#endif
