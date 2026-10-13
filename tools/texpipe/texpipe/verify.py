"""Checks a texture pack against the installed game the way the engine loads it, for packs that came from elsewhere.

Every manifest entry must name a LOD entry with the CRC-32 the manifest says, every file it lists must be there, color
images must be the LOD image's size times 1, 2 or 4, and maps and sprite shapes must be the size of the color images
they go with. The engine falls back to the original image for an entry that fails, so a failure here is an image the
pack doesn't replace in game. Baked lighting must come from the games.lod map with the CRC-32 the manifest says, in a
probe file that follows the format, or the engine lights that map without it.
"""

import json
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image

from texpipe import lod
from texpipe.crc import crc32_hex
from texpipe.gi import verify_gi
from texpipe.lodtool import find_lod

FORMAT = 1
SCALES = (1, 2, 4)
COLORS = ("raw", "final")
LODS = {"bitmaps": "bitmaps.lod", "icons": "icons.lod", "sprites": "sprites.lod"}


@dataclass
class Report:
    entries: dict[str, int] = field(default_factory=dict)  # Checked manifest entries per section.
    files: int = 0  # Image and probe files checked.
    failures: list[str] = field(default_factory=list)


def _size(path: Path) -> tuple[int, int] | None:
    try:
        with Image.open(path) as image:
            return image.size
    except OSError:
        return None


def _check_file(pack: Path, relative: str, report: Report) -> tuple[int, int] | None:
    report.files += 1
    size = _size(pack / relative)
    if size is None:
        report.failures.append(f"{relative}: missing or not an image")
    return size


def _check_scale(relative: str, size: tuple[int, int], lod_size: tuple[int, int], report: Report) -> None:
    if not any(size == (lod_size[0] * scale, lod_size[1] * scale) for scale in SCALES):
        report.failures.append(f"{relative}: {size[0]}x{size[1]}, must be {lod_size[0]}x{lod_size[1]} times 1, 2 or 4")


def _check_same(relative: str, size: tuple[int, int], expected: tuple[int, int], what: str, report: Report) -> None:
    if size != expected:
        report.failures.append(f"{relative}: {size[0]}x{size[1]}, must be {expected[0]}x{expected[1]} like {what}")


def _lod_entry(archive: lod.Lod, section: str, name: str, crc: str, report: Report) -> bytes | None:
    if name not in archive:
        report.failures.append(f"{section}/{name}: no such entry in {LODS[section]}")
        return None
    raw = archive.read(name)
    if crc32_hex(raw) != crc.lower():
        report.failures.append(
            f"{section}/{name}: made from a different LOD entry, CRC-32 {crc} against {crc32_hex(raw)}"
        )
        return None
    return raw


def _image_size(section: str, name: str, raw: bytes) -> tuple[int, int]:
    if section == "sprites":
        return lod.sprite_size(raw)
    if name.endswith(".pcx"):
        return lod.pcx_size(raw)
    return lod.image_size(raw)


def verify_pack(pack: Path, data: Path) -> Report:
    """Checks every manifest entry of the pack in `pack` against the game in `data`."""
    report = Report()
    manifest_path = pack / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        report.failures.append(f"manifest.json: {error}")
        return report
    if manifest.get("format") != FORMAT:
        report.failures.append(f"manifest.json: format {manifest.get('format')}, the engine reads format {FORMAT}")
        return report
    if manifest.get("colors", "raw") not in COLORS:
        report.failures.append(f"manifest.json: colors '{manifest['colors']}', must be one of {', '.join(COLORS)}")

    for section, file_name in LODS.items():
        entries = {name.lower(): entry for name, entry in manifest.get(section, {}).items()}
        report.entries[section] = len(entries)
        if not entries:
            continue
        archive = lod.Lod(find_lod(data, file_name))
        for name, entry in sorted(entries.items()):
            raw = _lod_entry(archive, section, name, entry.get("crc32", ""), report)
            if raw is None:
                continue
            lod_size = _image_size(section, name, raw)
            if section == "sprites":
                sizes = []
                for palette_id in entry.get("palettes", []):
                    relative = f"sprites/{name}/{palette_id}.png"
                    size = _check_file(pack, relative, report)
                    if size is not None:
                        _check_scale(relative, size, lod_size, report)
                        sizes.append(size)
                if "shape" in entry:
                    relative = f"shape/sprites/{name}.png"
                    size = _check_file(pack, relative, report)
                    if size is not None and sizes:
                        _check_same(relative, size, sizes[0], "the sprite's images", report)
                continue
            relative = f"{section}/{name}.png"
            size = _check_file(pack, relative, report)
            if size is None:
                continue
            _check_scale(relative, size, lod_size, report)
            for kind in entry.get("maps", []):
                map_relative = f"{kind}/bitmaps/{name}.png"
                map_size = _check_file(pack, map_relative, report)
                if map_size is not None:
                    _check_same(map_relative, map_size, size, "its color image", report)

    gi = {name.lower(): entry for name, entry in manifest.get("gi", {}).items()}
    if gi:
        report.entries["gi"] = len(gi)
        report.files += verify_gi(pack, data, gi, report.failures)
    return report
