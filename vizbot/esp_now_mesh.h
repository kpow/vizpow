#ifndef ESP_NOW_MESH_H
#define ESP_NOW_MESH_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"

// ============================================================================
// ESP-NOW Local Mesh — Bot-to-Bot Discovery & WLED Coordination
// ============================================================================
// Lightweight ESP-NOW mesh for peer discovery and cooperative WLED arbitration.
// All bots broadcast their state every 3 seconds; peers are cached locally.
//
// WLED coordination: Before sending DDP, check meshAnyPeerWledActive().
// If a peer is active, defer. When starting DDP, call meshSetWledActive(true)
// which triggers an immediate broadcast so peers learn within ~1ms.
//
// Thread safety:
// - meshOnReceive() runs on Core 0 WiFi context (ISR-like, keep fast)
// - pollMeshBroadcast() runs on Core 0 WiFi task loop
// - meshAnyPeerWledActive() / meshSetWledActive() called from Core 0
// ============================================================================

// Timing constants
#define MESH_BROADCAST_INTERVAL_MS 3000   // Normal periodic broadcast
#define MESH_STALE_TIMEOUT_MS      15000  // Peer expiry (5 missed broadcasts)
#define MESH_MAX_PEERS             10
#define MESH_SCAN_BURST_COUNT      3      // Rapid broadcasts on scan request
#define MESH_SCAN_BURST_INTERVAL   500    // ms between burst broadcasts

// Protocol version — increment on packet format change
#define MESH_PROTOCOL_VERSION 1

// ============================================================================
// MeshStatePacket — 30 bytes, packed, broadcast via ESP-NOW
// ============================================================================
// Matches plan doc B.1. Contains bot identity, expression, sensors, and flags.

struct __attribute__((packed)) MeshStatePacket {
  uint8_t  version;        // Protocol version (MESH_PROTOCOL_VERSION)
  uint8_t  deviceId;       // Last byte of MAC address (unique enough for local mesh)
  uint8_t  expression;     // Current facial expression index
  uint8_t  botState;       // BOT_ACTIVE=0, BOT_IDLE=1, etc.
  uint8_t  battery;        // Battery % (0 for USB-powered, future use)
  uint8_t  mode;           // Current mode (MODE_BOT=0)
  uint8_t  effect;         // Current ambient effect index
  uint8_t  palette;        // Current palette index

  int16_t  pitch;          // Orientation pitch × 100 (degrees)
  int16_t  roll;           // Orientation roll × 100 (degrees)
  uint16_t heading;        // Compass heading (0-359, or 0xFFFF if unavailable)

  uint8_t  audioLevel;     // Mic audio level (0-255)
  uint8_t  proximity;      // Proximity sensor raw (0-255)
  uint16_t ambientLux;     // Ambient light (lux)

  uint8_t  reserved[6];    // reserved[0] bit 0 = wledActive flag
                           // reserved[1..5] = future use
};

static_assert(sizeof(MeshStatePacket) == 24, "MeshStatePacket must be 24 bytes");

// ============================================================================
// MeshPeer — cached peer state
// ============================================================================

struct MeshPeer {
  uint8_t          mac[6];
  uint8_t          deviceId;
  MeshStatePacket  lastPacket;
  unsigned long    lastSeenMs;
  bool             active;
};

// ============================================================================
// MeshData — static mesh state (~460 bytes BSS)
// ============================================================================

static struct {
  MeshPeer      peers[MESH_MAX_PEERS];
  uint8_t       peerCount;
  uint8_t       myDeviceId;
  bool          initialized;
  bool          wledActive;        // Are WE currently sending DDP?

  unsigned long lastBroadcastMs;

  // Scan burst state
  uint8_t       scanBurstRemaining;
  unsigned long scanBurstNextMs;
} meshData = {};

// ============================================================================
// Forward declarations — externs from other headers
// ============================================================================

extern uint8_t getBotExpression();
extern uint8_t getBotState();
extern uint8_t effectIndex;
extern uint8_t paletteIndex;
extern float accelX, accelY, accelZ;
extern volatile bool meshScanRequested;

