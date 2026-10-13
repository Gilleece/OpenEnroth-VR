import json
import random

import pytest
from PIL import Image, PngImagePlugin

from texpipe.maps import MAPS, map_path
from texpipe.pack import build_manifest, optimize_png, run_pack
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
    assert run_pack(work, out, "raw", None) == {
        "bitmaps": 1,
        "icons": 1,
        "sprites": 0,
        "maps": 0,
        "sprite images": 0,
        "generated": 0,
        "shapes": 0,
        "gi": 0,
        "optimized MiB": 0.0,
    }
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


def noisy_image(mode: str, size=(64, 32)) -> Image.Image:
    # Smooth gradients with some noise, so that the harder compression has something to find.
    rng = random.Random(1)
    image = Image.new("RGBA", size)
    image.putdata(
        [
            (x * 4 % 256, y * 8 % 256, (x + y + rng.randrange(8)) % 256, 255 if (x + y) % 5 else 0)
            for y in range(size[1])
            for x in range(size[0])
        ]
    )
    return image.convert(mode) if mode != "P" else image.convert("RGB").quantize(64)


@pytest.mark.parametrize("mode", ["RGB", "RGBA", "L", "LA", "P"])
def test_optimized_pngs_decode_to_the_same_pixels_and_are_no_larger(tmp_path, mode):
    path, original = tmp_path / "image.png", tmp_path / "original.png"
    params = {"transparency": 3} if mode == "P" else {}
    noisy_image(mode).save(path, compress_level=1, **params)
    original.write_bytes(path.read_bytes())
    saved = optimize_png(path)
    assert saved == original.stat().st_size - path.stat().st_size > 0
    with Image.open(path) as optimized, Image.open(original) as expected:
        assert optimized.mode == expected.mode == mode
        assert optimized.info.get("transparency") == expected.info.get("transparency")
        assert optimized.convert("RGBA").tobytes() == expected.convert("RGBA").tobytes()
        assert optimized.tobytes() == expected.tobytes() or mode == "P"


def test_pngs_with_other_chunks_are_left_alone(tmp_path):
    # A chunk a plain save drops, like gamma or a text chunk, may change how the image is read.
    path = tmp_path / "image.png"
    info = PngImagePlugin.PngInfo()
    info.add_text("Comment", "kept")
    noisy_image("RGB").save(path, compress_level=1, pnginfo=info)
    before = path.read_bytes()
    assert optimize_png(path) == 0
    assert path.read_bytes() == before


def test_pack_optimizes_the_pngs_it_writes_unless_told_not_to(tmp_path):
    work = tmp_path / "work"
    add_image(work, "bitmaps", "wall", (32, 16), 2)
    upscaled = upscaled_dir(work, "bitmaps") / "wall.png"
    noisy_image("RGB").save(upscaled, compress_level=1)
    for kind in MAPS:
        map_path(work, kind, "wall").parent.mkdir(parents=True, exist_ok=True)
        noisy_image("RGB").save(map_path(work, kind, "wall"), compress_level=1)

    run_pack(work, tmp_path / "plain", "raw", None, config={"pack": {"optimize_png": False}})
    assert (tmp_path / "plain" / "bitmaps" / "wall.png").read_bytes() == upscaled.read_bytes()

    counts = run_pack(work, tmp_path / "optimized", "raw", None)
    for relative in ("bitmaps/wall.png", "normal/bitmaps/wall.png", "roughness/bitmaps/wall.png"):
        optimized = tmp_path / "optimized" / relative
        assert optimized.stat().st_size < (tmp_path / "plain" / relative).stat().st_size
        with Image.open(optimized) as image, Image.open(tmp_path / "plain" / relative) as plain:
            assert image.tobytes() == plain.tobytes()
    assert counts["optimized MiB"] == 0.0  # A few kilobytes.
