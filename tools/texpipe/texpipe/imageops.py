"""Small image operations on Pillow images that the pipeline steps share."""

import math

from PIL import Image, ImageChops, ImageFilter, ImageMath, ImageOps, ImageStat

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


def fill_transparent(image: Image.Image) -> Image.Image:
    """Gives every transparent pixel a color, ring by ring from the opaque area: each takes the average color of its
    neighbours that already have one.

    `bleed_transparent` only colors the first ring, the rest stays black. An upscaler reads far past the first ring, so
    for it the colors must carry on all the way.
    """
    rgba = image.convert("RGBA")
    alpha = rgba.getchannel("A")
    known = alpha.point(lambda value: 1 if value != TRANSPARENT else 0).convert("I")
    channels = [channel.convert("I") for channel in rgba.split()[:3]]
    while known.getextrema()[0] == 0:
        count = _neighbour_sum(known)
        reached = ImageMath.lambda_eval(lambda a: a["min"](a["c"], 1) * (1 - a["k"]), c=count, k=known)
        if reached.getextrema()[1] == 0:
            break  # Nothing opaque to fill from.
        for i, channel in enumerate(channels):
            total = _neighbour_sum(ImageMath.lambda_eval(lambda a: a["c"] * a["k"], c=channel, k=known))
            average = ImageMath.lambda_eval(lambda a: a["t"] / a["max"](a["n"], 1), t=total, n=count)
            channels[i] = ImageMath.lambda_eval(
                lambda a: a["c"] * (1 - a["r"]) + a["v"] * a["r"], c=channel, r=reached, v=average
            )
        known = ImageMath.lambda_eval(lambda a: a["k"] + a["r"], k=known, r=reached)
    return Image.merge("RGBA", (*(channel.convert("L") for channel in channels), alpha))


def blend_wrap_seam(image: Image.Image, width: int) -> Image.Image:
    """Eases the jump between the opposite edges of a tile that doesn't quite tile.

    The `width` outermost columns and rows on each side move towards the average of the two edges, the outermost one
    the most, so that the tile meets its neighbours without a hard line.
    """
    result = image.copy()
    for transpose in (False, True):
        work = result.transpose(Image.Transpose.TRANSPOSE) if transpose else result
        columns = work.width
        first, last = work.crop((0, 0, 1, work.height)), work.crop((columns - 1, 0, columns, work.height))
        middle = Image.blend(first, last, 0.5)
        for i in range(min(width, columns // 2)):
            weight = (width - i) / (width + 1)
            for x in (i, columns - 1 - i):
                column = work.crop((x, 0, x + 1, work.height))
                work.paste(Image.blend(column, middle, weight), (x, 0))
        result = work.transpose(Image.Transpose.TRANSPOSE) if transpose else work
    return result


def soften(image: Image.Image, strength: float, radius: float, tileable: bool) -> Image.Image:
    """Blends `image` towards a blurred copy of itself by `strength`, from 0 for unchanged to 1 for fully blurred. The
    alpha is kept. A tileable image is blurred across its edges as if it were tiled."""
    rgb, alpha = split_alpha(image)
    border = math.ceil(radius * 3) if tileable else 0
    padded = wrap_pad(rgb, border) if tileable else rgb
    blurred = crop_border(padded.filter(ImageFilter.GaussianBlur(radius)), border)
    return merge_alpha(Image.blend(rgb, blurred, strength), alpha)


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


def _mean_difference(a: Image.Image, b: Image.Image) -> float:
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / len(a.getbands())


def matching_edges(image: Image.Image, tolerance: float) -> tuple[bool, bool]:
    """Whether the left and right edges of `image`, and its top and bottom edges, continue each other.

    They do when the mean difference across the edge pair is at most `tolerance` times the mean difference between
    neighbouring columns, or rows, inside the image. A transition tile, like snow running into dirt, has edges that
    don't: wrapping it would bring the snow side into the dirt side.
    """
    rgb = image.convert("RGB")
    width, height = rgb.size
    result = []
    for size, crop in ((width, lambda a, b: (a, 0, b, height)), (height, lambda a, b: (0, a, width, b))):
        if size < 2:
            result.append(True)
            continue
        across = _mean_difference(rgb.crop(crop(0, 1)), rgb.crop(crop(size - 1, size)))
        inside = _mean_difference(rgb.crop(crop(0, size - 1)), rgb.crop(crop(1, size)))
        result.append(across <= tolerance * inside)
    return result[0], result[1]


def tile_pad(image: Image.Image, border: int, wrap_x: bool, wrap_y: bool) -> Image.Image:
    """Surrounds `image` with `border` pixels, wrapped from the opposite edge along the axes that wrap and copied from
    the outermost pixels along the others."""
    if wrap_x and wrap_y:
        return wrap_pad(image, border)
    if not wrap_x and not wrap_y:
        return clamp_pad(image, border, border)
    if wrap_x:
        return wrap_pad(clamp_pad(image, 0, border), border).crop(
            (0, border, image.width + 2 * border, image.height + 3 * border)
        )
    rotated = image.transpose(Image.Transpose.TRANSPOSE)
    return tile_pad(rotated, border, True, False).transpose(Image.Transpose.TRANSPOSE)


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