// ============================================================================
// Packet Builder
// ============================================================================

static void meshBuildPacket(MeshStatePacket& pkt) {
  memset(&pkt, 0, sizeof(pkt));
  pkt.version   = MESH_PROTOCOL_VERSION;
  pkt.deviceId  = meshData.myDeviceId;
  pkt.expression = getBotExpression();
  pkt.botState  = getBotState();
  pkt.battery   = 0;  // USB-powered, no battery monitoring yet
  pkt.mode      = 0;  // MODE_BOT
  pkt.effect    = effectIndex;
  pkt.palette   = paletteIndex;

  // Orientation from accelerometer (pitch/roll in degrees × 100)
  if (sysStatus.imuReady) {
    float pitch = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 57.2958f;
    float roll  = atan2(accelY, accelZ) * 57.2958f;
    pkt.pitch = (int16_t)(pitch * 100);
    pkt.roll  = (int16_t)(roll * 100);
  }
  pkt.heading = 0xFFFF;  // No magnetometer

  // Core S3 sensors
  #ifdef TARGET_CORES3
  if (sysStatus.proxLightReady) {
    extern struct ProxLightState proxLight;
    pkt.proximity  = (uint8_t)min((uint16_t)255, proxLight.rawProximity);
    pkt.ambientLux = proxLight.ambientLux;
  }
  if (sysStatus.micReady) {
    extern struct AudioAnalysis audioAnalysis;
    pkt.audioLevel = (uint8_t)(constrain(audioAnalysis.smoothLevel * 255.0f, 0.0f, 255.0f));
  }
  #endif

  // WLED active flag in reserved[0] bit 0
  if (meshData.wledActive) {
    pkt.reserved[0] |= 0x01;
  }
}

// ============================================================================
// Peer Cache Management
// ============================================================================

// Find peer by MAC, returns index or -1
static int8_t meshFindPeer(const uint8_t* mac) {
  for (uint8_t i = 0; i < meshData.peerCount; i++) {
    if (memcmp(meshData.peers[i].mac, mac, 6) == 0) {
      return i;
    }
  }
  return -1;
}

// Update existing peer or add new one
static void meshUpsertPeer(const uint8_t* mac, const MeshStatePacket& pkt) {
  int8_t idx = meshFindPeer(mac);

  if (idx >= 0) {
    // Update existing
    meshData.peers[idx].lastPacket = pkt;
    meshData.peers[idx].lastSeenMs = millis();
    meshData.peers[idx].active = true;
    return;
  }

  // Add new peer
  if (meshData.peerCount < MESH_MAX_PEERS) {
    MeshPeer& p = meshData.peers[meshData.peerCount];
    memcpy(p.mac, mac, 6);
    p.deviceId   = pkt.deviceId;
    p.lastPacket = pkt;
    p.lastSeenMs = millis();
    p.active     = true;
    meshData.peerCount++;

    DBG("Mesh: new peer 0x");
    if (pkt.deviceId < 0x10) DBG("0");
    DBGLN(String(pkt.deviceId, HEX));
  }
}

// Remove stale peers not seen within MESH_STALE_TIMEOUT_MS
static void meshEvictStale() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < meshData.peerCount; ) {
    if (meshData.peers[i].active &&
        (now - meshData.peers[i].lastSeenMs) > MESH_STALE_TIMEOUT_MS) {
      DBG("Mesh: peer 0x");
      if (meshData.peers[i].deviceId < 0x10) DBG("0");
      DBG(String(meshData.peers[i].deviceId, HEX));
      DBGLN(" stale, evicted");

      // Shift remaining peers down
      for (uint8_t j = i; j < meshData.peerCount - 1; j++) {
        meshData.peers[j] = meshData.peers[j + 1];
      }
      meshData.peerCount--;
      // Don't increment i — check shifted element
    } else {
      i++;
    }
  }
}

