"""Command line entry point: texpipe extract | upscale | pack."""

import argparse
import time
from pathlib import Path

from texpipe import lodtool
from texpipe.backends.realesrgan import resolve_binary
from texpipe.config import SCALES, load_config
from texpipe.extract import run_extract
from texpipe.pack import COLORS, run_pack
from texpipe.upscale import BACKENDS, run_upscale

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_MASK_TABLE = REPO_ROOT / "resources" / "data" / "resource_mask_table.json"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="texpipe", description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    extract = commands.add_parser("extract", help="Dump bitmaps.lod and icons.lod as PNGs with transparency.")
    extract.add_argument("--data", type=Path, required=True, help="MM7 game folder, the one with DATA in it.")
    extract.add_argument("--out", type=Path, required=True, help="Work folder to create.")
    extract.add_argument("--lodtool", help=f"LodTool executable, defaults to ${lodtool.LODTOOL_ENV} or PATH.")
    extract.add_argument("--mask-table", type=Path, default=DEFAULT_MASK_TABLE, help="resource_mask_table.json.")

    upscale = commands.add_parser("upscale", help="Upscale the extracted images.")
    upscale.add_argument("--work", type=Path, required=True, help="Work folder made by extract.")
    upscale.add_argument("--scale", type=int, choices=SCALES, help="Scale for every image, overrides the config.")
    upscale.add_argument("--backend", choices=BACKENDS, help="Backend for every image, overrides the config.")
    upscale.add_argument("--config", type=Path, help="Config file, defaults to the bundled default_config.toml.")
    upscale.add_argument("--binary", help="realesrgan-ncnn-vulkan executable, downloaded on first use if omitted.")

    pack = commands.add_parser("pack", help="Write the texture pack from the upscaled images.")
    pack.add_argument("--work", type=Path, required=True, help="Work folder made by extract and upscale.")
    pack.add_argument("--out", type=Path, required=True, help="Pack folder to write.")
    pack.add_argument("--colors", choices=COLORS, default="raw", help="How the engine treats the pack's colors.")

    for command in (extract, upscale, pack):
        command.add_argument(
            "--only", action="append", help="Only handle LOD entries matching this glob, e.g. 'ch1*'. Repeatable."
        )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    start = time.monotonic()
    if args.command == "extract":
        result = run_extract(args.data, args.out, lodtool.resolve_lodtool(args.lodtool), args.mask_table, args.only)
        print(f"Extracted {result['extracted']}, skipped {len(result['skipped'])}, failed {len(result['failed'])}.")
        for name, reason in result["failed"].items():
            print(f"  failed {name}: {reason}")
    elif args.command == "upscale":
        config = load_config(args.config)
        counts = run_upscale(args.work, config, args.only, args.backend, args.scale, resolve_binary(args.binary))
        print(f"Upscaled {counts}.")
    else:
        print(f"Packed {run_pack(args.work, args.out, args.colors, args.only)}.")
    print(f"Took {time.monotonic() - start:.1f} s.")
    return 0
