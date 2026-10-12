"""Upscaling backends. Each one takes a batch of jobs and writes every job's target image."""

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Job:
    source: Path
    target: Path
    scale: int
    tileable: bool  # World textures tile, so their edges must stay continuous across the seam.


def check_size(image_size: tuple[int, int], source_size: tuple[int, int], scale: int) -> None:
    expected = (source_size[0] * scale, source_size[1] * scale)
    assert image_size == expected, f"upscaled image is {image_size}, expected {expected}"
