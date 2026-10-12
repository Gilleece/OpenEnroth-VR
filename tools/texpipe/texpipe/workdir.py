"""Layout of the work folder that the extract, upscale and pack steps hand over to each other.

raw/<section>/<entry>           LOD entries as LodTool extract --raw writes them, only read for the CRC-32.
lodpng/<section>/<entry>.png    LodTool's PNG conversion, palette applied but no transparency.
images/<section>/<entry>.png    The extracted image with transparency, and <entry>.json describing it. Sprites are
                                images/sprites/<entry>/<palette id>.png, one per palette.
upscaled/<section>/<entry>.png  The upscale step's output, and <entry>.json saying how it was made.
summary.json                    What extract skipped and what failed.
stamps/<section>.json           Which LOD file raw/ and lodpng/ were extracted from, so a rerun can skip LodTool.
"""

import fnmatch
import json
from dataclasses import asdict, dataclass
from pathlib import Path

SECTIONS = ("bitmaps", "icons", "sprites")
SUMMARY_FILE = "summary.json"


@dataclass
class WorkImage:
    name: str  # LOD entry name, lowercase, e.g. "d8s2wl" or "foo.pcx". For sprites "<entry>/<palette id>".
    section: str
    width: int
    height: int
    crc32: str  # CRC-32 of the raw LOD entry, 8 hex digits.
    transparent: bool
    mask_ambiguous: bool = False


@dataclass
class UpscaledImage:
    backend: str
    scale: int
    brightness: list[float] | None = None  # Per channel factors the brightness correction applied, if it ran.


def images_dir(work: Path, section: str) -> Path:
    return work / "images" / section


def upscaled_dir(work: Path, section: str) -> Path:
    return work / "upscaled" / section


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def save_image_info(work: Path, image: WorkImage) -> None:
    write_json(images_dir(work, image.section) / f"{image.name}.json", asdict(image))


def load_image_infos(work: Path, only: list[str] | None = None) -> list[WorkImage]:
    """Returns every extracted image, optionally only those whose entry name matches the glob `only`."""
    result = []
    for section in SECTIONS:
        folder = images_dir(work, section)
        for path in sorted(folder.rglob("*.json")):
            if matches(path.relative_to(folder).with_suffix("").as_posix(), only):
                result.append(WorkImage(**read_json(path)))
    return result


def save_upscaled_info(work: Path, image: WorkImage, info: UpscaledImage) -> None:
    write_json(upscaled_dir(work, image.section) / f"{image.name}.json", asdict(info))


def load_upscaled_info(work: Path, image: WorkImage) -> UpscaledImage | None:
    path = upscaled_dir(work, image.section) / f"{image.name}.json"
    return UpscaledImage(**read_json(path)) if path.is_file() else None


def is_up_to_date(outputs: list[Path], inputs: list[Path]) -> bool:
    """True if every output exists and is newer than every input."""
    if not all(output.is_file() for output in outputs):
        return False
    return min(output.stat().st_mtime_ns for output in outputs) >= max(path.stat().st_mtime_ns for path in inputs)


def matches(name: str, only: list[str] | None) -> bool:
    """True if no filter is given or `name` matches any of the globs in `only`."""
    return not only or any(fnmatch.fnmatchcase(name, glob.lower()) for glob in only)
