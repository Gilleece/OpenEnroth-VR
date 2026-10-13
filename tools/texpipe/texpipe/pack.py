"""The pack step: the upscaled images and a manifest.json in the layout the engine loads."""

import shutil
from pathlib import Path

from PIL import Image

from texpipe.maps import MAPS, map_path
from texpipe.regen import generated_image, record_path
from texpipe.workdir import (
    SECTIONS,
    WorkImage,
    load_image_infos,
    load_upscaled_info,
    read_json,
    upscaled_dir,
    write_json,
)

MANIFEST_FORMAT = 1
COLORS = ("raw", "final")
README_TEXT = """\
This texture pack was generated on this computer from the user's own copy of Might and Magic VII.

It contains only upscaled derivatives of the game's images and material maps computed from
them, none of the original game data. It does nothing on its
own: the game has to be installed, and OpenEnroth loads these images in place of the ones in the game's LOD files.
"""


GENERATED_README = """
{count} sprites in this pack went through an additional generative clean-up pass with {model}, from the
upscaled sprites, and passed automatic checks against the originals: same silhouette, same average colors, same
shapes and shading. They are listed in generated_sprites.txt. All other sprites are only upscaled.
"""


def build_manifest(entries: dict[str, dict[str, dict]], colors: str) -> dict:
    """Builds manifest.json from {section: {entry name: manifest entry}}."""
    if colors not in COLORS:
        raise ValueError(f"colors must be one of {COLORS}, got '{colors}'")
    manifest = {"format": MANIFEST_FORMAT, "colors": colors}
    for section in SECTIONS:
        manifest[section] = dict(sorted(entries.get(section, {}).items()))
    return manifest


def run_pack(
    work: Path, out: Path, colors: str, only: list[str] | None, sprites_from: str | None = None
) -> dict[str, int]:
    """Copies every upscaled image and its maps into the pack and writes the manifest and README.

    With `sprites_from`, a model whose regen outputs exist, sprites come from those outputs where there are any.
    Returns the number of entries per section, of bitmaps with maps, of sprite images, and of generated sprites.
    """
    entries: dict[str, dict[str, dict]] = {section: {} for section in SECTIONS}
    generated: set[str] = set()
    for image in load_image_infos(work, only):
        upscaled = load_upscaled_info(work, image)
        if upscaled is None:
            continue
        source = upscaled_dir(work, image.section) / f"{image.name}.png"
        if image.section == "sprites" and sprites_from:
            sprite, palette_id = image.name.rsplit("/", 1)
            regenerated = generated_image(work, sprites_from, sprite, int(palette_id))
            if regenerated is not None and read_json(record_path(work, sprites_from, sprite))["passed"]:
                source = regenerated
                generated.add(sprite)
        size = _check_scaled(source, (image.width, image.height), upscaled.scale)
        _copy(source, out / image.section / f"{image.name}.png")
        if image.section == "sprites":
            _add_sprite_image(entries["sprites"], image)
            continue
        entry = {"crc32": image.crc32}
        if image.section == "bitmaps" and all(map_path(work, kind, image.name).is_file() for kind in MAPS):
            for kind in MAPS:
                _check_size(map_path(work, kind, image.name), size)
                _copy(map_path(work, kind, image.name), out / kind / "bitmaps" / f"{image.name}.png")
            entry["maps"] = list(MAPS)
        entries[image.section][image.name] = entry
    write_json(out / "manifest.json", build_manifest(entries, colors))
    readme = README_TEXT
    if sprites_from:
        readme += GENERATED_README.format(count=len(generated), model=sprites_from)
        (out / "generated_sprites.txt").write_text("".join(f"{name}\n" for name in sorted(generated)), encoding="utf-8")
    (out / "README.txt").write_text(readme, encoding="utf-8")
    counts = {section: len(section_entries) for section, section_entries in entries.items()}
    counts["maps"] = sum(1 for entry in entries["bitmaps"].values() if "maps" in entry)
    counts["sprite images"] = sum(len(entry["palettes"]) for entry in entries["sprites"].values())
    counts["generated sprites"] = len(generated)
    return counts


def _add_sprite_image(sprites: dict[str, dict], image: WorkImage) -> None:
    """Adds one palette of a sprite to its manifest entry, `{"crc32": ..., "palettes": [sorted ids]}`."""
    name, palette_id = image.name.rsplit("/", 1)
    entry = sprites.setdefault(name, {"crc32": image.crc32, "palettes": []})
    if entry["crc32"] != image.crc32:
        raise ValueError(f"sprite {name} has images from different sprites.lod entries")
    entry["palettes"] = sorted([*entry["palettes"], int(palette_id)])


def _copy(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)


def _check_scaled(path: Path, size: tuple[int, int], scale: int) -> tuple[int, int]:
    return _check_size(path, (size[0] * scale, size[1] * scale))


def _check_size(path: Path, size: tuple[int, int]) -> tuple[int, int]:
    with Image.open(path) as image:
        if image.size != size:
            raise ValueError(f"{path} is {image.size}, expected {size}")
    return size
