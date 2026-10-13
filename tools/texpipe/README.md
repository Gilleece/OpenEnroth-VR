# texpipe

Builds a texture pack for OpenEnroth from your own copy of Might and Magic VII: higher resolution versions of the
bitmaps, icons and sprites in the game's LOD files, with normal and roughness maps for the bitmaps. The pack only
holds derivatives of the game's images, and the game has to be installed to use it.

The steps run in order: `extract`, `upscale`, `maps`, then `pack`. The optional `regen` pass sits between `maps` and
`pack`, and the optional `bake` adds baked lighting to a pack after `pack`. Extract, upscale and regen skip what
they already did, so a rerun only redoes what changed.

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
neighbours, so filtering does not darken the edges. In bitmaps, like the shore tiles, the colors carry on ring by ring
across the whole transparent area, because the 4x upscale reads far past the first ring.

Extract and upscale skip what is already done. LodTool only runs again when a LOD file changed, and an image is only
redone when its output is older than its inputs, or for upscale was made with another backend, scale, rule option or
wrapped border. Pass
`--force` to redo everything. The realesrgan backend works in batches of 1000 images, so an interrupted run keeps the
batches it finished.

**upscale** runs the extracted images through a backend at 2x or 4x:

- `nearest` is plain nearest neighbour scaling. It changes nothing but the size, which makes it the way to test the
  pipeline and the engine end to end.
- `realesrgan` runs the `realesrgan-ncnn-vulkan` executable, which needs a GPU with Vulkan. It is downloaded into
  `~/.cache/texpipe` on first use, or `TEXPIPE_CACHE` if set. Pass `--binary` to use one you already have. Alpha is
  upscaled separately from the color and put back afterwards. Bitmaps tile across walls and floors, so they are padded
  with a border wrapped around from the opposite edge before upscaling, and cropped back after, which keeps the seams
  continuous. The border, `wrap_border`, is 64 pixels, wide enough for both sides of a seam to come out alike, and
  `tile_size` keeps the binary from splitting a padded bitmap into tiles of its own. Along an axis where the bitmap's
  opposite edges differ by more than `wrap_tolerance` times the step between neighbouring rows inside it, like the
  snow and dirt sides of a transition tile, the border copies the edge instead of wrapping. The model also drops the
  bright speckles of dithered textures, so for the sections listed in `preserve_brightness`, bitmaps and sprites by
  default, each channel of the result is rescaled to the source's mean over the pixels that are at least half opaque.
  `max_brightness_factor` caps the rescale, 3 by default, so that a channel the model left nearly black does not turn
  into speckles. The factors go into the image's JSON file under `upscaled/`.

```
python -m texpipe upscale --work ../../mm7_hd/work --backend nearest --scale 2
```

Without `--backend` and `--scale`, each image gets what `texpipe/default_config.toml` says for its section, name and
width. The default upscales bitmaps 4x and icons and sprites 2x with `realesrgan`, and sends icons narrower than 12
pixels and the minimap arrows to `nearest` at 2x, because the model smears tiny glyphs. Pass `--config` to use your own
copy.

A rule can also change an image around the upscale. `seam_blend` eases that many columns and rows on each side of a
tile towards each other before upscaling, for tiles whose opposite edges don't match, like the indoor water `wtrtyl`.
`soften` blends the upscaled image towards a copy blurred by two source pixels, from 0 for none to 1, for results that
come out too crisp, like the wave crests of the sea `hdwtr000` to `hdwtr007`.

**maps** makes a normal map and a roughness map for every upscaled bitmap, from its luminance. The normal map is a
Sobel filter over the slightly blurred luminance, in tangent space with the OpenGL convention: red to the right, green
to the top row, flat is (128, 128, 255). The roughness map sits at 90, the stock specular, where an area is as busy as
the image's average, and goes rougher or smoother where it is busier or calmer. Both wrap around the edges, or copy
them, as the upscale did. Their strength, blur and range are in the `[maps]` section of the config.

```
python -m texpipe maps --work ../../mm7_hd/work
```

