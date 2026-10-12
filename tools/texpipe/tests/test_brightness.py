from PIL import Image

from texpipe.brightness import channel_means, match_brightness


def test_restores_the_mean_of_each_channel():
    source = Image.new("RGB", (2, 2), (100, 50, 200))
    darker = Image.new("RGB", (4, 4), (80, 40, 160))
    result, factors = match_brightness(source, darker, 3.0)
    assert channel_means(result) == (100.0, 50.0, 200.0)
    assert [round(factor, 2) for factor in factors] == [1.25, 1.25, 1.25]


def test_ignores_transparent_pixels():
    source = Image.new("RGBA", (2, 1), (100, 100, 100, 255))
    source.putpixel((1, 0), (0, 0, 0, 0))
    upscaled = Image.new("RGBA", (4, 2), (50, 50, 50, 255))
    upscaled.paste((0, 0, 0, 0), (2, 0, 4, 2))
    result, factors = match_brightness(source, upscaled, 3.0)
    assert factors == (2.0, 2.0, 2.0)
    assert result.getpixel((0, 0)) == (100, 100, 100, 255)
    assert result.getpixel((3, 0)) == (0, 0, 0, 0)


def test_an_already_matching_image_is_unchanged():
    image = Image.new("RGB", (2, 2), (10, 20, 30))
    result, factors = match_brightness(image, image.resize((4, 4)), 3.0)
    assert factors == (1.0, 1.0, 1.0)
    assert result.tobytes() == image.resize((4, 4)).tobytes()


def test_caps_the_factor_of_a_nearly_black_channel():
    # A channel the model left nearly black must not have its few stray values blown up into speckles.
    source = Image.new("RGB", (2, 2), (40, 100, 100))
    upscaled = Image.new("RGB", (4, 4), (0, 100, 100))
    upscaled.putpixel((0, 0), (2, 100, 100))
    result, factors = match_brightness(source, upscaled, 3.0)
    assert factors[0] == 3.0
    assert result.getpixel((0, 0)) == (6, 100, 100)
