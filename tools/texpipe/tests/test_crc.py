from texpipe.crc import crc32_hex


def test_check_value():
    # The CRC-32 check value from the algorithm's specification.
    assert crc32_hex(b"123456789") == "cbf43926"


def test_keeps_leading_zeros():
    assert crc32_hex(b"") == "00000000"
    assert len(crc32_hex(b"a")) == 8
