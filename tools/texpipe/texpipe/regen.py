"""The regen step: a generative clean-up pass over the upscaled sprites, held to the stock look by fidelity gates.

For every sprite the model gets the upscaled image in its base palette, the first palette it is drawn with, on a flat
background. Its output keeps the stock silhouette: the alpha always comes from the upscaled stock alpha, and edge
pixels keep the upscaled colors. An output that fails a gate is replaced by the upscaled image. The other palette
variants are derived from the base one with the palette pair mapping, so they cost nothing and recolor exactly.

Work that is done is recorded per sprite under regen/<model>/, so an interrupted run resumes where it stopped.
"""

import fnmatch
import re
import shutil
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass, field
from pathlib import Path
from urllib.error import HTTPError

from PIL import Image

from texpipe import gates, palettemap
from texpipe.backends.imageedit import ImageEditClient, ImageEditError
from texpipe.backends.localedit import is_local
from texpipe.sprites import load_palette, load_sprite_palettes
from texpipe.workdir import images_dir, read_json, upscaled_dir, write_json

DEFAULT_PROMPT = (
    "This is one animation frame of a sprite from a 1999 role-playing game whose sprites were pre-rendered from 3D "
    "models, on a flat single-colour background. Produce a cleaner, higher-detail version of exactly this frame. Keep "
    "the same pose, silhouette, proportions, camera angle, colours, lighting direction and shading style. Keep the "
    "pre-rendered look, no restyling, no added elements, no shadow, keep the flat background exactly the same colour, "
    "same framing."
)
FATAL_HTTP_CODES = (401, 402, 403)  # Bad key, no credit or no access: every further call fails the same way.
FRAME_NAME = re.compile(r"^(?P<stem>.+)(?P<frame>[a-z])(?P<direction>[0-7])$")


@dataclass(frozen=True)
class RegenOptions:
    model: str
    prompt: str = DEFAULT_PROMPT
    seed: int | None = 7
    temperature: float | None = 0.2
    thresholds: gates.Thresholds = field(default_factory=gates.Thresholds)
    include: tuple[str, ...] = ("*",)
    exclude: tuple[str, ...] = ()
    max_cost: float | None = None
    workers: int = 4  # Calls in flight at once, each one waits seconds for the model.
    cost_per_image: float = 0.034  # For the dry run's projection, as measured on the pilot.

    @staticmethod
    def from_config(config: dict, model: str | None = None, max_cost: float | None = None) -> "RegenOptions":
        section = config.get("regen", {})
        thresholds = gates.Thresholds(**section.get("thresholds", {}))
        model = model or section["model"]
        prompt = section.get("prompt", DEFAULT_PROMPT)
        if is_local(model):
            # A diffusion model takes a description of the result, not an instruction.
            prompt = section.get("local", {}).get("prompt", prompt)
        return RegenOptions(
            model=model,
            prompt=prompt,
            seed=section.get("seed", 7),
            temperature=section.get("temperature", 0.2),
            thresholds=thresholds,
            include=tuple(section.get("include", ["*"])),
            exclude=tuple(section.get("exclude", [])),
            max_cost=max_cost if max_cost is not None else section.get("max_cost"),
            workers=section.get("workers", 4),
            cost_per_image=section.get("cost_per_image", 0.034),
        )


def model_tag(model: str) -> str:
    """Folder name for a model's outputs, the identifier without its namespace."""
    return model.split("/")[-1]


def regen_dir(work: Path, model: str) -> Path:
    return work / "regen" / model_tag(model)


def record_path(work: Path, model: str, name: str) -> Path:
    return regen_dir(work, model) / "sprites" / f"{name}.json"


def output_path(work: Path, model: str, name: str, palette_id: int) -> Path:
    return regen_dir(work, model) / "sprites" / name / f"{palette_id}.png"


def wanted(name: str, options: RegenOptions, only: list[str] | None) -> bool:
    included = any(fnmatch.fnmatchcase(name, glob) for glob in options.include)
    excluded = any(fnmatch.fnmatchcase(name, glob) for glob in options.exclude)
    return included and not excluded and (not only or any(fnmatch.fnmatchcase(name, glob.lower()) for glob in only))


def on_background(image: Image.Image, background: tuple[int, int, int] = gates.BACKGROUND) -> Image.Image:
    flat = Image.new("RGBA", image.size, (*background, 255))
    return Image.alpha_composite(flat, image.convert("RGBA")).convert("RGB")


