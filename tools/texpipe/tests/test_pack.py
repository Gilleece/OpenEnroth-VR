import json

import pytest
from PIL import Image

from texpipe.maps import MAPS, map_path
from texpipe.pack import build_manifest, run_pack
from texpipe.workdir import UpscaledImage, WorkImage, images_dir, save_image_info, save_upscaled_info, upscaled_dir


def test_manifest_lists_both_sections_sorted():
    entries = {
        "bitmaps": {"b": {"crc32": "0000000b"}, "a": {"crc32": "0000000a", "maps": ["normal", "roughness"]}},
        "icons": {"x.pcx": {"crc32": "12345678"}},
    }
    manifest = build_manifest(entries, "raw")
    assert manifest == {
        "format": 1,
        "colors": "raw",
        "bitmaps": {"a": {"crc32": "0000000a", "maps": ["normal", "roughness"]}, "b": {"crc32": "0000000b"}},
        "icons": {"x.pcx": {"crc32": "12345678"}},
        "sprites": {},
    }
    assert list(manifest["bitmaps"]) == ["a", "b"]


def test_manifest_with_an_empty_section():
    assert build_manifest({}, "final")["icons"] == {}


def test_manifest_rejects_unknown_colors():
    with pytest.raises(ValueError):
        build_manifest({}, "grey")


def add_image(work, section, name, size, scale, upscaled_size=None):
    # A work folder entry as extract and upscale leave it, with a tiny generated image.
    info = WorkImage(name, section, size[0], size[1], "deadbeef", False)
    save_image_info(work, info)
    Image.new("RGB", size).save(images_dir(work, section) / f"{name}.png")
    (upscaled_dir(work, section) / f"{name}.png").parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGB", upscaled_size or (size[0] * scale, size[1] * scale)).save(
        upscaled_dir(work, section) / f"{name}.png"
    )
    save_upscaled_info(work, info, UpscaledImage("nearest", scale))


def test_pack_writes_images_manifest_and_readme(tmp_path):
    work, out = tmp_path / "work", tmp_path / "pack"
    add_image(work, "bitmaps", "wall", (4, 2), 2)
    add_image(work, "icons", "frame.pcx", (3, 3), 4)
    assert run_pack(work, out, "raw", None) == {"bitmaps": 1, "icons": 1, "sprites": 0, "maps": 0, "sprite images": 0}
    manifest = json.loads((out / "manifest.json").read_text())
    assert manifest["bitmaps"] == {"wall": {"crc32": "deadbeef"}}
    assert manifest["icons"] == {"frame.pcx": {"crc32": "deadbeef"}}
    assert Image.open(out / "icons" / "frame.pcx.png").size == (12, 12)
    assert "own copy of Might and Magic VII" in (out / "README.txt").read_text()


def test_pack_rejects_a_wrongly_sized_image(tmp_path):
    add_image(tmp_path / "work", "bitmaps", "wall", (4, 2), 2, upscaled_size=(8, 5))
    with pytest.raises(ValueError):
        run_pack(tmp_path / "work", tmp_path / "pack", "raw", None)


def test_pack_takes_the_maps_of_a_bitmap(tmp_path):
    work, out = tmp_path / "work", tmp_path / "pack"
    add_image(work, "bitmaps", "wall", (4, 2), 2)
    for kind in MAPS:
        map_path(work, kind, "wall").parent.mkdir(parents=True, exist_ok=True)
        Image.new("L", (8, 4)).save(map_path(work, kind, "wall"))
    assert run_pack(work, out, "raw", None)["maps"] == 1
    assert json.loads((out / "manifest.json").read_text())["bitmaps"]["wall"]["maps"] == ["normal", "roughness"]
    assert (out / "normal" / "bitmaps" / "wall.png").is_file()
    assert (out / "roughness" / "bitmaps" / "wall.png").is_file()


def test_pack_rejects_a_map_of_the_wrong_size(tmp_path):
    work = tmp_path / "work"
    add_image(work, "bitmaps", "wall", (4, 2), 2)
    for kind in MAPS:
        map_path(work, kind, "wall").parent.mkdir(parents=True, exist_ok=True)
        Image.new("L", (4, 2)).save(map_path(work, kind, "wall"))
    with pytest.raises(ValueError):
        run_pack(work, tmp_path / "pack", "raw", None)


def test_pack_groups_sprite_palettes_into_one_manifest_entry(tmp_path):
    work, out = tmp_path / "work", tmp_path / "pack"
    add_image(work, "sprites", "m246a0/248", (3, 5), 2)
    add_image(work, "sprites", "m246a0/247", (3, 5), 2)
    counts = run_pack(work, out, "raw", None)
    assert counts["sprites"] == 1
    assert counts["sprite images"] == 2
    manifest = json.loads((out / "manifest.json").read_text())
    assert manifest["sprites"] == {"m246a0": {"crc32": "deadbeef", "palettes": [247, 248]}}
    assert Image.open(out / "sprites" / "m246a0" / "247.png").size == (6, 10)
