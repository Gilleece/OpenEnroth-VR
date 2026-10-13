# texpipe

Builds a texture pack for OpenEnroth from your own copy of Might and Magic VII: higher resolution versions of the
images in `bitmaps.lod` and `icons.lod`. The pack only holds upscaled derivatives, and the game has to be installed to
use it.

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
`OpenEnroth dump-sprite-palettes <file>`. Palette index 0 is transparent, and like other transparent pixels it takes
the average color of its opaque neighbours, so filtering does not darken the edges.

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
  `preserve_brightness` each channel of the result is rescaled to the source's mean. The factors go into the image's
  JSON file under `upscaled/`.

```
python -m texpipe upscale --work ../../mm7_hd/work --backend nearest --scale 2
```

Without `--backend` and `--scale`, each image gets what `texpipe/default_config.toml` says for its section, name and
width. The default sends icons narrower than 12 pixels and the minimap arrows to `nearest`, because the model smears
tiny glyphs. Pass `--config` to use your own copy.

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

`regen` sends each upscaled sprite, in the first palette it is drawn with, to an image-to-image model through the
chat completions endpoint at openrouter.ai, with a prompt that asks for a cleaner version of exactly that frame and
nothing else. The key is read from the `OPENROUTER_API_KEY` environment variable and nowhere else. The sprite goes on
a flat background in whichever of a few colors its own colors are furthest from. The result is held to the original:

- The alpha always comes from the upscaled stock alpha, and edge pixels keep the upscaled colors, so silhouettes and
  the engine's picking stay exactly as before.
- Three gates must pass, otherwise the upscaled image is used: the silhouette the model drew against the stock alpha
  (IoU), the largest per-channel difference of the mean color, and the mean luminance difference after a 2 px blur.
  The thresholds are in `[regen.thresholds]`, the scores go into `regen/<model>/sprites/<entry>.json`.
- The other palettes of a sprite are derived from the generated one: each pixel is matched to the nearest color of the
  base palette and moved by the difference to the same entry of the target palette. Exact palette colors recolor
  exactly as the game does, and costs come once per sprite, not once per palette.

The run is resumable: a sprite with a record is not sent again, and `regen/<model>/summary.json` keeps the cost so far.
`--max-cost` stops it before the total would go over a budget. `--rescore` gates the stored outputs again with the
current thresholds, without calling the model. `regen-report` prints the gate scores, the fallbacks, the cost, the frame
to frame consistency against the stock animation and a check of the palette mapping, and draws a contact sheet.
`pack --sprites-from <model>` builds a pack with the generated sprites that passed, and lists them in the pack's
`generated_sprites.txt`.

```
python -m texpipe regen --work ../../mm7_hd/work --palettes ../../mm7_hd/sprite_palettes.json --max-cost 50
python -m texpipe pack --work ../../mm7_hd/work --out ../../mm7_hd/regen --sprites-from google/gemini-3.1-flash-lite-image
```

## Using the pack

Set `texture_pack` in the `[graphics]` section of `openenroth.ini` to the pack folder. A relative path is resolved
against the game data folder, so with the game in `mm7` next to `mm7_hd`:

```
[graphics]
texture_pack = ../mm7_hd/nearest2x
```

The log then says `Using texture pack '...' with N bitmaps and M icons.` at startup.

## Tests

```
pip install pytest
python -m pytest
```

The tests use small generated images and don't need the game data.
