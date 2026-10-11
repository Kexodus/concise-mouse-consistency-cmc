"""Render the CMC Nexus/press-kit art with headless Microsoft Edge.

Run from the repo root after `presskit/src/cutout.py`:

    python presskit/src/build.py

Every image uses the minimal logo palette: near-black, off-white, one blue accent.
"""
from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOGO = ROOT / "presskit" / "logo"
OUT = ROOT / "presskit" / "nexus"
EDGE = Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe")

BG = "#0b0b0d"
FG = "#f2eeea"
ACCENT = "#89bedd"

BASE_CSS = f"""
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
html, body {{ width: 100%; height: 100%; overflow: hidden; background: {BG}; color: {FG};
  font-family: 'Bahnschrift', 'Segoe UI', sans-serif; font-weight: 400; }}
.caps {{ text-transform: uppercase; letter-spacing: 0.3em; }}
.semi {{ font-weight: 600; }}
.muted {{ color: rgba(242, 238, 234, 0.55); }}
.dim {{ color: rgba(242, 238, 234, 0.32); }}
.blue {{ color: {ACCENT}; }}
.rule {{ height: 2px; background: rgba(242, 238, 234, 0.16); }}
.dot {{ width: 14px; height: 14px; border-radius: 50%; background: {ACCENT}; flex: none; }}
"""


def img(name: str) -> str:
    return (LOGO / name).as_uri()


# Reticle mark redrawn from the logo: off-white ring, four-point star, blue centre dot.
def mark(size: int) -> str:
    arm = "M50 2 L55 40 L50 46 L45 40 Z"
    arms = "".join(f'<path d="{arm}" transform="rotate({r} 50 50)" fill="{FG}"/>' for r in (0, 90, 180, 270))
    return (
        f'<svg width="{size}" height="{size}" viewBox="0 0 100 100">'
        f'<circle cx="50" cy="50" r="30" fill="none" stroke="{FG}" stroke-width="6"/>'
        f"{arms}"
        f'<circle cx="50" cy="50" r="8" fill="{ACCENT}"/></svg>'
    )


ICON_STROKE = f'fill="none" stroke="{FG}" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"'
ICONS = {
    "sprint": (
        f'<path d="M22 26 L44 50 L22 74" {ICON_STROKE}/>'
        f'<path d="M46 26 L68 50 L46 74" {ICON_STROKE}/>'
        f'<circle cx="82" cy="50" r="7" fill="{ACCENT}"/>'
    ),
    "bow": (
        f'<path d="M34 12 Q78 50 34 88" {ICON_STROKE}/>'
        f'<path d="M34 12 L34 88" fill="none" stroke="{FG}" stroke-width="2.5" stroke-linecap="round"/>'
        f'<path d="M14 50 L80 50" {ICON_STROKE}/>'
        f'<circle cx="86" cy="50" r="7" fill="{ACCENT}"/>'
    ),
    "time": (
        f'<path d="M28 14 L72 14 M28 86 L72 86 M32 14 Q32 40 50 50 Q68 40 68 14 M32 86 Q32 60 50 50 Q68 60 68 86" {ICON_STROKE}/>'
        f'<circle cx="50" cy="74" r="7" fill="{ACCENT}"/>'
    ),
    "sliders": (
        f'<path d="M14 26 L86 26 M14 50 L86 50 M14 74 L86 74" {ICON_STROKE}/>'
        f'<circle cx="34" cy="26" r="8" fill="{BG}" stroke="{FG}" stroke-width="5"/>'
        f'<circle cx="66" cy="50" r="8" fill="{ACCENT}"/>'
        f'<circle cx="46" cy="74" r="8" fill="{BG}" stroke="{FG}" stroke-width="5"/>'
    ),
}


def icon(name: str, size: int) -> str:
    return f'<svg width="{size}" height="{size}" viewBox="0 0 100 100">{ICONS[name]}</svg>'


def page(body: str, css: str = "") -> str:
    return f"<!doctype html><html><head><meta charset='utf-8'><style>{BASE_CSS}{css}</style></head><body>{body}</body></html>"


# --- compositions -----------------------------------------------------------


def primary() -> str:
    css = """
    .wrap { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; gap: 120px; }
    .logo { height: 760px; }
    .text { width: 760px; }
    .name { font-size: 64px; line-height: 1.12; letter-spacing: 0.2em; }
    """
    body = f"""
    <div class="wrap">
      <img class="logo" src="{img('cmc-logo-transparent.png')}">
      <div class="text">
        <div class="name caps semi">Concise<br>Mouse<br>Consistency</div>
      </div>
    </div>"""
    return page(body, css)


