#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>
#include <Update.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "config.h"
#include "system_status.h"

extern WebServer server;

// ============================================================================
// OTA State
// ============================================================================

struct OTAState {
  bool updateAvailable = false;
  bool checkInProgress = false;
  bool updateInProgress = false;
  char remoteVersion[16] = "";
  char downloadURL[256] = "";
  size_t downloadSize = 0;
  uint8_t progress = 0;       // 0-100
  char lastError[64] = "";
};

static OTAState otaState;

// ============================================================================
// DigiCert Global Root G2 — GitHub's CA
// ============================================================================
// api.github.com and github.com use DigiCert. Pinning this root CA avoids
// the esp_crt_bundle static init crash on generic ESP32-S3 boards (TARGET_LCD).
// Valid until 2038-01-15.

static const char digicert_root_g2_pem[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wzt
CO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQv
IOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMCcit6E7UDP3GUJoMnlMaqpnBRb/7GIjSMTOh5bN1aNQot6HlHUwAkGpXG2
OINB/P5rg8P7Y3SJ5sRj6SqV45J6ttTGS3YfE8VbKhSccMlWJb/qC9kp8Daa+JjG
zMu2S4DJ4Z1wG0yAVDxBmpJ/mSu4wSCdBACOiHmRIDRMqXnr2FCBIL0VHzzsDDFc
PRbVQHBCyJwCdpLvLTEFHrO4TI+WDvohUYGjlMjMFpJLlTG0fIx/RACphYnQ33cC
V5oB4sFieSLsMJpUe5he7LVz5MbOm3R/FrMRkPfQbfhmIBxSFMJsKzjLPaIs9Rls
Q0Q=
-----END CERTIFICATE-----
)PEM";

// ============================================================================
// Semver Comparison
// ============================================================================
// Returns: 1 if remote > local, 0 if equal, -1 if remote < local

static int compareSemver(const char* remote, const char* local) {
  int rMaj = 0, rMin = 0, rPatch = 0;
  int lMaj = 0, lMin = 0, lPatch = 0;
  // Skip leading 'v' if present
  if (remote[0] == 'v' || remote[0] == 'V') remote++;
  if (local[0] == 'v' || local[0] == 'V') local++;
  sscanf(remote, "%d.%d.%d", &rMaj, &rMin, &rPatch);
  sscanf(local, "%d.%d.%d", &lMaj, &lMin, &lPatch);
  if (rMaj != lMaj) return rMaj > lMaj ? 1 : -1;
  if (rMin != lMin) return rMin > lMin ? 1 : -1;
  if (rPatch != lPatch) return rPatch > lPatch ? 1 : -1;
  return 0;
}

// ============================================================================
// GitHub Release Check
// ============================================================================
// Queries GitHub Releases API for the latest release. Parses JSON to find
// a matching .bin asset for this board type. Runs on Core 0 wifi task.

