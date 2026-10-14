"""Makes the UWP package's logos (src/uwp/Assets) from src/images/shadps4.png.

    python tools/uwp-make-assets.py
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src" / "images" / "shadps4.png"
OUT = ROOT / "src" / "uwp" / "Assets"

# name: (width, height, logo share of the shorter side); transparent around the logo
SIZES = {
    "StoreLogo.png": (50, 50, 1.0),
    "Square44x44Logo.png": (44, 44, 1.0),
    "Square150x150Logo.png": (150, 150, 0.7),
    "Wide310x150Logo.png": (310, 150, 0.7),
    "SplashScreen.png": (620, 300, 0.6),
}


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    logo = Image.open(SOURCE).convert("RGBA")
    for name, (width, height, share) in SIZES.items():
        canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        side = int(min(width, height) * share)
        scaled = logo.copy()
        scaled.thumbnail((side, side), Image.LANCZOS)
        canvas.alpha_composite(scaled, ((width - scaled.width) // 2, (height - scaled.height) // 2))
        canvas.save(OUT / name)
        print(f"{name}: {width}x{height}")


if __name__ == "__main__":
    main()
