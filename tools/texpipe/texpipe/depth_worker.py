"""Builds sprite shape images with a monocular depth model. Runs in its own Python environment, see the README.

This file imports nothing from texpipe, only what that environment has: torch, transformers and Pillow. texpipe's
`depth` command starts it with a JSON job file:

    {"model": <model id>, "background": [r, g, b], "blur": <pack pixels>, "erode": <pack pixels>,
     "percentiles": [<farthest>, <nearest>], fractions of the group's opaque depths that map to 0 and 255,
     "groups": [{"thickness": <game units>, "scale": <pack pixels per LOD pixel>, "items": [[color png, shape png]]}]}

Each shape image is RGBA8 at the color image's size: normal x and y in R and G as n * 0.5 + 0.5, depth in B with 255
nearest and 127.5 on the billboard plane, and the color image's alpha in A.
"""

import json
import sys
import time

import torch
import torch.nn.functional as functional
from PIL import Image
from transformers import AutoImageProcessor, AutoModelForDepthEstimation

OPAQUE_ALPHA = 128  # Pixels at least this opaque count as the sprite's body.
MAX_PERCENTILE_SAMPLES = 100_000
SOBEL_X = torch.tensor([[-1.0, 0.0, 1.0], [-2.0, 0.0, 2.0], [-1.0, 0.0, 1.0]]) / 8  # Slope per pixel.


def estimate(model, processor, device, rgba: torch.Tensor, background: list[int]) -> torch.Tensor:
    """Relative inverse depth of a sprite composited over a flat background, larger is nearer, at its own size."""
    alpha = rgba[3:4] / 255
    flat = rgba[:3] * alpha + torch.tensor(background, dtype=torch.float32).view(3, 1, 1) * (1 - alpha)
    image = Image.fromarray(flat.permute(1, 2, 0).round().byte().numpy())
    inputs = processor(images=image, return_tensors="pt").to(device)
    with torch.no_grad():
        predicted = model(**inputs).predicted_depth.float()
    size = (rgba.shape[1], rgba.shape[2])
    return functional.interpolate(predicted.unsqueeze(1), size=size, mode="bicubic", align_corners=False)[0, 0].cpu()


def pull_push(value: torch.Tensor, known: torch.Tensor) -> torch.Tensor:
    """Fills the unknown pixels of `value` from the known ones around them, through a pyramid of halved sizes."""
    levels = [(value * known, known)]
    while max(levels[-1][0].shape) > 1:
        weighted, weight = levels[-1]
        weighted = functional.avg_pool2d(weighted[None, None], 2, ceil_mode=True)[0, 0]
        weight = functional.avg_pool2d(weight[None, None], 2, ceil_mode=True)[0, 0]
        levels.append((weighted, weight))
    filled = levels[-1][0] / levels[-1][1].clamp(min=1e-6)
    for weighted, weight in reversed(levels[:-1]):
        coarse = functional.interpolate(filled[None, None], size=weighted.shape, mode="bilinear", align_corners=False)[
            0, 0
        ]
        own = weighted / weight.clamp(min=1e-6)
        filled = torch.where(weight > 0.999, own, own * weight + coarse * (1 - weight))
    return filled


def gaussian_blur(value: torch.Tensor, sigma: float) -> torch.Tensor:
    if sigma <= 0:
        return value
    radius = max(1, round(sigma * 3))
    x = torch.arange(-radius, radius + 1, dtype=torch.float32)
    kernel = torch.exp(-(x**2) / (2 * sigma**2))
    kernel /= kernel.sum()
    padded = functional.pad(value[None, None], (radius, radius, radius, radius), mode="replicate")
    padded = functional.conv2d(padded, kernel.view(1, 1, 1, -1))
    return functional.conv2d(padded, kernel.view(1, 1, -1, 1))[0, 0]


def sobel(value: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
    """Slopes per pixel towards the right and towards the bottom row."""
    padded = functional.pad(value[None, None], (1, 1, 1, 1), mode="replicate")
    dx = functional.conv2d(padded, SOBEL_X.view(1, 1, 3, 3))[0, 0]
    dy = functional.conv2d(padded, SOBEL_X.t().contiguous().view(1, 1, 3, 3))[0, 0]
    return dx, dy


def load_rgba(path: str) -> torch.Tensor:
    with Image.open(path) as image:
        rgba = image.convert("RGBA")
    return (
        torch.frombuffer(bytearray(rgba.tobytes()), dtype=torch.uint8)
        .view(rgba.height, rgba.width, 4)
        .permute(2, 0, 1)
        .float()
    )


def build_group(group: dict, job: dict, model, processor, device) -> None:
    colors = [load_rgba(color) for color, _ in group["items"]]
    depths = [estimate(model, processor, device, rgba, job["background"]) for rgba in colors]
    bodies = []
    for rgba in colors:
        body = (rgba[3] >= OPAQUE_ALPHA).float()
        erode = job["erode"]
        # The model blends the outermost pixels with the background, the body's own depth starts a little further in.
        eroded = (
            -functional.max_pool2d(-body[None, None], 2 * erode + 1, stride=1, padding=erode)[0, 0] if erode else body
        )
        bodies.append(eroded if eroded.any() else body)

    # One range for the whole group, so that the depth doesn't pump from frame to frame. Percentiles keep a sword tip
    # or a branch from squeezing the body's relief, the few pixels past them are clamped.
    values = torch.cat([depth[body > 0] for depth, body in zip(depths, bodies, strict=True) if body.any()])
    if values.numel():
        sample = values[torch.randperm(values.numel())[:MAX_PERCENTILE_SAMPLES]]  # torch.quantile has a size limit.
        low, high = (torch.quantile(sample, q).item() for q in job["percentiles"])
    else:
        low, high = 0.0, 1.0
    span = (high - low) or 1.0

    for (_, target), rgba, depth, body in zip(group["items"], colors, depths, bodies, strict=True):
        level = pull_push(((depth - low) / span).clamp(0, 1), body)  # 0 farthest, 1 nearest.
        units = gaussian_blur((level - 0.5) * group["thickness"], job["blur"])
        dx, dy = sobel(units)
        nx = -dx * group["scale"]  # Nearer to the right faces left. One pack pixel is 1 / scale game units.
        ny = dy * group["scale"]  # Nearer towards the bottom row faces up.
        length = torch.sqrt(nx * nx + ny * ny + 1)
        channels = [nx / length * 0.5 + 0.5, ny / length * 0.5 + 0.5, level]
        rgb = torch.stack([(channel * 255).round().clamp(0, 255) for channel in channels])
        shape = torch.cat([rgb, rgba[3:4]]).byte().permute(1, 2, 0).contiguous()
        Image.frombytes("RGBA", (shape.shape[1], shape.shape[0]), shape.numpy().tobytes()).save(target)


def main() -> int:
    with open(sys.argv[1], encoding="utf-8") as file:
        job = json.load(file)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    processor = AutoImageProcessor.from_pretrained(job["model"])
    model = AutoModelForDepthEstimation.from_pretrained(job["model"]).to(device).eval()
    started = time.monotonic()
    images = 0
    for index, group in enumerate(job["groups"]):
        build_group(group, job, model, processor, device)
        images += len(group["items"])
        if (index + 1) % 50 == 0:
            print(f"{index + 1} of {len(job['groups'])} groups in {time.monotonic() - started:.0f} s", flush=True)
    print(f"device {device}, {images} images in {time.monotonic() - started:.1f} s", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
