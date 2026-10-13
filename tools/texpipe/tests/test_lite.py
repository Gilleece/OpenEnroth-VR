import json

from PIL import Image

from texpipe.config import LITE_CONFIG, load_config
from texpipe.maps import map_path
from texpipe.pack import run_pack
from texpipe.workdir import UpscaledImage, WorkImage, images_dir, save_image_info, save_upscaled_info, upscaled_dir


def test_lite_profile_scales_bitmaps_down_and_remakes_their_maps(tmp_path):
    work, out = tmp_path / "work", tmp_path / "pack"
    info = WorkImage("wall", "bitmaps", 4, 2, "deadbeef", False)
    save_image_info(work, info)
    Image.new("RGB", (4, 2)).save(images_dir(work, "bitmaps") / "wall.png")
    upscaled_dir(work, "bitmaps").mkdir(parents=True)
    wall = Image.new("RGB", (16, 8), (200, 100, 50))
    wall.putpixel((3, 3), (0, 0, 0))
    wall.save(upscaled_dir(work, "bitmaps") / "wall.png")
    save_upscaled_info(work, info, UpscaledImage("realesrgan", 4))
    for kind in ("normal", "roughness"):
        map_path(work, kind, "wall").parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGB", (16, 8)).save(map_path(work, kind, "wall"))
    config = load_config(LITE_CONFIG)
    assert config["default"]["scale"] == 2
    run_pack(work, out, "raw", None, config=config)
    for relative in ("bitmaps/wall.png", "normal/bitmaps/wall.png", "roughness/bitmaps/wall.png"):
        assert Image.open(out / relative).size == (8, 4)
    assert json.loads((out / "manifest.json").read_text())["bitmaps"]["wall"]["maps"] == ["normal", "roughness"]
    run_pack(work, tmp_path / "full", "raw", None, config=load_config(None))
    assert Image.open(tmp_path / "full" / "bitmaps" / "wall.png").size == (16, 8)
