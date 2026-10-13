"""The pack step: the upscaled images and a manifest.json in the layout the engine loads."""

import io
import os
import shutil
import struct
import zipfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from PIL import Image

from texpipe.depth import shape_entry, shape_path
from texpipe.maps import MAPS, MapOptions, map_path, normal_map, roughness_map, save_map
from texpipe.regen import generated_image, record_path
from texpipe.regen_icons import generated_icon
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
{count} sprites and icons in this pack went through an additional generative clean-up pass with {model}, from
the upscaled images, and passed automatic checks against the originals: same silhouette, same average colors, same
shapes and shading. They are listed in generated.txt. Everything else is only upscaled.
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
    work: Path,
    out: Path,
    colors: str,
    only: list[str] | None,
    regen_from: str | None = None,
    config: dict | None = None,
) -> dict[str, int]:
    """Copies every upscaled image and its maps into the pack and writes the manifest and README.

    With `regen_from`, a model whose regen outputs exist, sprites and icons come from those outputs that passed. Images
    upscaled past the config's `[pack] max_scale` are scaled down to it, and their maps made again at that size.
    The baked lighting a `bake` run left in the pack stays, unless the config's `[pack] gi` is off, which removes it.
    Every PNG written is compressed harder, as `optimize_pngs` does, unless `[pack] optimize_png` is off.
    Returns the number of entries per section, of bitmaps with maps, of sprite images, of generated sprites, of sprite
    shapes and of baked maps, and the MiB the harder compression saved.
    """
    config = config or {}
    max_scale = config.get("pack", {}).get("max_scale", 4)
    written: list[Path] = []
    map_options = MapOptions.from_config(config)
    entries: dict[str, dict[str, dict]] = {section: {} for section in SECTIONS}
    generated: set[str] = set()
    for image in load_image_infos(work, only):
        upscaled = load_upscaled_info(work, image)
        if upscaled is None:
            continue
        source = upscaled_dir(work, image.section) / f"{image.name}.png"
        if image.section == "sprites" and regen_from:
            sprite, palette_id = image.name.rsplit("/", 1)
            regenerated = generated_image(work, regen_from, sprite, int(palette_id))
            if regenerated is not None and read_json(record_path(work, regen_from, sprite))["passed"]:
                source = regenerated
                generated.add(sprite)
        if image.section == "icons" and regen_from:
            regenerated = generated_icon(work, regen_from, image.name)
            if regenerated is not None:
                source = regenerated
                generated.add(image.name)
        size = _check_scaled(source, (image.width, image.height), upscaled.scale)
        if upscaled.scale > max_scale:
            size = (image.width * max_scale, image.height * max_scale)
            _downscale(source, out / image.section / f"{image.name}.png", size)
        else:
            _copy(source, out / image.section / f"{image.name}.png")
        written.append(out / image.section / f"{image.name}.png")
        if image.section == "sprites":
            _add_sprite_image(entries["sprites"], image)
            shape = _add_sprite_shape(work, out, entries["sprites"], image.name.rsplit("/", 1)[0], size)
            if shape is not None:
                written.append(shape)
            continue
        entry = {"crc32": image.crc32}
        if image.section == "bitmaps" and all(map_path(work, kind, image.name).is_file() for kind in MAPS):
            if upscaled.scale > max_scale:
                _remake_maps(out, image.name, upscaled.wraps, map_options)
            else:
                for kind in MAPS:
                    _check_size(map_path(work, kind, image.name), size)
                    _copy(map_path(work, kind, image.name), out / kind / "bitmaps" / f"{image.name}.png")
            written += [out / kind / "bitmaps" / f"{image.name}.png" for kind in MAPS]
            entry["maps"] = list(MAPS)
        entries[image.section][image.name] = entry
    saved = optimize_pngs(written) if config.get("pack", {}).get("optimize_png", True) else 0
    manifest = build_manifest(entries, colors)
    _carry_gi(out, manifest, config.get("pack", {}).get("gi", True))
    write_json(out / "manifest.json", manifest)
    readme = README_TEXT
    if regen_from:
        readme += GENERATED_README.format(count=len(generated), model=regen_from)
        (out / "generated.txt").write_text("".join(f"{name}\n" for name in sorted(generated)), encoding="utf-8")
    (out / "README.txt").write_text(readme, encoding="utf-8")
    counts = {section: len(section_entries) for section, section_entries in entries.items()}
    counts["maps"] = sum(1 for entry in entries["bitmaps"].values() if "maps" in entry)
    counts["sprite images"] = sum(len(entry["palettes"]) for entry in entries["sprites"].values())
    counts["generated"] = len(generated)
    counts["shapes"] = sum(1 for entry in entries["sprites"].values() if "shape" in entry)
    counts["gi"] = len(manifest.get("gi", {}))
    counts["optimized MiB"] = round(saved / 2**20, 1)
    return counts


