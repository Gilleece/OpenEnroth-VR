"""Small image operations on Pillow images that the pipeline steps share."""

from PIL import Image, ImageChops

OPAQUE = 255
TRANSPARENT = 0
NEIGHBOURS = [(dx, dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dx or dy]


def color_key(image: Image.Image, color: tuple[int, int, int]) -> Image.Image:
    """Returns an RGBA copy of `image` where every pixel of exactly `color` is transparent."""
    rgb = image.convert("RGB")
    matches = [
        channel.point(lambda value, wanted=wanted: OPAQUE if value == wanted else TRANSPARENT)
        for channel, wanted in zip(rgb.split(), color, strict=True)
    ]
    all_match = ImageChops.multiply(ImageChops.multiply(matches[0], matches[1]), matches[2])
    result = rgb.copy()
    result.putalpha(ImageChops.invert(all_match))
    return result


def bleed_transparent(image: Image.Image) -> Image.Image:
    """Gives each transparent pixel the average color of its opaque neighbours, as the engine does for bitmaps.

    Filtering and upscaling then don't pull a mask color into the edges.
    """
    result = image.convert("RGBA")
    source = image.convert("RGBA").load()
    pixels = result.load()
    width, height = result.size
    alpha = result.getchannel("A").tobytes()
    for index in (i for i, value in enumerate(alpha) if value == TRANSPARENT):
        y, x = divmod(index, width)
        pixels[x, y] = _neighbour_average(source, x, y, width, height)
    return result


def _neighbour_average(source, x: int, y: int, width: int, height: int) -> tuple[int, int, int, int]:
    opaque = [
        source[x + dx, y + dy]
        for dx, dy in NEIGHBOURS
        if 0 <= x + dx < width and 0 <= y + dy < height and source[x + dx, y + dy][3] != TRANSPARENT
    ]
    if not opaque:
        return (0, 0, 0, TRANSPARENT)
    return tuple(sum(pixel[i] for pixel in opaque) // len(opaque) for i in range(3)) + (TRANSPARENT,)


def has_transparency(image: Image.Image) -> bool:
    return image.mode == "RGBA" and image.getchannel("A").getextrema()[0] < OPAQUE


def split_alpha(image: Image.Image) -> tuple[Image.Image, Image.Image | None]:
    """Splits `image` into its RGB part and its alpha channel, the latter None for a fully opaque image."""
    rgba = image.convert("RGBA")
    alpha = rgba.getchannel("A") if has_transparency(rgba) else None
    return rgba.convert("RGB"), alpha


def merge_alpha(rgb: Image.Image, alpha: Image.Image | None) -> Image.Image:
    """Inverse of `split_alpha`. `alpha` must be the same size as `rgb`."""
    result = rgb.convert("RGBA")
    if alpha is not None:
        assert alpha.size == rgb.size, f"alpha is {alpha.size}, rgb is {rgb.size}"
        result.putalpha(alpha.convert("L"))
    return result


def wrap_pad(image: Image.Image, border: int) -> Image.Image:
    """Surrounds `image` with `border` pixels taken from its opposite edges, as if it were tiled."""
    width, height = image.size
    result = Image.new(image.mode, (width + 2 * border, height + 2 * border))
    tiles_x = -(-border // width)
    tiles_y = -(-border // height)
    for ty in range(-tiles_y, tiles_y + 1):
        for tx in range(-tiles_x, tiles_x + 1):
            result.paste(image, (border + tx * width, border + ty * height))
    return result


def clamp_pad(image: Image.Image, border_x: int, border_y: int) -> Image.Image:
    """Surrounds `image` with copies of its outermost pixels, `border_x` on the left and right, `border_y` above and
    below."""
    width, height = image.size
    result = Image.new(image.mode, (width + 2 * border_x, height + 2 * border_y))
    result.paste(image, (border_x, border_y))
    if border_x:
        result.paste(image.crop((0, 0, 1, height)).resize((border_x, height)), (0, border_y))
        result.paste(image.crop((width - 1, 0, width, height)).resize((border_x, height)), (border_x + width, border_y))
    if border_y:
        full_width = width + 2 * border_x
        top = result.crop((0, border_y, full_width, border_y + 1)).resize((full_width, border_y))
        bottom = result.crop((0, border_y + height - 1, full_width, border_y + height)).resize((full_width, border_y))
        result.paste(top, (0, 0))
        result.paste(bottom, (0, border_y + height))
    return result


def crop_border(image: Image.Image, border_x: int, border_y: int | None = None) -> Image.Image:
    """Inverse of `wrap_pad` and `clamp_pad`, for an image that may have been scaled since: borders are in its current
    pixels. `border_y` defaults to `border_x`."""
    border_y = border_x if border_y is None else border_y
    width, height = image.size
    return image.crop((border_x, border_y, width - border_x, height - border_y))
