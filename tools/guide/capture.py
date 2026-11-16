"""Capture web-page screenshots for the user guide.

Needs the portal mock running (python tools/portal_preview.py, port 8080) and
Google Chrome. Writes docs/guide/shots/web_*.png at phone width, trimmed to
the page content.
"""
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "guide" / "shots"
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
BASE = "http://localhost:8080"

PAGES = {
    "home": "/",
    "wifi": "/wifi",
    "wifi_pick": "/wifi?type=aq:tampa&pick=1",
    "settings": "/settings",
    "settings_search": "/settings?type=hq:gatwick",
    "airports": "/airports",
    "airports_find": "/airports?type=ident:orlando",
    "live": "/live",
}


def trim(path: Path):
    """Crop the empty background below the content (keeps a small margin)."""
    im = Image.open(path).convert("RGB")
    w, h = im.size
    bg = im.getpixel((w // 2, h - 2))
    px = im.load()
    bottom = h
    for y in range(h - 1, 0, -1):
        if any(sum(abs(a - b) for a, b in zip(px[x, y], bg)) > 24 for x in range(0, w, 3)):
            bottom = min(h, y + 40)
            break
    im.crop((0, 0, w, bottom)).save(path, optimize=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    prof = tempfile.mkdtemp(prefix="guidecap")
    for name, url in PAGES.items():
        out = OUT / f"web_{name}.png"
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                        "--force-device-scale-factor=2", "--window-size=500,2400",
                        "--virtual-time-budget=6000", f"--user-data-dir={prof}",
                        f"--screenshot={out}", BASE + url],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
        trim(out)
        print(name, Image.open(out).size)


if __name__ == "__main__":
    main()
