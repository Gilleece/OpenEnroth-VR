"""The regen pass for icons: character portraits and the UI art.

Icons have no palette variants and many are fully opaque, so each upscaled icon is sent once, and the silhouette gate
only applies to icons with transparency. Everything else works as for sprites: the alpha and edge colors come from
the upscaled image, and an output that fails a gate falls back to it.
"""

import time
from dataclasses import asdict, replace
from pathlib import Path

from PIL import Image

from texpipe import gates, imageops
from texpipe.backends.imageedit import ImageEditClient
from texpipe.regen import RegenOptions, compose, dry_run, on_background, regen_dir, run_batches, wanted
from texpipe.workdir import load_image_infos, load_upscaled_info, read_json, upscaled_dir, write_json

SECTION = "icons"
DEFAULT_ICON_PROMPT = (
    "This is a portrait or interface artwork from a 1999 role-playing game, painted and pre-rendered at low "
    "resolution. "
    "Produce a cleaner, higher-detail version of exactly this image. Keep the same composition, framing, pose, facial "
    "features, expression, colours, lighting and painted style. No restyling, no added elements, no text changes."
)


def icon_options(config: dict, model: str | None = None, max_cost: float | None = None) -> RegenOptions:
    """Options from [regen.icons], falling back to [regen] for anything the section leaves out."""
    base = RegenOptions.from_config(config, model, max_cost)
    section = config.get("regen", {}).get("icons", {})
    return replace(
        base,
        model=model or section.get("model", base.model),
        prompt=section.get("prompt", DEFAULT_ICON_PROMPT),
        temperature=section.get("temperature", base.temperature),
        thresholds=gates.Thresholds(**section["thresholds"]) if "thresholds" in section else base.thresholds,
        include=tuple(section.get("include", ["*"])),
        exclude=tuple(section.get("exclude", [])),
        max_cost=max_cost if max_cost is not None else section.get("max_cost", base.max_cost),
        cost_per_image=section.get("cost_per_image", base.cost_per_image),
    )


def icon_record_path(work: Path, model: str, name: str) -> Path:
    return regen_dir(work, model) / SECTION / f"{name}.json"


def icon_output_path(work: Path, model: str, name: str) -> Path:
    return regen_dir(work, model) / SECTION / f"{name}.png"


def pending_icons(work: Path, options: RegenOptions, only: list[str] | None) -> list[tuple[str]]:
    """The wanted icons that are upscaled and have no record yet."""
    return [
        (image.name,)
        for image in load_image_infos(work, only)
        if image.section == SECTION
        and wanted(image.name, options, None)
        and load_upscaled_info(work, image) is not None
        and not icon_record_path(work, options.model, image.name).is_file()
    ]


def regen_icon(work: Path, client: ImageEditClient, name: str, options: RegenOptions) -> dict:
    """Generates one icon, gates it, and writes the result or the fallback. Returns the record."""
    reference = Image.open(upscaled_dir(work, SECTION) / f"{name}.png").convert("RGBA")
    transparent = imageops.has_transparency(reference)
    background = gates.choose_background(reference) if transparent else gates.BACKGROUND
    started = time.monotonic()
    result = client.edit(
        options.model, options.prompt, on_background(reference, background), options.seed, options.temperature
    )
    raw = result.image.resize(reference.size, Image.Resampling.LANCZOS)
    output = icon_output_path(work, options.model, name)
    output.parent.mkdir(parents=True, exist_ok=True)
    raw.save(output.with_suffix(".model.png"))
    generated = compose(raw, reference)
    scores = gates.score(generated, reference, raw, background, check_silhouette=transparent)
    passed = scores.passes(options.thresholds)
    (generated if passed else reference).save(output)
    record = {
        "model": options.model,
        "background": list(background),
        "cost": result.cost,
        "seconds": round(time.monotonic() - started, 1),
        "scores": asdict(scores),
        "passed": passed,
    }
    write_json(icon_record_path(work, options.model, name), record)
    return record


def run_icons(
    work: Path, options: RegenOptions, only: list[str] | None, client: ImageEditClient | None = None, dry: bool = False
) -> dict:
    """Runs the pass over every pending icon. With `dry`, only reports what it would send and cost."""
    pending = pending_icons(work, options, only)
    if dry:
        return dry_run(pending, options)
    client = client or ImageEditClient()
    return run_batches(work, options, pending, lambda item: regen_icon(work, client, item[0], options), SECTION)


def generated_icon(work: Path, model: str, name: str) -> Path | None:
    """The pack image for an icon from this model's run if it passed the gates, else None."""
    record = icon_record_path(work, model, name)
    if not record.is_file():
        return None
    return icon_output_path(work, model, name) if read_json(record)["passed"] else None