**pack** writes the pack folder: `manifest.json`, `bitmaps/` and `icons/` with one PNG per LOD entry, the maps under
`normal/bitmaps/` and `roughness/bitmaps/`, sprites as `sprites/<entry>/<palette id>.png`, sprite shapes under
`shape/sprites/`, and a `README.txt`.
`--colors raw`, the default, says the images have the LOD palette colors, and the engine applies its saturation and
lightness settings to the pack's bitmaps and sprites. Use `--colors final` only if you have graded the
upscaled images yourself. Every PNG pack writes is saved again with Pillow's `optimize` on all cores, which is
lossless and makes the pack a few percent smaller. A PNG is only replaced when the result decodes to the same pixels
and is smaller. `optimize_png = false` in the config's `[pack]` section turns it off, and `workers = <n>` there runs it
in n processes instead of one per core, to leave cores free for other work.

```
python -m texpipe pack --work ../../mm7_hd/work --out ../../mm7_hd/nearest2x
```

With `--archive <file.zip>`, pack also zips the pack folder for sharing, under a folder of the pack's name with
`README.txt` first, and prints the archive's size. The order and the timestamps are fixed, so the same pack makes the
same archive. PNGs are stored as they are, they don't compress any further. With `--archive-split <GiB>` as well, the
archive comes as numbered parts, `<name>.part1.zip` and on, each at most that size. Every part is a complete zip of
some of the pack's files under the same folder, so extracting all of them into one place gives the pack. `README.txt`
opens the first part, and every part carries a `parts.txt` that lists them all.

The lite profile, `texpipe/lite_config.toml`, makes a pack about half the size: bitmaps at 2x instead of 4x, the rest
as in the full one. Packing with it scales the 4x bitmaps of a full work folder down with Lanczos and makes their
maps again at 2x, so no image goes through the model again. An upscale run with it makes the bitmaps at 2x in the
first place, the model's 4x output scaled down the same way.

```
python -m texpipe pack --work ../../mm7_hd/work --out ../../mm7_hd/esrgan-lite --config texpipe/lite_config.toml --archive ../../mm7_hd/esrgan-lite.zip
```

**bake** bakes the lighting of every map into the pack with the engine: irradiance probes per map, in
`gi/<map file>.bin`, which the engine lights the world with. It runs `OpenEnroth bake-gi` once per map, so every map
gets its own time and a crash costs only that map. It then checks each probe file against the format and records the
map in the manifest's `gi` section with the CRC-32 of the `games.lod` entry, which texpipe reads itself and compares
with the one the engine reports. The engine ignores the probes of a map whose entry has changed since. A map that
fails loses its probe file and manifest entry, and the maps not baked in a run keep theirs. It prints the time and the
size of each map, then the total, and exits with 1 if any map failed. Pass the engine with `--engine` or set
`TEXPIPE_OPENENROTH`, and `--map d18.blv`, which can be repeated, bakes only those maps.

```
python -m texpipe bake --data ../../mm7 --out ../../mm7_hd/esrgan --engine <path to OpenEnroth>
```

Bake adds to the manifest `pack` wrote and refuses to run without one. A later `pack` keeps the baked lighting
already in the pack folder when it writes the manifest again, as long as the config's `[pack] gi` is on, which it is
in both profiles. Turned off, `pack` removes it from the pack.

**verify** checks a pack against the installed game the way the engine loads it, for packs that came from elsewhere:
every manifest entry against the CRC-32 of its LOD entry, every listed file present, color images at the LOD image's
size times 1, 2 or 4, maps and sprite shapes the size of the color images they go with, and the baked lighting
against the CRC-32 of its map in `games.lod` and the probe file format. It prints the counts and
the first failures, and exits with 1 if anything failed. The engine uses the original image for an entry that fails,
so a failure is an image the pack doesn't replace in game. It reads the LOD archives itself, LodTool isn't needed.

```
python -m texpipe verify --pack ../../mm7_hd/esrgan --data ../../mm7
```

All commands take `--only <glob>`, which can be repeated, to handle only the LOD entries matching it, e.g.
`--only "ch1*" --only "ib-*"` for a quick run.

## The regen pass

