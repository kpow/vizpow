Import("env")
import re, shutil, os

def post_build(source, target, env):
    # Read FIRMWARE_VERSION from config.h
    with open("config.h") as f:
        m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', f.read())
        version = m.group(1) if m else "unknown"

    src = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
    dst = os.path.join(env.subst("$BUILD_DIR"), f"vizbot-{env['PIOENV']}-v{version}.bin")
    if os.path.isfile(src):
        shutil.copy2(src, dst)
        print(f"  -> Copied to {os.path.basename(dst)}")

env.AddPostAction("$BUILD_DIR/firmware.bin", post_build)
