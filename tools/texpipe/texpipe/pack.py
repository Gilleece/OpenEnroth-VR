"""The pack step: the upscaled images and a manifest.json in the layout the engine loads."""

import shutil
from pathlib import Path

from PIL import Image

from texpipe.workdir import SECTIONS, load_image_infos, load_upscaled_info, upscaled_dir, write_json

MANIFEST_FORMAT = 1
COLORS = ("raw", "final")
README_TEXT = """\
This texture pack was generated on this computer from the user's own copy of Might and Magic VII.

It contains only upscaled derivatives of the game's images, none of the original game data. It does nothing on its
own: the game has to be installed, and OpenEnroth loads these images in place of the ones in the game's LOD files.
"""


def build_manifest(crcs: dict[str, dict[str, str]], colors: str) -> dict:
    """Builds manifest.json from {section: {entry name: crc32}}."""
    if colors not in COLORS:
        raise ValueError(f"colors must be one of {COLORS}, got '{colors}'")
    manifest = {"format": MANIFEST_FORMAT, "colors": colors}
    for section in SECTIONS:
        manifest[section] = {name: {"crc32": crc} for name, crc in sorted(crcs.get(section, {}).items())}
    return manifest


def run_pack(work: Path, out: Path, colors: str, only: list[str] | None) -> dict[str, int]:
    """Copies every upscaled image into the pack and writes the manifest and README. Returns counts per section."""
    crcs: dict[str, dict[str, str]] = {section: {} for section in SECTIONS}
    for image in load_image_infos(work, only):
        upscaled = load_upscaled_info(work, image)
        if upscaled is None:
            continue
        source = upscaled_dir(work, image.section) / f"{image.name}.png"
        _check_scaled(source, (image.width, image.height), upscaled.scale)
        target = out / image.section / f"{image.name}.png"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        crcs[image.section][image.name] = image.crc32
    write_json(out / "manifest.json", build_manifest(crcs, colors))
    (out / "README.txt").write_text(README_TEXT, encoding="utf-8")
    return {section: len(entries) for section, entries in crcs.items()}


def _check_scaled(path: Path, size: tuple[int, int], scale: int) -> None:
    with Image.open(path) as image:
        if image.size != (size[0] * scale, size[1] * scale):
            raise ValueError(f"{path} is {image.size}, expected {size} times {scale}")