def compose(generated: Image.Image, reference: Image.Image) -> Image.Image:
    """Generated colors where the reference is fully opaque, the reference everywhere else, the reference's alpha.

    Edge pixels keep the reference colors because the model drew them against the flat background.
    """
    rgba = reference.convert("RGBA")
    alpha = rgba.getchannel("A")
    inside = alpha.point(lambda value: 255 if value == 255 else 0)
    colors = Image.composite(generated.convert("RGB"), rgba.convert("RGB"), inside)
    colors.putalpha(alpha)
    return colors


def regen_sprite(
    work: Path,
    client: ImageEditClient,
    name: str,
    palette_ids: list[int],
    palettes: dict[int, bytes],
    options: RegenOptions,
) -> dict:
    """Generates the base palette image of one sprite, gates it and derives its other palettes. Returns the record."""
    reference = Image.open(upscaled_dir(work, "sprites") / name / f"{palette_ids[0]}.png").convert("RGBA")
    started = time.monotonic()
    background = gates.choose_background(reference)
    model_input = on_background(reference, background)
    result = client.edit(options.model, options.prompt, model_input, options.seed, options.temperature)
    raw = result.image.resize(reference.size, Image.Resampling.LANCZOS)
    folder = output_path(work, options.model, name, palette_ids[0]).parent
    folder.mkdir(parents=True, exist_ok=True)
    raw.save(folder / "model.png")
    scores, passed = finish_sprite(work, name, palette_ids, palettes, options, raw, background)
    record = {
        "model": options.model,
        "background": list(background),
        "base_palette": palette_ids[0],
        "palettes": palette_ids,
        "cost": result.cost,
        "seconds": round(time.monotonic() - started, 1),
        "scores": asdict(scores),
        "passed": passed,
    }
    write_json(record_path(work, options.model, name), record)
    return record


def finish_sprite(
    work: Path,
    name: str,
    palette_ids: list[int],
    palettes: dict[int, bytes],
    options: RegenOptions,
    raw: Image.Image,
    background: tuple[int, int, int],
) -> tuple[gates.Scores, bool]:
    """Gates the model's output of one sprite and writes its palette images, generated or fallen back."""
    base = palette_ids[0]
    reference = Image.open(upscaled_dir(work, "sprites") / name / f"{base}.png").convert("RGBA")
    generated = compose(raw, reference)
    scores = gates.score(generated, reference, raw, background)
    passed = scores.passes(options.thresholds)
    final = generated if passed else reference
    final.save(output_path(work, options.model, name, base))
    for palette_id in palette_ids[1:]:
        target = output_path(work, options.model, name, palette_id)
        if passed:
            palettemap.recolor(final, palettes[base], palettes[palette_id]).save(target)
        else:
            shutil.copyfile(upscaled_dir(work, "sprites") / name / f"{palette_id}.png", target)
    return scores, passed


def rescore(work: Path, palettes_json: Path, options: RegenOptions, only: list[str] | None) -> dict:
    """Gates the stored model outputs again, for new gates or thresholds, without calling the model."""
    summary = load_summary(work, options.model)
    palette_pngs = work / "lodpng" / "bitmaps"
    fallbacks = 0
    for name, palette_ids in sorted(load_sprite_palettes(palettes_json).items()):
        path = record_path(work, options.model, name)
        if not path.is_file() or not wanted(name, options, only):
            continue
        record = read_json(path)
        raw = Image.open(output_path(work, options.model, name, palette_ids[0]).parent / "model.png").convert("RGB")
        palettes = {palette_id: load_palette(palette_pngs / f"pal{palette_id:03d}.png") for palette_id in palette_ids}
        background = tuple(record.get("background", gates.BACKGROUND))
        scores, record["passed"] = finish_sprite(work, name, palette_ids, palettes, options, raw, background)
        record["scores"] = asdict(scores)
        write_json(path, record)
        fallbacks += 0 if record["passed"] else 1
    if not only:
        summary["fallbacks"] = fallbacks
        write_json(regen_dir(work, options.model) / "summary.json", summary)
    return summary


def summary_path(work: Path, model: str, section: str = "sprites") -> Path:
    return regen_dir(work, model) / ("summary.json" if section == "sprites" else f"summary-{section}.json")


def load_summary(work: Path, model: str, section: str = "sprites") -> dict:
    path = summary_path(work, model, section)
    return read_json(path) if path.is_file() else {"cost": 0.0, "generated": 0, "fallbacks": 0, "errors": {}}


