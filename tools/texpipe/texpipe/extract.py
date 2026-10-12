"""The extract step: every bitmap and icon as a PNG with its transparency, plus what the later steps need to know."""

import re
from collections import defaultdict
from pathlib import Path

from PIL import Image

from texpipe import imageops, lodtool
from texpipe.crc import crc32_hex
from texpipe.lodimage import read_lod_image_info
from texpipe.masks import MaskDecision, decide_lod_mask, decide_pcx_mask, load_mask_table
from texpipe.workdir import SUMMARY_FILE, WorkImage, images_dir, matches, save_image_info, write_json

LOD_FILES = {"bitmaps": "bitmaps.lod", "icons": "icons.lod"}
PALETTE_ENTRY = re.compile(r"pal\d{3}")
FONT_SUFFIX = ".fnt"  # LodTool renders fonts to a glyph sheet, the engine never loads them as icons.


def unsafe_file_names(entry_names: list[str]) -> set[str]:
    """Returns the file names, lowercase, under which entries would overwrite each other when extracted.

    Windows drops trailing spaces and dots from file names and ignores case, so bitmaps.lod entries like "hhm3el2" and
    "hhm3el2 " land in the same file.
    """
    entries_by_file = defaultdict(list)
    for name in entry_names:
        entries_by_file[name.lower().rstrip(" .")].append(name)
    return {file for file, names in entries_by_file.items() if len(names) > 1 or names[0] != file}


def run_extract(data_dir: Path, work: Path, lodtool_path: Path, mask_table: Path, only: list[str] | None) -> dict:
    """Extracts both LODs into `work` and returns the summary it also writes to summary.json."""
    table = load_mask_table(mask_table)
    summary = {"extracted": {}, "skipped": [], "unsafe_names": [], "failed": {}}
    for section, lod_name in LOD_FILES.items():
        lod = lodtool.find_lod(data_dir, lod_name)
        raw_dir, png_dir = work / "raw" / section, work / "lodpng" / section
        lodtool.extract(lodtool_path, lod, raw_dir, raw=True)
        lodtool.extract(lodtool_path, lod, png_dir, raw=False)
        unsafe = unsafe_file_names(lodtool.list_entries(lodtool_path, lod))
        summary["unsafe_names"] += sorted(f"{section}/{name}" for name in unsafe)
        summary["extracted"][section] = _extract_section(
            work, section, raw_dir, png_dir, table[section], unsafe, summary, only
        )
    write_json(work / SUMMARY_FILE, summary)
    return summary


def _extract_section(work, section, raw_dir, png_dir, masks, unsafe, summary, only) -> int:
    count = 0
    for raw_path in sorted(raw_dir.iterdir()):
        name = raw_path.name.lower()
        key = f"{section}/{name}"
        if not matches(name, only) or name in unsafe:
            continue
        png_path = png_dir / f"{raw_path.name}.png"
        if PALETTE_ENTRY.fullmatch(name) or name.endswith(FONT_SUFFIX) or not png_path.is_file():
            summary["skipped"].append(key)
            continue
        try:
            extract_image(work, section, name, raw_path.read_bytes(), png_path, masks.get(name, "default"))
            count += 1
        except (OSError, ValueError) as error:
            summary["failed"][key] = str(error)
    return count


def extract_image(work: Path, section: str, name: str, raw: bytes, png_path: Path, mask_value: str) -> WorkImage:
    """Applies the mask to LodTool's PNG of one entry and stores the result and its description in `work`."""
    with Image.open(png_path) as png:
        image = png.convert("RGB")
    decision = _mask_decision(name, raw, mask_value)
    result = image
    if decision.color is not None:
        result = imageops.color_key(image, decision.color)
        result = imageops.bleed_transparent(result) if imageops.has_transparency(result) else image
    info = WorkImage(
        name=name,
        section=section,
        width=image.width,
        height=image.height,
        crc32=crc32_hex(raw),
        transparent=imageops.has_transparency(result),
        mask_ambiguous=decision.ambiguous,
    )
    out_dir = images_dir(work, section)
    out_dir.mkdir(parents=True, exist_ok=True)
    result.save(out_dir / f"{name}.png")
    save_image_info(work, info)
    return info


def _mask_decision(name: str, raw: bytes, mask_value: str) -> MaskDecision:
    if name.endswith(".pcx"):
        return decide_pcx_mask(mask_value)
    info = read_lod_image_info(raw)
    if info is None:
        raise ValueError("not a LOD image, no palette to take the transparent color from")
    return decide_lod_mask(mask_value, info)