`regen` sends upscaled images to an image-to-image model through the chat completions endpoint at openrouter.ai, with
a prompt that asks for a cleaner version of exactly that image and nothing else. The default model is
`google/gemini-3.1-flash-lite-image`, set in `[regen]`, and `--model` picks another. The key is read from the
`OPENROUTER_API_KEY` environment variable and nowhere else, and a run without it stops with an error. A run also stops
at once when the API answers that the key is invalid or out of credit.

The released pack doesn't use this pass: the full run was too expensive. A pilot on this computer's GPU with SDXL and
a tile ControlNet passed the gates but looked worse than the plain upscale. Flux Kontext, the stronger candidate for
a local run, is gated: its weights need a Hugging Face login with the model's licence accepted.

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

## Sprite depth

`depth` gives every sprite a shape image, so that the engine can draw billboards with per-pixel depth and lighting.
There is one per `sprites.lod` entry, palette independent, at `shape/sprites/<entry>.png` in the pack, RGBA8 and the
pixel size of the sprite's pack images:

- R and G hold the surface normal's x (right) and y (up) in the image plane, as `n * 0.5 + 0.5`. The engine rebuilds
  z.
- B holds the depth, 255 nearest the viewer and 0 farthest, linear in game units over the sprite's thickness, so the
  billboard plane is at 127.5.
- A is the coverage alpha of the color image.

The depth comes from Depth Anything V2 run on the upscaled image in the sprite's first palette, over a grey
background. It is relative depth, so it's normalised per animation group: the 2nd percentile of the opaque depths of
all the group's images maps to 0 and the 98th to 255, and the few pixels past them are clamped. One range per group
keeps the depth from pumping from frame to frame. The groups come from the frame table, through the `groups` that
`OpenEnroth dump-sprite-palettes` writes. The model blurs the outermost pixels of a silhouette into the background,
so their depth is taken from 2 pixels further in. The normals come from a Sobel over the depth blurred by 1 pixel,
with one LOD pixel as one game unit. Transparent pixels take the values of the opaque ones around them, so that
filtering doesn't pull the edges towards black. The manifest gets `"shape": {"thickness": ...}` in the sprite's entry.
The thickness is the full front to back depth in game units: 0.6 times the median opaque width of the group's images
in LOD pixels, clamped to 8 to 256. Sprites that the `[regen]` excludes match, spell effects, arrows, splashes
and torch flames, stay flat. The options are in the `[depth]` section of the config.

The model needs PyTorch, which has no place in texpipe's requirements, so it runs in an environment of its own.
`texpipe/depth_worker.py` is all that runs there. With a CUDA GPU, outside the repository:

```
python -m venv C:/texpipe-envs/depth
C:/texpipe-envs/depth/Scripts/python -m pip install torch --index-url https://download.pytorch.org/whl/cu128
C:/texpipe-envs/depth/Scripts/python -m pip install transformers pillow
```

The model is downloaded from Hugging Face on first use, into `HF_HOME` if set. Pass the environment's Python with
`--python` or set `TEXPIPE_DEPTH_PYTHON`:

```
OpenEnroth dump-sprite-palettes ../../mm7_hd/sprite_palettes.json
python -m texpipe depth --work ../../mm7_hd/work --palettes ../../mm7_hd/sprite_palettes.json --python C:/texpipe-envs/depth/Scripts/python
```

A rerun only redoes the groups whose upscaled images changed, `--force` redoes everything. `pack` picks the shape
images up on its own.

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

`enhanced` turns on the enhanced renderer: linear blending, bloom, FXAA, sun and torch shadows, ambient occlusion,
terrain variation, the baked indirect light of the pack's `bake` step and world space billboards, with the 3D world
drawn at the window's resolution. Entries in the ini that differ from their defaults win over it. The video options
screen toggles it, and has toggles of their own for sprite depth, sprite crossfade and the indirect light, which is
`global_illumination` in the ini.
`ui_scale` draws the UI at 2 or 4 times the render size, so that the pack's icons show their detail. Both
`texture_pack` and `ui_scale` take effect on restart.

## Tests

```
pip install pytest
python -m pytest
```

The tests use small generated images and probe files, and don't need the game data.
