"""Command line entry point: texpipe extract | upscale | maps | pack."""

import argparse
import json
import os
import time
from pathlib import Path

from texpipe import lodtool
from texpipe.backends.localedit import PYTHON_ENV as REGEN_PYTHON_ENV
from texpipe.backends.localedit import LocalEditClient, is_local
from texpipe.backends.realesrgan import resolve_binary
from texpipe.config import SCALES, load_config
from texpipe.depth import run_depth
from texpipe.extract import run_extract
from texpipe.maps import run_maps
from texpipe.pack import COLORS, run_pack
from texpipe.regen import RegenOptions, regen_report, rescore, run_regen
from texpipe.regen_icons import icon_options, run_icons
from texpipe.upscale import BACKENDS, run_upscale

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_MASK_TABLE = REPO_ROOT / "resources" / "data" / "resource_mask_table.json"
DEPTH_PYTHON_ENV = "TEXPIPE_DEPTH_PYTHON"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="texpipe", description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    extract = commands.add_parser("extract", help="Dump bitmaps.lod and icons.lod as PNGs with transparency.")
    extract.add_argument("--data", type=Path, required=True, help="MM7 game folder, the one with DATA in it.")
    extract.add_argument("--out", type=Path, required=True, help="Work folder to create.")
    extract.add_argument("--lodtool", help=f"LodTool executable, defaults to ${lodtool.LODTOOL_ENV} or PATH.")
    extract.add_argument("--mask-table", type=Path, default=DEFAULT_MASK_TABLE, help="resource_mask_table.json.")
    extract.add_argument("--sprites", action="store_true", help="Also extract the sprites listed in --palettes.")
    extract.add_argument("--palettes", type=Path, help="Output of 'OpenEnroth dump-sprite-palettes', for --sprites.")

    upscale = commands.add_parser("upscale", help="Upscale the extracted images.")
    upscale.add_argument("--work", type=Path, required=True, help="Work folder made by extract.")
    upscale.add_argument("--scale", type=int, choices=SCALES, help="Scale for every image, overrides the config.")
    upscale.add_argument("--backend", choices=BACKENDS, help="Backend for every image, overrides the config.")
    upscale.add_argument("--config", type=Path, help="Config file, defaults to the bundled default_config.toml.")
    upscale.add_argument("--binary", help="realesrgan-ncnn-vulkan executable, downloaded on first use if omitted.")

    maps = commands.add_parser("maps", help="Make normal and roughness maps for the upscaled bitmaps.")
    maps.add_argument("--work", type=Path, required=True, help="Work folder made by extract and upscale.")
    maps.add_argument("--config", type=Path, help="Config file, defaults to the bundled default_config.toml.")

    pack = commands.add_parser("pack", help="Write the texture pack from the upscaled images.")
    pack.add_argument("--work", type=Path, required=True, help="Work folder made by extract and upscale.")
    pack.add_argument("--out", type=Path, required=True, help="Pack folder to write.")
    pack.add_argument("--colors", choices=COLORS, default="raw", help="How the engine treats the pack's colors.")
    pack.add_argument("--regen-from", help="Take sprites and icons from this model's regen outputs that passed.")

    regen = commands.add_parser(
        "regen", help="Generative clean-up pass over the upscaled sprites, with fidelity gates."
    )
    regen.add_argument("--work", type=Path, required=True, help="Work folder with upscaled sprites.")
    regen.add_argument("--palettes", type=Path, required=True, help="Output of 'OpenEnroth dump-sprite-palettes'.")
    regen.add_argument("--model", help="Model identifier, defaults to the config's.")
    regen.add_argument("--max-cost", type=float, help="Stop before the total cost of the run goes over this.")
    regen.add_argument("--config", type=Path, help="Config file, defaults to the bundled default_config.toml.")
    regen.add_argument(
        "--rescore", action="store_true", help="Gate the stored outputs again, without calling the model."
    )
    regen.add_argument("--section", choices=("sprites", "icons"), default="sprites", help="What to regenerate.")
    regen.add_argument("--dry-run", action="store_true", help="List what would be sent and the cost, call nothing.")
    regen.add_argument(
        "--python", type=Path, help=f"Python of the local model's environment, defaults to ${REGEN_PYTHON_ENV}."
    )

    depth = commands.add_parser("depth", help="Shape images with normals and depth for the upscaled sprites.")
    depth.add_argument("--work", type=Path, required=True, help="Work folder with upscaled sprites.")
    depth.add_argument("--palettes", type=Path, required=True, help="Output of 'OpenEnroth dump-sprite-palettes'.")
    depth.add_argument(
        "--python", type=Path, help=f"Python of the depth model's environment, defaults to ${DEPTH_PYTHON_ENV}."
    )
    depth.add_argument("--config", type=Path, help="Config file, defaults to the bundled default_config.toml.")
    depth.add_argument("--force", action="store_true", help="Run the model again even where its output is up to date.")

    report = commands.add_parser("regen-report", help="Gate scores, frame consistency and a contact sheet of a run.")
    report.add_argument("--work", type=Path, required=True, help="Work folder of the run.")
    report.add_argument("--palettes", type=Path, required=True, help="Output of 'OpenEnroth dump-sprite-palettes'.")
    report.add_argument("--model", required=True, help="Model whose outputs to report on.")
    report.add_argument("--sheet", type=Path, help="Write a contact sheet of a few sprites here.")

    for command in (extract, upscale):
        command.add_argument("--force", action="store_true", help="Redo everything, even what is up to date.")
    for command in (extract, upscale, maps, pack, regen, report, depth):
        command.add_argument(
            "--only", action="append", help="Only handle LOD entries matching this glob, e.g. 'ch1*'. Repeatable."
        )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    start = time.monotonic()
    if args.command == "extract":
        if args.sprites and not args.palettes:
            raise SystemExit("--sprites needs --palettes, the output of 'OpenEnroth dump-sprite-palettes'.")
        lodtool_path = lodtool.resolve_lodtool(args.lodtool)
        palettes = args.palettes if args.sprites else None
        result = run_extract(args.data, args.out, lodtool_path, args.mask_table, args.only, palettes, args.force)
        print(
            f"Extracted {result['extracted']}, already up to date {result['up_to_date']}, "
            f"skipped {len(result['skipped'])}, failed {len(result['failed'])}, "
            f"left out {len(result['unsafe_names'])} entries whose names collide as files."
        )
        for name, reason in result["failed"].items():
            print(f"  failed {name}: {reason}")
    elif args.command == "upscale":
        config = load_config(args.config)
        binary = resolve_binary(args.binary)
        counts = run_upscale(args.work, config, args.only, args.backend, args.scale, binary, args.force)
        print(f"Upscaled {counts}.")
    elif args.command == "maps":
        print(f"Made maps for {run_maps(args.work, load_config(args.config), args.only)} bitmaps.")
    elif args.command == "regen":
        config = load_config(args.config)
        if args.section == "icons":
            options = icon_options(config, args.model, args.max_cost)
            print(f"Regen icons {run_icons(args.work, options, args.only, dry=args.dry_run)}.")
        elif args.rescore:
            options = RegenOptions.from_config(config, args.model, args.max_cost)
            print(f"Regen {rescore(args.work, args.palettes, options, args.only)}.")
        else:
            options = RegenOptions.from_config(config, args.model, args.max_cost)
            client = None
            if is_local(options.model) and not args.dry_run:
                python = args.python or os.environ.get(REGEN_PYTHON_ENV)
                if not python:
                    raise SystemExit(
                        f"Pass --python or set {REGEN_PYTHON_ENV} to the Python of the local model's environment."
                    )
                client = LocalEditClient(Path(python), config.get("regen", {}).get("local", {}))
            try:
                print(f"Regen {run_regen(args.work, args.palettes, options, args.only, client, args.dry_run)}.")
            finally:
                if client is not None:
                    client.close()
    elif args.command == "depth":
        python = args.python or os.environ.get(DEPTH_PYTHON_ENV)
        if not python:
            raise SystemExit(f"Pass --python or set {DEPTH_PYTHON_ENV} to the Python of the depth model's environment.")
        config = load_config(args.config)
        print(f"Depth {run_depth(args.work, args.palettes, config, args.only, Path(python), args.force)}.")
    elif args.command == "regen-report":
        print(json.dumps(regen_report(args.work, args.palettes, args.model, args.only, args.sheet), indent=2))
    else:
        print(f"Packed {run_pack(args.work, args.out, args.colors, args.only, args.regen_from)}.")
    print(f"Took {time.monotonic() - start:.1f} s.")
    return 0
