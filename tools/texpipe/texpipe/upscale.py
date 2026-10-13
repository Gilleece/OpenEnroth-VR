"""The upscale step: runs every extracted image through the backend the config picks for it."""

from collections import defaultdict
from dataclasses import replace
from pathlib import Path

from PIL import Image

from texpipe import imageops
from texpipe.backends import Job
from texpipe.backends.nearest import NearestBackend
from texpipe.backends.realesrgan import DEFAULT_WRAP_BORDER, DEFAULT_WRAP_TOLERANCE, RealEsrganBackend
from texpipe.brightness import match_brightness
from texpipe.config import Choice, choose
from texpipe.workdir import (
    UpscaledImage,
    WorkImage,
    images_dir,
    is_up_to_date,
    load_image_infos,
    load_upscaled_info,
    prepared_dir,
    save_upscaled_info,
    upscaled_dir,
)

BACKENDS = ("nearest", "realesrgan")
BATCH_SIZE = 1000
SOFTEN_RADIUS = 2  # Blur radius of the `soften` option, in source pixels.


def make_backend(name: str, config: dict, binary: Path | None):
    if name == "nearest":
        return NearestBackend()
    if name == "realesrgan":
        return RealEsrganBackend(config.get("realesrgan", {}), binary)
    raise ValueError(f"unknown backend '{name}', known backends are {', '.join(BACKENDS)}")


def make_job(work: Path, image: WorkImage, scale: int) -> Job:
    return Job(
        source=images_dir(work, image.section) / f"{image.name}.png",
        target=upscaled_dir(work, image.section) / f"{image.name}.png",
        scale=scale,
        tileable=image.section == "bitmaps",
    )


def preserve_brightness(job: Job, max_factor: float) -> list[float]:
    """Rescales the job's output to the mean brightness of its source and returns the factors it used."""
    with Image.open(job.source) as source, Image.open(job.target) as target:
        result, factors = match_brightness(source, target, max_factor)
    result.save(job.target)
    return [round(factor, 4) for factor in factors]


def wrap_border(config: dict, choice: Choice, image: WorkImage) -> int | None:
    """Source pixels of wrapped padding the backend puts around the image, None if it puts none."""
    if choice.backend != "realesrgan" or image.section != "bitmaps":
        return None
    return config.get("realesrgan", {}).get("wrap_border", DEFAULT_WRAP_BORDER)


def edge_wraps(config: dict, choice: Choice, image: WorkImage, source: Path) -> tuple[bool, bool] | None:
    """Along which axes the backend wraps the padding of an image, from its source as the backend gets it. None if it
    pads without wrapping."""
    if wrap_border(config, choice, image) is None:
        return None
    with Image.open(source) as opened:
        prepared = opened.convert("RGB")
    if choice.seam_blend:
        prepared = imageops.blend_wrap_seam(prepared, choice.seam_blend)
    tolerance = config.get("realesrgan", {}).get("wrap_tolerance", DEFAULT_WRAP_TOLERANCE)
    return imageops.matching_edges(prepared, tolerance)


def upscaled_info(
    config: dict, choice: Choice, image: WorkImage, brightness: list[float] | None, wraps: tuple[bool, bool] | None
) -> UpscaledImage:
    return UpscaledImage(
        choice.backend,
        choice.scale,
        brightness,
        choice.seam_blend,
        choice.soften,
        wrap_border(config, choice, image),
        list(wraps) if wraps is not None else None,
    )


def is_done(work: Path, image: WorkImage, choice: Choice, config: dict) -> bool:
    """True if the image was already upscaled the same way, from the source as it is now."""
    info = load_upscaled_info(work, image)
    if info is None:
        return False
    wraps = edge_wraps(config, choice, image, make_job(work, image, choice.scale).source)
    if replace(info, brightness=None) != upscaled_info(config, choice, image, None, wraps):
        return False
    job = make_job(work, image, choice.scale)
    return is_up_to_date([job.target], [job.source])


