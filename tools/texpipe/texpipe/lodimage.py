"""The two facts about a raw LOD image that LodTool's PNG output loses: the transparency flag and the palette.

LodTool decodes the pixels. This only reads the fixed size header fields and the palette that trails the entry.
"""

import struct
from dataclasses import dataclass

HEADER_SIZE = 48
PALETTE_SIZE = 256 * 3
DATA_SIZE_OFFSET = 20
FLAGS_OFFSET = 44
ZERO_IS_TRANSPARENT_FLAG = 0x200


@dataclass(frozen=True)
class LodImageInfo:
    zero_is_transparent: bool
    palette: tuple[tuple[int, int, int], ...]


def read_lod_image_info(raw: bytes) -> LodImageInfo | None:
    """Returns the flag and palette of a raw LOD image entry, or None if `raw` is not laid out like one."""
    if len(raw) < HEADER_SIZE + PALETTE_SIZE:
        return None
    (data_size,) = struct.unpack_from("<I", raw, DATA_SIZE_OFFSET)
    if len(raw) != HEADER_SIZE + data_size + PALETTE_SIZE:
        return None
    (flags,) = struct.unpack_from("<I", raw, FLAGS_OFFSET)
    palette_bytes = raw[-PALETTE_SIZE:]
    palette = tuple(tuple(palette_bytes[i : i + 3]) for i in range(0, PALETTE_SIZE, 3))
    return LodImageInfo(bool(flags & ZERO_IS_TRANSPARENT_FLAG), palette)
