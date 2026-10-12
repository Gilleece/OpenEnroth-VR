"""Sprites for the extract step: one image per sprites.lod entry and palette it is drawn with.

A sprite stores palette indices. The game colors it with the palettes the sprite frame table gives, one per monster
variant, not with the palette in the sprite's own header. Index 0 is transparent.
"""

from pathlib import Path

from PIL import Image

from texpipe import imageops
from texpipe.crc import crc32_hex
from texpipe.workdir import WorkImage, images_dir, is_up_to_date, read_json, save_image_info

SECTION = "sprites"


def load_sprite_palettes(path: Path) -> dict[str, list[int]]:
    """Reads the output of `OpenEnroth dump-sprite-palettes`: {lowercase sprites.lod name: palette ids}."""
    return read_json(path)["sprites"]


def load_palette(png: Path) -> bytes:
    """Reads a palette as LodTool extracts it from bitmaps.lod, a 256x1 image, into 768 bytes of RGB."""
    with Image.open(png) as image:
        return image.convert("RGB").tobytes()


def apply_palette(indices: Image.Image, palette: bytes) -> Image.Image:
    """Colors an index image with `palette`. Index 0 is transparent and takes the color of its opaque neighbours."""
    paletted = Image.frombytes("P", indices.size, indices.convert("L").tobytes())
    paletted.putpalette(palette)
    result = paletted.convert("RGB")
    result.putalpha(indices.convert("L").point(lambda index: 255 if index else 0))
    return imageops.bleed_transparent(result)


def sprite_image_name(name: str, palette_id: int) -> str:
    """Work image name of one sprite and palette, which also makes the pack path sprites/<name>/<id>.png."""
    return f"{name}/{palette_id}"


def extract_sprite(
    work: Path, name: str, raw_path: Path, png_path: Path, palettes: dict[int, Path], inputs: list[Path], force: bool
) -> int:
    """Writes the images of one sprite that are not up to date. Returns how many it wrote."""
    raw, indices = None, None
    written = 0
    for palette_id, palette_png in palettes.items():
        image_name = sprite_image_name(name, palette_id)
        output = images_dir(work, SECTION) / f"{image_name}.png"
        if not force and is_up_to_date(
            [output, output.with_suffix(".json")], [raw_path, png_path, palette_png, *inputs]
        ):
            continue
        if raw is None:
            raw = raw_path.read_bytes()
            with Image.open(png_path) as png:
                indices = png.convert("L")
        image = apply_palette(indices, load_palette(palette_png))
        output.parent.mkdir(parents=True, exist_ok=True)
        image.save(output)
        save_image_info(
            work,
            WorkImage(image_name, SECTION, image.width, image.height, crc32_hex(raw), imageops.has_transparency(image)),
        )
        written += 1
    return written
