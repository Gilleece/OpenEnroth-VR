"""Small image operations on Pillow images that the pipeline steps share."""

from PIL import Image, ImageChops, ImageFilter, ImageMath, ImageOps

OPAQUE = 255
TRANSPARENT = 0


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

    Filtering and upscaling then don't pull a mask color into the edges. A transparent pixel with no opaque neighbour
    becomes black. Runs on whole images in Pillow, sprites are too many to walk pixel by pixel.
    """
    rgba = image.convert("RGBA")
    alpha = rgba.getchannel("A")
    opaque = alpha.point(lambda value: 1 if value != TRANSPARENT else 0).convert("I")
    count = _neighbour_sum(opaque)
    channels = []
    for channel in rgba.split()[:3]:
        total = _neighbour_sum(ImageMath.lambda_eval(lambda a: a["c"] * a["o"], c=channel.convert("I"), o=opaque))
        average = ImageMath.lambda_eval(lambda a: a["t"] / a["max"](a["n"], 1), t=total, n=count)
        channels.append(average.convert("L"))
    bled = Image.merge("RGBA", (*channels, alpha))
    return Image.composite(rgba, bled, alpha.point(lambda value: OPAQUE if value != TRANSPARENT else 0))


def _neighbour_sum(image: Image.Image) -> Image.Image:
    """Sums the 8 neighbours of every pixel of an "I" image, treating pixels outside it as 0."""
    padded = ImageOps.expand(image, 1, 0)
    total = padded.filter(ImageFilter.Kernel((3, 3), [1, 1, 1, 1, 0, 1, 1, 1, 1], scale=1))
    return total.crop((1, 1, image.width + 1, image.height + 1))


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
