import fnmatch
import json

from PIL import Image

from tests.test_regen import FakeEndpoint, make_work
from texpipe.backends.imageedit import ImageEditClient
from texpipe.config import load_config
from texpipe.regen import RegenOptions, run_regen
from texpipe.regen_icons import icon_options, icon_output_path, icon_record_path, run_icons
from texpipe.workdir import UpscaledImage, WorkImage, images_dir, save_image_info, save_upscaled_info, upscaled_dir


def add_icon(work, name, image):
    # An extracted and upscaled icon, the upscale being the image itself.
    info = WorkImage(name, "icons", image.width, image.height, "deadbeef", False)
    save_image_info(work, info)
    image.save(images_dir(work, "icons") / f"{name}.png")
    upscaled_dir(work, "icons").mkdir(parents=True, exist_ok=True)
    image.save(upscaled_dir(work, "icons") / f"{name}.png")
    save_upscaled_info(work, info, UpscaledImage("nearest", 1))


def test_an_opaque_icon_skips_the_silhouette_gate(tmp_path):
    work = tmp_path / "work"
    add_icon(work, "pc01-01", Image.new("RGBA", (8, 8), (90, 60, 40, 255)))
    options = RegenOptions(model="vendor/model", include=("pc*",))
    run_icons(work, options, None, ImageEditClient(FakeEndpoint(lambda image: image), key="k"))
    record = json.loads(icon_record_path(work, "vendor/model", "pc01-01").read_text())
    assert record["scores"]["iou"] is None
    assert record["passed"]
    assert icon_output_path(work, "vendor/model", "pc01-01").is_file()


def test_a_transparent_icon_is_held_to_its_silhouette(tmp_path):
    work = tmp_path / "work"
    icon = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    icon.paste((90, 200, 40, 255), (4, 4, 12, 12))
    add_icon(work, "ib-frame", icon)
    options = RegenOptions(model="vendor/model", include=("ib-*",))
    filled = FakeEndpoint(lambda image: Image.new("RGB", image.size, (90, 200, 40)))
    run_icons(work, options, None, ImageEditClient(filled, key="k"))
    record = json.loads(icon_record_path(work, "vendor/model", "ib-frame").read_text())
    assert record["scores"]["iou"] < 0.5
    assert not record["passed"]


def test_a_dry_run_calls_nothing_and_projects_the_cost(tmp_path):
    work, palettes = make_work(tmp_path, ("goba0", "goba1"))
    endpoint = FakeEndpoint(lambda image: image)
    options = RegenOptions(model="vendor/model", cost_per_image=0.05)
    result = run_regen(work, palettes, options, None, ImageEditClient(endpoint, key="k"), dry=True)
    assert result["would_send"] == 2
    assert result["projected_cost"] == 0.1
    assert not endpoint.calls


def test_the_default_config_keeps_effects_super_resolution_only():
    options = RegenOptions.from_config(load_config(None))

    def excluded(name):
        return any(fnmatch.fnmatchcase(name, glob) for glob in options.exclude)

    assert excluded("sp07h10") and excluded("spell01a") and excluded("torch01a") and excluded("splash1")
    assert not excluded("m246aa0") and not excluded("tree01") and not excluded("obj001")


def test_icon_options_fall_back_to_the_sprite_options():
    options = icon_options(load_config(None))
    assert options.model == RegenOptions.from_config(load_config(None)).model
    assert options.include == ("pc*", "npc*", "ib-*")
