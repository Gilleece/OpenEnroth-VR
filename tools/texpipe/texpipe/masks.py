"""Which color of an image is transparent, following the engine's resource mask table."""

import json
from dataclasses import dataclass
from pathlib import Path

from texpipe.lodimage import LodImageInfo

Color = tuple[int, int, int]


@dataclass(frozen=True)
class MaskDecision:
    color: Color | None  # Pixels of this color become transparent, None keeps the image opaque.
    ambiguous: bool = False  # Other palette entries share the color of entry #0 and the engine keeps them opaque.


def load_mask_table(path: Path) -> dict[str, dict[str, str]]:
    """Reads resource_mask_table.json into {section: {lowercase name: value}}."""
    table = json.loads(path.read_text(encoding="utf-8"))
    return {
        section: {name.lower(): value for name, value in table.get(section, {}).items()}
        for section in ("bitmaps", "icons")
    }


def parse_color(value: str) -> Color | None:
    """Parses "#rrggbb", returns None for anything else."""
    if len(value) != 7 or not value.startswith("#"):
        return None
    return tuple(int(value[i : i + 2], 16) for i in (1, 3, 5))


def decide_pcx_mask(value: str) -> MaskDecision:
    """PCX icons have no palette flag, the engine only applies color masks to them."""
    return MaskDecision(parse_color(value))


def decide_lod_mask(value: str, info: LodImageInfo) -> MaskDecision:
    """Mirrors the engine: "none", "zero", "default" (the image's own flag) or a "#rrggbb" color."""
    color = parse_color(value)
    if color is not None:
        return MaskDecision(color)
    if value == "none":
        return MaskDecision(None)
    if value == "zero" or info.zero_is_transparent:
        zero = info.palette[0]
        return MaskDecision(zero, zero in info.palette[1:])
    return MaskDecision(None)
