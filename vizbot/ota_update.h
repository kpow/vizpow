#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>
#include <Update.h>
#include <WebServer.h>
#include "esp_ota_ops.h"
#include "config.h"
#include "system_status.h"

extern WebServer server;

// ============================================================================
// Manual Upload Handler
// ============================================================================

static bool otaUploadError = false;
static bool otaUploadSuccess = false;

static void handleOTAUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaUploadError = false;
    otaUploadSuccess = false;

    DBG("OTA Upload: ");
    DBGLN(upload.filename.c_str());

    // Board type check — filename should contain BOARD_TYPE
    if (upload.filename.indexOf(BOARD_TYPE) < 0) {
      char err[64];
      snprintf(err, sizeof(err), "Wrong board type (expected %s in filename)", BOARD_TYPE);
      DBGLN(err);
      otaUploadError = true;
      return;
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      DBGLN("OTA: Update.begin failed");
      otaUploadError = true;
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaUploadError) return;

    // Check magic byte on first chunk
    if (upload.totalSize == 0 && upload.currentSize > 0) {
      if (upload.buf[0] != 0xE9) {
        DBGLN("OTA: Invalid firmware (bad magic byte)");
        otaUploadError = true;
        return;
      }
    }

    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      DBGLN("OTA: Write failed");
      otaUploadError = true;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (otaUploadError) {
      Update.abort();
      return;
    }

    if (!Update.end(true)) {
      DBGLN("OTA: Finalize failed");
      otaUploadError = true;
      return;
    }

    DBGLN("OTA Upload: Success!");
    otaUploadSuccess = true;

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaUploadError = true;
  }
}

static void handleOTAResult() {
  if (otaUploadError) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Upload failed — check board type and file\"}");
  } else if (otaUploadSuccess) {
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Update successful. Rebooting...\"}");
    delay(1000);
    ESP.restart();
  } else {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Unknown error\"}");
  }
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
.section{margin-top:20px;padding-top:16px;border-top:2px solid #000}
.btn{display:inline-block;padding:10px 20px;background:#FFD23F;color:#000;border:3px solid #000;box-shadow:3px 3px 0 0 #000;font-weight:800;font-size:13px;text-transform:uppercase;letter-spacing:0.05em;cursor:pointer;transition:all 0.1s}
.btn:hover{transform:translate(1px,1px);box-shadow:2px 2px 0 0 #000}
.btn:active{transform:translate(3px,3px);box-shadow:0 0 0 0 #000}
.btn:disabled{background:#ccc;cursor:not-allowed;transform:none;box-shadow:3px 3px 0 0 #000}
.progress-wrap{display:none;margin-top:14px}
.progress-bar{height:24px;background:#eee;border:2px solid #000;position:relative;overflow:hidden}
.progress-fill{height:100%;background:#88D498;width:0%;transition:width 0.3s}
.progress-text{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);font-size:12px;font-weight:800}
.status{margin-top:12px;padding:10px;font-size:13px;font-weight:600;display:none}
.status.ok{display:block;background:#d4edda;border:2px solid #2d7d46;color:#2d7d46}
.status.err{display:block;background:#f8d7da;border:2px solid #c0392b;color:#c0392b}
.file-input{margin:10px 0}
.file-input input[type=file]{font-size:13px}
.back{display:inline-block;margin:16px;font-size:13px;color:#333;font-weight:600;text-decoration:none}
.back:hover{text-decoration:underline}
.hint{font-size:12px;color:#666;margin-top:8px}
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
      <h2>Upload Firmware</h2>
      <p style="font-size:13px;margin-bottom:8px">Upload a .bin firmware file. The filename must include the board type.</p>
      <div class="file-input"><input type="file" id="fw" accept=".bin"></div>
      <button class="btn" id="uploadBtn" onclick="doUpload()">Upload</button>
      <p class="hint">Expected filename: vizbot-<strong id="boardHint">...</strong>-x.x.x.bin</p>
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

  fetch('/state').then(r => r.json()).then(function(s) {
    document.getElementById('curVer').textContent = 'v' + (s.firmwareVersion || '?');
    document.getElementById('curBoard').textContent = s.boardType || '?';
    document.getElementById('curDevice').textContent = s.device || '?';
    document.getElementById('boardHint').textContent = s.boardType || '...';
  }).catch(function(){});
  </script>
</body>
</html>
)rawliteral";

static void handleOTAPage() {
  server.send_P(200, "text/html", otaPageHTML);
}

// ============================================================================
// Boot Confirmation
// ============================================================================

static void otaMarkBootValid() {
  esp_ota_mark_app_valid_cancel_rollback();
  DBGLN("OTA: Boot confirmed valid");
}

#endif // OTA_UPDATE_H
