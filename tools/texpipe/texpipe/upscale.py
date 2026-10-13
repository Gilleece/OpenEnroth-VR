"""The upscale step: runs every extracted image through the backend the config picks for it."""

from collections import defaultdict
from pathlib import Path

from PIL import Image

from texpipe.backends import Job
from texpipe.backends.nearest import NearestBackend
from texpipe.backends.realesrgan import RealEsrganBackend
from texpipe.brightness import match_brightness
from texpipe.config import Choice, choose
from texpipe.workdir import (
    UpscaledImage,
    WorkImage,
    images_dir,
    is_up_to_date,
    load_image_infos,
    load_upscaled_info,
    save_upscaled_info,
    upscaled_dir,
)

BACKENDS = ("nearest", "realesrgan")
BATCH_SIZE = 1000


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


def is_done(work: Path, image: WorkImage, choice: Choice) -> bool:
    """True if the image was already upscaled the same way, from the source as it is now."""
    info = load_upscaled_info(work, image)
    if info is None or (info.backend, info.scale) != (choice.backend, choice.scale):
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
        if force or not is_done(work, image, choice):
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
        save_upscaled_info(work, image, UpscaledImage(choice.backend, choice.scale, factors))
    for choice, images in groups.items():
        backend_object = make_backend(choice.backend, config, binary)
        options = config.get(choice.backend, {})
        batch_size = options.get("batch_size", BATCH_SIZE)
        for start in range(0, len(images), batch_size):
            _run_batch(work, backend_object, choice, options, images[start : start + batch_size])
        counts[f"{choice.backend} {choice.scale}x"] = len(images)
    return counts


def _run_batch(work: Path, backend, choice: Choice, options: dict, images: list[WorkImage]) -> None:
    """Upscales one batch and records it as done, so an interrupted run keeps every finished batch."""
    jobs = [make_job(work, image, choice.scale) for image in images]
    backend.run(jobs)
    preserve, max_factor = options.get("preserve_brightness", []), options.get("max_brightness_factor", 3.0)
    for image, job in zip(images, jobs, strict=True):
        factors = preserve_brightness(job, max_factor) if image.section in preserve else None
        save_upscaled_info(work, image, UpscaledImage(choice.backend, choice.scale, factors))
