import json
import struct
from pathlib import Path

import pytest

from tests.test_verify import lod_bytes
from texpipe.cli import main
from texpipe.config import LITE_CONFIG, load_config
from texpipe.crc import crc32_hex
from texpipe.gi import read_header, run_bake
from texpipe.pack import run_pack
from texpipe.verify import verify_pack

INDOOR_MAP = b"indoor map data"
OUTDOOR_MAP = b"outdoor map data"
OUTDOOR_HOURS = (0.0, 4.0, 8.0, 12.0, 16.0, 20.0, 22.0)


def gi_bytes(layout=0, dims=(2, 3, 1), hours=(0.0,), heights=(), magic=b"OEGI", version=1, extra=b""):
    # A hand-written probe file, every coefficient 1.0 in f16.
    data = struct.pack("<4sII3f3f3II", magic, version, layout, 0, 0, 0, 64, 64, 64, *dims, len(hours))
    data += struct.pack(f"<{len(hours)}f", *hours)
    data += struct.pack(f"<{len(hours) * 3}f", *([0.5] * len(hours) * 3))
    data += struct.pack(f"<{len(heights)}f", *heights)
    probes = dims[0] * dims[1] * dims[2]
    data += struct.pack("<e", 1.0) * (12 * probes * len(hours))
    return data + extra


def outdoor_gi_bytes():
    return gi_bytes(layout=1, dims=(2, 2, 2), hours=OUTDOOR_HOURS, heights=(100.0, 400.0))


def make_game(tmp_path: Path) -> Path:
    data = tmp_path / "mm7"
    (data / "DATA").mkdir(parents=True)
    entries = {"d18.blv": INDOOR_MAP, "d18.dlv": b"delta", "out01.odm": OUTDOOR_MAP, "out01.ddm": b"delta"}
    (data / "DATA" / "games.lod").write_bytes(lod_bytes(entries))
    return data


class FakeEngine:
    """Stands in for 'OpenEnroth bake-gi': writes the probe file and the report the way bake-gi does."""

    def __init__(self, files: dict[str, bytes], code=0, crcs: dict[str, str] | None = None):
        self.files = files
        self.code = code
        self.crcs = crcs or {"d18.blv": crc32_hex(INDOOR_MAP), "out01.odm": crc32_hex(OUTDOOR_MAP)}
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str]) -> int:
        self.commands.append(command)
        out = Path(command[command.index("--out") + 1])
        name = command[command.index("--map") + 1]
        data = self.files[name]
        (out / "gi" / f"{name}.bin").write_bytes(data)
        probes = struct.unpack_from("<3I", data, 36)
        report = {
            "maps": {
                name: {
                    "file": f"gi/{name}.bin",
                    "crc32": self.crcs[name],
                    "probes": probes[0] * probes[1] * probes[2],
                    "seconds": 2.5,
                }
            }
        }
        Path(command[command.index("--report") + 1]).write_text(json.dumps(report))
        return self.code


def test_reads_indoor_and_outdoor_headers():
    indoor = read_header(gi_bytes())
    assert (indoor.layout, indoor.dims, indoor.probes, indoor.time_hours) == (0, (2, 3, 1), 6, (0.0,))
    assert indoor.average_irradiance == ((0.5, 0.5, 0.5),)
    outdoor = read_header(outdoor_gi_bytes())
    assert (outdoor.layout, outdoor.probes, outdoor.layer_heights) == (1, 8, (100.0, 400.0))
    assert outdoor.time_hours == OUTDOOR_HOURS


@pytest.mark.parametrize(
    "data, message",
    [
        (gi_bytes(magic=b"OEGX"), "magic"),
        (gi_bytes(version=2), "version 2"),
        (gi_bytes(layout=2), "layout 2"),
        (gi_bytes(hours=(0.0, 12.0)), "2 time keys"),
        (gi_bytes(hours=(6.0,)), "hour 0"),
        (gi_bytes(dims=(2, 0, 1)), "at least 1"),
        (gi_bytes(extra=b"\0\0"), "need"),
        (gi_bytes()[:-2], "need"),
        (gi_bytes(layout=1, dims=(1, 1, 2), hours=OUTDOOR_HOURS, heights=(400.0, 100.0)), "layer heights"),
        (gi_bytes(layout=1, dims=(1, 1, 1), hours=OUTDOOR_HOURS[::-1], heights=(0.0,)), "must rise"),
        (b"OEGI", "shorter"),
    ],
)
def test_rejects_files_that_break_the_format(data, message):
    with pytest.raises(ValueError, match=message):
        read_header(data)