ARCHIVE_TIME = (1980, 1, 1, 0, 0, 0)  # The earliest a zip can hold, so the same pack makes the same archive.
STORED_SUFFIXES = (".png",)  # Already compressed, deflating them again only costs time.


PARTS_LISTING = "parts.txt"
ENTRY_OVERHEAD = 200  # Bytes a zip adds per file besides the name twice, with room for zip64 fields.
PART_RESERVE = 64 * 1024  # For the parts listing and the end of the archive.


def _archive_files(pack: Path) -> list[Path]:
    """The pack's files in archive order: README.txt first, the rest sorted by path."""
    files = sorted(
        (path for path in pack.rglob("*") if path.is_file()), key=lambda path: path.relative_to(pack).as_posix()
    )
    files.sort(key=lambda path: path.relative_to(pack).as_posix() != "README.txt")
    return files


def _write_entry(output: zipfile.ZipFile, name: str, data: bytes, stored: bool) -> None:
    info = zipfile.ZipInfo(name, ARCHIVE_TIME)
    info.compress_type = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    output.writestr(info, data)


def write_archive(pack: Path, archive: Path) -> int:
    """Zips the pack folder into `archive` under a folder of the pack's name, README.txt first and the rest sorted, with
    fixed timestamps. Returns the archive's size in bytes."""
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "w") as output:
        for path in _archive_files(pack):
            name = f"{pack.name}/{path.relative_to(pack).as_posix()}"
            _write_entry(output, name, path.read_bytes(), path.suffix in STORED_SUFFIXES)
    return archive.stat().st_size


def write_archive_parts(pack: Path, archive: Path, limit: int) -> list[tuple[Path, int]]:
    """Zips the pack folder into numbered parts next to `archive`, each at most `limit` bytes, like `write_archive`.

    Every part is a complete zip under the same folder name, so extracting them all into one place gives the pack.
    README.txt opens the first part, and each part starts with the same parts.txt listing of all of them. Files are
    never split, the PNGs are far below any sensible limit. Returns each part's path and size in bytes.
    """
    parts: list[list[Path]] = [[]]
    used = PART_RESERVE
    for path in _archive_files(pack):
        name_size = len(f"{pack.name}/{path.relative_to(pack).as_posix()}".encode())
        cost = path.stat().st_size + 2 * name_size + ENTRY_OVERHEAD  # Stored or deflated, never larger than this.
        if cost + PART_RESERVE > limit:
            raise ValueError(f"{path} alone doesn't fit in a part of {limit} bytes")
        if parts[-1] and used + cost > limit:
            parts.append([])
            used = PART_RESERVE
        parts[-1].append(path)
        used += cost

    paths = [archive.with_name(f"{archive.stem}.part{index}{archive.suffix}") for index in range(1, len(parts) + 1)]
    listing = f"{pack.name} comes in {len(parts)} parts. Extract all of them into the same folder.\n\n" + "".join(
        f"{path.name}: {len(files)} files\n" for path, files in zip(paths, parts, strict=True)
    )
    archive.parent.mkdir(parents=True, exist_ok=True)
    result = []
    for path, files in zip(paths, parts, strict=True):
        with zipfile.ZipFile(path, "w") as output:
            listed = False
            for file in files:
                relative = file.relative_to(pack).as_posix()
                if relative != "README.txt" and not listed:
                    _write_entry(output, f"{pack.name}/{PARTS_LISTING}", listing.encode(), False)
                    listed = True
                _write_entry(output, f"{pack.name}/{relative}", file.read_bytes(), file.suffix in STORED_SUFFIXES)
            if not listed:
                _write_entry(output, f"{pack.name}/{PARTS_LISTING}", listing.encode(), False)
        result.append((path, path.stat().st_size))
    return result


