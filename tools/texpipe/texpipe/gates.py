"""Fidelity measures between a generated sprite and the image it was made from.

All of them work on Pillow images of the same size, with a mask of the sprite's opaque pixels.
"""

from dataclasses import dataclass

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat

BACKGROUND = (40, 40, 40)  # Flat background for display, and for runs that recorded none.
BACKGROUNDS = ((255, 0, 255), (0, 255, 0), (0, 0, 255), (40, 40, 40))  # Candidates for the model's input.
FOREGROUND_THRESHOLD = 24  # Largest channel difference from the background that still counts as background.
BACKGROUND_LIKE = 1
OUTSIDE = 2


@dataclass(frozen=True)
class Thresholds:
    min_iou: float = 0.92
    max_color_delta: float = 10.0
    max_distance: float = 0.06


@dataclass(frozen=True)
class Scores:
    iou: float | None  # None where the silhouette is not checked, for opaque images.
    color_delta: float  # Largest per-channel difference of the means over the sprite, 0..255.
    distance: float  # Mean luminance difference after a blur, over the sprite, 0..1.

    def passes(self, thresholds: Thresholds) -> bool:
        return (
            (self.iou is None or self.iou >= thresholds.min_iou)
            and self.color_delta <= thresholds.max_color_delta
            and self.distance <= thresholds.max_distance
        )


def alpha_mask(image: Image.Image) -> Image.Image:
    """Pixels that are at least half opaque."""
    return image.convert("RGBA").getchannel("A").point(lambda value: 255 if value >= 128 else 0)


def foreground_mask(image: Image.Image, background: tuple[int, int, int] = BACKGROUND) -> Image.Image:
    """What the model drew on the flat background: everything but the background-colored area around the edges.

    Background-colored pixels enclosed by the drawing are dark parts of the sprite, not background, so the
    background is flood filled in from the image border instead of being keyed out by color.
    """
    difference = ImageChops.difference(image.convert("RGB"), Image.new("RGB", image.size, background))
    largest = ImageChops.lighter(ImageChops.lighter(*difference.split()[:2]), difference.split()[2])
    like_background = largest.point(lambda value: BACKGROUND_LIKE if value <= FOREGROUND_THRESHOLD else 0)
    width, height = image.size
    border = [(x, y) for x in range(width) for y in (0, height - 1)] + [
        (x, y) for y in range(height) for x in (0, width - 1)
    ]
    for point in border:
        if like_background.getpixel(point) == BACKGROUND_LIKE:
            ImageDraw.floodfill(like_background, point, OUTSIDE)
    return like_background.point(lambda value: 0 if value == OUTSIDE else 255)


def background_overlap(image: Image.Image, background: tuple[int, int, int]) -> float:
    """Share of the sprite's opaque pixels that are close enough to `background` to be mistaken for it."""
    rgba = image.convert("RGBA")
    difference = ImageChops.difference(rgba.convert("RGB"), Image.new("RGB", rgba.size, background))
    largest = ImageChops.lighter(ImageChops.lighter(*difference.split()[:2]), difference.split()[2])
    close = largest.point(lambda value: 255 if value <= 2 * FOREGROUND_THRESHOLD else 0)
    mask = alpha_mask(rgba)
    total = ImageStat.Stat(mask).sum[0]
    return ImageStat.Stat(ImageChops.multiply(close, mask)).sum[0] / total if total else 0.0


def choose_background(image: Image.Image) -> tuple[int, int, int]:
    """The candidate background that the sprite's own colors are furthest from, so the drawing can be told apart."""
    return min(BACKGROUNDS, key=lambda background: background_overlap(image, background))


def iou(first: Image.Image, second: Image.Image) -> float:
    """Intersection over union of two 0/255 masks. Two empty masks agree perfectly."""
    intersection = ImageStat.Stat(ImageChops.multiply(first, second)).sum[0]
    union = ImageStat.Stat(ImageChops.lighter(first, second)).sum[0]
    return intersection / union if union else 1.0


def color_delta(first: Image.Image, second: Image.Image, mask: Image.Image) -> float:
    means = [ImageStat.Stat(image.convert("RGB"), mask).mean for image in (first, second)]
    return max(abs(a - b) for a, b in zip(*means, strict=True))


def distance(first: Image.Image, second: Image.Image, mask: Image.Image, blur: float = 2.0) -> float:
    """Mean difference of the blurred luminance over `mask`, 0 for identical images and 1 at most.

    The blur leaves out the fine detail the pass is meant to add and keeps the shapes and shading it must not change.
    """
    blurred = [image.convert("L").filter(ImageFilter.GaussianBlur(blur)) for image in (first, second)]
    stat = ImageStat.Stat(ImageChops.difference(*blurred), mask)
    return stat.mean[0] / 255 if stat.count[0] else 0.0


def score(
    generated: Image.Image,
    reference: Image.Image,
    generated_on_background: Image.Image,
    background: tuple[int, int, int] = BACKGROUND,
    check_silhouette: bool = True,
) -> Scores:
    """Scores a generated sprite against the image it was made from.

    @param generated                    Generated colors, the size of `reference`.
    @param reference                    The RGBA image the model was given, its alpha is the stock silhouette.
    @param generated_on_background      The model's output as returned, on the flat background, resized to match.
    """
    mask = alpha_mask(reference)
    return Scores(
        iou=round(iou(foreground_mask(generated_on_background, background), mask), 4) if check_silhouette else None,
        color_delta=round(color_delta(generated, reference, mask), 2),
        distance=round(distance(generated, reference, mask), 4),
    )


def sequence_change(frames: list[Image.Image], masks: list[Image.Image]) -> float:
    """Mean difference between consecutive frames over the union of their masks, 0..1. Needs 2 frames or more."""
    changes = []
    for i in range(len(frames) - 1):
        union = ImageChops.lighter(masks[i], masks[i + 1])
        stat = ImageStat.Stat(ImageChops.difference(frames[i].convert("L"), frames[i + 1].convert("L")), union)
        changes.append(stat.mean[0] / 255 if stat.count[0] else 0.0)
    return sum(changes) / len(changes)