// ============================================================================
// ESP-NOW Receive Callback
// ============================================================================
// Runs on Core 0 WiFi context. Must be fast — just memcpy and update cache.

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void meshOnReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = info->src_addr;
#else
static void meshOnReceive(const uint8_t* mac, const uint8_t* data, int len) {
#endif
  if (len != sizeof(MeshStatePacket)) return;

  const MeshStatePacket* pkt = (const MeshStatePacket*)data;

  // Ignore our own broadcasts (reflected by AP)
  if (pkt->deviceId == meshData.myDeviceId) return;

  // Version check
  if (pkt->version != MESH_PROTOCOL_VERSION) return;

  meshUpsertPeer(mac, *pkt);
}

// ============================================================================
// Broadcast
// ============================================================================

static bool meshBroadcast() {
  if (!meshData.initialized) return false;

  MeshStatePacket pkt;
  meshBuildPacket(pkt);

  static const uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_err_t result = esp_now_send(broadcastAddr, (const uint8_t*)&pkt, sizeof(pkt));

  return (result == ESP_OK);
}

// ============================================================================
// Init
// ============================================================================

void initMesh() {
  if (meshData.initialized) return;

  // Get our device ID from MAC
  uint8_t mac[6];
  WiFi.macAddress(mac);
  meshData.myDeviceId = mac[5];  // Last byte of MAC

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    DBGLN("Mesh: ESP-NOW init FAILED");
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(meshOnReceive);

  // Add broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memset(peerInfo.peer_addr, 0xFF, 6);
  peerInfo.channel = 0;  // Use current channel
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_AP;  // Send via AP interface

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    DBGLN("Mesh: failed to add broadcast peer");
    esp_now_deinit();
    return;
  }

  meshData.initialized = true;
  meshData.lastBroadcastMs = millis();

  DBG("Mesh: ESP-NOW ready, deviceId=0x");
  if (meshData.myDeviceId < 0x10) DBG("0");
  DBGLN(String(meshData.myDeviceId, HEX));
}

// ============================================================================
// Poll — called from wifiServerTask loop (Core 0)
// ============================================================================

void pollMeshBroadcast() {
  if (!meshData.initialized) return;

  unsigned long now = millis();

  // Handle scan burst (triggered by CMD_MESH_SCAN)
  if (meshScanRequested) {
    meshScanRequested = false;
    meshData.scanBurstRemaining = MESH_SCAN_BURST_COUNT;
    meshData.scanBurstNextMs = now;
    DBGLN("Mesh: scan burst started");
  }

  if (meshData.scanBurstRemaining > 0 && now >= meshData.scanBurstNextMs) {
    meshBroadcast();
    meshData.scanBurstRemaining--;
    meshData.scanBurstNextMs = now + MESH_SCAN_BURST_INTERVAL;
    meshData.lastBroadcastMs = now;
    DBG("Mesh: scan burst ");
    DBGLN(MESH_SCAN_BURST_COUNT - meshData.scanBurstRemaining);
    return;
  }

  // Normal periodic broadcast
  if (now - meshData.lastBroadcastMs >= MESH_BROADCAST_INTERVAL_MS) {
    meshBroadcast();
    meshData.lastBroadcastMs = now;

    // Evict stale peers on each normal broadcast cycle
    meshEvictStale();
  }
}

// ============================================================================
// WLED Coordination — cooperative announce-and-check
// ============================================================================

// Returns true if any peer has wledActive=1 in their last state packet
bool meshAnyPeerWledActive() {
  for (uint8_t i = 0; i < meshData.peerCount; i++) {
    if (meshData.peers[i].active &&
        (meshData.peers[i].lastPacket.reserved[0] & 0x01)) {
      return true;
    }
  }
  return false;
}

// Set our own wledActive flag and trigger immediate broadcast
void meshSetWledActive(bool active) {
  if (meshData.wledActive == active) return;
  meshData.wledActive = active;

  // Immediate broadcast so peers learn in ~1ms
  if (meshData.initialized) {
    meshBroadcast();
  }
}

// ============================================================================
// Accessors
// ============================================================================

uint8_t meshGetPeerCount() {
  return meshData.peerCount;
}

bool meshIsInitialized() {
  return meshData.initialized;
}

#endif // ESP_NOW_MESH_H