static void checkForUpdate() {
  if (otaState.checkInProgress || otaState.updateInProgress) return;
  if (WiFi.status() != WL_CONNECTED) {
    strncpy(otaState.lastError, "No WiFi connection", sizeof(otaState.lastError));
    return;
  }

  otaState.checkInProgress = true;
  otaState.lastError[0] = '\0';
  DBGLN("OTA: Checking GitHub for updates...");

  char url[128];
  snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", OTA_GITHUB_REPO);

  esp_http_client_config_t config = {};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.buffer_size = 1024;
  config.buffer_size_tx = 512;
  config.cert_pem = digicert_root_g2_pem;
  config.timeout_ms = 10000;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    strncpy(otaState.lastError, "HTTP init failed", sizeof(otaState.lastError));
    otaState.checkInProgress = false;
    return;
  }

  esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");
  esp_http_client_set_header(client, "User-Agent", "vizBot-OTA/1.0");

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    DBG("OTA: open err=");
    DBGLN(esp_err_to_name(err));
    strncpy(otaState.lastError, "Connection failed", sizeof(otaState.lastError));
    esp_http_client_cleanup(client);
    otaState.checkInProgress = false;
    return;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int httpCode = esp_http_client_get_status_code(client);

  DBG("OTA: HTTP ");
  DBG(httpCode);
  DBG(" len=");
  DBGLN(content_length);

  if (httpCode != 200) {
    snprintf(otaState.lastError, sizeof(otaState.lastError), "GitHub API HTTP %d", httpCode);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    otaState.checkInProgress = false;
    return;
  }

  // Read response into String (GitHub release JSON is typically 2-8KB)
  // We use LittleFS temp file to avoid heap pressure during TLS
  String response;
  char buf[256];
  int read_len;

  if (sysStatus.littlefsReady) {
    File tmp = LittleFS.open("/ota_tmp.json", "w");
    if (tmp) {
      while ((read_len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        tmp.write((uint8_t*)buf, read_len);
      }
      tmp.close();
      esp_http_client_close(client);
      esp_http_client_cleanup(client);

      File tmpR = LittleFS.open("/ota_tmp.json", "r");
      if (tmpR) {
        response = tmpR.readString();
        tmpR.close();
      }
      LittleFS.remove("/ota_tmp.json");
    }
  } else {
    // Fallback: read directly (risky on low-heap boards)
    response.reserve(4096);
    while ((read_len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
      response.concat(buf, read_len);
      if (response.length() > 8192) break;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
  }

  if (response.length() == 0) {
    strncpy(otaState.lastError, "Empty response", sizeof(otaState.lastError));
    otaState.checkInProgress = false;
    return;
  }

  // Parse JSON
  JsonDocument doc;
  DeserializationError jsonErr = deserializeJson(doc, response);
  response = ""; // Free memory immediately

  if (jsonErr) {
    snprintf(otaState.lastError, sizeof(otaState.lastError), "JSON parse: %s", jsonErr.c_str());
    otaState.checkInProgress = false;
    return;
  }

  const char* tagName = doc["tag_name"];
  if (!tagName) {
    strncpy(otaState.lastError, "No tag_name in release", sizeof(otaState.lastError));
    otaState.checkInProgress = false;
    return;
  }

  DBG("OTA: Latest release: ");
  DBGLN(tagName);

  // Compare versions
  if (compareSemver(tagName, FIRMWARE_VERSION) <= 0) {
    DBGLN("OTA: Already up to date");
    otaState.updateAvailable = false;
    otaState.checkInProgress = false;
    return;
  }

  // Find matching asset for this board type
  JsonArray assets = doc["assets"];
  bool found = false;
  for (JsonObject asset : assets) {
    const char* name = asset["name"];
    if (!name) continue;
    // Look for board type in filename (e.g. "vizbot-m5cores3-2.1.0.bin")
    if (strstr(name, BOARD_TYPE) && strstr(name, ".bin")) {
      const char* dlUrl = asset["browser_download_url"];
      if (dlUrl) {
        strncpy(otaState.remoteVersion, tagName, sizeof(otaState.remoteVersion) - 1);
        strncpy(otaState.downloadURL, dlUrl, sizeof(otaState.downloadURL) - 1);
        otaState.downloadSize = asset["size"] | 0;
        otaState.updateAvailable = true;
        found = true;
        DBG("OTA: Found asset: ");
        DBG(name);
        DBG(" size=");
        DBGLN(otaState.downloadSize);
        break;
      }
    }
  }

  if (!found) {
    snprintf(otaState.lastError, sizeof(otaState.lastError),
             "No asset for %s in %s", BOARD_TYPE, tagName);
    otaState.updateAvailable = false;
  }

  otaState.checkInProgress = false;
}

// ============================================================================
// Auto-Download + Flash from GitHub
// ============================================================================
// Downloads the .bin asset and streams it directly to the Update library.
// Runs on Core 0 wifi task. The device reboots on success.

static bool performOTAFromURL() {
  if (otaState.updateInProgress) return false;
  if (!otaState.updateAvailable || otaState.downloadURL[0] == '\0') {
    strncpy(otaState.lastError, "No update URL available", sizeof(otaState.lastError));
    return false;
  }

  otaState.updateInProgress = true;
  otaState.progress = 0;
  otaState.lastError[0] = '\0';

  DBGLN("OTA: Starting download from GitHub...");
  DBG("OTA: URL: ");
  DBGLN(otaState.downloadURL);

  esp_http_client_config_t config = {};
  config.url = otaState.downloadURL;
  config.method = HTTP_METHOD_GET;
  config.buffer_size = 1024;
  config.buffer_size_tx = 512;
  config.cert_pem = digicert_root_g2_pem;
  config.timeout_ms = 30000;
  // GitHub redirects asset downloads — follow them
  config.max_redirection_count = 5;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    strncpy(otaState.lastError, "HTTP init failed", sizeof(otaState.lastError));
    otaState.updateInProgress = false;
    return false;
  }

  esp_http_client_set_header(client, "User-Agent", "vizBot-OTA/1.0");
  esp_http_client_set_header(client, "Accept", "application/octet-stream");

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    snprintf(otaState.lastError, sizeof(otaState.lastError), "Connect: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    otaState.updateInProgress = false;
    return false;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int httpCode = esp_http_client_get_status_code(client);

  if (httpCode != 200) {
    snprintf(otaState.lastError, sizeof(otaState.lastError), "HTTP %d", httpCode);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    otaState.updateInProgress = false;
    return false;
  }

  size_t updateSize = (content_length > 0) ? content_length : otaState.downloadSize;
  if (updateSize == 0) {
    strncpy(otaState.lastError, "Unknown firmware size", sizeof(otaState.lastError));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    otaState.updateInProgress = false;
    return false;
  }

  if (!Update.begin(updateSize, U_FLASH)) {
    snprintf(otaState.lastError, sizeof(otaState.lastError), "Update.begin: %s", Update.errorString());
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    otaState.updateInProgress = false;
    return false;
  }

  DBGLN("OTA: Downloading and flashing...");

  char buf[512];
  size_t written = 0;
  bool magicChecked = false;
  bool aborted = false;
  int read_len;

  while ((read_len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
    // Check ESP32 magic byte on first chunk
    if (!magicChecked) {
      if ((uint8_t)buf[0] != 0xE9) {
        strncpy(otaState.lastError, "Invalid firmware (bad magic byte)", sizeof(otaState.lastError));
        aborted = true;
        break;
      }
      magicChecked = true;
    }

    size_t w = Update.write((uint8_t*)buf, read_len);
    if (w != (size_t)read_len) {
      snprintf(otaState.lastError, sizeof(otaState.lastError), "Write failed at %u", written);
      aborted = true;
      break;
    }
    written += w;
    otaState.progress = (uint8_t)((written * 100) / updateSize);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (aborted) {
    Update.abort();
    otaState.updateInProgress = false;
    return false;
  }

  if (!Update.end(true)) {
    snprintf(otaState.lastError, sizeof(otaState.lastError), "Finalize: %s", Update.errorString());
    otaState.updateInProgress = false;
    return false;
  }

  DBGLN("OTA: Update successful! Rebooting...");
  otaState.progress = 100;
  return true;  // Caller should delay briefly then ESP.restart()
}

// ============================================================================
// Manual Upload Handler (fallback for AP-only)
// ============================================================================

static bool otaUploadError = false;
static bool otaUploadSuccess = false;

static void handleOTAUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaUploadError = false;
    otaUploadSuccess = false;
    otaState.updateInProgress = true;
    otaState.progress = 0;
    otaState.lastError[0] = '\0';

    DBG("OTA Upload: ");
    DBGLN(upload.filename.c_str());

    // Board type check — filename should contain BOARD_TYPE
    if (upload.filename.indexOf(BOARD_TYPE) < 0) {
      snprintf(otaState.lastError, sizeof(otaState.lastError),
               "Wrong board type (expected %s in filename)", BOARD_TYPE);
      DBGLN(otaState.lastError);
      otaUploadError = true;
      return;
    }

    size_t maxSize = (UPDATE_SIZE_UNKNOWN);
    if (!Update.begin(maxSize, U_FLASH)) {
      snprintf(otaState.lastError, sizeof(otaState.lastError),
               "Update.begin: %s", Update.errorString());
      otaUploadError = true;
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaUploadError) return;

    // Check magic byte on first chunk
    if (upload.totalSize == 0 && upload.currentSize > 0) {
      if (upload.buf[0] != 0xE9) {
        strncpy(otaState.lastError, "Invalid firmware (bad magic byte)", sizeof(otaState.lastError));
        otaUploadError = true;
        return;
      }
    }

    size_t w = Update.write(upload.buf, upload.currentSize);
    if (w != upload.currentSize) {
      snprintf(otaState.lastError, sizeof(otaState.lastError),
               "Write failed at %u", upload.totalSize);
      otaUploadError = true;
      return;
    }

    // Estimate progress (totalSize includes current chunk)
    if (upload.totalSize > 0) {
      // We don't know total file size from multipart, estimate from partition
      const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
      if (part) {
        otaState.progress = (uint8_t)min(99UL, (unsigned long)(upload.totalSize * 100) / part->size);
      }
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (otaUploadError) {
      Update.abort();
      otaState.updateInProgress = false;
      return;
    }

    if (!Update.end(true)) {
      snprintf(otaState.lastError, sizeof(otaState.lastError),
               "Finalize: %s", Update.errorString());
      otaUploadError = true;
      otaState.updateInProgress = false;
      return;
    }

    DBGLN("OTA Upload: Success!");
    otaState.progress = 100;
    otaUploadSuccess = true;

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    strncpy(otaState.lastError, "Upload aborted", sizeof(otaState.lastError));
    otaUploadError = true;
    otaState.updateInProgress = false;
  }
}

static void handleOTAResult() {
  if (otaUploadError) {
    String json = "{\"success\":false,\"error\":\"" + String(otaState.lastError) + "\"}";
    server.send(200, "application/json", json);
    otaState.updateInProgress = false;
  } else if (otaUploadSuccess) {
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Update successful. Rebooting...\"}");
    delay(1000);
    ESP.restart();
  } else {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Unknown error\"}");
    otaState.updateInProgress = false;
  }
}

// ============================================================================
// HTTP Endpoint Handlers
// ============================================================================

static void handleUpdateCheck() {
  if (otaState.checkInProgress) {
    server.send(200, "application/json", "{\"status\":\"checking\"}");
    return;
  }
  checkForUpdate();
  String json = "{\"updateAvailable\":" + String(otaState.updateAvailable ? "true" : "false");
  if (otaState.updateAvailable) {
    json += ",\"version\":\"" + String(otaState.remoteVersion) + "\"";
    json += ",\"size\":" + String(otaState.downloadSize);
  }
  if (otaState.lastError[0]) {
    json += ",\"error\":\"" + String(otaState.lastError) + "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

static void handleUpdateStart() {
  if (otaState.updateInProgress) {
    server.send(200, "application/json", "{\"status\":\"in_progress\",\"progress\":" + String(otaState.progress) + "}");
    return;
  }
  if (!otaState.updateAvailable) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"No update available\"}");
    return;
  }

  // Send response first, then start the update
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Update starting...\"}");

  // Give the HTTP response time to send
  delay(500);

  if (performOTAFromURL()) {
    delay(1000);
    ESP.restart();
  }
}

