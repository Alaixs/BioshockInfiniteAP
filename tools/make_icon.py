"""Generates mod/src/ap_icon.h: the Archipelago logo, BGRA pixels (PF_A8R8G8B8), square.

    python tools/make_icon.py [image] [--size 256] [--preview preview.png]

Default: assets/logo_archi.png (BioShock version), otherwise the icon of the Archipelago install.
A picture without transparency on a black background is cut out: only the black background
CONNECTED TO THE EDGES becomes transparent (black inside the logo is kept).
"""

import argparse
import pathlib

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
DEFAULT_SOURCES = [ROOT / "assets" / "logo_archi.png", pathlib.Path(r"C:\ProgramData\Archipelago\data\icon.png")]
OUT = ROOT / "mod" / "src" / "ap_icon.h"
DARK = 6  # background luminance: almost pure black (the logo has dark areas around 12)


def remove_black_background(image: Image.Image) -> Image.Image:
    """Alpha = 0 for the dark background connected to the edges, soft edges."""
    rgb = image.convert("RGB")
    dark = rgb.convert("L").point(lambda v: 255 if v < DARK else 0)
    # Flood fill from the four corners, through dark pixels only.
    for corner in ((0, 0), (dark.width - 1, 0), (0, dark.height - 1), (dark.width - 1, dark.height - 1)):
        if dark.getpixel(corner) == 255:
            ImageDraw.floodfill(dark, corner, 128)
    background = dark.point(lambda v: 255 if v == 128 else 0)
    # Soft edge: trim one pixel, then blur the mask slightly.
    background = background.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(1.5))
    alpha = ImageChops.invert(background)
    out = rgb.convert("RGBA")
    out.putalpha(alpha)
    return out


# "Parchment" background of the game's icons: cream in the middle, brown towards the edge of the
# round frame, stains and grain. Colors taken from a game icon (Possession, Veni! Vidi! Vigor!).
PARCHMENT_STOPS = [
    (0.0, (240, 221, 156)),
    (0.4, (230, 200, 132)),
    (0.65, (210, 170, 106)),
    (0.82, (170, 128, 78)),
    (1.0, (105, 76, 45)),
]


def parchment(size: int, seed: int = 7) -> Image.Image:
    import math
    import random

    rng = random.Random(seed)

    def noise(cells: int) -> Image.Image:
        small = Image.new("L", (cells, cells))
        small.putdata([rng.randint(0, 255) for _ in range(cells * cells)])
        return small.resize((size, size), Image.BICUBIC)

    # Fractal noise: large stains + details, centered on 0.
    layers = [(noise(4), 0.45), (noise(9), 0.3), (noise(23), 0.17), (noise(64), 0.08)]
    grain = [rng.gauss(0, 5) for _ in range(size * size)]
    data = [layer.getdata() for layer, _ in layers]
    out = []
    half = size / 2
    for y in range(size):
        for x in range(size):
            t = min(math.hypot(x + 0.5 - half, y + 0.5 - half) / (half * 0.98), 1.0)
            for (t0, c0), (t1, c1) in zip(PARCHMENT_STOPS, PARCHMENT_STOPS[1:]):
                if t <= t1:
                    k = (t - t0) / (t1 - t0)
                    base = [c0[i] + (c1[i] - c0[i]) * k for i in range(3)]
                    break
            i = y * size + x
            n = sum((d[i] - 128) / 128 * w for d, (_, w) in zip(data, layers))  # -1..1
            shade = 1 + 0.24 * n
            out.append(tuple(max(0, min(255, int(c * shade + grain[i]))) for c in base) + (255,))
    image = Image.new("RGBA", (size, size))
    image.putdata(out)
    return image


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", nargs="?")
    parser.add_argument("--size", type=int, default=256)
    parser.add_argument("--preview")
    parser.add_argument("--no-parchment", action="store_true", help="logo alone, without the game icons' background")
    parser.add_argument("--logo-scale", type=float, default=1.0, help="size of the logo on the background (0-1)")
    args = parser.parse_args()
    source = pathlib.Path(args.image) if args.image else next(p for p in DEFAULT_SOURCES if p.exists())
    size = args.size

    image = Image.open(source)
    if "A" not in image.getbands():
        image = remove_black_background(image)
    image = image.convert("RGBA")
    box = image.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    image = image.crop(box)
    # Centered square with a small margin, then scaled down.
    side = int(max(image.size) * 1.04)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(image, ((side - image.width) // 2, (side - image.height) // 2))
    if args.no_parchment:
        square = square.resize((size, size), Image.LANCZOS)
    else:
        logo_size = int(size * args.logo_scale)
        logo = square.resize((logo_size, logo_size), Image.LANCZOS)
        square = parchment(size)
        offset = ((size - logo_size) // 2, (size - logo_size) // 2)
        # Soft shadow under the logo, like the relief of the game's drawings.
        shadow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        shadow_alpha = Image.new("L", (size, size), 0)
        shadow_alpha.paste(logo.getchannel("A").point(lambda v: v * 0.55), (offset[0] + 2, offset[1] + 3))
        shadow.putalpha(shadow_alpha.filter(ImageFilter.GaussianBlur(3)))
        square.alpha_composite(shadow)
        square.alpha_composite(logo, offset)
    if args.preview:
        square.save(args.preview)

    bgra = bytearray()
    for r, g, b, a in square.getdata():
        bgra += bytes((b, g, r, a))
    # Named from the repo root, whatever the current folder: the header stays the same.
    shown = source.resolve()
    shown = shown.relative_to(ROOT).as_posix() if shown.is_relative_to(ROOT) else source.name
    lines = [
        "#pragma once",
        "",
        f"// Archipelago logo {size}x{size}, BGRA (PF_A8R8G8B8). Generated by tools/make_icon.py from",
        f"// {shown}. Do not edit by hand.",
        "",
        "#include <cstdint>",
        "",
        "namespace bsap::icon {",
        "",
        f"constexpr int kApIconSize = {size};",
        f"constexpr std::uint8_t kApIconBgra[{len(bgra)}] = {{",
    ]
    for i in range(0, len(bgra), 24):
        lines.append("    " + ", ".join(f"0x{v:02X}" for v in bgra[i : i + 24]) + ",")
    lines += ["};", "", "}  // namespace bsap::icon", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"{source} {Image.open(source).size} -> {OUT} ({size}x{size}, {len(bgra)} bytes)")


if __name__ == "__main__":
    main()
