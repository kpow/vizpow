Import("env")
import subprocess, shutil, os, glob

def post_build(source, target, env):
    try:
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--always"], stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        version = "dev"

    src = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
    name = f"vizpow-{env['PIOENV']}-{version}.bin"
    dst = os.path.join(env.subst("$BUILD_DIR"), name)
    if os.path.isfile(src):
        shutil.copy2(src, dst)
        print(f"  -> Copied to {os.path.basename(dst)}")

        # Copy to top-level builds/ directory
        project_dir = env.subst("$PROJECT_DIR")
        builds_dir = os.path.join(os.path.dirname(project_dir), "builds")
        os.makedirs(builds_dir, exist_ok=True)
        shutil.copy2(src, os.path.join(builds_dir, name))
        print(f"  -> Published to builds/{name}")

        # Prune: keep only the 3 most recent builds per target
        pattern = os.path.join(builds_dir, f"vizpow-{env['PIOENV']}-*.bin")
        existing = sorted(glob.glob(pattern), key=os.path.getmtime, reverse=True)
        for old in existing[3:]:
            os.remove(old)
            print(f"  -> Pruned old build: {os.path.basename(old)}")

env.AddPostAction("$BUILD_DIR/firmware.bin", post_build)
