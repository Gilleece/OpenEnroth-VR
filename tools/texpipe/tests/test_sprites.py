import os

from PIL import Image

from texpipe.sprites import apply_palette, extract_sprite, sprite_image_name
from texpipe.workdir import images_dir, is_up_to_date, load_image_infos

PALETTE = bytes(value for index in range(256) for value in (index, 255 - index, 7))


def index_image(rows):
    image = Image.new("L", (len(rows[0]), len(rows)))
    image.putdata([index for row in rows for index in row])
    return image


def test_palette_colors_and_index_zero_is_transparent():
    image = apply_palette(index_image([[0, 10, 20]]), PALETTE)
    assert image.mode == "RGBA"
    assert image.getpixel((1, 0)) == (10, 245, 7, 255)
    assert image.getpixel((2, 0)) == (20, 235, 7, 255)
    assert image.getpixel((0, 0))[3] == 0


def test_transparent_pixels_take_the_average_of_their_opaque_neighbours():
    image = apply_palette(index_image([[10, 0, 30], [0, 0, 0]]), PALETTE)
    assert image.getpixel((1, 0)) == (20, 235, 7, 0)
    assert image.getpixel((1, 1)) == (20, 235, 7, 0)
    assert image.getpixel((0, 1)) == (10, 245, 7, 0)


def write_inputs(folder):
    # A raw entry, LodTool's index PNG of it and one palette PNG, as extract finds them.
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "gob").write_bytes(b"raw sprite")
    index_image([[0, 5], [6, 0]]).save(folder / "gob.png")
    palette = Image.frombytes("RGB", (256, 1), PALETTE)
    palette.save(folder / "pal247.png")
    return folder / "gob", folder / "gob.png", {247: folder / "pal247.png"}


def test_extract_sprite_writes_one_image_per_palette(tmp_path):
    raw, png, palettes = write_inputs(tmp_path / "in")
    work = tmp_path / "work"
    assert extract_sprite(work, "gob", raw, png, palettes, [], force=False) == 1
    assert sprite_image_name("gob", 247) == "gob/247"
    assert (images_dir(work, "sprites") / "gob" / "247.png").is_file()
    [info] = load_image_infos(work)
    assert (info.name, info.section, info.width, info.height, info.transparent) == ("gob/247", "sprites", 2, 2, True)


def test_extract_sprite_skips_up_to_date_images(tmp_path):
    raw, png, palettes = write_inputs(tmp_path / "in")
    work = tmp_path / "work"
    extract_sprite(work, "gob", raw, png, palettes, [], force=False)
    assert extract_sprite(work, "gob", raw, png, palettes, [], force=False) == 0
    assert extract_sprite(work, "gob", raw, png, palettes, [], force=True) == 1
    future = os.stat(raw).st_mtime_ns + 10**9
    os.utime(raw, ns=(future, future))
    assert extract_sprite(work, "gob", raw, png, palettes, [], force=False) == 1


def test_up_to_date_needs_every_output_newer_than_every_input(tmp_path):
    source, target = tmp_path / "source", tmp_path / "target"
    source.write_text("a")
    assert not is_up_to_date([target], [source])
    target.write_text("b")
    os.utime(target, ns=(os.stat(source).st_mtime_ns + 10**9, os.stat(source).st_mtime_ns + 10**9))
    assert is_up_to_date([target], [source])
    os.utime(source, ns=(os.stat(target).st_mtime_ns + 10**9, os.stat(target).st_mtime_ns + 10**9))
    assert not is_up_to_date([target], [source])
