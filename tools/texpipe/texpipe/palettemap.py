"""Derives the other palette variants of a sprite from an image made with its base palette.

Each pixel is matched to the nearest color of the base palette, and the difference between that entry and the same
entry of the target palette is added to it. A pixel that is exactly a base palette color becomes exactly the target
color, as the game's own recolor does, and finer detail between palette colors is carried over.
"""

import math
from functools import lru_cache

from PIL import Image, ImageChops, ImageMath

CELL = 16  # Color cube cell size of the nearest color search.


@lru_cache(maxsize=64)
def _cell_candidates(palette: bytes) -> tuple[tuple[int, ...], ...]:
    """For every cell of the color cube, the palette entries that can be the nearest to some color in it.

    An entry further from the cell center than the closest entry plus the cell diagonal can never win, which leaves
    a handful of candidates per cell. Entry 0 is the transparent index of sprites and never a candidate.
    """
    colors = [tuple(palette[3 * index : 3 * index + 3]) for index in range(256)]
    diagonal = CELL * math.sqrt(3)
    cells = []
    for cell in range((256 // CELL) ** 3):
        center = [(cell // (256 // CELL) ** (2 - axis) % (256 // CELL)) * CELL + CELL / 2 for axis in range(3)]
        distances = [math.dist(center, colors[index]) for index in range(1, 256)]
        closest = min(distances)
        cells.append(tuple(index + 1 for index, d in enumerate(distances) if d <= closest + diagonal))
    return tuple(cells)


def nearest_index(color: bytes, palette: bytes) -> int:
    """Index of the palette color nearest to `color`, never 0. Ties go to the lower index."""
    side = 256 // CELL
    cell = (color[0] // CELL * side + color[1] // CELL) * side + color[2] // CELL
    return min(
        _cell_candidates(palette)[cell],
        key=lambda index: (sum((color[axis] - palette[3 * index + axis]) ** 2 for axis in range(3)), index),
    )


def nearest_indices(image: Image.Image, palette: bytes) -> Image.Image:
    """Index of the nearest color of `palette` for every pixel, never 0."""
    rgb = image.convert("RGB")
    data = rgb.tobytes()
    cache: dict[bytes, int] = {}
    indices = bytearray(len(data) // 3)
    for i in range(len(indices)):
        color = data[3 * i : 3 * i + 3]
        index = cache.get(color)
        if index is None:
            index = cache[color] = nearest_index(color, palette)
        indices[i] = index
    return Image.frombytes("L", rgb.size, bytes(indices))


def colors_of(indices: Image.Image, palette: bytes) -> Image.Image:
    image = Image.frombytes("P", indices.size, indices.tobytes())
    image.putpalette(palette)
    return image.convert("RGB")


def recolor(image: Image.Image, base: bytes, target: bytes) -> Image.Image:
    """Moves `image`, made with palette `base`, over to palette `target`. Alpha is kept as it is."""
    rgba = image.convert("RGBA")
    indices = nearest_indices(rgba, base)
    shift_from, shift_to = colors_of(indices, base), colors_of(indices, target)
    channels = []
    for color, old, new in zip(rgba.split()[:3], shift_from.split(), shift_to.split(), strict=True):
        moved = ImageMath.lambda_eval(
            lambda a: a["convert"](
                a["max"](a["min"](a["int"](a["c"]) + a["int"](a["n"]) - a["int"](a["o"]), 255), 0), "L"
            ),
            c=color,
            n=new,
            o=old,
        )
        channels.append(moved)
    return Image.merge("RGBA", (*channels, rgba.getchannel("A")))


def mismatch(first: Image.Image, second: Image.Image, mask: Image.Image) -> float:
    """Share of the pixels under `mask` whose colors differ between the two images."""
    difference = ImageChops.difference(first.convert("RGB"), second.convert("RGB")).convert("L")
    differing = ImageChops.multiply(difference.point(lambda value: 255 if value else 0), mask)
    total = sum(1 for value in mask.tobytes() if value)
    return sum(1 for value in differing.tobytes() if value) / total if total else 0.0
