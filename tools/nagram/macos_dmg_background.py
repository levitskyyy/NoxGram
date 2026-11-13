#!/usr/bin/env python3
"""Draw the background of the NoxGram disk image window.

Writes background.png and background@2x.png next to --out and joins them
into the --out TIFF, which Finder shows sharp on Retina screens. The result
is committed; run this again only to change the design. Needs Pillow and
macOS (tiffutil, the SF Pro font).
"""

import argparse
import math
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Must match WINDOW and the icon places in macos_dmg.py.
WIDTH, HEIGHT = 640, 400
APPLICATION_AT = (170, 180)
APPLICATIONS_AT = (470, 180)

TOP = (246, 248, 253)
BOTTOM = (226, 232, 245)
ACCENT = (52, 92, 196)
CAPTION = (84, 96, 124)
FONT = "/System/Library/Fonts/SFNS.ttf"


def draw(scale):
    width, height = WIDTH * scale, HEIGHT * scale
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    for y in range(height):
        t = y / (height - 1)
        row = tuple(round(a + (b - a) * t) for a, b in zip(TOP, BOTTOM))
        for x in range(width):
            pixels[x, y] = row

    # A light arc from the application to the Applications folder.
    layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    canvas = ImageDraw.Draw(layer)
    x0, x1 = (APPLICATION_AT[0] + 82) * scale, (APPLICATIONS_AT[0] - 82) * scale
    y = APPLICATION_AT[1] * scale
    lift = 16 * scale
    points = []
    steps = 64
    for i in range(steps + 1):
        t = i / steps
        points.append((x0 + (x1 - x0) * t, y - lift * math.sin(math.pi * t)))
    stroke = round(3 * scale)
    for i in range(steps - 3):
        alpha = round(90 + 165 * i / steps)
        canvas.line([points[i], points[i + 1]], fill=ACCENT + (alpha,), width=stroke)
    # The arrow head follows the end of the arc.
    (ax, ay), (bx, by) = points[-4], points[-1]
    angle = math.atan2(by - ay, bx - ax)
    size = 13 * scale
    head = [
        (bx, by),
        (bx - size * math.cos(angle - 0.5), by - size * math.sin(angle - 0.5)),
        (bx - size * 0.62 * math.cos(angle), by - size * 0.62 * math.sin(angle)),
        (bx - size * math.cos(angle + 0.5), by - size * math.sin(angle + 0.5)),
    ]
    canvas.polygon(head, fill=ACCENT + (255,))
    image = Image.alpha_composite(image.convert("RGBA"), layer)

    text = ImageDraw.Draw(image)
    font = ImageFont.truetype(FONT, 13 * scale)
    caption = "Drag NoxGram to Applications to install it"
    box = text.textbbox((0, 0), caption, font=font)
    text.text(
        ((width - (box[2] - box[0])) / 2, 318 * scale),
        caption,
        font=font,
        fill=CAPTION)
    return image.convert("RGB")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True, help="the TIFF to write")
    out = Path(parser.parse_args().out)
    out.parent.mkdir(parents=True, exist_ok=True)
    single = out.with_name("background.png")
    double = out.with_name("background@2x.png")
    draw(1).save(single, dpi=(72, 72))
    draw(2).save(double, dpi=(144, 144))
    subprocess.run(
        ["tiffutil", "-cathidpicheck", str(single), str(double), "-out", str(out)],
        check=True)


if __name__ == "__main__":
    main()
