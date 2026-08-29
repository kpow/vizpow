#!/usr/bin/env python3
"""Are the stackchan servos actually alive?

Commands a move, then reads the angle back. BSP derives that angle from a real
ReadPos on the servo bus, so if the number MOVES the bus and the servo are both
answering. A frozen number, or one pinned at the clamp limit (-1280 yaw / 0
pitch, which is what a failed ReadPos of -1 maps to), means the bus is dead.
"""
import json, sys, time, urllib.request
IP = sys.argv[1] if len(sys.argv) > 1 else "10.0.0.93"

def get(p):
    try:
        return json.load(urllib.request.urlopen(f"http://{IP}{p}", timeout=8))
    except Exception as e:
        return {"_err": str(e)[:60]}

def angles():
    st = get("/state")
    if "_err" in st: return None, None, st["_err"]
    sc = st.get("stackchan", {})
    return sc.get("servoXAngle"), sc.get("servoYAngle"), None

x0, y0, err = angles()
if err:
    print(f"DEVICE UNREACHABLE: {err}")
    print("-> not a servo problem; the board needs a cold boot.")
    sys.exit(2)
print(f"before : yaw={x0} pitch={y0}")

for tgt in (600, -600):
    get(f"/bot/head/set_angles?yaw={tgt}&time=600")
    time.sleep(2.0)
    x, y, err = angles()
    print(f"cmd {tgt:>5}: yaw={x} pitch={y}")
    if x is not None and x0 is not None and x != x0:
        print("\nSERVOS LIVE - the bus is answering and the head is tracking.")
        get("/bot/head/recenter")
        sys.exit(0)

print("\nSERVOS NOT RESPONDING - angle never changed.")
print("-> hit http://%s/bot/servo/reinit ; if that doesn't help, cold boot." % IP)
sys.exit(1)
