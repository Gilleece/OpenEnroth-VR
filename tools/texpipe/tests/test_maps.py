import pytest
from PIL import Image

from texpipe.maps import MapOptions, map_path, normal_map, roughness_map, save_map

OPTIONS = MapOptions(strength=2.0, blur=0.0, wrap_border=4)


def vertical_edge(width=16, height=8, left=40, right=200):
    # Dark left half, bright right half: the surface rises to the right.
    image = Image.new("RGB", (width, height), (left,) * 3)
    image.paste((right,) * 3, (width // 2, 0, width, height))
    return image


def test_flat_input_gives_a_flat_normal_and_baseline_roughness():
    flat = Image.new("RGB", (8, 6), (100, 120, 140))
    assert {color for _, color in normal_map(flat, OPTIONS).getcolors()} == {(128, 128, 255)}
    assert {value for _, value in roughness_map(flat, OPTIONS).getcolors()} == {90}


def test_maps_have_the_size_of_the_color_image():
    image = vertical_edge(10, 6)
    assert normal_map(image, OPTIONS).size == (10, 6)
    assert normal_map(image, OPTIONS).mode == "RGB"
    assert roughness_map(image, OPTIONS).size == (10, 6)
    assert roughness_map(image, OPTIONS).mode == "L"


def test_a_vertical_edge_tilts_the_normal_away_from_the_rise():
    normal = normal_map(vertical_edge(), OPTIONS)
    red, green, blue = normal.getpixel((8, 4))  # First bright column, the surface rises to its left side.
    assert red < 128
    assert green == 128
    assert blue < 255
    assert normal.getpixel((4, 4)) == (128, 128, 255)  # Inside the flat dark half.


def test_an_edge_rising_towards_the_top_row_tilts_the_normal_down():
    image = vertical_edge().rotate(-90, expand=True)  # Bright half on top after the rotation.
    red, green, _ = normal_map(image, OPTIONS).getpixel((4, 7))
    assert red == 128
    assert green < 128


def test_wrap_padding_keeps_the_tiling_seam_continuous():
    # The right edge meets the left one when tiled, so the seam is an edge too, rising back to the left.
    normal = normal_map(vertical_edge(), OPTIONS)
    assert normal.getpixel((0, 4))[0] > 128
    assert normal.getpixel((15, 4))[0] > 128
    tiled = Image.new("RGB", (32, 8))
    tiled.paste(vertical_edge(), (0, 0))
    tiled.paste(vertical_edge(), (16, 0))
    assert normal_map(tiled, OPTIONS).crop((0, 0, 16, 8)).tobytes() == normal.tobytes()


def test_busier_areas_are_rougher():
    image = Image.new("RGB", (16, 16), (128, 128, 128))
    for x in range(0, 8, 2):
        for y in range(16):
            image.putpixel((x, y), (255, 255, 255))
    roughness = roughness_map(image, OPTIONS)
    assert roughness.getpixel((3, 8)) > 90
    assert roughness.getpixel((12, 8)) < 90


@pytest.mark.parametrize("mode", ["RGB", "L"])
def test_saved_maps_have_no_color_space_chunks(tmp_path, mode):
    image = Image.new(mode, (4, 4))
    image.info = {"gamma": 0.45455, "icc_profile": b"profile"}
    path = map_path(tmp_path, "normal", "wall")
    save_map(image, path)
    data = path.read_bytes()
    assert b"gAMA" not in data
    assert b"iCCP" not in data
    assert b"sRGB" not in data
