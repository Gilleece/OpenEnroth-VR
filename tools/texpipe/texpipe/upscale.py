"""The upscale step: runs every extracted image through the backend the config picks for it."""

from collections import defaultdict
from pathlib import Path

from PIL import Image

from texpipe.backends import Job
from texpipe.backends.nearest import NearestBackend
from texpipe.backends.realesrgan import RealEsrganBackend
from texpipe.brightness import match_brightness
from texpipe.config import Choice, choose
from texpipe.workdir import UpscaledImage, WorkImage, images_dir, load_image_infos, save_upscaled_info, upscaled_dir

BACKENDS = ("nearest", "realesrgan")


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


def plan(work: Path, config: dict, only: list[str] | None, backend: str | None, scale: int | None):
    """Groups the images to upscale by the backend and scale they get."""
    groups: dict[Choice, list[WorkImage]] = defaultdict(list)
    for image in load_image_infos(work, only):
        groups[choose(config, image.section, image.name, image.width, backend, scale)].append(image)
    return groups


def run_upscale(
    work: Path, config: dict, only: list[str] | None, backend: str | None, scale: int | None, binary: Path | None
) -> dict[str, int]:
    """Upscales and returns how many images each backend and scale handled."""
    counts = {}
    for choice, images in plan(work, config, only, backend, scale).items():
        jobs = [make_job(work, image, choice.scale) for image in images]
        make_backend(choice.backend, config, binary).run(jobs)
        options = config.get(choice.backend, {})
        preserve, max_factor = options.get("preserve_brightness", []), options.get("max_brightness_factor", 3.0)
        for image, job in zip(images, jobs, strict=True):
            factors = preserve_brightness(job, max_factor) if image.section in preserve else None
            save_upscaled_info(work, image, UpscaledImage(choice.backend, choice.scale, factors))
        counts[f"{choice.backend} {choice.scale}x"] = len(images)
    return counts
