import json
import struct
import zipfile
import zlib

from PIL import Image

from texpipe import lod
from texpipe.cli import main
from texpipe.crc import crc32_hex
from texpipe.pack import write_archive
from texpipe.verify import verify_pack


def lod_bytes(entries: dict[str, bytes]) -> bytes:
    # A one directory LOD archive laid out like the games' own.
    header = (
        b"LOD\0" + b"MMVII".ljust(80, b"\0") + b"test".ljust(80, b"\0") + struct.pack("<III", 100, 0, 1) + bytes(80)
    )
    table, data = b"", b""
    offset = len(entries) * 32
    for name, raw in entries.items():
        table += name.encode().ljust(16, b"\0") + struct.pack("<IIIHH", offset + len(data), len(raw), 0, 0, 0)
        data += raw
    root = b"root".ljust(16, b"\0") + struct.pack("<IIIHH", 288, len(table) + len(data), 0, len(entries), 0)
    return header + root + table + data


def lod_image(name: str, width: int, height: int) -> bytes:
    header = name.encode().ljust(16, b"\0") + struct.pack("<IIHH", width * height, width * height, width, height)
    return header.ljust(48, b"\0") + bytes(width * height) + bytes(768)


def lod_sprite(name: str, width: int, height: int) -> bytes:
    return name.encode().ljust(12, b"\0") + struct.pack("<IHH", 0, width, height) + bytes(12)


def compressed_pcx(width: int, height: int) -> bytes:
    pcx = struct.pack("<bbbbhhhh", 10, 5, 1, 8, 0, 0, width - 1, height - 1).ljust(128, b"\0") + bytes(width * height)
    packed = zlib.compress(pcx)
    return struct.pack("<I4sII", 91969, b"mvii", len(packed), len(pcx)) + packed


def make_game_and_pack(tmp_path, wall_size=(8, 4), shape_size=(4, 6)):
    data = tmp_path / "mm7"
    (data / "DATA").mkdir(parents=True)
    wall, frame, gob = lod_image("wall", 4, 2), compressed_pcx(3, 3), lod_sprite("gob", 2, 3)
    (data / "DATA" / "BITMAPS.LOD").write_bytes(lod_bytes({"Wall": wall}))
    (data / "DATA" / "icons.lod").write_bytes(lod_bytes({"frame.pcx": frame}))
    (data / "DATA" / "sprites.lod").write_bytes(lod_bytes({"gob": gob}))
    pack = tmp_path / "pack"
    manifest = {
        "format": 1,
        "colors": "raw",
        "bitmaps": {"wall": {"crc32": crc32_hex(wall), "maps": ["normal"]}},
        "icons": {"frame.pcx": {"crc32": crc32_hex(frame)}},
        "sprites": {"gob": {"crc32": crc32_hex(gob), "palettes": [3], "shape": {"thickness": 8.0}}},
    }
    images = {
        "bitmaps/wall.png": wall_size,
        "normal/bitmaps/wall.png": wall_size,
        "icons/frame.pcx.png": (12, 12),
        "sprites/gob/3.png": (4, 6),
        "shape/sprites/gob.png": shape_size,
    }
    for relative, size in images.items():
        (pack / relative).parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGBA", size).save(pack / relative)
    (pack / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    (pack / "README.txt").write_text("readme", encoding="utf-8")
    return data, pack


def test_lod_reader_finds_entries_case_insensitively(tmp_path):
    path = tmp_path / "a.lod"
    path.write_bytes(lod_bytes({"Wall": b"abc", "wall": b"second"}))
    archive = lod.Lod(path)
    assert "WALL" in archive and archive.read("wall") == b"abc"  # The first of duplicates wins, as in the engine.


def test_a_good_pack_passes(tmp_path):
    data, pack = make_game_and_pack(tmp_path)
    report = verify_pack(pack, data)
    assert report.failures == []
    assert report.entries == {"bitmaps": 1, "icons": 1, "sprites": 1}
    assert report.files == 5


def test_wrong_sizes_fail(tmp_path):
    data, pack = make_game_and_pack(tmp_path, wall_size=(12, 6), shape_size=(2, 3))
    failures = verify_pack(pack, data).failures
    assert any(failure.startswith("bitmaps/wall.png: 12x6, must be 4x2 times") for failure in failures)
    assert any(failure.startswith("shape/sprites/gob.png: 2x3, must be 4x6") for failure in failures)


def test_a_changed_lod_entry_and_a_missing_file_fail(tmp_path):
    data, pack = make_game_and_pack(tmp_path)
    (data / "DATA" / "BITMAPS.LOD").write_bytes(lod_bytes({"wall": lod_image("wall", 4, 3)}))
    (pack / "sprites" / "gob" / "3.png").unlink()
    failures = verify_pack(pack, data).failures
    assert any("bitmaps/wall: made from a different LOD entry" in failure for failure in failures)
    assert "sprites/gob/3.png: missing or not an image" in failures


def test_an_unknown_manifest_format_fails(tmp_path):
    data, pack = make_game_and_pack(tmp_path)
    (pack / "manifest.json").write_text(json.dumps({"format": 2}), encoding="utf-8")
    assert verify_pack(pack, data).failures == ["manifest.json: format 2, the engine reads format 1"]


def test_archive_puts_the_readme_first_and_is_reproducible(tmp_path):
    _, pack = make_game_and_pack(tmp_path)
    first, second = tmp_path / "a.zip", tmp_path / "b.zip"
    size = write_archive(pack, first)
    write_archive(pack, second)
    assert size == first.stat().st_size
    assert first.read_bytes() == second.read_bytes()
    with zipfile.ZipFile(first) as archive:
        names = archive.namelist()
        assert names[0] == "pack/README.txt"
        assert names[1:] == sorted(names[1:])
        assert archive.read("pack/README.txt") == b"readme"


def test_verify_command_exits_non_zero_on_a_failure(tmp_path, capsys):
    data, pack = make_game_and_pack(tmp_path)
    assert main(["verify", "--pack", str(pack), "--data", str(data)]) == 0
    (pack / "icons" / "frame.pcx.png").unlink()
    assert main(["verify", "--pack", str(pack), "--data", str(data)]) == 1
    assert "icons/frame.pcx.png: missing or not an image" in capsys.readouterr().out
