# texpipe

Builds a texture pack for OpenEnroth from your own copy of Might and Magic VII: higher resolution versions of the
bitmaps, icons and sprites in the game's LOD files, with normal and roughness maps for the bitmaps. The pack only
holds derivatives of the game's images, and the game has to be installed to use it.

The steps run in order: `extract`, `upscale`, `maps`, then `pack`. The optional `regen` pass sits between `maps` and
`pack`. Extract, upscale and regen skip what they already did, so a rerun only redoes what changed.

## Setup

Needs Python 3.11 or later and Pillow:

```
pip install -r tools/texpipe/requirements.txt
```

The LOD files are read with the engine's own `LodTool`, so build the `LodTool` target first. Pass its path with
`--lodtool` or set `TEXPIPE_LODTOOL`.

Run the commands from `tools/texpipe`, as `python -m texpipe <command>`.

## Commands

Put everything under the `mm7_hd` folder at the repository root. It is gitignored, and nothing generated from the game
data may be committed.

**extract** dumps every bitmap and icon as a PNG, with the palette applied and transparency taken from the image's own
flag and `resources/data/resource_mask_table.json`, the same way the engine does it. Palettes and the few entries that
are not images are skipped. So are the eight bitmaps whose names collide as files, like `hhm3el2` and `hhm3el2 ` with
a trailing space: Windows writes both to the same file, so neither CRC-32 could be trusted. Next to each PNG goes a
JSON file with the entry name, section, size, transparency and the CRC-32 of the raw LOD entry, which the engine uses to
check that a pack matches the installed game. `summary.json` lists what was skipped, left out and failed.

```
python -m texpipe extract --data ../../mm7 --out ../../mm7_hd/work --lodtool <path to LodTool>
```

With `--sprites --palettes <file>`, extract also writes one image per sprite and palette it is drawn with. The game
colors a sprite with palettes from the sprite frame table, which only the engine reads, so make the file first with
the engine:

```
OpenEnroth dump-sprite-palettes ../../mm7_hd/sprite_palettes.json
python -m texpipe extract --data ../../mm7 --out ../../mm7_hd/work --lodtool <path to LodTool> --sprites --palettes ../../mm7_hd/sprite_palettes.json
```

Palette index 0 is transparent, and like other transparent pixels it takes the average color of its opaque
neighbours, so filtering does not darken the edges.

Extract and upscale skip what is already done. LodTool only runs again when a LOD file changed, and an image is only
redone when its output is older than its inputs, or for upscale was made with another backend or scale. Pass
`--force` to redo everything. The realesrgan backend works in batches of 1000 images, so an interrupted run keeps the
batches it finished.

**upscale** runs the extracted images through a backend at 2x or 4x:

- `nearest` is plain nearest neighbour scaling. It changes nothing but the size, which makes it the way to test the
  pipeline and the engine end to end.
- `realesrgan` runs the `realesrgan-ncnn-vulkan` executable, which needs a GPU with Vulkan. It is downloaded into
  `~/.cache/texpipe` on first use, or `TEXPIPE_CACHE` if set. Pass `--binary` to use one you already have. Alpha is
  upscaled separately from the color and put back afterwards. Bitmaps tile across walls and floors, so they are padded
  with a border wrapped around from the opposite edge before upscaling, and cropped back after, which keeps the seams
  continuous. The model also drops the bright speckles of dithered textures, so for the sections listed in
  `preserve_brightness`, bitmaps and sprites by default, each channel of the result is rescaled to the source's mean
  over the pixels that are at least half opaque. `max_brightness_factor` caps the rescale, 3 by default, so that a
  channel the model left nearly black does not turn into speckles. The factors go into the image's JSON file under
  `upscaled/`.

```
python -m texpipe upscale --work ../../mm7_hd/work --backend nearest --scale 2
```

Without `--backend` and `--scale`, each image gets what `texpipe/default_config.toml` says for its section, name and
width. The default upscales bitmaps 4x and icons and sprites 2x with `realesrgan`, and sends icons narrower than 12
pixels and the minimap arrows to `nearest` at 2x, because the model smears tiny glyphs. Pass `--config` to use your own
copy.

**maps** makes a normal map and a roughness map for every upscaled bitmap, from its luminance. The normal map is a
Sobel filter over the slightly blurred luminance, in tangent space with the OpenGL convention: red to the right, green
to the top row, flat is (128, 128, 255). The roughness map sits at 90, the stock specular, where an area is as busy as
the image's average, and goes rougher or smoother where it is busier or calmer. Both wrap around the edges like the
upscale does. Their strength, blur and range are in the `[maps]` section of the config.

```
python -m texpipe maps --work ../../mm7_hd/work
```

**pack** writes the pack folder: `manifest.json`, `bitmaps/` and `icons/` with one PNG per LOD entry, the maps under
`normal/bitmaps/` and `roughness/bitmaps/`, sprites as `sprites/<entry>/<palette id>.png`, and a `README.txt`.
`--colors raw`, the default, says the images have the LOD palette colors, and the engine applies its saturation and
lightness settings to the pack's bitmaps and sprites. Use `--colors final` only if you have graded the
upscaled images yourself.

