Import("env")
import re, shutil, os, glob

# Post-build: publish the app image into the repo-root builds/ folder, following
# the same convention as the main vizbot firmware:
#   builds/vizmini-<board>-v<version>.bin   (upload this via OTA, or USB-flash it)
# Keeps the 3 newest.

def post_build(source, target, env):
    with open("config.h") as f:
        m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', f.read())
        version = m.group(1) if m else "unknown"

    board_type = {"c3-oled": "c3"}.get(env["PIOENV"], env["PIOENV"])
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    builds_dir = os.path.join(os.path.dirname(project_dir), "builds")
    os.makedirs(builds_dir, exist_ok=True)

    app_src = os.path.join(build_dir, "firmware.bin")
    if not os.path.isfile(app_src):
        return

    app_name = f"vizmini-{board_type}-v{version}.bin"
    shutil.copy2(app_src, os.path.join(builds_dir, app_name))
    print(f"  -> Published to builds/{app_name}")

    # Prune: keep the 3 newest.
    files = sorted(glob.glob(os.path.join(builds_dir, f"vizmini-{board_type}-v*.bin")),
                   key=os.path.getmtime, reverse=True)
    for old in files[3:]:
        os.remove(old)
        print(f"  -> Pruned old build: {os.path.basename(old)}")

env.AddPostAction("$BUILD_DIR/firmware.bin", post_build)