def dry_run(pending: list, options: RegenOptions) -> dict:
    """What a run would send and what it would cost, without calling anything."""
    names = [item[0] for item in pending]
    return {
        "would_send": len(names),
        "projected_cost": round(len(names) * options.cost_per_image, 2),
        "cost_per_image": options.cost_per_image,
        "first": names[:20],
    }


def run_batches(work: Path, options: RegenOptions, pending: list, generate, section: str = "sprites") -> dict:
    """Runs `generate` over `pending` items, `options.workers` at a time, and keeps the section's summary.

    Stops before the total would go over `max_cost`, judging the next batch by the average cost so far, and at the
    first error that every further call would repeat. Other errors are recorded per item, which is retried next run.
    """
    summary = load_summary(work, options.model, section)
    summary.pop("stopped", None)
    with ThreadPoolExecutor(options.workers) as pool:
        for start in range(0, len(pending), options.workers):
            batch = pending[start : start + options.workers]
            average = summary["cost"] / summary["generated"] if summary["generated"] else 0.0
            if options.max_cost is not None and summary["cost"] + average * len(batch) > options.max_cost:
                summary["stopped"] = f"budget of {options.max_cost} reached"
                break
            futures = {item[0]: pool.submit(generate, item) for item in batch}
            for name, future in futures.items():
                try:
                    record = future.result()
                except HTTPError as error:
                    summary["errors"][name] = str(error)[:300]
                    if error.code in FATAL_HTTP_CODES:
                        summary["stopped"] = f"HTTP {error.code}, the account cannot pay for more calls"
                    continue
                except (ImageEditError, OSError, ValueError, KeyError) as error:
                    summary["errors"][name] = str(error)[:300]
                    continue
                summary["errors"].pop(name, None)
                summary["cost"] = round(summary["cost"] + record["cost"], 6)
                summary["generated"] += 1
                summary["fallbacks"] += 0 if record["passed"] else 1
            write_json(summary_path(work, options.model, section), summary)
            if "stopped" in summary:
                break
    return summary


def pending_sprites(work: Path, palettes_json: Path, options: RegenOptions, only: list[str] | None) -> list:
    """The wanted sprites that have an upscaled base image and no record yet, with their palette ids."""
    return [
        (name, palette_ids)
        for name, palette_ids in sorted(load_sprite_palettes(palettes_json).items())
        if wanted(name, options, only)
        and not record_path(work, options.model, name).is_file()
        and (upscaled_dir(work, "sprites") / name / f"{palette_ids[0]}.png").is_file()
    ]


def run_regen(
    work: Path,
    palettes_json: Path,
    options: RegenOptions,
    only: list[str] | None,
    client: ImageEditClient | None = None,
    dry: bool = False,
) -> dict:
    """Runs the pass over every pending sprite. With `dry`, only reports what it would send and cost."""
    pending = pending_sprites(work, palettes_json, options, only)
    if dry:
        return dry_run(pending, options)
    client = client or ImageEditClient()
    palette_pngs = work / "lodpng" / "bitmaps"

    def generate(item):
        name, palette_ids = item
        palettes = {palette_id: load_palette(palette_pngs / f"pal{palette_id:03d}.png") for palette_id in palette_ids}
        return regen_sprite(work, client, name, palette_ids, palettes, options)

    return run_batches(work, options, pending, generate)


def generated_image(work: Path, model: str, name: str, palette_id: int) -> Path | None:
    """The pack image for a sprite and palette from this model's run, or None if the sprite was not generated."""
    if not record_path(work, model, name).is_file():
        return None
    path = output_path(work, model, name, palette_id)
    return path if path.is_file() else None


def frame_sequences(names: list[str]) -> dict[tuple[str, str], list[str]]:
    """Groups sprite names into animations: same stem and direction, ordered by frame letter."""
    sequences: dict[tuple[str, str], list[str]] = {}
    for name in sorted(names):
        match = FRAME_NAME.match(name)
        if match:
            sequences.setdefault((match["stem"], match["direction"]), []).append(name)
    return {key: frames for key, frames in sequences.items() if len(frames) > 1}