static void handleUpdateProgress() {
  String json = "{\"inProgress\":" + String(otaState.updateInProgress ? "true" : "false") +
                ",\"progress\":" + String(otaState.progress);
  if (otaState.lastError[0]) {
    json += ",\"error\":\"" + String(otaState.lastError) + "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

// ============================================================================
// Update Page HTML
// ============================================================================

static const char otaPageHTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <title>vizBot Update</title>
  <style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;background:#e8e4dc;color:#000;min-height:100vh}
.hdr{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:#FFD23F;border:3px solid #000;box-shadow:0 5px 0 0 #000}
.logo{font-size:20px;font-weight:800;color:#000;letter-spacing:-0.5px}
.card{background:#fffdf5;border:3px solid #000;box-shadow:5px 5px 0 0 #000;padding:20px;margin:16px;max-width:600px;margin-left:auto;margin-right:auto}
h2{font-size:13px;color:#000;text-transform:uppercase;letter-spacing:0.08em;margin-bottom:14px;font-weight:800}
.info-row{display:flex;justify-content:space-between;padding:6px 0;font-size:13px;border-bottom:1px solid #eee}
.info-label{color:#666;font-weight:600}
.info-val{font-weight:700}
.ver-new{color:#2d7d46;font-weight:800}
.section{margin-top:20px;padding-top:16px;border-top:2px solid #000}
.btn{display:inline-block;padding:10px 20px;background:#FFD23F;color:#000;border:3px solid #000;box-shadow:3px 3px 0 0 #000;font-weight:800;font-size:13px;text-transform:uppercase;letter-spacing:0.05em;cursor:pointer;text-decoration:none;transition:all 0.1s}
.btn:hover{transform:translate(1px,1px);box-shadow:2px 2px 0 0 #000}
.btn:active{transform:translate(3px,3px);box-shadow:0 0 0 0 #000}
.btn:disabled{background:#ccc;cursor:not-allowed;transform:none;box-shadow:3px 3px 0 0 #000}
.btn-danger{background:#FF6B6B}
.progress-wrap{display:none;margin-top:14px}
.progress-bar{height:24px;background:#eee;border:2px solid #000;position:relative;overflow:hidden}
.progress-fill{height:100%;background:#88D498;width:0%;transition:width 0.3s}
.progress-text{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);font-size:12px;font-weight:800}
.status{margin-top:12px;padding:10px;font-size:13px;font-weight:600;display:none}
.status.ok{display:block;background:#d4edda;border:2px solid #2d7d46;color:#2d7d46}
.status.err{display:block;background:#f8d7da;border:2px solid #c0392b;color:#c0392b}
.status.warn{display:block;background:#fff3cd;border:2px solid #856404;color:#856404}
.warn-text{font-size:12px;color:#856404;margin-top:8px;font-weight:600}
.file-input{margin:10px 0}
.file-input input[type=file]{font-size:13px}
.back{display:inline-block;margin:16px;font-size:13px;color:#333;font-weight:600;text-decoration:none}
.back:hover{text-decoration:underline}
  </style>
</head>
<body>
  <div class="hdr">
    <span class="logo">vizBot</span>
    <span style="font-size:12px;font-weight:700">FIRMWARE UPDATE</span>
  </div>

  <div class="card">
    <h2>Current Firmware</h2>
    <div class="info-row"><span class="info-label">Version</span><span class="info-val" id="curVer">---</span></div>
    <div class="info-row"><span class="info-label">Board</span><span class="info-val" id="curBoard">---</span></div>
    <div class="info-row"><span class="info-label">Device</span><span class="info-val" id="curDevice">---</span></div>

    <div class="section">
      <h2>Auto Update</h2>
      <p style="font-size:13px;margin-bottom:12px">Check GitHub for the latest firmware release.</p>
      <button class="btn" id="checkBtn" onclick="doCheck()">Check for Updates</button>
      <div id="updateInfo" style="display:none;margin-top:12px">
        <div class="info-row"><span class="info-label">Available</span><span class="info-val ver-new" id="newVer">---</span></div>
        <div class="info-row"><span class="info-label">Size</span><span class="info-val" id="newSize">---</span></div>
        <button class="btn btn-danger" id="updateBtn" onclick="doAutoUpdate()" style="margin-top:12px">Update Now</button>
        <div class="warn-text">Do not disconnect power or close this page during the update.</div>
      </div>
      <div class="progress-wrap" id="autoProgress">
        <div class="progress-bar"><div class="progress-fill" id="autoFill"></div><span class="progress-text" id="autoPct">0%</span></div>
      </div>
      <div class="status" id="autoStatus"></div>
    </div>

    <div class="section">
      <h2>Manual Upload</h2>
      <p style="font-size:13px;margin-bottom:8px">Upload a .bin firmware file directly. Use this if the device has no internet.</p>
      <div class="file-input"><input type="file" id="fw" accept=".bin"></div>
      <button class="btn" id="uploadBtn" onclick="doUpload()">Upload</button>
      <div class="progress-wrap" id="manualProgress">
        <div class="progress-bar"><div class="progress-fill" id="manualFill"></div><span class="progress-text" id="manualPct">0%</span></div>
      </div>
      <div class="status" id="manualStatus"></div>
    </div>
  </div>

  <a href="/" class="back">&larr; Back to Control Panel</a>

  <script>
  function show(id, cls, msg) {
    var el = document.getElementById(id);
    el.className = 'status ' + cls;
    el.textContent = msg;
  }

  async function doCheck() {
    var btn = document.getElementById('checkBtn');
    btn.textContent = 'Checking...';
    btn.disabled = true;
    document.getElementById('autoStatus').className = 'status';
    document.getElementById('updateInfo').style.display = 'none';
    try {
      var r = await fetch('/update/check');
      var d = await r.json();
      if (d.error) {
        show('autoStatus', 'err', 'Error: ' + d.error);
      } else if (d.updateAvailable) {
        document.getElementById('newVer').textContent = d.version;
        document.getElementById('newSize').textContent = (d.size / 1024).toFixed(0) + ' KB';
        document.getElementById('updateInfo').style.display = 'block';
      } else {
        show('autoStatus', 'ok', 'Firmware is up to date!');
      }
    } catch(e) {
      show('autoStatus', 'err', 'Connection error');
    }
    btn.textContent = 'Check for Updates';
    btn.disabled = false;
  }

  async function doAutoUpdate() {
    var btn = document.getElementById('updateBtn');
    btn.disabled = true;
    btn.textContent = 'Updating...';
    document.getElementById('autoProgress').style.display = 'block';
    try {
      var r = await fetch('/update/start');
      var d = await r.json();
      if (!d.success) {
        show('autoStatus', 'err', 'Error: ' + d.error);
        btn.disabled = false;
        btn.textContent = 'Update Now';
        return;
      }
      // Poll progress
      var poll = setInterval(async function() {
        try {
          var pr = await fetch('/update/progress');
          var pd = await pr.json();
          document.getElementById('autoFill').style.width = pd.progress + '%';
          document.getElementById('autoPct').textContent = pd.progress + '%';
          if (pd.error) {
            clearInterval(poll);
            show('autoStatus', 'err', 'Error: ' + pd.error);
            btn.disabled = false;
            btn.textContent = 'Update Now';
          }
        } catch(e) {
          // Connection lost = device is rebooting
          clearInterval(poll);
          document.getElementById('autoFill').style.width = '100%';
          document.getElementById('autoPct').textContent = '100%';
          show('autoStatus', 'ok', 'Update complete! Device is rebooting...');
          setTimeout(function() { location.href = '/'; }, 15000);
        }
      }, 1000);
    } catch(e) {
      show('autoStatus', 'err', 'Connection error');
      btn.disabled = false;
      btn.textContent = 'Update Now';
    }
  }

  function doUpload() {
    var file = document.getElementById('fw').files[0];
    if (!file) { show('manualStatus', 'err', 'Select a .bin file first'); return; }
    if (!file.name.endsWith('.bin')) { show('manualStatus', 'err', 'File must be a .bin firmware'); return; }

    var xhr = new XMLHttpRequest();
    var form = new FormData();
    form.append('firmware', file);

    xhr.upload.addEventListener('progress', function(e) {
      if (e.lengthComputable) {
        var pct = Math.round((e.loaded / e.total) * 100);
        document.getElementById('manualFill').style.width = pct + '%';
        document.getElementById('manualPct').textContent = pct + '%';
      }
    });

    xhr.onload = function() {
      try {
        var r = JSON.parse(xhr.responseText);
        if (r.success) {
          show('manualStatus', 'ok', 'Update successful! Rebooting...');
          setTimeout(function() { location.href = '/'; }, 15000);
        } else {
          show('manualStatus', 'err', 'Error: ' + r.error);
          document.getElementById('uploadBtn').disabled = false;
          document.getElementById('uploadBtn').textContent = 'Upload';
        }
      } catch(e) {
        show('manualStatus', 'ok', 'Update complete! Rebooting...');
        setTimeout(function() { location.href = '/'; }, 15000);
      }
    };

    xhr.onerror = function() {
      show('manualStatus', 'ok', 'Device is rebooting with new firmware...');
      setTimeout(function() { location.href = '/'; }, 15000);
    };

    xhr.open('POST', '/update');
    xhr.send(form);

    document.getElementById('uploadBtn').disabled = true;
    document.getElementById('uploadBtn').textContent = 'Uploading...';
    document.getElementById('manualProgress').style.display = 'block';
    document.getElementById('manualStatus').className = 'status';
  }

  // Load current state
  fetch('/state').then(r => r.json()).then(function(s) {
    document.getElementById('curVer').textContent = 'v' + (s.firmwareVersion || '?');
    document.getElementById('curBoard').textContent = s.boardType || '?';
    document.getElementById('curDevice').textContent = s.device || '?';
  }).catch(function(){});
  </script>
</body>
</html>
)rawliteral";

static void handleOTAPage() {
  server.send(200, "text/html", otaPageHTML);
}

// ============================================================================
// Boot Confirmation
// ============================================================================
// Call early in setup() to tell the bootloader this firmware is valid.
// Without this, the bootloader may roll back on the next reboot.

static void otaMarkBootValid() {
  esp_ota_mark_app_valid_cancel_rollback();
  DBGLN("OTA: Boot confirmed valid");
}

#endif // OTA_UPDATE_H
