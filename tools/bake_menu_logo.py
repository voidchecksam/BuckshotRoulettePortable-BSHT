"""The port's menu title: art/menu-title/port_menu_title.png -> assets/baked/menu_logo.rgba.

    python tools/bake_menu_logo.py

app::PortBranding draws the title 1:1 with GL_NEAREST where the original 3D
title card's text sat (276 design pixels wide), so it is rasterized here to
exactly that size: the device then samples one texel per screen pixel and never
resamples a 3840-pixel source every frame. The source is white letters on
black, so their brightness is the alpha. Cropped to the ink first, resized with
alpha premultiplied (no dark or bright fringe on the outline), written as the
raw image assets::ReadRawRgba reads: u32 width, u32 height, then RGBA8.
"""
import os
import struct
import sys

from PIL import Image

SOURCE = "art/menu-title/port_menu_title.png"
OUT = "assets/baked/menu_logo.rgba"
# The original title card's text width (src/app/port_branding.h kLogoTop..).
WIDTH = 276


def main():
    # White on black: the brightness is how much of each pixel is letter.
    alpha = Image.open(SOURCE).convert("L")
    img = Image.new("RGBA", alpha.size, (255, 255, 255, 0))
    img.putalpha(alpha)
    ink = alpha.getbbox()
    if ink is None:
        print("FATAL: %s has no opaque pixels" % SOURCE)
        return 1
    img = img.crop(ink)
    height = round(img.height * WIDTH / img.width)
    img = img.convert("RGBa").resize((WIDTH, height), Image.LANCZOS).convert("RGBA")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)   # a fresh clone has no assets/baked
    with open(OUT, "wb") as f:
        f.write(struct.pack("<II", img.width, img.height))
        f.write(img.tobytes())
    print("wrote %s %dx%d (%d KB)" % (OUT, img.width, img.height, img.width * img.height * 4 // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
