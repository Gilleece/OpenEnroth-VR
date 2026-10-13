import pytest
from PIL import Image

from texpipe import imageops


def make_image(width, height, mode="RGB"):
    # Every pixel gets a distinct color so that misplaced pixels show.
    image = Image.new(mode, (width, height))
    for y in range(height):
        for x in range(width):
            color = (x * 40, y * 40, 7)
            image.putpixel((x, y), color + (255,) if mode == "RGBA" else color)
    return image


def test_wrap_pad_takes_pixels_from_the_opposite_edge():
    image = make_image(3, 2)
    padded = imageops.wrap_pad(image, 1)
    assert padded.size == (5, 4)
    assert padded.getpixel((0, 0)) == image.getpixel((2, 1))
    assert padded.getpixel((4, 1)) == image.getpixel((0, 0))
    assert padded.getpixel((1, 3)) == image.getpixel((0, 0))


def test_wrap_pad_border_wider_than_the_image():
    image = make_image(2, 2)
    padded = imageops.wrap_pad(image, 5)
    assert padded.size == (12, 12)
    for y in range(12):
        for x in range(12):
            assert padded.getpixel((x, y)) == image.getpixel(((x - 5) % 2, (y - 5) % 2))


@pytest.mark.parametrize("border", [1, 4, 7])
def test_crop_border_undoes_wrap_pad(border):
    image = make_image(4, 3)
    assert imageops.crop_border(imageops.wrap_pad(image, border), border).tobytes() == image.tobytes()


def test_crop_border_after_scaling():
    image = make_image(4, 3)
    scaled = imageops.wrap_pad(image, 2).resize((16, 14), Image.Resampling.NEAREST)
    expected = image.resize((8, 6), Image.Resampling.NEAREST)
    assert imageops.crop_border(scaled, 4).tobytes() == expected.tobytes()


def test_split_and_merge_alpha_round_trip():
    image = make_image(3, 3, "RGBA")
    image.putpixel((1, 1), (10, 20, 30, 0))
    image.putpixel((2, 0), (40, 50, 60, 128))
    rgb, alpha = imageops.split_alpha(image)
    assert rgb.mode == "RGB"
    assert alpha.mode == "L"
    assert alpha.getpixel((1, 1)) == 0
    assert imageops.merge_alpha(rgb, alpha).tobytes() == image.tobytes()


def test_split_alpha_of_an_opaque_image_has_no_alpha():
    rgb, alpha = imageops.split_alpha(make_image(2, 2, "RGBA"))
    assert alpha is None
    assert imageops.merge_alpha(rgb, None).getpixel((1, 1))[3] == 255


def test_merge_alpha_rejects_a_different_size():
    with pytest.raises(AssertionError):
        imageops.merge_alpha(make_image(2, 2), Image.new("L", (3, 3)))


def test_color_key_makes_only_the_exact_color_transparent():
    image = Image.new("RGB", (2, 1))
    image.putpixel((0, 0), (0, 252, 252))
    image.putpixel((1, 0), (0, 252, 251))
    keyed = imageops.color_key(image, (0, 252, 252))
    assert keyed.getpixel((0, 0))[3] == 0
    assert keyed.getpixel((1, 0)) == (0, 252, 251, 255)


def test_bleed_transparent_averages_the_opaque_neighbours():
    image = Image.new("RGBA", (3, 1), (0, 0, 0, 0))
    image.putpixel((0, 0), (100, 0, 0, 255))
    image.putpixel((2, 0), (0, 200, 0, 255))
    assert imageops.bleed_transparent(image).getpixel((1, 0)) == (50, 100, 0, 0)


def test_clamp_pad_repeats_the_edge_pixels():
    image = make_image(3, 2)
    padded = imageops.clamp_pad(image, 2, 1)
    assert padded.size == (7, 4)
    assert padded.getpixel((0, 0)) == image.getpixel((0, 0))
    assert padded.getpixel((6, 3)) == image.getpixel((2, 1))
    assert padded.getpixel((3, 0)) == image.getpixel((1, 0))
    assert imageops.crop_border(padded, 2, 1).tobytes() == image.tobytes()


