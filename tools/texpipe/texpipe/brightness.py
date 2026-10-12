"""Keeps an upscaled image as bright as its source.

Super-resolution models treat dithering as noise and drop its bright speckles, which leaves dithered textures darker.
"""

from PIL import Image, ImageStat

Factors = tuple[float, float, float]


def opaque_mask(image: Image.Image) -> Image.Image | None:
    """Returns a mask of the opaque pixels, or None if `image` has no transparency."""
    if image.mode != "RGBA":
        return None
    alpha = image.getchannel("A")
    return alpha.point(lambda value: 255 if value else 0) if alpha.getextrema()[0] < 255 else None


def channel_means(image: Image.Image) -> Factors:
    """Mean of each color channel over the opaque pixels."""
    stat = ImageStat.Stat(image.convert("RGB"), opaque_mask(image))
    return tuple(stat.mean[:3]) if stat.count[0] else (0.0, 0.0, 0.0)


def match_brightness(source: Image.Image, upscaled: Image.Image, max_factor: float) -> tuple[Image.Image, Factors]:
    """Scales each color channel of `upscaled` so its mean matches `source`, by at most `max_factor`.

    Returns the result and the factor applied to each channel. The cap matters for channels that are nearly black: the
    model leaves a few stray values there, and scaling those by the full ratio turns them into bright speckles.
    """
    targets, actual = channel_means(source), channel_means(upscaled)
    factors = tuple(
        min(target / mean, max_factor) if mean else 1.0 for target, mean in zip(targets, actual, strict=True)
    )
    channels = list(upscaled.convert("RGBA").split())
    for i, factor in enumerate(factors):
        channels[i] = channels[i].point(lambda value, factor=factor: min(255, round(value * factor)))
    result = Image.merge("RGBA", channels)
    return (result if upscaled.mode == "RGBA" else result.convert("RGB")), factors
