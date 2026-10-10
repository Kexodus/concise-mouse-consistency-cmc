"""Cut the minimal CMC logo into transparent emblem, wordmark, and full-logo PNGs."""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "assets" / "cmc-nexus-logo-v2-minimal.png"
OUT = ROOT / "presskit" / "logo"
BG = (11, 11, 13)


def to_transparent(im: Image.Image) -> Image.Image:
    im = im.convert("RGB")
    out = Image.new("RGBA", im.size)
    src, dst = im.load(), out.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            c = src[x, y]
            a = min(1.0, max(abs(c[i] - BG[i]) for i in range(3)) / 200.0)
            if a <= 0.02:
                dst[x, y] = (0, 0, 0, 0)
                continue
            rgb = tuple(max(0, min(255, round((c[i] - BG[i] * (1 - a)) / a))) for i in range(3))
            dst[x, y] = (*rgb, round(a * 255))
    return out


def trim(im: Image.Image, pad: int) -> Image.Image:
    box = im.getbbox()
    im = im.crop(box)
    canvas = Image.new("RGBA", (im.width + pad * 2, im.height + pad * 2))
    canvas.paste(im, (pad, pad))
    return canvas


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    logo = Image.open(SRC)
    full = to_transparent(logo)
    trim(full, 24).save(OUT / "cmc-logo-transparent.png", optimize=True)
    trim(full.crop((0, 140, logo.width, 872)), 16).save(OUT / "cmc-emblem-transparent.png", optimize=True)
    trim(full.crop((0, 874, logo.width, logo.height)), 12).save(OUT / "cmc-wordmark-transparent.png", optimize=True)
    logo.convert("RGB").save(OUT / "cmc-logo-1254.png", optimize=True)
    logo.convert("RGB").resize((512, 512), Image.LANCZOS).save(OUT / "cmc-logo-512.png", optimize=True)
    logo.convert("RGB").resize((256, 256), Image.LANCZOS).save(OUT / "cmc-logo-256.png", optimize=True)


if __name__ == "__main__":
    main()
