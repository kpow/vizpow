"""Force the flash offset PlatformIO uploads an app to.

PlatformIO derives the application offset from the partition table and picks
**ota_0's offset whenever the table has an ota_0** (espressif32 builder/main.py:
`if partition["subtype"] == "ota_0": app_offset = next_offset`).

That is correct for exactly one of the three apps sharing partitions_kfun.csv —
the game firmware, which *is* ota_0. The chooser and vizBot need their own
offsets or they upload straight over the games, silently: the flash succeeds, the
wrong app answers, and the only symptom is confusing behaviour later. That is not
hypothetical — it is what this script was written after.

!! The Replace() must happen in a PRE-ACTION on the upload target, not at script
load time. The platform assigns ESP32_APP_OFFSET itself after extra_scripts are
processed (builder/main.py ~:864, from INTEGRATION_EXTRA_DATA), so a plain
top-level Replace() is overwritten and does nothing — while still printing a
reassuring message. Pre-actions run after that, immediately before the upload
command is expanded, so this one sticks.

Usage from platformio.ini:

    extra_scripts = post:tools/app_offset.py
    custom_app_offset = 0x10000

Import() is injected by SCons; the editor cannot see it.
"""

Import("env")  # noqa: F821

offset = env.GetProjectOption("custom_app_offset", None)  # noqa: F821


def _force_offset(source, target, env):  # noqa: ARG001
    env.Replace(ESP32_APP_OFFSET=str(offset))
    print("app_offset: uploading this env to %s" % offset)


if offset:
    env.AddPreAction("upload", _force_offset)  # noqa: F821
