import base64
import io
import json
from urllib.error import HTTPError

import pytest
from PIL import Image

from texpipe import gates, palettemap
from texpipe.backends.imageedit import ImageEditClient, ImageEditError, build_request, parse_response
from texpipe.regen import (
    RegenOptions,
    consistency,
    frame_sequences,
    on_background,
    output_path,
    record_path,
    rescore,
    run_regen,
)
from texpipe.workdir import upscaled_dir, write_json

BASE = bytes(value for index in range(256) for value in (index, index // 2, 255 - index))
TARGET = bytes(value for index in range(256) for value in (255 - index, index, index // 3))


def response_with(image: Image.Image, cost: float = 0.03) -> dict:
    buffer = io.BytesIO()
    image.save(buffer, "PNG")
    url = "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()
    return {"choices": [{"message": {"images": [{"image_url": {"url": url}}]}}], "usage": {"cost": cost}}


def sprite(color=(120, 60, 135)):
    # A 16x16 sprite: an opaque 8x8 square of one base palette color on a transparent background.
    image = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    image.paste((*color, 255), (4, 4, 12, 12))
    return image


class FakeEndpoint:
    """Answers every call with what `make` returns for the input image, and counts the calls."""

    def __init__(self, make):
        self.make = make
        self.calls = []

    def __call__(self, url, headers, body):
        self.calls.append(body)
        data = body["messages"][0]["content"][1]["image_url"]["url"].split(",", 1)[1]
        return response_with(self.make(Image.open(io.BytesIO(base64.b64decode(data))).convert("RGB")))


def make_work(tmp_path, names=("goba0",)):
    work = tmp_path / "work"
    for name in names:
        for palette_id, palette in ((247, BASE), (248, TARGET)):
            folder = upscaled_dir(work, "sprites") / name
            folder.mkdir(parents=True, exist_ok=True)
            palettemap.recolor(sprite(), BASE, palette).save(folder / f"{palette_id}.png")
    pngs = work / "lodpng" / "bitmaps"
    pngs.mkdir(parents=True)
    Image.frombytes("RGB", (256, 1), BASE).save(pngs / "pal247.png")
    Image.frombytes("RGB", (256, 1), TARGET).save(pngs / "pal248.png")
    palettes = tmp_path / "palettes.json"
    write_json(palettes, {"count": 2 * len(names), "sprites": {name: [247, 248] for name in names}})
    return work, palettes


def test_request_carries_the_model_seed_temperature_and_image():
    body = build_request("vendor/model", "prompt", Image.new("RGB", (2, 2)), 7, 0.2)
    assert (body["model"], body["seed"], body["temperature"]) == ("vendor/model", 7, 0.2)
    assert body["messages"][0]["content"][1]["image_url"]["url"].startswith("data:image/png;base64,")
    assert "seed" not in build_request("m", "p", Image.new("RGB", (2, 2)), None, None)


def test_response_without_an_image_is_an_error():
    with pytest.raises(ImageEditError):
        parse_response({"choices": [{"message": {"content": "sorry"}}]})
    with pytest.raises(ImageEditError):
        parse_response({"error": {"message": "no credits"}})


def test_missing_key_is_a_clear_error(monkeypatch):
    monkeypatch.delenv("OPENROUTER_API_KEY", raising=False)
    with pytest.raises(ImageEditError, match="OPENROUTER_API_KEY"):
        ImageEditClient(FakeEndpoint(lambda image: image)).edit("m", "p", Image.new("RGB", (2, 2)))


def test_client_returns_the_image_and_its_cost():
    result = ImageEditClient(FakeEndpoint(lambda image: image), key="k").edit("m", "p", Image.new("RGB", (2, 2)))
    assert result.cost == 0.03
    assert result.image.size == (2, 2)


def test_iou_of_identical_and_disjoint_masks():
    left = Image.new("L", (4, 2), 0)
    left.paste(255, (0, 0, 2, 2))
    right = Image.new("L", (4, 2), 0)
    right.paste(255, (2, 0, 4, 2))
    assert gates.iou(left, left) == 1.0
    assert gates.iou(left, right) == 0.0


def test_an_unchanged_image_passes_every_gate():
    reference = sprite()
    scores = gates.score(reference, reference, on_background(reference))
    assert scores.iou == 1.0
    assert scores.color_delta == 0.0
    assert scores.distance == 0.0
    assert scores.passes(gates.Thresholds())


def entry(palette, index):
    return tuple(palette[3 * index : 3 * index + 3])


def test_recolor_of_exact_palette_colors_matches_the_target_palette():
    recolored = palettemap.recolor(sprite(entry(BASE, 10)), BASE, TARGET)
    assert recolored.getpixel((5, 5)) == (*entry(TARGET, 10), 255)
    assert recolored.getpixel((0, 0))[3] == 0


def test_recolor_carries_detail_between_palette_colors_over():
    # One step above entry 10 in red is as close to entry 11, the tie goes to entry 10 and the step is carried over.
    red, green, blue = entry(BASE, 10)
    recolored = palettemap.recolor(sprite((red + 1, green, blue)), BASE, TARGET)
    target_red, target_green, target_blue = entry(TARGET, 10)
    assert recolored.getpixel((5, 5)) == (target_red + 1, target_green, target_blue, 255)


def test_recolor_never_matches_the_transparent_index():
    indices = palettemap.nearest_indices(Image.new("RGB", (1, 1), entry(BASE, 0)), BASE)
    assert indices.getpixel((0, 0)) != 0


def test_a_faithful_output_is_kept_and_its_palettes_derived(tmp_path):
    work, palettes = make_work(tmp_path)
    endpoint = FakeEndpoint(lambda image: image.resize((image.width * 2, image.height * 2)))
    options = RegenOptions(model="vendor/model")
    summary = run_regen(work, palettes, options, None, ImageEditClient(endpoint, key="k"))
    assert (summary["generated"], summary["fallbacks"], summary["cost"]) == (1, 0, 0.03)
    record = json.loads(record_path(work, "vendor/model", "goba0").read_text())
    assert record["passed"]
    derived = Image.open(output_path(work, "vendor/model", "goba0", 248)).convert("RGBA")
    stock = Image.open(upscaled_dir(work, "sprites") / "goba0" / "248.png")
    assert derived.getpixel((8, 8)) == stock.getpixel((8, 8))


def test_an_output_that_moves_the_silhouette_falls_back(tmp_path):
    work, palettes = make_work(tmp_path)
    endpoint = FakeEndpoint(lambda image: Image.new("RGB", image.size, (200, 200, 200)))
    summary = run_regen(work, palettes, RegenOptions(model="vendor/model"), None, ImageEditClient(endpoint, key="k"))
    assert summary["fallbacks"] == 1
    fallback = Image.open(output_path(work, "vendor/model", "goba0", 247))
    assert fallback.tobytes() == Image.open(upscaled_dir(work, "sprites") / "goba0" / "247.png").tobytes()


def test_a_second_run_makes_no_calls(tmp_path):
    work, palettes = make_work(tmp_path)
    endpoint = FakeEndpoint(lambda image: image)
    client = ImageEditClient(endpoint, key="k")
    run_regen(work, palettes, RegenOptions(model="vendor/model"), None, client)
    summary = run_regen(work, palettes, RegenOptions(model="vendor/model"), None, client)
    assert len(endpoint.calls) == 1
    assert summary["generated"] == 1


def test_the_budget_stops_the_run(tmp_path):
    work, palettes = make_work(tmp_path, ("goba0", "goba1", "goba2"))
    endpoint = FakeEndpoint(lambda image: image)
    options = RegenOptions(model="vendor/model", max_cost=0.05, workers=1)
    summary = run_regen(work, palettes, options, None, ImageEditClient(endpoint, key="k"))
    assert len(endpoint.calls) == 1
    assert "stopped" in summary


def test_frames_group_by_stem_and_direction():
    sequences = frame_sequences(["m1aa0", "m1ab0", "m1aa1", "m1ab1", "m1ac0", "tree01"])
    assert sequences == {("m1a", "0"): ["m1aa0", "m1ab0", "m1ac0"], ("m1a", "1"): ["m1aa1", "m1ab1"]}


def test_unchanged_animations_have_a_consistency_ratio_of_one(tmp_path):
    work, palettes = make_work(tmp_path, ("m1aa0", "m1ab0"))
    shifted = sprite((90, 60, 135))
    shifted.save(upscaled_dir(work, "sprites") / "m1ab0" / "247.png")
    run_regen(
        work, palettes, RegenOptions(model="vendor/model"), None, ImageEditClient(FakeEndpoint(lambda i: i), key="k")
    )
    assert consistency(work, "vendor/model", ["m1aa0", "m1ab0"])["median_ratio"] == 1.0


def test_dark_parts_inside_the_drawing_are_foreground():
    # A ring drawn on the background, its inside the background color: the inside is part of the sprite.
    image = Image.new("RGB", (12, 12), gates.BACKGROUND)
    image.paste((200, 0, 0), (2, 2, 10, 10))
    image.paste(gates.BACKGROUND, (4, 4, 8, 8))
    mask = gates.foreground_mask(image)
    assert mask.getpixel((5, 5)) == 255
    assert mask.getpixel((0, 0)) == 0
    assert sum(1 for value in mask.tobytes() if value) == 64


def test_rescore_gates_stored_outputs_again_without_calls(tmp_path):
    work, palettes = make_work(tmp_path)
    endpoint = FakeEndpoint(lambda image: image)
    run_regen(work, palettes, RegenOptions(model="vendor/model"), None, ImageEditClient(endpoint, key="k"))
    strict = RegenOptions(model="vendor/model", thresholds=gates.Thresholds(min_iou=1.01))
    assert rescore(work, palettes, strict, None)["fallbacks"] == 1
    assert len(endpoint.calls) == 1


def test_the_background_is_the_candidate_furthest_from_the_sprite():
    assert gates.choose_background(sprite((30, 200, 40))) == (255, 0, 255)
    assert gates.choose_background(sprite((240, 20, 230))) == (0, 255, 0)


def test_a_payment_error_stops_the_run(tmp_path):
    work, palettes = make_work(tmp_path, ("goba0", "goba1", "goba2"))

    def no_credit(url, headers, body):
        no_credit.calls += 1
        raise HTTPError(url, 402, "Payment Required", {}, None)

    no_credit.calls = 0
    options = RegenOptions(model="vendor/model", workers=1)
    summary = run_regen(work, palettes, options, None, ImageEditClient(no_credit, key="k"))
    assert no_credit.calls == 1
    assert "402" in summary["stopped"]
