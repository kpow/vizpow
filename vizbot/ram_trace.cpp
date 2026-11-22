// Internal-RAM allocation tracer: bench builds only (env stackchan-ramtrace).
//
// Wraps the allocator at link time (-Wl,--wrap=...) and keeps a ring of the
// recent internal-RAM allocations of 256 bytes or more: when, how big, which
// task, which caller, and how much internal RAM was left after it. Each time
// internal free RAM hits a new low, the ring is frozen into a snapshot, so
// /debug/ramtrace shows exactly what was allocated on the way down to the low.
// Decode callers: xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf <addr>.
#ifdef VIZ_RAM_TRACE

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" {
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void* __real_heap_caps_malloc(size_t, uint32_t);
void* __real_heap_caps_calloc(size_t, size_t, uint32_t);
void* __real_heap_caps_realloc(void*, size_t, uint32_t);
}

struct RtEntry {
  uint32_t ms;
  uint32_t size;
  uint32_t freeAfter;
  uint32_t caller;
  char     task[8];
};

#define RT_RING 96
#define RT_MIN_SIZE 256
static RtEntry* rtRing = nullptr;       // PSRAM
static RtEntry* rtAtMin = nullptr;      // PSRAM: the ring frozen at the low
static RtEntry* rtCopy = nullptr;       // PSRAM: for the web handler
static volatile uint32_t rtCount = 0, rtAtMinCount = 0;
static uint32_t rtMinFree = UINT32_MAX, rtAtMinMs = 0;
static portMUX_TYPE rtMux = portMUX_INITIALIZER_UNLOCKED;

void ramTraceBegin() {
  if (rtRing) return;
  rtRing = (RtEntry*)__real_heap_caps_calloc(RT_RING, sizeof(RtEntry), MALLOC_CAP_SPIRAM);
  rtAtMin = (RtEntry*)__real_heap_caps_calloc(RT_RING, sizeof(RtEntry), MALLOC_CAP_SPIRAM);
  rtCopy = (RtEntry*)__real_heap_caps_calloc(RT_RING, sizeof(RtEntry), MALLOC_CAP_SPIRAM);
}

static inline void rtNote(void* p, size_t size, uint32_t caller) {
  if (!p || size < RT_MIN_SIZE || !rtRing || !esp_ptr_internal(p)) return;
  if (xPortInIsrContext()) return;
  const uint32_t freeNow = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const char* name = pcTaskGetName(nullptr);
  portENTER_CRITICAL_SAFE(&rtMux);
  RtEntry& e = rtRing[rtCount % RT_RING];
  e.ms = (uint32_t)(esp_timer_get_time() / 1000);
  e.size = size;
  e.freeAfter = freeNow;
  e.caller = caller;
  for (int i = 0; i < 8; i++) { e.task[i] = name ? name[i] : 0; if (!e.task[i]) break; }
  e.task[7] = 0;
  rtCount++;
  if (freeNow < rtMinFree) {
    rtMinFree = freeNow;
    rtAtMinMs = e.ms;
    const uint32_t n = rtCount < RT_RING ? rtCount : RT_RING;
    for (uint32_t i = 0; i < n; i++) rtAtMin[i] = rtRing[(rtCount - n + i) % RT_RING];
    rtAtMinCount = n;
  }
  portEXIT_CRITICAL_SAFE(&rtMux);
}

#define RT_CALLER ((uint32_t)__builtin_return_address(0))

extern "C" {
void* __wrap_malloc(size_t s) { void* p = __real_malloc(s); rtNote(p, s, RT_CALLER); return p; }
void* __wrap_calloc(size_t n, size_t s) { void* p = __real_calloc(n, s); rtNote(p, n * s, RT_CALLER); return p; }
void* __wrap_realloc(void* o, size_t s) { void* p = __real_realloc(o, s); rtNote(p, s, RT_CALLER); return p; }
void* __wrap_heap_caps_malloc(size_t s, uint32_t c) { void* p = __real_heap_caps_malloc(s, c); rtNote(p, s, RT_CALLER); return p; }
void* __wrap_heap_caps_calloc(size_t n, size_t s, uint32_t c) { void* p = __real_heap_caps_calloc(n, s, c); rtNote(p, n * s, RT_CALLER); return p; }
void* __wrap_heap_caps_realloc(void* o, size_t s, uint32_t c) { void* p = __real_heap_caps_realloc(o, s, c); rtNote(p, s, RT_CALLER); return p; }
}

// JSON for /debug/ramtrace (?reset=1 starts a new low).
String ramTraceJson(bool reset) {
  String j;
  j.reserve(9000);
  j = "{\"minFree\":" + String(rtMinFree) + ",\"atMs\":" + String(rtAtMinMs) +
      ",\"allocs\":" + String(rtCount) + ",\"atMin\":[";
  portENTER_CRITICAL(&rtMux);
  RtEntry* copy = rtCopy;
  const uint32_t n = (rtAtMin && copy) ? rtAtMinCount : 0;
  if (n) memcpy(copy, rtAtMin, n * sizeof(RtEntry));
  if (reset) { rtMinFree = UINT32_MAX; rtAtMinCount = 0; }
  portEXIT_CRITICAL(&rtMux);
  for (uint32_t i = 0; i < n; i++) {
    const RtEntry& e = copy[i];
    char row[96];
    snprintf(row, sizeof(row), "%s[%lu,%lu,%lu,\"0x%08lx\",\"%s\"]", i ? "," : "",
             (unsigned long)e.ms, (unsigned long)e.size, (unsigned long)e.freeAfter,
             (unsigned long)e.caller, e.task);
    j += row;
  }
  j += "]}";
  return j;
}

#endif // VIZ_RAM_TRACE
