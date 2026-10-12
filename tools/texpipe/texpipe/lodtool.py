"""Runs the engine's LodTool, which is the only thing here that reads LOD archives."""

import os
import shutil
import subprocess
from pathlib import Path

LODTOOL_ENV = "TEXPIPE_LODTOOL"


def resolve_lodtool(path: str | None) -> Path:
    """Finds the LodTool executable from the command line, then the environment, then PATH."""
    candidate = path or os.environ.get(LODTOOL_ENV) or shutil.which("LodTool")
    if not candidate or not Path(candidate).is_file():
        raise FileNotFoundError(
            f"LodTool not found. Build the LodTool target and pass --lodtool, or set {LODTOOL_ENV}."
        )
    return Path(candidate)


def find_lod(data_dir: Path, file_name: str) -> Path:
    """Finds `file_name` in the game's DATA folder, ignoring case, as the game data ships with mixed case names."""
    for folder in (data_dir / "DATA", data_dir / "data", data_dir):
        if not folder.is_dir():
            continue
        for entry in folder.iterdir():
            if entry.name.lower() == file_name.lower():
                return entry
    raise FileNotFoundError(f"No {file_name} in {data_dir}.")


def extract(lodtool: Path, lod: Path, out_dir: Path, raw: bool) -> None:
    """Extracts every entry of `lod` into `out_dir`. Without `raw`, images are converted to PNG."""
    command = [str(lodtool), "extract"]
    if raw:
        command.append("--raw")
    command += [str(lod), str(out_dir)]
    out_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
