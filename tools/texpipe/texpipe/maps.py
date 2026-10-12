"""The maps step: normal and roughness maps for the upscaled bitmaps, derived from their luminance.

Normal maps are tangent space in the OpenGL convention: red points to +U (right), green to the top row of the image,
blue out of the surface, and flat is (128, 128, 255). Roughness is 0 for smooth to 255 for fully rough, and the
baseline of 90 reproduces the stock specular. Both are linear 8-bit PNGs without gAMA or iCCP chunks, exactly the
pixel size of the color image.
"""

import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter, ImageMath, ImageStat

from texpipe import imageops
from texpipe.workdir import WorkImage, load_image_infos, load_upscaled_info, upscaled_dir

MAPS = ("normal", "roughness")
SOBEL_SCALE = 4  # Divides the Sobel sum so that slopes up to a quarter of the full range per pixel fit in 8 bits.
SOBEL_RAMP = 8  # Sobel sum of a ramp rising by 1 per pixel.
FLAT = 128


@dataclass(frozen=True)
class MapOptions:
    strength: float = 2.0
    blur: float = 1.0
    roughness_baseline: int = 90
    roughness_range: int = 40
    roughness_radius: int = 2
    wrap_border: int = 8

    @staticmethod
    def from_config(config: dict) -> "MapOptions":
        known = MapOptions.__dataclass_fields__
        return MapOptions(**{key: value for key, value in config.get("maps", {}).items() if key in known})


def map_path(work: Path, kind: str, name: str) -> Path:
    return work / "maps" / kind / "bitmaps" / f"{name}.png"


def _sobel(height: Image.Image, kernel: list[int]) -> Image.Image:
    return height.filter(ImageFilter.Kernel((3, 3), kernel, scale=SOBEL_SCALE, offset=FLAT))


def _clamp_to_l(args, value):
    return args["convert"](args["max"](args["min"](value + 0.5, 255), 0), "L")


def normal_map(color: Image.Image, options: MapOptions) -> Image.Image:
    """Builds the normal map of `color`, treating its luminance as height. Edges wrap around, as the bitmap tiles."""
    border = options.wrap_border
    height = imageops.wrap_pad(color.convert("L"), border)
    if options.blur:
        height = height.filter(ImageFilter.GaussianBlur(options.blur))
    dx = _sobel(height, [-1, 0, 1, -2, 0, 2, -1, 0, 1])  # Brighter to the right is above 128.
    dy = _sobel(height, [-1, -2, -1, 0, 0, 0, 1, 2, 1])  # Brighter towards the bottom row is above 128.
    dx, dy = imageops.crop_border(dx, border), imageops.crop_border(dy, border)

    # A surface rising to the right faces left, and one rising towards the bottom row faces the top.
    k = options.strength * SOBEL_SCALE / (SOBEL_RAMP * 255)
    max_square = 2 * (127 * k) ** 2 or 1.0
    square = ImageMath.lambda_eval(
        lambda a: _clamp_to_l(
            a, ((a["float"](a["x"]) - FLAT) ** 2 + (a["float"](a["y"]) - FLAT) ** 2) * (k * k) * (255 / max_square)
        ),
        x=dx,
        y=dy,
    )
    # 255 / length of (nx, ny, 1), looked up from the squared slope quantized to 8 bits.
    inverse_length = square.point(lambda q: round(255 / math.sqrt(1 + q * max_square / 255)))

    def channel(expression):
        return ImageMath.lambda_eval(lambda a: _clamp_to_l(a, expression(a)), x=dx, y=dy, n=inverse_length)

    red = channel(lambda a: FLAT - (a["float"](a["x"]) - FLAT) * k * 127 * a["float"](a["n"]) / 255)
    green = channel(lambda a: FLAT + (a["float"](a["y"]) - FLAT) * k * 127 * a["float"](a["n"]) / 255)
    blue = channel(lambda a: FLAT + 127 * a["float"](a["n"]) / 255)
    return Image.merge("RGB", (red, green, blue))


def roughness_map(color: Image.Image, options: MapOptions) -> Image.Image:
    """Builds the roughness map of `color`: busier areas than the image's average are rougher, calmer ones smoother."""
    border = max(options.wrap_border, options.roughness_radius * 2)
    luminance = imageops.wrap_pad(color.convert("L"), border)
    local_mean = luminance.filter(ImageFilter.BoxBlur(options.roughness_radius))
    busyness = ImageChops.difference(luminance, local_mean).filter(ImageFilter.BoxBlur(options.roughness_radius))
    busyness = imageops.crop_border(busyness, border)
    average = ImageStat.Stat(busyness).mean[0]
    baseline, spread = options.roughness_baseline, options.roughness_range
    return ImageMath.lambda_eval(
        lambda a: _clamp_to_l(
            a, baseline + spread * a["max"](a["min"]((a["float"](a["b"]) - average) / max(average, 1.0), 1.0), -1.0)
        ),
        b=busyness,
    )


def save_map(image: Image.Image, path: Path) -> None:
    """Saves without any chunk that would make the engine treat the data as anything but linear."""
    path.parent.mkdir(parents=True, exist_ok=True)
    image.info = {}
    image.save(path)


def make_maps(work: Path, image: WorkImage, options: MapOptions) -> None:
    with Image.open(upscaled_dir(work, image.section) / f"{image.name}.png") as color:
        color = color.convert("RGB")
    normal, roughness = normal_map(color, options), roughness_map(color, options)
    assert normal.size == roughness.size == color.size, f"{image.name}: maps must be {color.size}"
    save_map(normal, map_path(work, "normal", image.name))
    save_map(roughness, map_path(work, "roughness", image.name))


def run_maps(work: Path, config: dict, only: list[str] | None) -> int:
    """Makes maps for every upscaled bitmap and returns how many."""
    options = MapOptions.from_config(config)
    count = 0
    for image in load_image_infos(work, only):
        if image.section == "bitmaps" and load_upscaled_info(work, image) is not None:
            make_maps(work, image, options)
            count += 1
    return count
