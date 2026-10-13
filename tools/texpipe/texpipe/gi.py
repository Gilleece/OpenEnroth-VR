"""The bake step: baked global illumination, irradiance probes per map that the engine bakes into the pack.

The engine writes one `gi/<map file>.bin` per map with `OpenEnroth bake-gi`. This module runs it map by map, checks
every file it writes against the probe file format, and records the maps in the manifest's `gi` section with the
CRC-32 of the games.lod entry they were baked from.
"""

import json
import math
import os
import shutil
import struct
import subprocess
import tempfile
import time
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

from texpipe import lod
from texpipe.crc import crc32_hex
from texpipe.lodtool import find_lod
from texpipe.workdir import write_json

ENGINE_ENV = "TEXPIPE_OPENENROTH"
GAMES_LOD = "games.lod"
MAP_SUFFIXES = (".blv", ".odm")
MAGIC = b"OEGI"
VERSION = 1
LAYOUT_GRID = 0
LAYOUT_TERRAIN = 1
LAYOUTS = (LAYOUT_GRID, LAYOUT_TERRAIN)
MAX_TIME_KEYS = 24
PROBE_SIZE = 12 * 2  # Three channels of four f16 spherical harmonics coefficients.
FIXED_HEADER = struct.Struct("<4sII3f3f3II")  # Magic, version, layout, origin, spacing, dims, time key count.


@dataclass(frozen=True)
class GiHeader:
    layout: int
    origin: tuple[float, ...]
    spacing: tuple[float, ...]
    dims: tuple[int, ...]
    time_hours: tuple[float, ...]
    average_irradiance: tuple[tuple[float, ...], ...]
    layer_heights: tuple[float, ...]  # Outdoor maps only.

    @property
    def probes(self) -> int:
        return self.dims[0] * self.dims[1] * self.dims[2]


def gi_file(map_name: str) -> str:
    """Path of a map's probe file, relative to the pack."""
    return f"gi/{map_name}.bin"


def read_header(data: bytes) -> GiHeader:
    """Parses and checks a probe file, raising ValueError if it breaks the format or its size doesn't add up."""
    if len(data) < FIXED_HEADER.size:
        raise ValueError(f"{len(data)} bytes, shorter than the header")
    magic, version, layout, *rest = FIXED_HEADER.unpack_from(data)
    origin, spacing, dims, time_count = tuple(rest[0:3]), tuple(rest[3:6]), tuple(rest[6:9]), rest[9]
    if magic != MAGIC:
        raise ValueError(f"magic {magic!r}, must be {MAGIC!r}")
    if version != VERSION:
        raise ValueError(f"version {version}, must be {VERSION}")
    if layout not in LAYOUTS:
        raise ValueError(
            f"layout {layout}, must be {LAYOUT_GRID} for a grid or {LAYOUT_TERRAIN} for layers over the terrain"
        )
    if layout == LAYOUT_GRID and time_count != 1:
        raise ValueError(f"{time_count} time keys, layout {layout} has 1")
    if layout == LAYOUT_TERRAIN and not 2 <= time_count <= MAX_TIME_KEYS:
        raise ValueError(f"{time_count} time keys, layout {layout} has 2 to {MAX_TIME_KEYS}")
    if min(dims) == 0:
        raise ValueError(f"dims {dims}, every one must be at least 1")
    spaced = spacing if layout == LAYOUT_GRID else spacing[:2]  # The layer heights place the terrain layers.
    if not all(math.isfinite(value) for value in origin + spacing) or min(spaced) <= 0:
        raise ValueError(f"origin {origin} and spacing {spacing} must be finite, with the spacing positive")

    layer_count = dims[2] if layout == LAYOUT_TERRAIN else 0
    variable = struct.Struct(f"<{time_count}f{time_count * 3}f{layer_count}f")
    if len(data) < FIXED_HEADER.size + variable.size:
        raise ValueError(f"{len(data)} bytes, shorter than the header")
    values = variable.unpack_from(data, FIXED_HEADER.size)
    hours = values[:time_count]
    irradiance = values[time_count : time_count * 4]
    heights = values[time_count * 4 :]
    if not all(math.isfinite(value) for value in values):
        raise ValueError("the time keys, average irradiance and layer heights must be finite")
    if layout == LAYOUT_GRID and hours != (0.0,):
        raise ValueError(f"time keys {hours}, the grid of an indoor map has one at hour 0")
    if any(not 0 <= hour < 24 for hour in hours) or list(hours) != sorted(set(hours)):
        raise ValueError(f"time keys {hours}, must rise from 0 to under 24")
    if min(irradiance) < 0:
        raise ValueError("average irradiance must not be negative")
    if list(heights) != sorted(set(heights)):
        raise ValueError(f"layer heights {heights}, must rise")

    header = GiHeader(
        layout,
        origin,
        spacing,
        dims,
        hours,
        tuple(tuple(irradiance[key * 3 : key * 3 + 3]) for key in range(time_count)),
        heights,
    )
    expected = FIXED_HEADER.size + variable.size + time_count * header.probes * PROBE_SIZE
    if len(data) != expected:
        raise ValueError(
            f"{len(data)} bytes, {dims[0]}x{dims[1]}x{dims[2]} probes over {time_count} time keys need {expected}"
        )
    return header