def _carry_gi(out: Path, manifest: dict, ship: bool) -> None:
    """Carries the baked lighting already in the pack over to the new manifest, or removes it if `ship` is off."""
    path = out / "manifest.json"
    gi = read_json(path).get("gi", {}) if path.is_file() else {}
    if ship:
        if gi:
            manifest["gi"] = gi
    else:
        shutil.rmtree(out / "gi", ignore_errors=True)


def _add_sprite_image(sprites: dict[str, dict], image: WorkImage) -> None:
    """Adds one palette of a sprite to its manifest entry, `{"crc32": ..., "palettes": [sorted ids]}`."""
    name, palette_id = image.name.rsplit("/", 1)
    entry = sprites.setdefault(name, {"crc32": image.crc32, "palettes": []})
    if entry["crc32"] != image.crc32:
        raise ValueError(f"sprite {name} has images from different sprites.lod entries")
    entry["palettes"] = sorted([*entry["palettes"], int(palette_id)])


def _add_sprite_shape(work: Path, out: Path, sprites: dict[str, dict], name: str, size: tuple[int, int]) -> Path | None:
    """Copies a sprite's shape image into the pack and adds `"shape": {"thickness": ...}` to its entry, once.

    Returns the path written, if it wrote one.
    """
    entry = sprites[name]
    if "shape" in entry:
        return None
    shape = shape_entry(work, name)
    if shape is None:
        return None
    target = out / "shape" / "sprites" / f"{name}.png"
    with Image.open(shape_path(work, name)) as image:
        shape_size = image.size
    if shape_size == size:
        _copy(shape_path(work, name), target)
    elif shape_size[0] > size[0] and shape_size[0] * size[1] == shape_size[1] * size[0]:
        _downscale(shape_path(work, name), target, size)  # The sprite's images were scaled down to `max_scale`.
    else:
        raise ValueError(f"{shape_path(work, name)} is {shape_size}, expected {size}")
    entry["shape"] = shape
    return target


PLAIN_CHUNKS = {b"IHDR", b"PLTE", b"tRNS", b"IDAT", b"IEND"}  # What a Pillow save without extra parameters writes.


def _chunk_types(data: bytes) -> set[bytes]:
    types, offset = set(), 8
    while offset + 8 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, offset)
        types.add(kind)
        offset += length + 12
    return types


def _pixels(image: Image.Image) -> bytes:
    return image.convert("RGBA").tobytes() if image.mode == "P" else image.tobytes()


def optimize_png(path: Path) -> int:
    """Saves a PNG again with Pillow's `optimize`, if that makes it smaller and decodes to the same pixels.

    PNGs with chunks that a plain save would drop are left alone. Returns the bytes saved.
    """
    data = path.read_bytes()
    if not _chunk_types(data) <= PLAIN_CHUNKS:
        return 0
    with Image.open(io.BytesIO(data)) as image:
        image.load()
        output = io.BytesIO()
        params = {"transparency": image.info["transparency"]} if "transparency" in image.info else {}
        image.save(output, "PNG", optimize=True, **params)
        optimized = output.getvalue()
        with Image.open(io.BytesIO(optimized)) as check:
            check.load()
            same = check.mode == image.mode and check.size == image.size and _pixels(check) == _pixels(image)
    if not same or len(optimized) >= len(data):
        return 0
    temp = path.with_name(path.name + ".tmp")
    temp.write_bytes(optimized)
    temp.replace(path)
    return len(data) - len(optimized)


def optimize_pngs(paths: list[Path]) -> int:
    """Runs `optimize_png` over `paths` on every core. Returns the bytes saved."""
    if not paths:
        return 0
    workers = os.cpu_count() or 1
    with ProcessPoolExecutor(workers) as pool:
        return sum(pool.map(optimize_png, paths, chunksize=max(1, len(paths) // (workers * 16))))


def _downscale(source: Path, target: Path, size: tuple[int, int]) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(source) as image:
        image.resize(size, Image.Resampling.LANCZOS).save(target)


def _remake_maps(out: Path, name: str, wraps: list[bool] | None, options: MapOptions) -> None:
    """Makes the maps of a bitmap again from the scaled down color image already in the pack."""
    with Image.open(out / "bitmaps" / f"{name}.png") as color:
        color = color.convert("RGB")
    edges = tuple(wraps) if wraps is not None else (True, True)
    save_map(normal_map(color, options, edges), out / "normal" / "bitmaps" / f"{name}.png")
    save_map(roughness_map(color, options, edges), out / "roughness" / "bitmaps" / f"{name}.png")


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
