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
