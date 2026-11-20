#include "mem.h"

#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <mbedtls/platform.h>

// See mem.h. This file holds the only heap_caps_* allocation calls in the
// firmware, apart from libraries that allocate for themselves (the camera
// driver's frame buffer, LovyanGFX sprites).

static const char* const kTagNames[MEM_TAG_COUNT] = {
  "tls", "json", "midi", "voice", "wake", "art", "ota", "servo", "web", "diag",
};

struct MemStat {
  std::atomic<int32_t>  held{0};       // bytes held now
  std::atomic<int32_t>  peak{0};       // most ever held
  std::atomic<uint32_t> allocs{0};     // successful allocations
  std::atomic<uint32_t> fails{0};      // requests that got nothing
  std::atomic<uint32_t> internal{0};   // MEM_BIG requests that landed in internal RAM
};
static MemStat memStats[MEM_TAG_COUNT];

#define MEM_CAPS_BIG      (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define MEM_CAPS_INTERNAL (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

bool memHasBig() {
  // The heap's own answer. Arduino's psramFound() is stubbed to false on a
  // board definition without BOARD_HAS_PSRAM even when IDF has PSRAM up.
  // Only a yes is remembered: a call before the framework adds PSRAM to the
  // heap must not pin the answer to no.
  static bool has = false;
  if (!has) has = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
  return has;
}

static void memCount(MemTag who, void* p, bool wantedBig) {
  MemStat& s = memStats[who];
  if (!p) { s.fails++; return; }
  s.allocs++;
  const int32_t size = (int32_t)heap_caps_get_allocated_size(p);
  const int32_t now = s.held.fetch_add(size) + size;
  int32_t peak = s.peak.load();
  while (now > peak && !s.peak.compare_exchange_weak(peak, now)) {}
  if (wantedBig && !esp_ptr_external_ram(p)) s.internal++;
}

static void memUncount(MemTag who, void* p) {
  if (p) memStats[who].held.fetch_sub((int32_t)heap_caps_get_allocated_size(p));
}

void* memAlloc(MemTag who, size_t bytes, MemWhere where) {
  void* p = nullptr;
  if (where == MEM_DEFAULT) {
    p = malloc(bytes);
  } else {
    p = heap_caps_malloc(bytes, MEM_CAPS_BIG);
    if (!p && where == MEM_BIG) p = heap_caps_malloc(bytes, MEM_CAPS_INTERNAL);
  }
  memCount(who, p, where == MEM_BIG);
  return p;
}

void* memCalloc(MemTag who, size_t n, size_t size, MemWhere where) {
  void* p = nullptr;
  if (where == MEM_DEFAULT) {
    p = calloc(n, size);
  } else {
    p = heap_caps_calloc(n, size, MEM_CAPS_BIG);
    if (!p && where == MEM_BIG) p = heap_caps_calloc(n, size, MEM_CAPS_INTERNAL);
  }
  memCount(who, p, where == MEM_BIG);
  return p;
}

void* memRealloc(MemTag who, void* p, size_t bytes, MemWhere where) {
  if (!p) return memAlloc(who, bytes, where);
  if (bytes == 0) { memFree(who, p); return nullptr; }
  const int32_t before = (int32_t)heap_caps_get_allocated_size(p);
  void* q = nullptr;
  if (where == MEM_DEFAULT) {
    q = realloc(p, bytes);
  } else {
    q = heap_caps_realloc(p, bytes, MEM_CAPS_BIG);
    if (!q && where == MEM_BIG) q = heap_caps_realloc(p, bytes, MEM_CAPS_INTERNAL);
  }
  if (!q) { memStats[who].fails++; return nullptr; }   // a failed realloc leaves p as it was
  memStats[who].held.fetch_sub(before);
  memCount(who, q, where == MEM_BIG);
  return q;
}

void memFree(MemTag who, void* p) {
  if (!p) return;
  memUncount(who, p);
  heap_caps_free(p);   // takes either heap
}

size_t memLargest(bool big) {
  return heap_caps_get_largest_free_block(big && memHasBig() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
}

bool memRoomFor(size_t bigBytes, size_t internalBytes) {
  return memLargest(true) >= bigBytes && memLargest(false) >= internalBytes;
}

// mbedTLS is built to allocate in internal RAM (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC),
// so every HTTPS connection took ~44 KB of it for its 16 KB record buffers and
// handshake state: the RAM WiFi, DMA and the camera need. This is what
// CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC does, set at runtime.
static void* memTlsCalloc(size_t n, size_t size) { return memCalloc(MEM_TLS, n, size, MEM_BIG); }
static void  memTlsFree(void* p)                 { memFree(MEM_TLS, p); }

void memInit() {
  mbedtls_platform_set_calloc_free(memTlsCalloc, memTlsFree);
}

size_t memReportJson(char* buf, size_t cap) {
  if (!buf || cap == 0) return 0;
  size_t n = 0;
  auto add = [&](const char* fmt, ...) {
    if (n >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
    if (w > 0) n = (n + (size_t)w < cap) ? n + (size_t)w : cap - 1;
  };
  add("{\"hasBig\":%s", memHasBig() ? "true" : "false");
  add(",\"internal\":{\"free\":%u,\"minFree\":%u,\"largest\":%u}",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  add(",\"big\":{\"free\":%u,\"minFree\":%u,\"largest\":%u}",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  add(",\"owners\":{");
  for (int i = 0; i < MEM_TAG_COUNT; i++) {
    const MemStat& s = memStats[i];
    add("%s\"%s\":{\"held\":%d,\"peak\":%d,\"allocs\":%u,\"fails\":%u,\"internal\":%u}",
        i ? "," : "", kTagNames[i], (int)s.held.load(), (int)s.peak.load(),
        (unsigned)s.allocs.load(), (unsigned)s.fails.load(), (unsigned)s.internal.load());
  }
  add("}}");
  return n;
}