def test_bakes_every_map_and_records_it_in_the_manifest(tmp_path):
    data, pack = make_game(tmp_path), tmp_path / "pack"
    engine = FakeEngine({"d18.blv": gi_bytes(), "out01.odm": outdoor_gi_bytes()})
    result = run_bake(data, pack, Path("OpenEnroth"), runner=engine)
    assert [command[command.index("--map") + 1] for command in engine.commands] == ["d18.blv", "out01.odm"]
    assert engine.commands[0][:5] == ["OpenEnroth", "--headless", "--data-path", str(data), "bake-gi"]
    assert result.failures == {}
    assert result.baked == {
        "d18.blv": (2.5, len(gi_bytes())),
        "out01.odm": (2.5, len(outdoor_gi_bytes())),
    }
    manifest = json.loads((pack / "manifest.json").read_text())
    assert manifest["gi"] == {
        "d18.blv": {"crc32": crc32_hex(INDOOR_MAP), "file": "gi/d18.blv.bin", "probes": 6},
        "out01.odm": {"crc32": crc32_hex(OUTDOOR_MAP), "file": "gi/out01.odm.bin", "probes": 8},
    }
    assert verify_pack(pack, data).failures == []


def test_a_map_baked_from_another_map_file_is_dropped(tmp_path):
    # The engine's CRC must match the games.lod entry texpipe reads, or the entry would be ignored in game.
    data, pack = make_game(tmp_path), tmp_path / "pack"
    run_bake(
        data, pack, Path("OpenEnroth"), runner=FakeEngine({"d18.blv": gi_bytes(), "out01.odm": outdoor_gi_bytes()})
    )
    engine = FakeEngine({"d18.blv": gi_bytes()}, crcs={"d18.blv": "12345678"})
    result = run_bake(data, pack, Path("OpenEnroth"), ["D18.blv"], runner=engine)
    assert "CRC-32 12345678" in result.failures["d18.blv"]
    assert not (pack / "gi" / "d18.blv.bin").exists()
    assert list(json.loads((pack / "manifest.json").read_text())["gi"]) == ["out01.odm"]


def test_failures_are_reported_per_map(tmp_path):
    data, pack = make_game(tmp_path), tmp_path / "pack"
    result = run_bake(
        data, pack, Path("OpenEnroth"), ["d18.blv", "nowhere.blv"], runner=FakeEngine({"d18.blv": gi_bytes()}, code=3)
    )
    assert result.failures == {"d18.blv": "exit code 3", "nowhere.blv": "no such map in games.lod"}
    assert json.loads((pack / "manifest.json").read_text())["gi"] == {}

    bad = run_bake(data, pack, Path("OpenEnroth"), ["d18.blv"], runner=FakeEngine({"d18.blv": gi_bytes(version=2)}))
    assert bad.failures == {"d18.blv": "version 2, must be 1"}


def test_pack_keeps_the_baked_lighting_unless_the_config_drops_it(tmp_path):
    data, pack, work = make_game(tmp_path), tmp_path / "pack", tmp_path / "work"
    work.mkdir()
    run_bake(
        data, pack, Path("OpenEnroth"), runner=FakeEngine({"d18.blv": gi_bytes(), "out01.odm": outdoor_gi_bytes()})
    )
    counts = run_pack(work, pack, "raw", None, config=load_config(LITE_CONFIG))
    assert counts["gi"] == 2
    assert list(json.loads((pack / "manifest.json").read_text())["gi"]) == ["d18.blv", "out01.odm"]
    assert verify_pack(pack, data).failures == []

    counts = run_pack(work, pack, "raw", None, config={"pack": {"gi": False}})
    assert counts["gi"] == 0
    assert "gi" not in json.loads((pack / "manifest.json").read_text())
    assert not (pack / "gi").exists()


def test_verify_checks_the_gi_entries(tmp_path):
    data, pack = make_game(tmp_path), tmp_path / "pack"
    run_bake(
        data, pack, Path("OpenEnroth"), runner=FakeEngine({"d18.blv": gi_bytes(), "out01.odm": outdoor_gi_bytes()})
    )
    manifest = json.loads((pack / "manifest.json").read_text())
    manifest["gi"]["d18.blv"]["crc32"] = "00000000"
    manifest["gi"]["out01.odm"]["probes"] = 9
    manifest["gi"]["d01.blv"] = {"crc32": "00000000", "file": "gi/d01.blv.bin", "probes": 1}
    (pack / "manifest.json").write_text(json.dumps(manifest))
    report = verify_pack(pack, data)
    assert report.entries["gi"] == 3
    assert report.failures == [
        "gi/d01.blv: no such map in games.lod",
        f"gi/d18.blv: baked from a different map, CRC-32 00000000 against {crc32_hex(INDOOR_MAP)}",
        "gi/out01.odm.bin: the manifest says 9 probes, the file has 8",
    ]


def test_bake_command_line(tmp_path, monkeypatch, capsys):
    data, pack = make_game(tmp_path), tmp_path / "pack"
    engine = tmp_path / "OpenEnroth.exe"
    engine.write_bytes(b"")
    fake = FakeEngine({"d18.blv": gi_bytes()})
    monkeypatch.setattr("texpipe.gi.run_engine", fake)
    assert main(["bake", "--data", str(data), "--out", str(pack), "--engine", str(engine), "--map", "d18.blv"]) == 0
    output = capsys.readouterr().out
    assert "Baked d18.blv in 2.5 s" in output
    assert "Baked 1 maps into" in output
