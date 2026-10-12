import zlib


def crc32_hex(data: bytes) -> str:
    """Returns the CRC-32 of `data` as 8 lowercase hex digits, the form the pack manifest uses."""
    return f"{zlib.crc32(data):08x}"
