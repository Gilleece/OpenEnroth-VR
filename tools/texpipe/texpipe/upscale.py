"""The upscale step: runs every extracted image through the backend the config picks for it."""

from collections import defaultdict
from pathlib import Path

from texpipe.backends import Job
from texpipe.backends.nearest import NearestBackend
from texpipe.backends.realesrgan import RealEsrganBackend
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


def plan(work: Path, config: dict, only: list[str] | None, backend: str | None, scale: int | None):
    """Groups the images to upscale by the backend and scale they get."""
    groups: dict[Choice, list[WorkImage]] = defaultdict(list)
    for image in load_image_infos(work, only):
        groups[choose(config, image.section, image.name, backend, scale)].append(image)
    return groups


def run_upscale(
    work: Path, config: dict, only: list[str] | None, backend: str | None, scale: int | None, binary: Path | None
) -> dict[str, int]:
    """Upscales and returns how many images each backend and scale handled."""
    counts = {}
    for choice, images in plan(work, config, only, backend, scale).items():
        make_backend(choice.backend, config, binary).run([make_job(work, image, choice.scale) for image in images])
        for image in images:
            save_upscaled_info(work, image, UpscaledImage(choice.backend, choice.scale))
        counts[f"{choice.backend} {choice.scale}x"] = len(images)
    return counts
