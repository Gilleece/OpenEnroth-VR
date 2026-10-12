from texpipe.config import Choice, choose, load_config


def test_default_config_sends_small_and_direction_icons_to_nearest():
    config = load_config(None)
    assert choose(config, "icons", "mapdir3", 7) == Choice("nearest", 2)
    assert choose(config, "icons", "ib-statr", 4) == Choice("nearest", 2)
    assert choose(config, "icons", "ib-b-a.pcx", 468) == Choice("realesrgan", 2)
    assert choose(config, "bitmaps", "ch1b1", 8) == Choice("realesrgan", 4)


def test_command_line_overrides_the_rules():
    assert choose(load_config(None), "icons", "mapdir3", 7, "realesrgan", 4) == Choice("realesrgan", 4)
