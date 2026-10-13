"""Reads LOD archives directly, as far as checking a texture pack needs: the entries and the image sizes.

The layout follows the engine's LodReader and LodFormats. Everything else, decoding the pixels in particular, is left
to LodTool.
"""

import struct
import zlib
from pathlib import Path

HEADER_SIZE = 256  # Signature, version, description, then the root directory count.
DIRECTORY_ENTRY_SIZE = 32  # Name, offset, size, unused, item count, priority.
MM8_ENTRY_SIZE = 76  # The MM8 file entry has its offset and size at 64 and 68.
NAME_SIZE = 16
IMAGE_HEADER_SIZE = 48
SPRITE_HEADER_SIZE = 32
PCX_HEADER_SIZE = 128
COMPRESSION_VERSION = 91969
COMPRESSION_SIGNATURE = b"mvii"
PSEUDO_IMAGE_FLAG = 256


def _name(raw: bytes) -> str:
    return raw[:NAME_SIZE].split(b"\0", 1)[0].decode("latin-1")


class Lod:
    """The entries of one LOD archive, by lowercase name. The first of duplicate entries wins, as in the engine."""

    def __init__(self, path: Path):
        self._data = path.read_bytes()
        if self._data[:3] != b"LOD":
            raise ValueError(f"{path} is not a LOD archive")
        version = _name(self._data[4:84])
        (directories,) = struct.unpack_from("<I", self._data, 4 + 80 + 80 + 8)
        if directories != 1:
            raise ValueError(f"{path} has {directories} directories, LOD archives of the games have one")
        root_offset, _, _, items = struct.unpack_from("<IIIH", self._data, HEADER_SIZE + NAME_SIZE)
        self._entries: dict[str, tuple[int, int]] = {}
        for index in range(items):
            if version == "MMVIII":
                start = root_offset + index * MM8_ENTRY_SIZE
                offset, size = struct.unpack_from("<II", self._data, start + 64)
            else:
                start = root_offset + index * DIRECTORY_ENTRY_SIZE
                offset, size = struct.unpack_from("<II", self._data, start + NAME_SIZE)
            name = _name(self._data[start : start + NAME_SIZE]).lower()
            self._entries.setdefault(name, (root_offset + offset, size))

    def names(self) -> list[str]:
        """Every entry name, lowercase and sorted."""
        return sorted(self._entries)

    def __contains__(self, name: str) -> bool:
        return name.lower() in self._entries

    def read(self, name: str) -> bytes:
        offset, size = self._entries[name.lower()]
        return self._data[offset : offset + size]


def decode_maybe_compressed(raw: bytes) -> bytes:
    """The entry's data, unpacked if the LOD stored it compressed."""
    if len(raw) >= 16:
        version, signature, size, unpacked = struct.unpack_from("<I4sII", raw)
        if version == COMPRESSION_VERSION and signature == COMPRESSION_SIGNATURE:
            data = raw[16:] if size == len(raw) else raw[16 : 16 + size]  # Some entries give their own size.
            return zlib.decompress(data) if unpacked else data
    if len(raw) >= IMAGE_HEADER_SIZE:
        size, data_size, width, height = struct.unpack_from("<IIHH", raw, NAME_SIZE)
        unpacked, flags = struct.unpack_from("<II", raw, 40)
        if (
            size == 0
            and width == 0
            and height == 0
            and flags & PSEUDO_IMAGE_FLAG
            and len(raw) == IMAGE_HEADER_SIZE + data_size
        ):
            data = raw[IMAGE_HEADER_SIZE:]
            return zlib.decompress(data) if unpacked else data
    return raw


def image_size(raw: bytes) -> tuple[int, int]:
    """Size of a LOD image entry, as in its header."""
    return struct.unpack_from("<HH", raw, 24)


def sprite_size(raw: bytes) -> tuple[int, int]:
    """Size of a LOD sprite entry, as in its header."""
    return struct.unpack_from("<HH", raw, 16)


def pcx_size(raw: bytes) -> tuple[int, int]:
    """Size of a PCX image stored in a LOD, compressed or not."""
    data = decode_maybe_compressed(raw)
    xmin, ymin, xmax, ymax = struct.unpack_from("<hhhh", data, 4)
    return xmax - xmin + 1, ymax - ymin + 1