def header() -> str:
    css = """
    .wrap { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; }
    .lock { display: flex; align-items: center; gap: 56px; }
    .emblem { height: 300px; }
    .word { height: 132px; display: block; }
    .name { font-size: 30px; margin-top: 28px; }
    """
    body = f"""
    <div class="wrap"><div class="lock">
      <img class="emblem" src="{img('cmc-emblem-transparent.png')}">
      <div><img class="word" src="{img('cmc-wordmark-transparent.png')}">
      <div class="name caps muted">Concise Mouse Consistency</div></div>
    </div></div>"""
    return page(body, css)


def section(title: str) -> str:
    css = """
    .wrap { position: absolute; inset: 0; display: flex; align-items: center; gap: 28px; padding: 0 8px; }
    .t { font-size: 40px; white-space: nowrap; }
    """
    body = f"""<div class="wrap">{mark(64)}<div class="t caps semi">{title}</div></div>"""
    return page(body, css)


def divider() -> str:
    css = """
    .wrap { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; }
    """
    return page(f'<div class="wrap">{mark(32)}</div>', css)


def states() -> str:
    rows = [
        ("Freelook", 1.0),
        ("Sprint · 1st person", 0.5),
        ("Sprint · 3rd person", 0.5),
        ("Bow aim · 1st person", 0.5),
        ("Casting · 1st person", 0.5),
    ]
    css = """
    .wrap { position: absolute; inset: 150px 200px; display: flex; flex-direction: column; }
    .h { font-size: 54px; letter-spacing: 0.2em; }
    .legend { display: flex; gap: 48px; margin-top: 48px; font-size: 22px; }
    .legend span { display: inline-flex; align-items: center; gap: 14px; }
    .sw { width: 40px; height: 10px; border-radius: 5px; display: inline-block; }
    .rows { margin-top: 90px; display: flex; flex-direction: column; gap: 64px; }
    .row { display: grid; grid-template-columns: 440px 1fr; align-items: center; column-gap: 40px; }
    .label { font-size: 28px; }
    .bars { display: flex; flex-direction: column; gap: 10px; }
    .bar { height: 12px; border-radius: 6px; }
    .v { background: rgba(242,238,234,0.22); }
    .c { background: #f2eeea; }
    """
    body_rows = "".join(
        f"""<div class="row"><div class="label">{name}</div>
        <div class="bars"><div class="bar v" style="width:{v * 100:.0f}%"></div><div class="bar c" style="width:100%"></div></div></div>"""
        for name, v in rows
    )
    body = f"""
    <div class="wrap">
      <div class="h caps semi">Horizontal look speed</div>
      <div class="legend caps muted">
        <span><i class="sw" style="background:rgba(242,238,234,0.22)"></i>Vanilla</span>
        <span><i class="sw" style="background:#f2eeea"></i>CMC</span>
      </div>
      <div class="rows">{body_rows}</div>
    </div>"""
    return page(body, css)


def features() -> str:
    items = [
        ("sprint", "Sprint"),
        ("bow", "Bow aim"),
        ("time", "Slow time"),
        ("sliders", "Tuning"),
    ]
    css = """
    .wrap { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; }
    .grid { display: grid; grid-template-columns: repeat(4, 340px); gap: 40px; }
    .item { display: flex; flex-direction: column; align-items: center; text-align: center; }
    .t { font-size: 30px; margin-top: 48px; letter-spacing: 0.24em; white-space: nowrap; }
    """
    grid = "".join(f'<div class="item">{icon(k, 120)}<div class="t caps semi">{t}</div></div>' for k, t in items)
    return page(f'<div class="wrap"><div class="grid">{grid}</div></div>', css)


SECTIONS = [
    "About", "Features", "Requirements", "Installation", "Configuration",
    "Compatibility", "Known limits", "Changelog", "Support", "Credits",
]


def jobs() -> list[tuple[str, str, int, int]]:
    out = [
        ("cmc-primary-1920x1080.png", primary(), 1920, 1080),
        ("cmc-header-1920x480.png", header(), 1920, 480),
        ("cmc-gallery-features-1920x1080.png", features(), 1920, 1080),
        ("cmc-gallery-look-speed-1920x1080.png", states(), 1920, 1080),
        ("cmc-divider-1200x48.png", divider(), 1200, 48),
    ]
    for i, title in enumerate(SECTIONS, 1):
        slug = title.lower().replace(" ", "-")
        out.append((f"section-{i:02d}-{slug}.png", section(title), 1200, 112))
    return out


def render(html: str, dest: Path, w: int, h: int, work: Path) -> None:
    src = work / (dest.stem + ".html")
    src.write_text(html, encoding="utf-8")
    subprocess.run(
        [
            str(EDGE), "--headless=new", "--disable-gpu", "--hide-scrollbars",
            "--force-device-scale-factor=1", f"--user-data-dir={work / 'profile'}",
            f"--window-size={w},{h}", f"--screenshot={dest}", src.as_uri(),
        ],
        check=True, capture_output=True, timeout=120,
    )


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="cmc-presskit-"))
    try:
        for name, html, w, h in jobs():
            render(html, OUT / name, w, h, work)
            print(name)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