def map_names(archive: lod.Lod) -> list[str]:
    """The indoor and outdoor maps in games.lod, by entry name."""
    return [name for name in archive.names() if name.endswith(MAP_SUFFIXES)]


def resolve_engine(path: str | None) -> Path:
    """Finds the OpenEnroth executable from the command line, then the environment, then PATH."""
    candidate = path or os.environ.get(ENGINE_ENV) or shutil.which("OpenEnroth")
    if not candidate or not Path(candidate).is_file():
        raise FileNotFoundError(
            f"OpenEnroth not found. Build the OpenEnroth target and pass --engine, or set {ENGINE_ENV}."
        )
    return Path(candidate)


def bake_command(engine: Path, data: Path, out: Path, map_name: str, report: Path) -> list[str]:
    """The engine call that bakes one map into `out` and describes it in `report`."""
    return [
        str(engine),
        "--headless",
        "--data-path",
        str(data),
        "bake-gi",
        "--out",
        str(out),
        "--map",
        map_name,
        "--report",
        str(report),
    ]


def run_engine(command: list[str]) -> int:
    """Runs the engine with its log silenced and returns its exit code."""
    return subprocess.run(command, stdout=subprocess.DEVNULL, check=False).returncode


@dataclass
class BakeResult:
    baked: dict[str, tuple[float, int]] = field(default_factory=dict)  # Seconds and file bytes per map.
    failures: dict[str, str] = field(default_factory=dict)

    @property
    def total_bytes(self) -> int:
        return sum(size for _, size in self.baked.values())


