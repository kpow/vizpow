#!/usr/bin/env python3
"""Record a bot's USB serial log with wall-clock timestamps, for a soak.

Opening the port resets a CoreS3 (USB-CDC), so start this right at boot and
leave it running. Reconnects by itself if the port goes away (reboot, replug).

  ~/.platformio/penv/bin/python tools/soak/serial_log.py /dev/cu.usbmodem3101 tools/soak/runs/serial-mac.log
"""
import sys
import time

import serial

port, out = sys.argv[1], sys.argv[2]
with open(out, "a", buffering=1) as f:
    f.write(f"\n=== {time.strftime('%Y-%m-%d %H:%M:%S')} logging {port} ===\n")
    while True:
        try:
            with serial.Serial(port, 115200, timeout=1) as s:
                f.write(f"=== {time.strftime('%H:%M:%S')} port open ===\n")
                buf = b""
                while True:
                    buf += s.read(4096)
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        text = line.decode("utf-8", "replace").rstrip("\r")
                        stamp = time.strftime("%H:%M:%S")
                        f.write(f"{stamp} {text}\n")
                        if "WiFi" in text or "STA" in text or "abort" in text or "Guru" in text or "rst:" in text:
                            print(f"{stamp} {text}", flush=True)
        except (serial.SerialException, OSError) as e:
            f.write(f"=== {time.strftime('%H:%M:%S')} port lost: {e} ===\n")
            time.sleep(2)
