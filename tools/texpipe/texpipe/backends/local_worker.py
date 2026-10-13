"""Runs an image-to-image diffusion model for the regen pass. Runs in its own Python environment, see the README.

This file imports nothing from texpipe, only what that environment has: torch, diffusers and Pillow. texpipe starts
it once with a JSON settings file and then sends one request per line on stdin:

    {"input": <png>, "output": <png>, "prompt": <text>, "seed": <int>}

and reads one line back per request, {"seconds": <float>} or {"error": <text>}. The settings name the base model, the
ControlNet that holds the structure, and the sampling: {"base", "controlnet", "vae", "negative_prompt", "strength",
"steps", "guidance", "control_scale", "size"}.
"""

import json
import sys
import time
import traceback

import torch
from diffusers import AutoencoderKL, ControlNetModel, StableDiffusionXLControlNetImg2ImgPipeline
from PIL import Image, ImageFilter


def load(settings: dict):
    dtype = torch.float16
    controlnet = ControlNetModel.from_pretrained(settings["controlnet"], torch_dtype=dtype)
    vae = AutoencoderKL.from_pretrained(settings["vae"], torch_dtype=dtype)
    pipe = StableDiffusionXLControlNetImg2ImgPipeline.from_pretrained(
        settings["base"], controlnet=controlnet, vae=vae, torch_dtype=dtype, variant="fp16"
    )
    pipe.set_progress_bar_config(disable=True)
    return pipe.to("cuda")


def model_size(width: int, height: int, longest: int) -> tuple[int, int]:
    """The size the model works at: the longest side at `longest`, both sides multiples of 64."""
    scale = longest / max(width, height)
    return max(64, round(width * scale / 64) * 64), max(64, round(height * scale / 64) * 64)


def edit(pipe, settings: dict, request: dict) -> None:
    with Image.open(request["input"]) as opened:
        source = opened.convert("RGB")
    size = model_size(source.width, source.height, settings["size"])
    image = source.resize(size, Image.Resampling.LANCZOS)
    generator = torch.Generator("cuda").manual_seed(request["seed"])
    result = pipe(
        prompt=request["prompt"],
        negative_prompt=settings["negative_prompt"],
        image=image,
        control_image=image,
        strength=settings["strength"],
        num_inference_steps=settings["steps"],
        guidance_scale=settings["guidance"],
        controlnet_conditioning_scale=settings["control_scale"],
        width=size[0],
        height=size[1],
        generator=generator,
    ).images[0]
    result = result.resize(source.size, Image.Resampling.LANCZOS)
    # The input sits on a flat background, which an image to image model drifts and textures. Keep it outside a margin
    # around the drawing, so the edit stays on the sprite.
    background = source.getpixel((0, 0))
    drawing = Image.new("L", source.size, 0)
    tolerance = settings.get("background_tolerance", 24)  # Faint halo pixels count as background, as the gates see it.
    drawing.putdata(
        [
            255 if max(abs(a - b) for a, b in zip(pixel, background, strict=True)) > tolerance else 0
            for pixel in source.getdata()
        ]
    )
    margin = settings.get("margin", 8)
    if margin:
        drawing = drawing.filter(ImageFilter.MaxFilter(2 * margin + 1))
    Image.composite(result, source, drawing).save(request["output"])


def main() -> int:
    with open(sys.argv[1], encoding="utf-8") as file:
        settings = json.load(file)
    pipe = load(settings)
    print(json.dumps({"ready": True}), flush=True)
    for line in sys.stdin:
        if not line.strip():
            continue
        started = time.monotonic()
        try:
            edit(pipe, settings, json.loads(line))
            reply = {"seconds": round(time.monotonic() - started, 2)}
        except Exception:  # noqa: BLE001, the caller records the error per image and goes on.
            reply = {"error": traceback.format_exc()[-1500:]}
        print(json.dumps(reply), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
