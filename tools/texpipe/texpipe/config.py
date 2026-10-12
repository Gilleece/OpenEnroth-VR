"""The upscale config: which backend and scale to use for which asset class."""

import fnmatch
import tomllib
from dataclasses import dataclass
from pathlib import Path

DEFAULT_CONFIG = Path(__file__).with_name("default_config.toml")
SCALES = (2, 4)


@dataclass(frozen=True)
class Choice:
    backend: str
    scale: int


def load_config(path: Path | None) -> dict:
    with open(path or DEFAULT_CONFIG, "rb") as file:
        return tomllib.load(file)


def choose(config: dict, section: str, name: str, backend: str | None = None, scale: int | None = None) -> Choice:
    """Picks the backend and scale for one image. Explicit `backend` and `scale` win over the config."""
    rule = next((rule for rule in config.get("rule", []) if _rule_matches(rule, section, name)), config["default"])
    choice = Choice(backend or rule["backend"], scale or rule["scale"])
    if choice.scale not in SCALES:
        raise ValueError(f"scale must be one of {SCALES}, got {choice.scale}")
    return choice


def _rule_matches(rule: dict, section: str, name: str) -> bool:
    return rule.get("section", section) == section and fnmatch.fnmatchcase(name, rule.get("match", "*"))
