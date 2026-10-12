import struct

from texpipe.lodimage import HEADER_SIZE, LodImageInfo, read_lod_image_info
from texpipe.masks import decide_lod_mask, decide_pcx_mask

PALETTE = ((0, 252, 252),) + tuple((i, i, i) for i in range(1, 256))


def make_raw_image(flags, pixel_bytes=b"\x01\x02\x03\x04"):
    # A raw LOD image entry: header, pixel data and the 256 color palette.
    header = bytearray(HEADER_SIZE)
    struct.pack_into("<II", header, 16, len(pixel_bytes), len(pixel_bytes))
    struct.pack_into("<HH", header, 24, 2, 2)
    struct.pack_into("<I", header, 44, flags)
    return bytes(header) + pixel_bytes + bytes(c for color in PALETTE for c in color)


def test_reads_the_flag_and_palette():
    info = read_lod_image_info(make_raw_image(0x200))
    assert info.zero_is_transparent
    assert info.palette[0] == (0, 252, 252)
    assert not read_lod_image_info(make_raw_image(0x002)).zero_is_transparent


def test_rejects_a_blob_that_is_not_an_image():
    assert read_lod_image_info(make_raw_image(0)[:-1]) is None
    assert read_lod_image_info(b"PCX") is None


def test_lod_mask_follows_the_flag_by_default():
    assert decide_lod_mask("default", LodImageInfo(True, PALETTE)).color == (0, 252, 252)
    assert decide_lod_mask("default", LodImageInfo(False, PALETTE)).color is None


def test_lod_mask_overrides():
    assert decide_lod_mask("none", LodImageInfo(True, PALETTE)).color is None
    assert decide_lod_mask("zero", LodImageInfo(False, PALETTE)).color == (0, 252, 252)
    assert decide_lod_mask("#102030", LodImageInfo(True, PALETTE)).color == (16, 32, 48)


def test_lod_mask_flags_a_zero_color_that_other_entries_share():
    palette = ((5, 5, 5),) + PALETTE[1:]
    decision = decide_lod_mask("zero", LodImageInfo(False, palette))
    assert decision.ambiguous


def test_pcx_mask_only_takes_colors():
    assert decide_pcx_mask("zero").color is None
    assert decide_pcx_mask("#000000").color == (0, 0, 0)
