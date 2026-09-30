"""Post-build script: create a single factory image for the web flasher.

`pio run` builds firmware.bin, which is only the application and belongs at
0x10000. A blank board also needs the bootloader, partition table and
boot_app0 image at their own offsets. This script merges all of them into
.pio/build/<env>/firmware-merged.bin, which is flashed at offset 0.

The offsets, flash mode, flash frequency and flash size are taken from the
same settings PlatformIO uses for `pio run -t upload`, so the merged image
matches a normal USB upload for every board environment.
"""
Import("env")

board = env.BoardConfig()
APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"
MERGED_BIN = "$BUILD_DIR/firmware-merged.bin"


def merge_bin(source, target, env):
    extra_images = []
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        extra_images += [offset, '"%s"' % env.subst(image)]

    cmd = [
        '"$PYTHONEXE"',
        '"$UPLOADER"',
        "--chip", board.get("build.mcu", "esp32"),
        "merge_bin",
        "-o", '"%s"' % MERGED_BIN,
        "--flash_mode", "${__get_board_flash_mode(__env__)}",
        "--flash_freq", "${__get_board_f_image(__env__)}",
        "--flash_size", board.get("upload.flash_size", "4MB"),
    ] + extra_images + ["$ESP32_APP_OFFSET", '"%s"' % APP_BIN]

    env.Execute(env.VerboseAction(" ".join(cmd), "Merging factory image %s" % MERGED_BIN))


env.AddPostAction(APP_BIN, merge_bin)
