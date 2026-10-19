"""Build the Vita LiveArea art from the source images in livearea/.

WHY THIS IS A SCRIPT AND NOT THREE CHECKED-IN PNGs
--------------------------------------------------
Because the format requirement is the whole point, and a checked-in PNG cannot
enforce it. Installation failed at 98% with error 0x8010113D on images that were
correct by every ordinary measure: right dimensions, standard PNG, 8 bits per
channel, non-interlaced.

    EVERY IMAGE UNDER sce_sys MUST BE 8-BIT *PALETTISED* PNG.

"8-bit" there means colour type 3, an indexed palette, what pngquant produces,
not 8 bits per channel. A 32-bit RGBA PNG is rejected by the promoter after
the package has already copied, which is why the failure lands at 98% and reads
like a corrupt transfer rather than a format error.

SOURCES
-------
    livearea/background.png   the key art
    livearea/logo.jpg         the shell logo

Run from the repo root:
    python tools/make_livearea.py
"""

import os
import struct
import sys

from PIL import Image

SRC_DIR = "livearea"
OUT_DIR = "assets/livearea"

# Sizes the Vita requires: a wrong one is rejected.
ICON = (128, 128)
BACKGROUND = (840, 500)
STARTUP = (280, 158)

# PNG colour type 3. Anything else fails to install.
PNG_COLOUR_TYPE_PALETTE = 3

# Dark, grainy source art quantises badly at low palette counts; 256 with
# Floyd-Steinberg holds the grade well enough that the grain survives.
PALETTE_COLOURS = 256


def cover_fit(image, size):
    """Scale to cover `size`, then centre-crop.

    Cover rather than fit: letterboxing key art inside a LiveArea panel looks
    like a mistake, and these two aspect ratios are close enough that the crop
    takes only a sliver.
    """
    target_w, target_h = size
    src_w, src_h = image.size
    scale = max(target_w / src_w, target_h / src_h)
    scaled = image.resize((max(1, round(src_w * scale)),
                           max(1, round(src_h * scale))), Image.LANCZOS)
    left = (scaled.width - target_w) // 2
    top = (scaled.height - target_h) // 2
    return scaled.crop((left, top, left + target_w, top + target_h))


def save_palettised(image, path, expected):
    """Write as indexed-palette PNG, then prove it by reading the header back.

    Verifying from the file rather than trusting PIL's mode: this is the exact
    property whose absence made a vpk fail to install, and a silent fallback
    would reproduce that failure with no other symptom.
    """
    flat = image.convert("RGB").convert(
        "P", palette=Image.Palette.ADAPTIVE, colors=PALETTE_COLOURS,
        dither=Image.Dither.FLOYDSTEINBERG)
    flat.save(path, optimize=True)

    with open(path, "rb") as handle:
        signature = handle.read(8)
        handle.read(8)  # IHDR length and type
        width, height, depth, colour, _, _, interlace = struct.unpack(
            ">IIBBBBB", handle.read(13))

    if signature != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"FATAL: {path} is not a PNG")
    if (width, height) != expected:
        raise SystemExit(
            f"FATAL: {path} is {width}x{height}, must be "
            f"{expected[0]}x{expected[1]}")
    if colour != PNG_COLOUR_TYPE_PALETTE:
        raise SystemExit(
            f"FATAL: {path} has PNG colour type {colour}, needs "
            f"{PNG_COLOUR_TYPE_PALETTE} (palette).\n"
            "  The Vita promoter rejects anything else and the install fails\n"
            "  at 98% with 0x8010113D.")
    if interlace != 0:
        raise SystemExit(f"FATAL: {path} is interlaced")

    print(f"  {os.path.basename(path):14s} {width}x{height}  depth={depth} "
          f"palette  {os.path.getsize(path)} bytes")


def load_source(name):
    path = os.path.join(SRC_DIR, name)
    if not os.path.exists(path):
        raise SystemExit(
            f"FATAL: {path} is missing.\n"
            f"  The LiveArea art is built from the images in {SRC_DIR}/.")
    return Image.open(path)


def main():
    if not os.path.isdir("assets"):
        raise SystemExit("FATAL: run from the repo root")
    os.makedirs(OUT_DIR, exist_ok=True)

    art = load_source("background.png")
    logo = load_source("logo.jpg")

    # The bubble: the logo, square, so it reads at 128px among other bubbles.
    save_palettised(logo.resize(ICON, Image.LANCZOS),
                    os.path.join(OUT_DIR, "ICON0.PNG"), ICON)

    # The panel behind the gate, and the image shown while the game loads.
    save_palettised(cover_fit(art, BACKGROUND),
                    os.path.join(OUT_DIR, "bg.png"), BACKGROUND)
    save_palettised(cover_fit(art, STARTUP),
                    os.path.join(OUT_DIR, "startup.png"), STARTUP)

    print(f"wrote 3 palettised images to {OUT_DIR} from {SRC_DIR}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
