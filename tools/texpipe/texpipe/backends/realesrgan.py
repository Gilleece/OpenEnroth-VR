"""Real-ESRGAN through the prebuilt realesrgan-ncnn-vulkan executable, downloaded into a cache on first use."""

import hashlib
import os
import platform
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from itertools import repeat
from pathlib import Path

from PIL import Image

from texpipe import imageops
from texpipe.backends import Job, check_size

RELEASE_URL = "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-{}.zip"
RELEASE_PLATFORMS = {"Windows": "windows", "Linux": "ubuntu", "Darwin": "macos"}
RELEASE_SHA256 = {
    "windows": "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d",
    "ubuntu": "e5aa6eb131234b87c0c51f82b89390f5e3e642b7b70f2b9bbe95b6a285a40c96",
    "macos": "e0ad05580abfeb25f8d8fb55aaf7bedf552c375b5b4d9bd3c8d59764d2cc333a",
}
EXECUTABLE = "realesrgan-ncnn-vulkan"


def cache_dir() -> Path:
    """Where downloaded tools go: TEXPIPE_CACHE, else ~/.cache/texpipe. Never the repository.

    Not %LOCALAPPDATA%, because packaged Python installs on Windows redirect writes there into a private folder.
    """
    if "TEXPIPE_CACHE" in os.environ:
        return Path(os.environ["TEXPIPE_CACHE"])
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "texpipe"


def find_executable(folder: Path) -> Path | None:
    for path in folder.rglob(f"{EXECUTABLE}*"):
        if path.is_file() and path.suffix in ("", ".exe"):
            return path
    return None


def verify_archive(archive: Path, expected_sha256: str) -> None:
    """Raises if the SHA-256 of `archive` is not `expected_sha256`."""
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != expected_sha256:
        raise ValueError(f"{archive.name} has SHA-256 {digest}, expected {expected_sha256}. Not unpacking it.")


def download_executable() -> Path:
    """Downloads, verifies and unpacks the release for this platform into the cache, unless it is already there."""
    folder = cache_dir() / "realesrgan"
    existing = find_executable(folder) if folder.is_dir() else None
    if existing:
        return existing
    release = RELEASE_PLATFORMS[platform.system()]
    url = RELEASE_URL.format(release)
    folder.mkdir(parents=True, exist_ok=True)
    archive = folder / "release.zip"
    print(f"Downloading {url}")
    urllib.request.urlretrieve(url, archive)
    try:
        verify_archive(archive, RELEASE_SHA256[release])
        with zipfile.ZipFile(archive) as zip_file:
            zip_file.extractall(folder)
    finally:
        archive.unlink()
    executable = find_executable(folder)
    if executable is None:
        raise FileNotFoundError(f"{url} has no {EXECUTABLE}")
    executable.chmod(executable.stat().st_mode | 0o111)
    return executable


@dataclass(frozen=True)
class Prepared:
    stem: str
    size: tuple[int, int]
    has_alpha: bool
    border: tuple[int, int]  # Padding on each side, in source pixels.


DEFAULT_WRAP_BORDER = 16


class RealEsrganBackend:
    name = "realesrgan"

    def __init__(self, options: dict, binary: Path | None = None):
        self._model = options.get("model", "realesrgan-x4plus")
        self._model_scale = options.get("model_scale", 4)
        self._wrap_border = options.get("wrap_border", DEFAULT_WRAP_BORDER)
        self._min_size = options.get("min_size", 32)
        self._threads = options.get("threads", "2:2:8")  # load:proc:save, writing the large PNGs is the slow part.
        self._tile_size = options.get("tile_size", 0)  # 0 lets the binary pick from the GPU memory.
        self._binary = binary

    def run(self, jobs: list[Job]) -> None:
        if not jobs:
            return
        binary = self._binary or download_executable()
        with tempfile.TemporaryDirectory(prefix="texpipe-") as temp:
            in_dir, out_dir = Path(temp) / "in", Path(temp) / "out"
            in_dir.mkdir()
            out_dir.mkdir()
            # Reading, padding, cropping and writing the images takes longer than the model, so it runs on all cores.
            with ProcessPoolExecutor() as pool:
                prepared = list(pool.map(self._prepare, jobs, range(len(jobs)), repeat(in_dir), chunksize=16))
                self._run_binary(binary, in_dir, out_dir)
                list(pool.map(self._finish, jobs, repeat(out_dir), prepared, chunksize=16))

    def _border_for(self, job: Job, size: tuple[int, int]) -> tuple[int, int]:
        """Tileable images get a wrapped border. Others get clamped edges, but only to reach the minimum size."""
        if job.tileable:
            return self._wrap_border, self._wrap_border
        return tuple(max(0, -(-(self._min_size - side) // 2)) for side in size)

    def _pad(self, job: Job, image: Image.Image, border: tuple[int, int]) -> Image.Image:
        if job.tileable:
            return imageops.wrap_pad(image, border[0])
        return imageops.clamp_pad(image, *border)

    def _prepare(self, job: Job, index: int, in_dir: Path) -> Prepared:
        """Writes the color and, separately, the alpha of one job as padded model inputs."""
        with Image.open(job.source) as source:
            rgb, alpha = imageops.split_alpha(source)
        item = Prepared(f"{index:06d}", rgb.size, alpha is not None, self._border_for(job, rgb.size))
        self._pad(job, rgb, item.border).save(in_dir / f"{item.stem}.png")
        if alpha is not None:
            # Wrapping the alpha would pull the cutout on one edge into the opposite edge of the upscaled image.
            imageops.clamp_pad(alpha, *item.border).convert("RGB").save(in_dir / f"{item.stem}a.png")
        return item

    def _run_binary(self, binary: Path, in_dir: Path, out_dir: Path) -> None:
        models = binary.parent / "models"
        command = [str(binary), "-i", str(in_dir), "-o", str(out_dir), "-n", self._model, "-s", str(self._model_scale)]
        command += ["-f", "png", "-m", str(models), "-j", self._threads, "-t", str(self._tile_size)]
        result = subprocess.run(command, check=True, stderr=subprocess.PIPE, text=True, errors="replace")
        failures = [line for line in result.stderr.splitlines() if "failed" in line]
        if failures:
            # A lost GPU device leaves garbage in this and every later output of the batch.
            raise RuntimeError(f"{binary.name} reported GPU errors, its output is unusable: {failures[0]}")

    def _finish(self, job: Job, out_dir: Path, item: Prepared) -> None:
        rgb = self._load_output(out_dir / f"{item.stem}.png", job, item)
        alpha = self._load_output(out_dir / f"{item.stem}a.png", job, item).convert("L") if item.has_alpha else None
        result = imageops.merge_alpha(rgb, alpha)
        check_size(result.size, item.size, job.scale)
        job.target.parent.mkdir(parents=True, exist_ok=True)
        result.save(job.target)

    def _load_output(self, path: Path, job: Job, item: Prepared) -> Image.Image:
        """Crops the padding off a model output and brings it to the source size times the job's scale."""
        with Image.open(path) as output:
            border = tuple(side * self._model_scale for side in item.border)
            image = imageops.crop_border(output.convert("RGB"), *border)
        target = (item.size[0] * job.scale, item.size[1] * job.scale)
        if image.size != target:
            image = image.resize(target, Image.Resampling.LANCZOS)
        return image


def resolve_binary(path: str | None) -> Path | None:
    if path is None:
        return None
    binary = Path(path)
    if not binary.is_file():
        found = shutil.which(path)
        if not found:
            raise FileNotFoundError(f"No executable at {path}")
        binary = Path(found)
    return binary
