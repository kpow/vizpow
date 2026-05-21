Import("env")
import re, shutil, os, glob, subprocess, sys

def post_build(source, target, env):
    with open("config.h") as f:
        m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', f.read())
        version = m.group(1) if m else "unknown"

    pio_env = env["PIOENV"]
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    builds_dir = os.path.join(os.path.dirname(project_dir), "builds")
    os.makedirs(builds_dir, exist_ok=True)

    app_src = os.path.join(build_dir, "firmware.bin")
    if not os.path.isfile(app_src):
        return

    app_name = f"vizbot-{pio_env}-v{version}.bin"
    shutil.copy2(app_src, os.path.join(build_dir, app_name))
    shutil.copy2(app_src, os.path.join(builds_dir, app_name))
    print(f"  -> Published to builds/{app_name}")

    # Merged factory image: bootloader (0x0) + partitions (0x8000) + app (0x10000).
    # Flash-size comes from the env's board config so the bootloader header byte 0x3
    # always matches the target hardware — preventing the broken-4MB-on-16MB-Core-S3
    # bug that bricked a device.
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    if os.path.isfile(bootloader) and os.path.isfile(partitions):
        board = env.BoardConfig()
        flash_size = board.get("upload.flash_size", "4MB")
        flash_mode = board.get("build.flash_mode", "dio")
        mcu = board.get("build.mcu", "esp32s3")
        factory_name = f"vizbot-{pio_env}-v{version}-factory.bin"
        factory_dst = os.path.join(builds_dir, factory_name)
        cmd = [
            sys.executable, "-m", "esptool",
            "--chip", mcu,
            "merge-bin", "-o", factory_dst,
            "--flash-mode", flash_mode,
            "--flash-size", flash_size,
            "0x0",     bootloader,
            "0x8000",  partitions,
            "0x10000", app_src,
        ]
        try:
            subprocess.check_call(cmd, stdout=subprocess.DEVNULL)
            print(f"  -> Published to builds/{factory_name} (flash_size={flash_size})")
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"  -> WARNING: factory.bin merge skipped: {e}")

    # Prune separately per variant so an app-only build doesn't evict a factory image (or vice versa).
    for pattern, is_factory in (
        (f"vizbot-{pio_env}-v*-factory.bin", True),
        (f"vizbot-{pio_env}-v*.bin",         False),
    ):
        files = sorted(glob.glob(os.path.join(builds_dir, pattern)), key=os.path.getmtime, reverse=True)
        if not is_factory:
            files = [f for f in files if not f.endswith("-factory.bin")]
        for old in files[3:]:
            os.remove(old)
            print(f"  -> Pruned old build: {os.path.basename(old)}")

env.AddPostAction("$BUILD_DIR/firmware.bin", post_build)
