Import("env")
import subprocess, shutil, os

def post_build(source, target, env):
    try:
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--always"], stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        version = "dev"

    src = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
    dst = os.path.join(env.subst("$BUILD_DIR"), f"vizpow-{env['PIOENV']}-{version}.bin")
    if os.path.isfile(src):
        shutil.copy2(src, dst)
        print(f"  -> Copied to {os.path.basename(dst)}")

env.AddPostAction("$BUILD_DIR/firmware.bin", post_build)