def needs_brightness(work: Path, image: WorkImage, choice: Choice, config: dict) -> bool:
    """True if the config wants the brightness of this image preserved, and its upscale was made without that."""
    preserve = config.get(choice.backend, {}).get("preserve_brightness", [])
    return image.section in preserve and load_upscaled_info(work, image).brightness is None


def plan(work: Path, config: dict, only: list[str] | None, backend: str | None, scale: int | None, force: bool = False):
    """Groups the images to upscale by the backend and scale they get.

    Returns the groups, the done images that only miss the brightness correction, and how many were done.
    """
    groups: dict[Choice, list[WorkImage]] = defaultdict(list)
    brightness_only: list[tuple[WorkImage, Choice]] = []
    done = 0
    for image in load_image_infos(work, only):
        choice = choose(config, image.section, image.name, image.width, backend, scale)
        if force or not is_done(work, image, choice, config):
            groups[choice].append(image)
        elif needs_brightness(work, image, choice, config):
            brightness_only.append((image, choice))
        else:
            done += 1
    return groups, brightness_only, done


def run_upscale(
    work: Path,
    config: dict,
    only: list[str] | None,
    backend: str | None,
    scale: int | None,
    binary: Path | None,
    force: bool = False,
) -> dict[str, int]:
    """Upscales what is not done yet and returns how many images each backend and scale handled."""
    groups, brightness_only, done = plan(work, config, only, backend, scale, force)
    counts = {"up to date": done, "brightness only": len(brightness_only)}
    for image, choice in brightness_only:
        max_factor = config.get(choice.backend, {}).get("max_brightness_factor", 3.0)
        factors = preserve_brightness(make_job(work, image, choice.scale), max_factor)
        save_upscaled_info(work, image, replace(load_upscaled_info(work, image), brightness=factors))
    for choice, images in groups.items():
        backend_object = make_backend(choice.backend, config, binary)
        options = config.get(choice.backend, {})
        batch_size = options.get("batch_size", BATCH_SIZE)
        for start in range(0, len(images), batch_size):
            _run_batch(work, config, backend_object, choice, images[start : start + batch_size])
        key = f"{choice.backend} {choice.scale}x"
        counts[key] = counts.get(key, 0) + len(images)  # Rules with options split one backend and scale into groups.
    return counts


def prepare_source(work: Path, image: WorkImage, choice: Choice, job: Job) -> Job:
    """Applies the changes the rule asks for before upscaling, and returns the job with the changed image as source."""
    if not choice.seam_blend:
        return job
    source = prepared_dir(work, image.section) / f"{image.name}.png"
    source.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(job.source) as original:
        imageops.blend_wrap_seam(original.convert("RGBA"), choice.seam_blend).save(source)
    return replace(job, source=source)


def finish_target(job: Job, choice: Choice) -> None:
    """Applies the changes the rule asks for after upscaling."""
    if not choice.soften:
        return
    with Image.open(job.target) as upscaled:
        result = imageops.soften(upscaled.convert("RGBA"), choice.soften, choice.scale * SOFTEN_RADIUS, job.tileable)
    result.save(job.target)


def _run_batch(work: Path, config: dict, backend, choice: Choice, images: list[WorkImage]) -> None:
    """Upscales one batch and records it as done, so an interrupted run keeps every finished batch."""
    jobs, wraps = [], []
    for image in images:
        job = make_job(work, image, choice.scale)
        wraps.append(edge_wraps(config, choice, image, job.source))
        jobs.append(prepare_source(work, image, choice, replace(job, wraps=wraps[-1] or (True, True))))
    backend.run(jobs)
    options = config.get(choice.backend, {})
    preserve, max_factor = options.get("preserve_brightness", []), options.get("max_brightness_factor", 3.0)
    for image, job, image_wraps in zip(images, jobs, wraps, strict=True):
        finish_target(job, choice)
        factors = preserve_brightness(job, max_factor) if image.section in preserve else None
        save_upscaled_info(work, image, upscaled_info(config, choice, image, factors, image_wraps))