def consistency(work: Path, model: str, names: list[str]) -> dict:
    """Frame to frame change of the generated animations against the upscaled stock ones they were made from.

    Measured on the model's outputs whether they passed the gates or not. A ratio of 1 means the generated frames
    change as much from one to the next as the stock frames do, more means the pass adds flicker.
    """
    ratios = []
    for frames in frame_sequences(names).values():
        records = [read_json(record_path(work, model, name)) for name in frames]
        base = records[0]["base_palette"]
        stock = [Image.open(upscaled_dir(work, "sprites") / name / f"{base}.png") for name in frames]
        raws = [Image.open(output_path(work, model, name, base).parent / "model.png") for name in frames]
        generated = [compose(raw, image) for raw, image in zip(raws, stock, strict=True)]
        masks = [gates.alpha_mask(image) for image in stock]
        stock_change = gates.sequence_change(stock, masks)
        if stock_change > 0:
            ratios.append(gates.sequence_change(generated, masks) / stock_change)
    ratios.sort()
    return {
        "sequences": len(ratios),
        "median_ratio": round(ratios[len(ratios) // 2], 3) if ratios else None,
        "max_ratio": round(ratios[-1], 3) if ratios else None,
    }


def contact_sheet(work: Path, model: str, names: list[str], palette_ids: list[int], out: Path) -> None:
    """Stock, upscaled and generated side by side, one row per sprite and palette."""
    cell = max(Image.open(upscaled_dir(work, "sprites") / names[0] / f"{palette_ids[0]}.png").size)
    rows = [(name, palette_id) for name in names for palette_id in palette_ids]
    sheet = Image.new("RGB", (cell * 3 + 16, cell * len(rows) + 4 * (len(rows) + 1)), (255, 255, 255))
    for row, (name, palette_id) in enumerate(rows):
        stock = Image.open(images_dir(work, "sprites") / name / f"{palette_id}.png").convert("RGBA")
        cells = [
            stock.resize((stock.width * 2, stock.height * 2), Image.Resampling.NEAREST),
            Image.open(upscaled_dir(work, "sprites") / name / f"{palette_id}.png").convert("RGBA"),
            Image.open(output_path(work, model, name, palette_id)).convert("RGBA"),
        ]
        for column, image in enumerate(cells):
            sheet.paste(on_background(image), (4 + column * (cell + 4), 4 + row * (cell + 4)))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


def palette_mapping_mismatch(work: Path, name: str, palette_ids: list[int], palettes: dict[int, bytes]) -> float:
    """Share of opaque pixels where mapping the stock base image differs from the stock recolor of another palette."""
    base = Image.open(images_dir(work, "sprites") / name / f"{palette_ids[0]}.png").convert("RGBA")
    mask = gates.alpha_mask(base)
    worst = 0.0
    for palette_id in palette_ids[1:]:
        stock = Image.open(images_dir(work, "sprites") / name / f"{palette_id}.png")
        mapped = palettemap.recolor(base, palettes[palette_ids[0]], palettes[palette_id])
        worst = max(worst, palettemap.mismatch(mapped, stock, mask))
    return worst


def _spread(values: list[float]) -> dict:
    values = sorted(values)
    if not values:
        return {}
    return {"min": values[0], "median": values[len(values) // 2], "max": values[-1]}


def regen_report(work: Path, palettes_json: Path, model: str, only: list[str] | None, sheet: Path | None) -> dict:
    """Summarizes a run: gate scores, fallbacks, cost, frame consistency and the palette mapping check."""
    sprite_palettes = load_sprite_palettes(palettes_json)
    names = [
        name
        for name in sorted(sprite_palettes)
        if record_path(work, model, name).is_file() and (not only or any(fnmatch.fnmatchcase(name, g) for g in only))
    ]
    records = {name: read_json(record_path(work, model, name)) for name in names}
    palette_pngs = work / "lodpng" / "bitmaps"
    mismatches = []
    for name in names:
        ids = sprite_palettes[name]
        if len(ids) > 1:
            palettes = {palette_id: load_palette(palette_pngs / f"pal{palette_id:03d}.png") for palette_id in ids}
            mismatches.append(palette_mapping_mismatch(work, name, ids, palettes))
    if sheet is not None and names:
        frames = frame_sequences(names)
        shown = next(iter(frames.values()), names)[:6]
        contact_sheet(work, model, shown, sprite_palettes[shown[0]][:2], sheet)
    return {
        "sprites": len(names),
        "passed": sum(1 for record in records.values() if record["passed"]),
        "fallbacks": sum(1 for record in records.values() if not record["passed"]),
        "cost": round(sum(record["cost"] for record in records.values()), 4),
        "cost_per_image": round(sum(record["cost"] for record in records.values()) / len(names), 4) if names else None,
        "iou": _spread([record["scores"]["iou"] for record in records.values()]),
        "color_delta": _spread([record["scores"]["color_delta"] for record in records.values()]),
        "distance": _spread([record["scores"]["distance"] for record in records.values()]),
        "consistency": consistency(work, model, names),
        "palette_mapping_mismatch": _spread(mismatches),
    }
