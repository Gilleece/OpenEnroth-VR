"""Client for an image-to-image edit endpoint that speaks the chat completions protocol.

The request carries a text prompt and one input image, the response carries the edited image and the cost of the
call. The HTTP transport is a parameter, so tests can run without a network.
"""

import base64
import io
import json
import os
import urllib.request
from collections.abc import Callable
from dataclasses import dataclass

from PIL import Image

ENDPOINT = "https://openrouter.ai/api/v1/chat/completions"
API_KEY_ENV = "OPENROUTER_API_KEY"
TIMEOUT_SECONDS = 300

Transport = Callable[[str, dict, dict], dict]  # (url, headers, body) -> parsed JSON response.


class ImageEditError(RuntimeError):
    pass


@dataclass(frozen=True)
class EditResult:
    image: Image.Image
    cost: float  # In the account's currency, as the response's usage reports it.


def http_transport(url: str, headers: dict, body: dict) -> dict:
    request = urllib.request.Request(url, data=json.dumps(body).encode(), headers=headers)
    with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
        return json.load(response)


def api_key() -> str:
    """Reads the API key from the environment, the only place it may come from."""
    key = os.environ.get(API_KEY_ENV)
    if not key:
        raise ImageEditError(f"Set the {API_KEY_ENV} environment variable to the API key.")
    return key


def encode_png(image: Image.Image) -> str:
    buffer = io.BytesIO()
    image.save(buffer, "PNG")
    return "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()


def decode_image(data_url: str) -> Image.Image:
    return Image.open(io.BytesIO(base64.b64decode(data_url.split(",", 1)[1]))).convert("RGB")


def build_request(model: str, prompt: str, image: Image.Image, seed: int | None, temperature: float | None) -> dict:
    body = {
        "model": model,
        "modalities": ["image", "text"],
        "usage": {"include": True},
        "messages": [
            {
                "role": "user",
                "content": [
                    {"type": "text", "text": prompt},
                    {"type": "image_url", "image_url": {"url": encode_png(image)}},
                ],
            }
        ],
    }
    if seed is not None:
        body["seed"] = seed
    if temperature is not None:
        body["temperature"] = temperature
    return body


def parse_response(response: dict) -> EditResult:
    """Takes the first image and the cost out of a response, or raises with what the response said instead."""
    if "error" in response:
        raise ImageEditError(f"request failed: {json.dumps(response['error'])[:300]}")
    message = response["choices"][0]["message"]
    images = message.get("images") or []
    if not images:
        raise ImageEditError(f"no image in the response: {str(message.get('content'))[:300]}")
    return EditResult(decode_image(images[0]["image_url"]["url"]), float(response.get("usage", {}).get("cost", 0.0)))


class ImageEditClient:
    def __init__(self, transport: Transport = http_transport, key: str | None = None):
        self._transport = transport
        self._key = key

    def edit(
        self, model: str, prompt: str, image: Image.Image, seed: int | None = None, temperature: float | None = None
    ) -> EditResult:
        headers = {"Authorization": f"Bearer {self._key or api_key()}", "Content-Type": "application/json"}
        return parse_response(
            self._transport(ENDPOINT, headers, build_request(model, prompt, image, seed, temperature))
        )
