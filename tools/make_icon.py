"""Generate the SimagicFix application and tray icons.

The icon is an original community mark inspired by Simagic's red/white
identity: a white italic "S" on a red circle. It does not reuse the official
Simagic logo asset.

Requires Pillow (development-time only):
    python -m pip install --user pillow
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
GUI = ROOT / "src" / "gui"
RESOURCES = ROOT / "resources"
SIZE = 1024

APP_SIZES = [(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48),
             (64, 64), (128, 128), (256, 256)]
TRAY_SIZES = [(16, 16), (20, 20), (24, 24), (32, 32)]


def load_font(size: int) -> ImageFont.FreeTypeFont:
    candidates = [
        Path("C:/Windows/Fonts/seguibli.ttf"),  # Segoe UI Black Italic
        Path("C:/Windows/Fonts/seguibl.ttf"),   # Segoe UI Black
        Path("C:/Windows/Fonts/arialbi.ttf"),   # Arial Bold Italic
    ]
    for candidate in candidates:
        if candidate.is_file():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def build_icon() -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    # Outer dark ring, then the Simagic-style red disc.
    draw.ellipse((16, 16, SIZE - 16, SIZE - 16), fill=(116, 13, 18, 255))
    draw.ellipse((44, 44, SIZE - 44, SIZE - 44), fill=(211, 32, 41, 255))
    draw.ellipse((70, 70, SIZE - 70, SIZE - 70), outline=(255, 255, 255, 70),
                 width=6)

    font = load_font(720)
    text = "S"
    bbox = draw.textbbox((0, 0), text, font=font)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    x = (SIZE - width) / 2 - bbox[0]
    y = (SIZE - height) / 2 - bbox[1] - 12
    draw.text((x, y), text, font=font, fill=(255, 255, 255, 255))

    return image


def main() -> None:
    GUI.mkdir(parents=True, exist_ok=True)
    RESOURCES.mkdir(parents=True, exist_ok=True)
    icon = build_icon()
    icon.save(GUI / "app.ico", format="ICO", sizes=APP_SIZES)
    icon.save(GUI / "tray.ico", format="ICO", sizes=TRAY_SIZES)
    icon.resize((256, 256), Image.LANCZOS).save(RESOURCES / "icon-preview.png")
    icon.save(RESOURCES / "icon-source.png")
    print("Generated:", GUI / "app.ico", GUI / "tray.ico")


if __name__ == "__main__":
    main()
