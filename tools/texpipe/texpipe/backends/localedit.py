"""The regen pass's local backend: an image-to-image diffusion model on this computer's GPU.

The model needs PyTorch and diffusers, which have no place in texpipe's requirements, so `local_worker.py` runs it in
an environment of its own, started once and fed one image at a time. The client has the same `edit` call as the cloud
`ImageEditClient`, so the regen pass, its gates, the alpha and the palette mapping stay the same.
"""

import json
import subprocess
import tempfile
import threading
from pathlib import Path

from PIL import Image

from texpipe.backends.imageedit import EditResult, ImageEditError

WORKER = Path(__file__).with_name("local_worker.py")
LOCAL_PREFIX = "local/"  # Model names that pick this backend.
PYTHON_ENV = "TEXPIPE_REGEN_PYTHON"


def is_local(model: str) -> bool:
    return model.startswith(LOCAL_PREFIX)


class LocalEditClient:
    """Runs the worker on the first `edit` and keeps it until `close`. Calls from several threads take turns."""

    def __init__(self, python: Path, settings: dict):
        self._python = python
        self._settings = settings
        self._process: subprocess.Popen | None = None
        self._lock = threading.Lock()
        self._temp = tempfile.TemporaryDirectory(prefix="texpipe-local-")
        self._count = 0

    def _start(self) -> subprocess.Popen:
        settings = Path(self._temp.name) / "settings.json"
        settings.write_text(json.dumps(self._settings), encoding="utf-8")
        process = subprocess.Popen(
            [str(self._python), str(WORKER), str(settings)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        ready = process.stdout.readline()
        if not ready:
            raise ImageEditError(
                f"the local model's worker exited while loading, see its output above ({self._python})"
            )
        return process

    def edit(
        self, model: str, prompt: str, image: Image.Image, seed: int | None = None, temperature: float | None = None
    ) -> EditResult:
        with self._lock:
            if self._process is None:
                self._process = self._start()
            self._count += 1
            source = Path(self._temp.name) / f"in{self._count}.png"
            target = Path(self._temp.name) / f"out{self._count}.png"
            image.save(source)
            request = {"input": str(source), "output": str(target), "prompt": prompt, "seed": seed or 0}
            self._process.stdin.write(json.dumps(request) + "\n")
            reply = json.loads(self._process.stdout.readline() or '{"error": "the worker exited"}')
            if "error" in reply:
                raise ImageEditError(f"local model failed: {reply['error']}")
            with Image.open(target) as result:
                return EditResult(result.convert("RGB"), 0.0)

    def close(self) -> None:
        if self._process is not None:
            self._process.stdin.close()
            self._process.wait()
            self._process = None
        self._temp.cleanup()
