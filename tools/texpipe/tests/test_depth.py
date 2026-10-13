import json

from PIL import Image

from texpipe.depth import DepthOptions, load_sprites, plan, shape_path, shape_record_path, thickness, wanted
from texpipe.pack import run_pack
from texpipe.workdir import UpscaledImage, WorkImage, save_image_info, save_upscaled_info, upscaled_dir, write_json


def make_sprite(work, name, palette, width, height=8, opaque_width=None):
    # An upscaled 2x sprite image with an opaque block of `opaque_width` LOD pixels.
    info = WorkImage(f"{name}/{palette}", "sprites", width, height, "deadbeef", True)
    save_image_info(work, info)
    image = Image.new("RGBA", (width * 2, height * 2), (0, 0, 0, 0))
    image.paste((200, 100, 50, 255), (0, 0, (opaque_width or width) * 2, height * 2))
    path = upscaled_dir(work, "sprites") / name / f"{palette}.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)
    save_upscaled_info(work, info, UpscaledImage("nearest", 2))
    return path


def write_palettes(path, sprites, groups):
    path.write_text(json.dumps({"count": 0, "sprites": sprites, "groups": groups}), encoding="utf-8")


def test_sprites_in_no_group_get_a_group_of_their_own(tmp_path):
    write_palettes(tmp_path / "p.json", {"a": [3, 1], "b": [2], "c": [2]}, [["b", "a"]])
    sprites, groups = load_sprites(tmp_path / "p.json")
    assert sprites["a"] == [1, 3]
    assert groups == [["a", "b"], ["c"]]


def test_effects_are_left_flat():
    options = DepthOptions(exclude=("sp[0-9]*", "spell*"))
    assert not wanted("sp01a", options, None)
    assert wanted("m246aa0", options, None)


def test_thickness_follows_the_median_width_and_is_clamped(tmp_path):
    work = tmp_path / "work"
    paths = [make_sprite(work, name, 1, 40, opaque_width=w) for name, w in (("a", 10), ("b", 20), ("c", 40))]
    assert thickness(paths, 2, DepthOptions()) == 12.0  # A swung out weapon in one frame doesn't count.
    assert thickness(paths[:1], 2, DepthOptions()) == 8.0


def test_plan_skips_groups_that_are_up_to_date(tmp_path):
    work = tmp_path / "work"
    make_sprite(work, "a", 1, 10)
    write_palettes(tmp_path / "p.json", {"a": [1]}, [["a"]])
    [group] = plan(work, tmp_path / "p.json", DepthOptions(), None, False)
    assert group["names"] == ["a"] and group["scale"] == 2 and group["thickness"] == 8.0
    shape_path(work, "a").parent.mkdir(parents=True)
    Image.new("RGBA", (20, 16)).save(shape_path(work, "a"))
    assert plan(work, tmp_path / "p.json", DepthOptions(), None, False) == []


def test_pack_adds_the_shape_to_the_sprite_entry(tmp_path):
    work, out = tmp_path / "work", tmp_path / "pack"
    make_sprite(work, "a", 1, 10)
    make_sprite(work, "a", 2, 10)
    shape_path(work, "a").parent.mkdir(parents=True)
    Image.new("RGBA", (20, 16)).save(shape_path(work, "a"))
    write_json(shape_record_path(work, "a"), {"thickness": 40.0})
    counts = run_pack(work, out, "raw", None)
    manifest = json.loads((out / "manifest.json").read_text())
    assert manifest["sprites"]["a"] == {"crc32": "deadbeef", "palettes": [1, 2], "shape": {"thickness": 40.0}}
    assert (out / "shape" / "sprites" / "a.png").is_file()
    assert counts["shapes"] == 1