```
python -m texpipe pack --work ../../mm7_hd/work --out ../../mm7_hd/nearest2x
```

All commands take `--only <glob>`, which can be repeated, to handle only the LOD entries matching it, e.g.
`--only "ch1*" --only "ib-*"` for a quick run.

## The regen pass

`regen` sends upscaled images to an image-to-image model through the chat completions endpoint at openrouter.ai, with
a prompt that asks for a cleaner version of exactly that image and nothing else. The default model is
`google/gemini-3.1-flash-lite-image`, set in `[regen]`, and `--model` picks another. The key is read from the
`OPENROUTER_API_KEY` environment variable and nowhere else, and a run without it stops with an error. A run also stops
at once when the API answers that the key is invalid or out of credit.

`--section sprites`, the default, sends each sprite once, in the first palette it is drawn with. `--section icons`
sends the icons that `[regen.icons]` includes, the character and NPC portraits and the UI frame by default, with their
own prompt, temperature and thresholds. Icons have no palette variants, so each is sent as it is. The image goes on a
flat background in whichever of a few colors its own colors are furthest from. The result is held to the original:

- The alpha always comes from the upscaled stock alpha, and edge pixels keep the upscaled colors, so silhouettes and
  the engine's picking stay exactly as before.
- Three gates must pass, otherwise the upscaled image is used: the silhouette the model drew against the stock alpha
  (IoU), the largest per-channel difference of the mean color, and the mean luminance difference after a 2 px blur.
  Icons without transparency have no silhouette to compare and skip the first gate. The thresholds are in
  `[regen.thresholds]` and `[regen.icons.thresholds]`, and the scores go into `regen/<model>/sprites/<entry>.json` or
  `regen/<model>/icons/<entry>.json`.
- The other palettes of a sprite are derived from the generated one: each pixel is matched to the nearest color of the
  base palette and moved by the difference to the same entry of the target palette. Exact palette colors recolor
  exactly as the game does, and costs come once per sprite, not once per palette.

The `include` and `exclude` globs of each section pick the LOD entries it sends. The default config leaves out the
spell effects (`sp[0-9]*`, `spell*`), arrows and splashes (`arrow*`, `splash*`) and torch flames (`torch*`). They are
glows, smears and a few pixels on screen, with no surface detail to recover, and the animated ones would flicker.

`--dry-run` calls nothing. It prints how many images a run would send, the first few names, and the projected cost
from `cost_per_image`. The run is resumable: an image with a record is not sent again, and
`regen/<model>/summary.json` keeps the cost so far. `--max-cost` stops it before the total would go over a budget.
`--rescore` gates the stored sprites again with the current thresholds, without calling the model. `regen-report`
prints the gate scores, the fallbacks, the cost, the frame to frame consistency against the stock animation and a check
of the palette mapping, and draws a contact sheet with `--sheet`.

```
python -m texpipe regen --work ../../mm7_hd/work --palettes ../../mm7_hd/sprite_palettes.json --dry-run
python -m texpipe regen --work ../../mm7_hd/work --palettes ../../mm7_hd/sprite_palettes.json --max-cost 300
python -m texpipe regen --work ../../mm7_hd/work --palettes ../../mm7_hd/sprite_palettes.json --section icons
python -m texpipe regen-report --work ../../mm7_hd/work --palettes ../../mm7_hd/sprite_palettes.json --model google/gemini-3.1-flash-lite-image --sheet sheet.png
```

## Two packs

The same work folder makes two packs. `esrgan` holds only upscaled images and their maps. `regen` is the same pack
with the sprites and icons that passed the regen gates in place of the upscaled ones, listed in the pack's
`generated.txt`. Keeping both lets a player switch back with one line in the ini.

```
python -m texpipe pack --work ../../mm7_hd/work --out ../../mm7_hd/esrgan
python -m texpipe pack --work ../../mm7_hd/work --out ../../mm7_hd/regen --regen-from google/gemini-3.1-flash-lite-image
```

## Using the pack

Set `texture_pack` in the `[graphics]` section of `openenroth.ini` to the pack folder. A relative path is resolved
against the game data folder, so with the game in `mm7` next to `mm7_hd`:

```
[graphics]
texture_pack = ../mm7_hd/esrgan
enhanced = true
ui_scale = 2
```

The log then says `Using texture pack '...' with N bitmaps, M icons and K sprite images.` at startup. Point
`texture_pack` at `../mm7_hd/regen` for the generated sprites and icons.

`enhanced` turns on the enhanced renderer: linear blending, bloom, FXAA, sun shadows and world space billboards, with
the 3D world drawn at the window's resolution. Entries in the ini that differ from their defaults win over it.
`ui_scale` draws the UI at 2 or 4 times the render size, so that the pack's icons show their detail. Both
`texture_pack` and `ui_scale` take effect on restart.

## Tests

```
pip install pytest
python -m pytest
```

The tests use small generated images and don't need the game data.
