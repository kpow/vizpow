// Host stand-in for the Arduino core, just enough for vizbot/bot_eye_model.h to compile with
// c++ -std=c++17 on the Mac for the parity check. random() is a fixed LCG so runs repeat.
#pragma once
#include <cmath>
#include <math.h>
#include <cstdint>
#include <cstring>
#include <algorithm>
using std::min; using std::max;
#define PROGMEM
#define memcpy_P memcpy
#define TWO_PI 6.283185307179586476925286766559
#define HALF_PI 1.5707963267948966192313216916398
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
static uint32_t g_rng = 12345;
inline long random(long n) { g_rng = g_rng * 1664525u + 1013904223u; return n > 0 ? (long)((g_rng >> 8) % (uint32_t)n) : 0; }
inline long random(long a, long b) { return a + random(b - a); }
extern unsigned long g_millis;
inline unsigned long millis() { return g_millis; }
