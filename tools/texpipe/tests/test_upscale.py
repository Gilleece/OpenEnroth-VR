import os

from PIL import Image

from texpipe.brightness import channel_means
from texpipe.upscale import run_upscale
from texpipe.workdir import WorkImage, images_dir, load_image_infos, load_upscaled_info, save_image_info, upscaled_dir

PLAIN = {"default": {"backend": "nearest", "scale": 2}}
PRESERVING = {"default": {"backend": "nearest", "scale": 2}, "nearest": {"preserve_brightness": ["sprites"]}}


def make_work(tmp_path):
    work = tmp_path / "work"
    save_image_info(work, WorkImage("gob/247", "sprites", 2, 2, "deadbeef", False))
    Image.new("RGB", (2, 2), (100, 100, 100)).save(images_dir(work, "sprites") / "gob" / "247.png")
    return work


def test_a_second_run_skips_what_is_done(tmp_path):
    work = make_work(tmp_path)
    assert run_upscale(work, PLAIN, None, None, None, None)["nearest 2x"] == 1
    assert run_upscale(work, PLAIN, None, None, None, None) == {"up to date": 1, "brightness only": 0}


def test_a_changed_source_is_redone(tmp_path):
    work = make_work(tmp_path)
    run_upscale(work, PLAIN, None, None, None, None)
    source = images_dir(work, "sprites") / "gob" / "247.png"
    future = os.stat(source).st_mtime_ns + 10**9
    os.utime(source, ns=(future, future))
    assert run_upscale(work, PLAIN, None, None, None, None)["nearest 2x"] == 1


def test_brightness_is_added_to_a_done_image_without_upscaling_it_again(tmp_path):
    work = make_work(tmp_path)
    run_upscale(work, PLAIN, None, None, None, None)
    target = upscaled_dir(work, "sprites") / "gob" / "247.png"
    Image.new("RGB", (4, 4), (50, 50, 50)).save(target)  # As if the model had darkened it.
    os.utime(target, ns=(os.stat(target).st_mtime_ns + 10**9,) * 2)
    assert run_upscale(work, PRESERVING, None, None, None, None) == {"up to date": 0, "brightness only": 1}
    assert channel_means(Image.open(target)) == (100.0, 100.0, 100.0)
    [image] = load_image_infos(work)
    assert load_upscaled_info(work, image).brightness == [2.0, 2.0, 2.0]
    assert run_upscale(work, PRESERVING, None, None, None, None) == {"up to date": 1, "brightness only": 0}


def test_a_changed_rule_option_is_redone(tmp_path):
    work = make_work(tmp_path)
    run_upscale(work, PLAIN, None, None, None, None)
    softened = {"default": {"backend": "nearest", "scale": 2, "soften": 0.5}}
    assert run_upscale(work, softened, None, None, None, None)["nearest 2x"] == 1
    assert load_upscaled_info(work, load_image_infos(work, None)[0]).soften == 0.5


def test_seam_blend_upscales_the_eased_tile(tmp_path):
    work = tmp_path / "work"
    save_image_info(work, WorkImage("tile", "bitmaps", 4, 1, "deadbeef", False))
    tile = Image.new("RGB", (4, 1))
    for x, value in enumerate([0, 0, 200, 200]):
        tile.putpixel((x, 0), (value, value, value))
    tile.save(images_dir(work, "bitmaps") / "tile.png")
    config = {"default": {"backend": "nearest", "scale": 2, "seam_blend": 1}}
    run_upscale(work, config, None, None, None, None)
    with Image.open(upscaled_dir(work, "bitmaps") / "tile.png") as upscaled:
        row = [upscaled.getpixel((x, 0))[0] for x in range(8)]
    assert row[:2] == [50, 50] and row[6:] == [150, 150]  # Each edge moves halfway towards the other.
    assert row[2:6] == [0, 0, 200, 200]  # The middle is left alone.
