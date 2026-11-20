#ifndef MEM_H
#define MEM_H

// ============================================================================
// Memory owner
// ============================================================================
// Every buffer the firmware allocates by hand comes through here, under the
// name of whoever wants it. One place decides between PSRAM and internal RAM,
// one place answers "is there PSRAM", and /debug/mem lists who holds what.
// Not covered: memory libraries take for themselves (the camera's frame
// buffer, display sprites, WiFi and lwIP buffers) and static arrays.
//
// Internal RAM is the scarce one: WiFi, lwIP and DMA (the camera needs one
// 30720-byte block) can only use it. Anything large goes to PSRAM when the
// board has some.
// ============================================================================

#include <stddef.h>
#include <stdint.h>

enum MemTag : uint8_t {
  MEM_TLS = 0,    // mbedTLS records and handshake state (cloud, firmware download)
  MEM_JSON,       // ArduinoJson documents for cloud content
  MEM_MIDI,       // cloud MIDI sequences
  MEM_VOICE,      // voice turn: recording, playback and lip-sync buffers
  MEM_WAKE,       // wake word ring buffer
  MEM_ART,        // album art upload
  MEM_OTA,        // cloud firmware download chunk
  MEM_SERVO,      // servo probe bus
  MEM_WEB,        // web handler scratch buffers
  MEM_TAG_COUNT
};

enum MemWhere : uint8_t {
  MEM_BIG,        // PSRAM, internal RAM when there is none or it is full
  MEM_BIG_ONLY,   // PSRAM or nothing: too large for internal RAM
  MEM_DEFAULT,    // plain malloc: the framework picks
};

// Call first thing in setup(), before any TLS: routes mbedTLS through MEM_TLS.
void memInit();

// The one answer to "does this board have PSRAM in the heap".
bool memHasBig();

void* memAlloc(MemTag who, size_t bytes, MemWhere where = MEM_BIG);
void* memCalloc(MemTag who, size_t n, size_t size, MemWhere where = MEM_BIG);
void* memRealloc(MemTag who, void* p, size_t bytes, MemWhere where = MEM_BIG);
void  memFree(MemTag who, void* p);

// Largest free block: big = where MEM_BIG allocations land (PSRAM, or internal
// RAM on a board without it); otherwise internal RAM.
size_t memLargest(bool big);

// True when a bigBytes block fits where MEM_BIG lands AND an internalBytes
// block fits in internal RAM. For callers that would rather skip than fail.
bool memRoomFor(size_t bigBytes, size_t internalBytes);

// Writes the /debug/mem JSON into buf; returns its length.
size_t memReportJson(char* buf, size_t cap);

#endif // MEM_H