def _bake_map(
    engine: Path, data: Path, out: Path, name: str, crc: str, runner: Callable[[list[str]], int]
) -> tuple[dict, float, int]:
    """Bakes one map and checks the result. Returns its manifest entry, the seconds it took and the file's size."""
    with tempfile.TemporaryDirectory() as temp:
        report_path = Path(temp) / "report.json"
        start = time.monotonic()
        code = runner(bake_command(engine, data, out, name, report_path))
        seconds = time.monotonic() - start
        try:
            report = json.loads(report_path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise ValueError(f"exit code {code} and no report, {error}") from error
    if name in report.get("failed", {}):
        raise ValueError(f"the engine failed, {report['failed'][name]}")
    if code != 0:
        raise ValueError(f"exit code {code}")
    engine_entry = report.get("maps", {}).get(name)
    if engine_entry is None:
        raise ValueError("missing from the engine's report")
    relative = gi_file(name)
    if engine_entry.get("file") != relative:
        raise ValueError(f"the engine wrote {engine_entry.get('file')}, expected {relative}")
    if str(engine_entry.get("crc32", "")).lower() != crc:
        raise ValueError(f"the engine baked from CRC-32 {engine_entry.get('crc32')}, games.lod has {crc}")
    file_data = (out / relative).read_bytes()
    header = read_header(file_data)
    if engine_entry.get("probes") != header.probes:
        raise ValueError(f"the engine reported {engine_entry.get('probes')} probes, the file has {header.probes}")
    seconds = float(engine_entry.get("seconds", seconds))  # Without the engine's startup, when it says.
    return {"crc32": crc, "file": relative, "probes": header.probes}, seconds, len(file_data)


def run_bake(
    data: Path,
    out: Path,
    engine: Path,
    maps: list[str] | None = None,
    runner: Callable[[list[str]], int] | None = None,
    progress: Callable[[str, float, int], None] | None = None,
) -> BakeResult:
    """Bakes every map of the game in `data`, or just `maps`, into the pack in `out` and updates its manifest.

    The pack step must have run already. A map that fails loses its probe file and manifest entry, so the engine never
    loads a half-written bake. Entries of maps not baked this time stay as they were. `runner` runs one engine command
    and returns its exit code, the engine itself by default, and `progress` hears of every map baked with its seconds
    and bytes.
    """
    runner = runner or run_engine
    manifest = read_manifest(out)
    archive = lod.Lod(find_lod(data, GAMES_LOD))
    available = map_names(archive)
    names = [name.lower() for name in maps] if maps else available
    result = BakeResult()
    entries: dict[str, dict] = {}
    (out / "gi").mkdir(parents=True, exist_ok=True)
    for name in names:
        if name not in available:
            result.failures[name] = f"no such map in {GAMES_LOD}"
            continue
        try:
            entry, seconds, size = _bake_map(engine, data, out, name, crc32_hex(archive.read(name)), runner)
        except (OSError, ValueError) as error:
            result.failures[name] = str(error)
            (out / gi_file(name)).unlink(missing_ok=True)
            continue
        entries[name] = entry
        result.baked[name] = (seconds, size)
        if progress:
            progress(name, seconds, size)

    gi = {name: entry for name, entry in manifest.get("gi", {}).items() if name not in result.failures}
    gi.update(entries)
    manifest["gi"] = dict(sorted(gi.items()))
    write_json(out / "manifest.json", manifest)
    return result


def read_manifest(out: Path) -> dict:
    """The manifest the pack step wrote into `out`."""
    path = out / "manifest.json"
    if not path.is_file():
        raise FileNotFoundError(f"No manifest.json in {out}, run pack first.")
    return json.loads(path.read_text(encoding="utf-8"))


def verify_gi(pack: Path, data: Path, entries: dict[str, dict], failures: list[str]) -> int:
    """Checks the manifest's `gi` entries against games.lod and their probe files, adding to `failures`.

    Returns how many probe files it read.
    """
    if not entries:
        return 0
    archive = lod.Lod(find_lod(data, GAMES_LOD))
    files = 0
    for name, entry in sorted(entries.items()):
        relative = gi_file(name)
        if name not in archive:
            failures.append(f"gi/{name}: no such map in {GAMES_LOD}")
            continue
        crc = crc32_hex(archive.read(name))
        if str(entry.get("crc32", "")).lower() != crc:
            failures.append(f"gi/{name}: baked from a different map, CRC-32 {entry.get('crc32')} against {crc}")
            continue
        if entry.get("file") != relative:
            failures.append(f"gi/{name}: file {entry.get('file')}, must be {relative}")
            continue
        files += 1
        try:
            header = read_header((pack / relative).read_bytes())
        except OSError:
            failures.append(f"{relative}: missing")
            continue
        except ValueError as error:
            failures.append(f"{relative}: {error}")
            continue
        if entry.get("probes") != header.probes:
            failures.append(f"{relative}: the manifest says {entry.get('probes')} probes, the file has {header.probes}")
    return files