def test_clamp_pad_with_no_border_is_a_copy():
    image = make_image(3, 2)
    assert imageops.clamp_pad(image, 0, 0).tobytes() == image.tobytes()


def test_fill_transparent_reaches_past_the_first_ring():
    # Shore tiles were black beyond one pixel from the opaque area, and the upscaler pulled that black into the edge.
    image = Image.new("RGBA", (5, 1), (0, 0, 0, 0))
    image.putpixel((0, 0), (200, 100, 50, 255))
    filled = imageops.fill_transparent(image)
    assert [filled.getpixel((x, 0)) for x in range(5)] == [(200, 100, 50, 255)] + [(200, 100, 50, 0)] * 4


def test_fill_transparent_averages_the_neighbours_that_have_a_color():
    image = Image.new("RGBA", (3, 1), (0, 0, 0, 0))
    image.putpixel((0, 0), (100, 0, 0, 255))
    image.putpixel((2, 0), (200, 0, 0, 255))
    assert imageops.fill_transparent(image).getpixel((1, 0)) == (150, 0, 0, 0)


def test_fill_transparent_leaves_a_fully_transparent_image_black():
    image = Image.new("RGBA", (2, 2), (9, 9, 9, 0))
    assert imageops.fill_transparent(image).getpixel((1, 1)) == (9, 9, 9, 0)


def test_blend_wrap_seam_eases_both_axes():
    image = Image.new("RGB", (4, 4), (0, 0, 0))
    for y in range(4):
        image.putpixel((3, y), (120, 120, 120))
    image.putpixel((1, 3), (120, 120, 120))
    blended = imageops.blend_wrap_seam(image, 1)
    assert (blended.getpixel((0, 1)), blended.getpixel((3, 1))) == ((30, 30, 30), (90, 90, 90))
    assert (blended.getpixel((1, 0)), blended.getpixel((1, 3))) == ((30, 30, 30), (90, 90, 90))
    assert blended.getpixel((1, 1)) == (0, 0, 0)


def test_soften_of_a_flat_image_changes_nothing_and_keeps_alpha():
    image = Image.new("RGBA", (6, 6), (80, 90, 100, 255))
    image.putpixel((2, 2), (80, 90, 100, 0))
    softened = imageops.soften(image, 0.5, 1.0, tileable=True)
    assert softened.getpixel((0, 0)) == (80, 90, 100, 255)
    assert softened.getpixel((2, 2))[3] == 0


def test_soften_of_a_tile_blurs_across_its_edges():
    image = Image.new("RGB", (8, 8), (0, 0, 0))
    for y in range(8):
        image.putpixel((7, y), (255, 255, 255))
    softened = imageops.soften(image, 1.0, 1.0, tileable=True)
    assert softened.getpixel((0, 4))[0] > 0


def test_matching_edges_tells_a_transition_tile_from_a_tile():
    tile = Image.new("RGB", (8, 8), (100, 100, 100))
    for x in range(8):
        tile.putpixel((x, 3), (110, 110, 110))
    assert imageops.matching_edges(tile, 1.5) == (True, True)
    transition = tile.copy()
    for y in range(8):
        transition.putpixel((7, y), (250, 250, 250))  # Snow on the east edge only.
    assert imageops.matching_edges(transition, 1.5) == (False, True)


@pytest.mark.parametrize("wraps", [(True, True), (True, False), (False, True), (False, False)])
def test_tile_pad_wraps_or_copies_each_axis(wraps):
    image = make_image(4, 3)
    padded = imageops.tile_pad(image, 2, *wraps)
    assert padded.size == (8, 7)
    assert imageops.crop_border(padded, 2).tobytes() == image.tobytes()
    left = padded.getpixel((1, 3))
    assert left == (image.getpixel((3, 1)) if wraps[0] else image.getpixel((0, 1)))
    top = padded.getpixel((3, 1))
    assert top == (image.getpixel((1, 2)) if wraps[1] else image.getpixel((1, 0)))
