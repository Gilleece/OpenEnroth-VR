"""The depth step: a shape image per sprite, normals and depth from a monocular depth model, for the engine to give
billboards per-pixel depth.

One image per sprites.lod entry, palette independent, at the pixel size of the sprite's pack images, RGBA8:

- R, G: the surface normal's x (right) and y (up) in the image plane, as n * 0.5 + 0.5. The engine rebuilds z.
- B: depth, 255 nearest the viewer and 0 farthest, linear in game units over the sprite's thickness, so the billboard
  plane is at 127.5.
- A: the coverage alpha of the color image.

The model runs in its own Python environment through `depth_worker.py`, which also does the float work on its
output, as torch has no place in texpipe's own requirements. Depth is normalised over all the images of an animation
group at once, so that it doesn't pump from frame to frame.
"""

import fnmatch
import json
import statistics
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from texpipe.workdir import is_up_to_date, matches, read_json, upscaled_dir, write_json

WORKER = Path(__file__).with_name("depth_worker.py")
OPAQUE_ALPHA = 128  # Pixels at least this opaque count as the sprite's body.


@dataclass(frozen=True)
class DepthOptions:
    model: str = "depth-anything/Depth-Anything-V2-Small-hf"
    background: tuple[int, int, int] = (128, 128, 128)  # What the model sees behind the sprite.
    thickness_ratio: float = 0.6  # Thickness as a share of the opaque width of the sprite's group.
    min_thickness: float = 8.0
    max_thickness: float = 256.0
    blur: float = 1.0  # Gaussian sigma over the depth before the Sobel, in pack pixels.
    erode: int = 2  # Pack pixels of the silhouette whose depth is taken from further in, the model blurs them.
    percentiles: tuple[float, float] = (0.02, 0.98)  # Of the group's opaque depths, mapped to farthest and nearest.
    exclude: tuple[str, ...] = ()

    @staticmethod
    def from_config(config: dict) -> "DepthOptions":
        known = DepthOptions.__dataclass_fields__
        values = {key: value for key, value in config.get("depth", {}).items() if key in known}
        for key in ("background", "percentiles"):
            if key in values:
                values[key] = tuple(values[key])
        # Spell effects, projectiles, glows and particles stay flat, the same entries the regen pass leaves out.
        values["exclude"] = tuple(config.get("regen", {}).get("exclude", []))
        return DepthOptions(**values)


def shape_path(work: Path, name: str) -> Path:
    return work / "shape" / "sprites" / f"{name}.png"


def shape_record_path(work: Path, name: str) -> Path:
    return work / "shape" / "sprites" / f"{name}.json"


def load_sprites(palettes_json: Path) -> tuple[dict[str, list[int]], list[list[str]]]:
    """Reads the output of `OpenEnroth dump-sprite-palettes`: palettes per sprite and the animation groups. Sprites in
    no group get one of their own."""
    data = json.loads(palettes_json.read_text(encoding="utf-8"))
    sprites = {name: sorted(ids) for name, ids in data["sprites"].items()}
    groups = [sorted(group) for group in data.get("groups", [])]
    grouped = {name for group in groups for name in group}
    groups += [[name] for name in sorted(sprites) if name not in grouped]
    return sprites, groups


def base_image(work: Path, name: str, palette_ids: list[int]) -> Path:
    """The upscaled color image the depth is estimated from, the sprite in its first palette."""
    return upscaled_dir(work, "sprites") / name / f"{palette_ids[0]}.png"


def wanted(name: str, options: DepthOptions, only: list[str] | None) -> bool:
    return matches(name, only) and not any(fnmatch.fnmatchcase(name, pattern) for pattern in options.exclude)


def thickness(colors: list[Path], scale: int, options: DepthOptions) -> float:
    """Front to back depth in game units, from the median opaque width of the group's images in LOD pixels. The median
    leaves out the frames where a weapon or a limb swings out."""
    widths = []
    for path in colors:
        with Image.open(path) as image:
            alpha = image.convert("RGBA").getchannel("A")
        box = alpha.point(lambda value: 255 if value >= OPAQUE_ALPHA else 0).getbbox()
        if box is not None:
            widths.append(box[2] - box[0])
    width = statistics.median(widths) if widths else 0
    return min(max(width / scale * options.thickness_ratio, options.min_thickness), options.max_thickness)


def plan(work: Path, palettes_json: Path, options: DepthOptions, only: list[str] | None, force: bool) -> list[dict]:
    """The groups whose shape images are missing or older than their color images, as worker job entries."""
    sprites, groups = load_sprites(palettes_json)
    result = []
    for group in groups:
        names = [name for name in group if name in sprites and wanted(name, options, only)]
        names = [name for name in names if base_image(work, name, sprites[name]).is_file()]
        if not names:
            continue
        colors = [base_image(work, name, sprites[name]) for name in names]
        shapes = [shape_path(work, name) for name in names]
        if not force and is_up_to_date(shapes, colors):
            continue
        scale = read_json(colors[0].with_suffix(".json"))["scale"]
        result.append(
            {
                "names": names,
                "thickness": round(thickness(colors, scale, options), 1),
                "scale": scale,
                "items": [[str(color), str(shape)] for color, shape in zip(colors, shapes, strict=True)],
            }
        )
    return result


def run_worker(groups: list[dict], options: DepthOptions, python: Path) -> str:
    """Runs the depth model and the shape image builder in the model's environment. Returns its summary line."""
    with tempfile.TemporaryDirectory(prefix="texpipe-depth-") as temp:
        job = Path(temp) / "job.json"
        write_json(
            job,
            {
                "model": options.model,
                "background": list(options.background),
                "blur": options.blur,
                "erode": options.erode,
                "percentiles": list(options.percentiles),
                "groups": [{key: group[key] for key in ("thickness", "scale", "items")} for group in groups],
            },
        )
        result = subprocess.run([str(python), str(WORKER), str(job)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"depth worker failed:\n{result.stderr[-3000:]}")
    return result.stdout.strip().splitlines()[-1]


def run_depth(
    work: Path, palettes_json: Path, config: dict, only: list[str] | None, python: Path, force: bool = False
) -> dict:
    """Builds the shape images of every wanted sprite that needs one. Returns counts and the worker's timing."""
    options = DepthOptions.from_config(config)
    groups = plan(work, palettes_json, options, only, force)
    if not groups:
        return {"groups": 0, "images": 0}
    for group in groups:
        for name in group["names"]:
            shape_path(work, name).parent.mkdir(parents=True, exist_ok=True)
    summary = run_worker(groups, options, python)
    for group in groups:
        for name in group["names"]:
            write_json(shape_record_path(work, name), {"thickness": group["thickness"]})
    return {"groups": len(groups), "images": sum(len(group["names"]) for group in groups), "worker": summary}


def shape_entry(work: Path, name: str) -> dict | None:
    """The manifest's shape object for a sprite, or None if it has no shape image."""
    record = shape_record_path(work, name)
    if not record.is_file() or not shape_path(work, name).is_file():
        return None
    return {"thickness": read_json(record)["thickness"]}
