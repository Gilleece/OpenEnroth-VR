# Development HOWTO

This document describes the development process we're following. It's required reading for anyone intending to contribute.


## Dependencies

The project requires:
* [CMake 3.27+](https://cmake.org/download/).
* [Python 3.x](https://www.python.org/downloads/) (optional, for style checks).

By default, we are using prebuilt dependencies, and they are resolved automatically during the cmake phase.

Minimum required compiler versions are as follows:
* Visual Studio 2022 17.10;
* GCC 15;
* AppleClang 16 (Xcode 16.3).

The following IDEs have been tested and should work fine:
* Visual Studio (2022 17.10 or later);
* Visual Studio Code (2022 or later);
* CLion (2025 or later).


## Building on \*nix platforms

This project uses the [CMake](https://cmake.org) build system. Use the following commands to clone the repository and build OpenEnroth:

```
$ git clone --recurse-submodules --shallow-submodules https://github.com/OpenEnroth/OpenEnroth.git
$ cd OpenEnroth
$ cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build
```

To cross-compile for 32-bit x86, you can pass `-m32` via compiler flags to cmake. In the snipped above that would mean running `export CFLAGS="-m32" CXXFLAGS="-m32"` first.

You can also select platform dependent [generator](https://cmake.org/cmake/help/latest/manual/cmake-generators.7.html) for your favorite IDE.


## Building on MacOS

Everything written above for \*nix platforms applies.

Make sure you have the latest Xcode installed. Do this through the App Store or by downloading it from the [Apple Developer website](https://developer.apple.com/download/more/).


## Building on Windows

* Get [git](https://git-scm.com/download/win) and [Visual Studio 2022 17.10+](https://visualstudio.microsoft.com/downloads/). Note that you will need MSVC compiler, so while you can use Visual Studio Code, you will still need to download the plain old Visual Studio.
* Make sure you have Windows SDK v10.0.17134.0 or higher (required for UTF-8 locale support).
* Clone, fork or download the repo `https://github.com/OpenEnroth/OpenEnroth`.
* Setup CMake:
  * either install standalone cmake from the official website,
  * or add Microsoft one (that's coming with the VS installation) to your PATH environment variable (e.g `c:\Program Files\Microsoft Visual Studio\2022\<edition>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`). 
* Open the folder in Visual Studio.
* Select build configuration (x32 or x64) and wait for CMake configuration to complete.
* Select startup item as `OpenEnroth.exe`.
* Run!

If you wish you can also disable prebuilt dependencies by turning off `OE_USE_PREBUILT_DEPENDENCIES` cmake option and pass your own dependencies source, e.g. via [vcpkg](https://github.com/microsoft/vcpkg) integration.

__Be aware__ that Visual Studio has a bug with git submodules not syncing between branches. So when checking out the branch or switching to different branch you may need to run the following command manually: `git submodule update --init`


## Coding style

For the C++ code we are following the [Google C++ Style Guide](http://google.github.io/styleguide/cppguide.html). Source code is automatically checked against it and pull request will fail if you don't follow it.

To perform a style check before pushing anything you can build `check_style` target. In Visual Studio you can do that by going to ***Solution Explorer → Change Views → CMake targets***. Right click and build `check_style`, errors will be listed in output.

CI also runs `clang-tidy` with the checks listed in `.clang-tidy`, and any finding fails the build. To run it locally you need clang-tidy 22, with `run-clang-tidy` alongside it on PATH. Distro packages are usually older than that. The PyPI `clang-tidy` package has both binaries and is the one CI installs. Install it with pipx or into a virtualenv, not into the system python. Reconfigure so that cmake finds it, then build the `check_tidy` target. Only the ninja and makefile generators offer that target, because they are the ones that write a compile commands database.

We also follow some additional style preferences, as listed below.

Documentation:
* Documentation should be in doxydoc format with `@` used for tags, and starting with `/**` comment introducer. Use `@param[out]` for output parameters.
* Documentation should be written in English.
* Please leave original function offsets intact. If you have a chance, move them to doxygen comments using `@offset` doxygen tag (e.g. `@offset 0xCAFEBEEF`).

Naming:
* Use `MM_` prefix for macro naming. Macro names should be in `SNAKE_CASE_ALL_CAPS`.
* Use `SNAKE_CASE_ALL_CAPS` for `enum` values. E.g. `ITEM_CRATE_OF_ARROWHEADS`, `ITEM_SLOT_RING6`.
* For naming enum values, prefix the name with the name of the type, e.g. `MONSTER_TROLL_A` for a value of `enum class MonsterId`. For values that are then used in array bounds, put the `_FIRST`, `_LAST` and `_COUNT` right after the name of the type, e.g. `ITEM_FIRST_MESSAGE_SCROLL`.
* Use `CamelCase` for everything else.
* Type names should start with a capital letter. E.g. `IndexedArray`, `InputAction`, `LogLevel`. This applies to all types, including classes, structs, enums and typedefs, with certain exceptions as listed below.
* Method & function names should start with a lowercase letter. E.g. `Vec3::length`, `gridCellToWorldPosX`, `ceilingHeight`.
* Getters are named after what they return, without a `get` prefix. Setters do use a `set` prefix. So it's `damage()` and `setDamage()`, and not `getDamage()` and `setDamage()`.
* Variable names should start with a lowercase letter. E.g. `int monsterCount = level->monsterCount()`.
* Names of private members should start with an underscore to visually distinguish them from variables without having to spell out `this->` every single time. E.g. `_initialized = true`, where `_initialized` is a member field.
* Note that the above doesn't apply to POD-like types as for such types all members are public and are named just like ordinary variables.
* Exceptions to the rules above are STL-compatible interfaces, which should follow STL naming rules. So it's `value_type` for iterator value type, and `push_back` for a method that's adding an element to a container.

Code formatting:
* Lines are at most 200 columns, `check_style` enforces it. Prefer ~120, but readability wins over the number: a long line is fine, ugly wrapping isn't. Applies to all file types, not just C++. Don't wrap at 80!
* Comments stay within 120 columns, the 200 limit is for code. The exception is a trailing comment, it's part of the code line it's on and only needs to fit in 200 together with it.
* An `if`, a `for` or a `while` doesn't need braces when its body is a single statement that carries no braces itself. This applies recursively, so a brace at any depth forces braces all the way up. A braced initializer doesn't count.
* An `if` with an `else` braces all of its bodies. A lone `if` doesn't have to. When an `else` holds nothing but an `if`, write `} else if (...) {`.
* A `do`/`while`, a `switch` and a `try`/`catch` always brace their bodies.
* `*` and `&` in type declarations should be preceded by a space. So it's `char *string`, and not `char* string`.
* Sort method definitions in `.cpp` files in the same order as they appear in the `.h` file.
* In header files, use an additional `private:` label before listing all class fields at the end of the class declaration.
* Use `virtual` prefix for all virtual functions, even when `override` is also present.
* When an `#else` or `#endif` is far enough from its `#if` that the two don't fit on screen together, repeat the condition there verbatim, e.g. `#endif // _WINDOWS`. In an `#elif` chain, repeat the condition of the last `#elif`.
* Indent after the `#` inside a conditional, e.g. `#   define MM_FOO 1`, when the block is compact enough to read as preprocessor logic. A conditional wrapping ordinary code is left at column zero.

Language features:
* We use C++23. Prefer modern alternatives where appropriate, e.g. `contains()` instead of `find() != end()`.
* Use `using Alias = Type` instead of `typedef Type Alias`.
* Use `enum class`es followed by `using enum` statements instead of ordinary `enum`s. This provides type safety without changing the syntax. For flags, use `Flags` class.
* It's OK to use plain `enum`s if you really need to have implicit casts to integer types, but this is a very rare use case. If you're using `enum` values to index into some array, consider using `enum class` coupled with `IndexedArray`.
* Make your code speak for itself when it comes to ownership. If a function takes ownership of one of its parameters, it should take `std::unique_ptr` by value. If it allocates its result and passes ownership to the caller, then it should return `std::unique_ptr`.
* Use an `int` unless you need something else. Don’t try to avoid negative values by using `unsigned`, this implies many changes to the usual behavior of integers and can be a source of bugs. See a section in [core guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#arithmetic) for more info. However, don't overdo it, use `size_t` for indexing into STL containers.
* For string function parameters, use `std::string_view` passed by value. 
  * In general, you shouldn't bother optimizing code paths where you might save an allocation by moving in an `std::string` or passing it by const reference, the performance benefits are almost always negligible.
  * Use `TransparentString*` classes if you need to index into a string map using `std::string_view` keys.
  * However, feel free to use `std::string` or `const std::string &` parameters where it makes your code simpler (e.g. by avoiding jumping through hoops if you'll need to create an intermediate `std::string` object anyway).
  * C++ is notoriously bad when it comes to string concatenation (you can't concatenate `std::string_view` with a string literal using `operator+`, and never will). In most cases you should be fine just using `fmt::format` for this. If `fmt::format` looks like an overkill, use `join` from `Utility/String/Join.h`.
* We generally refrain from using namespaces because OpenEnroth is a relatively small codebase, and we don't need the measures advocated by the Google style guide to prevent name clashes.
  * We don't put user-facing classes into namespaces because it ultimately leads to code where you have `ns1::Context` and `ns2::Context`, and when coupled with a bit of `using` here and there this makes the code harder to read and reason about. Please spend some time coming up with good names for your classes instead.
  * We sometimes use namespaces to group related functions, e.g. see `namespace lod`.
  * Exception is `namespace detail` that you're encouraged to use to hide implementation details and to prevent cluttering of the global namespace.
  * Use `static` for functions and variables that are private to a `.cpp` file, and not an anonymous namespace. This is the opposite of what the Google style guide advises, but it reads better, and it's what the codebase does.
* `std::string`s in the code are assumed to be UTF8-encoded as advised by [utf8everywhere](https://utf8everywhere.org/). On Windows this is ensured by the `UnicodeCrt` class that sets up the standard library to use UTF8 encoding. Thus, you don't need to bother with `std::u8string`, and you can safely convert `std::filesystem::path` to string by calling `path.string()`.
  * This is not yet true for in-game strings. E.g. strings for the Russian localization are in CP1251.

Error handling:
* Use `assert`s to check for coding errors and conditions that must never be false, no matter how the program is run.
* Use exceptions for non-recoverable errors. It's usually OK to just throw an instance of `class Exception`.
* Use `Logger` for warnings and recoverable errors.
* We don't yet have a mechanism for displaying errors to the user through the UI. This document will be updated once this is implemented.

There is a lot of code in the project that doesn't follow these conventions. Please feel free to fix it, preferably not mixing up style and logical changes in the same PR.


## Code Organization

OpenEnroth code is broken up as follows:
* `thirdparty` – this is where all external libraries go.
* `Utility` – generic utility classes and functions go here. Utility classes should be domain-independent (e.g. should make sense in a context of some other project) and should depend only on `thirdparty` libraries.
* `Library` – collection of independent libraries that the engine is built on top of. Code here can depend on `Utility` and other libraries in `Library`. However, there should be no cyclical dependencies between libraries here.
* `Library/Platform` is our platform abstraction layer on top of SDL.
* `Core` – collection of libraries that are specific to OpenEnroth, including parts of the engine itself. The difference from `Library` is that code here makes little sense in the context of another project. Code here can depend on `Utility` and `Library`, and as in `Library` there should be no cyclical dependencies.
* The rest of the code is currently pretty tangled with each part depending on each other. This document will be updated once we have some progress there.

Our basic guidelines for code organization are:
* One `CMakeLists.txt` file per folder. Exceptions are /android, /CMakeModules and /resources.
* One class per source file, with the name of the source file matching the name of the class. Exceptions are small structs, which are usually easier to pack into a single source file, and helper classes, which generally should stay next to the main class. Note that this guideline doesn't apply to source files that mainly declare functions.
* Include a header from another folder by its full path from `src`, as in `#include "Engine/Graphics/Indoor.h"`. Only headers in the same folder are included by bare name, and `..` never appears in an include path. Folder-relative paths hide dependency edges from every tool that reads the include graph, so a package can look decoupled while it isn't.


## Rendering and VR

The 3D world is drawn through a small set of pieces that also let it go to a VR headset. This section is a map of them for anyone changing the renderer.

#### Camera views

`CameraView` in `Engine/Graphics/CameraView.h` holds everything a 3D pass needs to know about the camera it draws from: the eye position, the view and projection matrices, the frustum planes and the viewport in the render target. `CameraView::fromCamera3D` builds one from `pCamera3D`, which follows the party between frames and can be aimed at another pose with `Camera3D::updateFromPose`. `CameraView::fromPose` builds one from any position and orientation with an asymmetric frustum, which is how a headset eye is described. `Renderer::BeginScene3D` takes a `CameraView`, and every draw in the pass uses its matrices.

#### World frames

`WorldFrame` in `Engine/Graphics/WorldFrame.h` splits a frame of the 3D world into `prepare`, `draw` and `finish`, and `Engine::drawWorld` runs them. `prepare` does all the work that changes engine state. It builds the light stacks, the billboard lists, the particles and the bloodsplat decals for the frame, from whatever `pCamera3D` looks at. `draw` only reads that state, so it can run more than once per frame, once per headset eye for example. Code that advances an animation, adds a light, rolls a random number or edits a list must not run in `draw`. `finish` advances the special effects and runs once, after the last draw.

Outdoors the sky, the terrain and the buildings are lit with the previous frame's lights. The original game drew them before it rebuilt the light stacks for the frame, and `prepare` keeps that look by taking a `LightsSnapshot` of the stacks before it rebuilds them and lighting the static geometry with it. Indoors the faces see the current frame's lights.

#### Render targets

`OpenGLRenderTarget` is a framebuffer with a colour texture and an optional depth texture. The two the rest of the renderer is built around are the UI target and the world target. The UI target holds the whole game screen when the window is scaled, and is blitted into the window when the frame is presented. With `graphics.ui_scale` at 2 or 4 it has that many pixels per UI coordinate. The UI code keeps laying out in render dimensions, and `OpenGLRenderer::_uiPixels` maps its rectangles to the target's pixels for the viewports and scissor rectangles. The world target is used when `graphics.world_target_width` and `graphics.world_target_height` are set. The 3D world is then drawn into it at that size, and `Renderer::compositeWorldTarget` draws it into the game viewport of the UI target before the UI goes on top. With both sizes at 0 the world is drawn straight into the viewport as the original game does. With `graphics.linear_pipeline` set the world target holds linear colour as R11G11B10F floats, and `OpenGLRenderer::drawComposite` tonemaps it, with bloom and FXAA when they're enabled, into the viewport. With `graphics.msaa` at 2, 4 or 8, or `vr.msaa` for a headset's eyes, the world is drawn into a multisampled target of the same size and format instead, made by `OpenGLRenderTarget::resetMultisample` with colour and depth renderbuffers, and `EndScene3D` resolves its colour into the world target before anything reads it. The soft particles' depth copy blits from it as before, and GL resolves that to one of each pixel's samples. FXAA is skipped then. The depth prepass of the sprites with shapes turns on alpha to coverage, with the alpha sharpened to about a pixel's width, so their cutout edges get the samples' coverage instead of a hard cutoff, while their depth and parallax stay per pixel.

#### World space billboards and the sky

Sprites, particles, sphere effects, projectile beams and the sky used to be projected to the screen on the CPU and drawn through an orthographic matrix, which only works for the one camera that projected them. With `graphics.world_space_billboards` set they're drawn through the real view and projection matrices instead. `BaseRenderer` keeps a `WorldBillboard` next to each screen space billboard at the same index, so both go through the same sort. `Renderer::buildWorldBillboardQuads` turns them into quads facing one view, once per frame, so that every pass of the frame draws the same geometry. Most face the view with its roll taken out. Sprites that stand on their bottom edge only turn about the vertical, so that they stay upright when the view looks up or down. Flat, they stand across the view, all parallel. In a headset each turns to face the point between the eyes, so that turning the head leaves them still. That point follows the tracked head with a centimetre of slack, so that tracking noise doesn't turn them, and the eyes pick their sides of a sprite frame from the same steadied position. Before each eye of a headset draws, `Renderer::turnWorldSpritesTo` swaps in the side of a sprite's frame that faces that eye, in the rare case that it isn't the side the frame was prepared with. A sprite keeps the side it showed that eye in the previous frame until the eye is about 3 degrees past the boundary, so that the tracking's small movements don't flip it back and forth. The sky has a shader of its own, `glskyshader`, which intersects each pixel's view ray with the sky plane, outdoors on a quad covering the view and indoors on the sky faces themselves.

#### Sprite shapes

A texture pack can give a sprite a shape: `shape/sprites/<name>.png` next to its colour images, with `"shape": {"thickness": N}` in the sprite's manifest entry. The shape image is the size of the sprite's pack images and is shared by all of the sprite's palettes. RG is the surface normal's x (right) and y (up) as `n * 0.5 + 0.5`, with z toward the viewer rebuilt from them. B is the depth, 255 nearest the viewer, with 127.5 on the sprite's quad and the full 0 to 255 range spanning `thickness` game units. A is the coverage. `TexturePack::spriteShapeImage` checks it against the sprite's LOD entry like a colour image, and `AssetsManager::getSpriteShape` returns it as a `GraphicsImage`.

With `graphics.sprite_depth`, the linear pipeline and world space billboards, `glbillbshader` treats the shape as a height field over the quad. Each fragment moves along the quad's normal by its depth and writes that depth, so sprites cut into walls, floors and each other. The offset flattens over the bottom tenth of the sprite so feet stay on the floor. The texture lookup gets two steps of parallax from the difference between the eye's view direction and the head's. The head, and so flat play, sees the stock image, while each eye in a headset sees the sprite from its own side. Quads with shapes are grown by half the thickness to leave room for the shift. `OpenGLRenderer::drawWorldBillboards` draws the sprites with shapes first: the depth of their opaque parts in a prepass, then their colour, blended against that depth. Only then does it copy the world depth for the soft edges of additive billboards, and draw everything else in its sorted order.

With `graphics.sprite_lighting` as well, the shader multiplies the stock colour by the ratio of the light reaching the shape's normal to the light reaching the flat quad. The lights are the sun and point lights that the building or BSP pass lit the same view with. A flat sprite keeps its stock tint, and the ratio is clamped to 0.5 to 1.5, so the normals move light around a sprite without changing its overall brightness. `graphics.sprite_crossfade` is separate. It blends a sprite with the neighbouring side of its frame within 10 degrees of the boundary between them, per eye. The video options screen has toggles for depth, which also switches lighting, and for crossfade.

#### Baked indirect light

A texture pack can hold the indirect light of each map, `gi/<map file>.bin` with an entry in the manifest's `gi` section that carries the CRC-32 of the map's `games.lod` entry. `OpenEnroth bake-gi` makes them, and texpipe's `bake` step runs it once per map. `GiBaker` traces the level that the subcommand loaded. The ambient level, the sun and the stationary lights light each face a ray lands on, the face reflects that in the mean colour of its texture, and each probe stores what reaches it as L1 spherical harmonics of irradiance. `GiProbes.h` documents the file. Indoor maps get a grid over their sectors at one time of day. Outdoor maps get one column of probes per terrain cell, at three heights above the terrain, at seven times of day. `ResourceManager::packGiProbes` reads them for the map file, or returns nothing when the pack has none or they were baked from another version of the map.

With `graphics.global_illumination` and the linear pipeline, `OpenGLRenderer` looks the probes up when the map changes. It keeps every time key as floats, blends the two around the time of day on the CPU whenever the game minute changes, and uploads the result to one RGBA16F 3D texture on unit 18, with the red, green and blue coefficients as three blocks along z. Outdoors a 2D texture on unit 19 holds the terrain height under each column of probes, so the shaders can place a fragment between the layers. `gi.glsl` divides the irradiance on a face by the map's mean irradiance at that time, channel by channel, and clamps the ratio to 0.5 to 2. The mean is taken where the party walks, over the lowest layer on land outdoors, so a map that is mostly sea doesn't make its land look bright. The world shaders scale the sun's ambient light by the ratio in `SunAmbient`, next to the ambient occlusion. A channel whose mean is close to black keeps a ratio of 1, and so does water, which shows the sky. Sprites are drawn at full brightness in daylight, so billboards only take the tint of the ratio at the middle of the sprite, with its brightest channel at 1. Sprite lighting moves the ratio around a sprite's shape like the rest of the light. The video options screen has a toggle for it next to the enhanced one.

#### Drawing from other views

`WorldFrameViews` in `Engine/Graphics/WorldFrameViews.h` is the hook for drawing the world from views other than the party camera. `VrSystem` is its only implementation. `Engine::drawWorld` first asks it to aim `pCamera3D` at the views, so that `prepare` culls for what they see, then hands it the prepared frame to draw, and points `pCamera3D` back at the party once the frame is done. Between frames the party camera is the gameplay camera, and the head camera only culls and draws. Picking asks the hook too, and so does `AudioPlayer::UpdateSounds`, which hears the world from the head through `WorldFrameViews::listenerPose`. In VR the mouse picks along a ray through the cursor's point on the UI quad, and the keyboard picks the closest hit of a grid of rays that covers the party camera's field of view around the head's forward direction. Both go through `Vis::PickRay`, which tests the world space billboard quads.

A view that isn't the party camera draws into a colour texture it owns through `Renderer::setExternalWorldTarget`, and external world targets always take the world space billboard path. `Renderer::setUiLayer` makes the UI a layer of its own for an external compositor. The UI is then drawn over transparent black with the game viewport left empty, `Renderer::copyUiLayer` copies it out, and the window gets the view set by `Renderer::setViewportMirror` under the UI.

#### VR

VR is only built with the `OE_BUILD_VR` CMake option, on Windows, and goes through OpenXR with the OpenGL extension. All OpenXR calls are in `src/Engine/Vr/VrSystem.cpp`. It runs when `vr.enabled` is set, or for one run when the game is started with `--vr`. If no headset is found the game logs why and carries on flat. With VR on, the game keeps going when its desktop window loses focus, and every 5 seconds the log gives the number of frames sent to the headset and the average GPU time of an eye draw, from timestamp queries around each eye that are read back a few frames later. While the session runs the window grabs the mouse, so that a click can't land on another window. Alt-tab still switches away, and the grab comes back with the focus. `window.mouse_grab` keeps the value it had without VR.

The headset view starts at the head's position and facing when the session begins, at the party's eye level. The `vr.recenter` key, F8 by default, makes the head's current pose the new start. `vr.head_offset_radius` limits how far in metres the eyes can move away from it, so that leaning can't put them through a wall. `vr.near_clip` is the near clip distance in the headset, in game units at 100 to the metre. `vr.render_scale` multiplies the size of the eye images the headset recommends, and sun shadows always take the cheaper `pcf1` filter in the eyes. The UI hangs in the head origin space as a flat screen `vr.ui_width` metres wide and `vr.ui_distance` metres away, upright, in front of where the head looked when the screen opened, and stays there. Each screen that opens, like a menu, a book or a house, is placed again. With `vr.ui_follow` set to `lazy` it also eases back in front of the head once the head turns or moves past a small dead zone. Until the head origin is captured it hangs in front of the eyes. Mouse picking goes through the screen wherever it is. With `vr.hud_layout` set to `spread`, the game screen's HUD leaves the floating screen for cards of its own. The `HUD_CARDS` table in `VrSystem.cpp` holds the part of the screen each card shows and where it hangs: the portraits low in front, with the status bar on top of them and the right panel against their right side, the minimap to the upper right and the books and buttons to the upper left. `drawHudCards` draws each card into a swapchain of its own, three image pixels per screen pixel, through the renderer's card mode. `Renderer::beginUiCard` sends the 2D drawing into the card's image with the card's part of the screen filling it, so that `Engine::drawHudPanels`, the game's own panel drawing, draws the card at its own resolution, clipped to its part, and `DrawPopupFrameAround` frames it with the parchment, corners and edges of the game's popups. `Engine::DrawGUI` moves the characters' delayed reactions and the buff animations over the portraits on before the panels are drawn, so that drawing them again changes nothing in the game. The world viewport's overlay, cut a pixel in from its black frame, is copied into one more swapchain and hangs straight ahead where the floating screen would be. Every quad shows the whole of its swapchain image, since runtimes disagree on where a part of one is, and VrSmoke checks that. All of it hangs in the head origin space rather than VIEW space, so turning the party with the keys carries the HUD along while turning the head moves the eyes over it. It stays level: each card hangs at a fixed yaw and pitch from the origin, tilted to face it without roll, and neither the head's pitch nor the party's moves it. `vr.hud_distance`, `vr.hud_scale` and `vr.hud_spread` tune the cards all at once. While the HUD is spread the window's mouse is in SDL's relative mode and the HUD steers a cursor of its own. Mouse motion turns it by angles around the parts as they hang around the head, smoothly over the gaps between them too, and a few degrees past their outer edges at most, so that it's never lost. On a part, `Io::Mouse::setPinnedPosition` pins the game's cursor to the screen position under it, and clicks land there. Away from the parts the game's cursor waits off the screen, the HUD's cursor, or the item it holds, hangs on a small quad of its own on top of the HUD, and clicks land nowhere. With the classic layout and on every other screen the mouse is free in the window again, confined by the grab, and the system cursor comes back where the HUD's cursor was. A clicked button's pressed image, which its click window draws into the stock UI for a frame, goes on the card too through `pressedButtonFlashes`. Every other screen, and every frame without the world like the main menu's, shows on the floating screen. When the world stops being drawn, as behind a house, a book or a menu, the eye images last handed to the runtime go out again with their poses for 0.2 seconds under a black quad that turns opaque, and after that the world is left out, so the screen hangs in black. `vr.snap_turn` turns the party by `vr.snap_turn_degrees` per press of a turn key instead of smoothly, and `vr.minimal_hud` leaves out the frames around the game viewport. While the headset session runs, the video options screen shows a VR block of two pages in place of the gamma preview. The first has snap turning, the minimal HUD, the screen's distance and width, the head offset, a line that recenters like the key does, and a More line. The second has the HUD layout with its distance, scale and spread, the screen follow mode, the render scale, the multisampling and a Back line. Changes apply at once, apart from the render scale, which takes effect on restart.

On an RTX 5080 with the PICO runtime, `vr.render_scale` at 1.5 gives 2880x2880 eyes, and the enhanced preset with the esrgan texture pack takes 1.3 to 1.4 ms of GPU time per eye and about 2 ms of CPU time per frame, against 11.1 ms at 90 Hz. Most of the cost per eye grows with the pixel count, so `vr.render_scale`, 1 by default, is the setting to lower when a slower GPU drops frames. Ambient occlusion costs about 0.2 ms per eye, bloom about 0.1 ms, and a 1024 shadow map saves nothing measurable, so VR keeps the whole enhanced preset.

Measure VR frame times on a quiet machine, with nothing else using the CPU or the GPU. Builds, Python worker pools and other game processes starve the worker thread of the NVIDIA OpenGL driver, and the game's GL calls then take three to five times as much CPU time while the GPU waits between submissions. Leave the first minute after the game starts out of a measurement, because every pass runs slower until it settles. Keep the GPU fan steady too. On a card that stops its fans when idle, five to six minutes of load at eye size slow every pass two to nine times, about a minute after the fans start, with no throttle reason reported by the driver. The slowdown outlasts the session and the process, and it fades only after a minute or two of lighter load. `vr.render_scale` above 1 and ambient occlusion are where a hot card loses this time. At the default scale of 1 it didn't set in within nine minutes on an RTX 5080.

Things known to be left for later:
* Spell targeting that picks the closest actor uses the headset's culling view, which is wider than the flat view. Whether that feels right needs a session in the headset.
* With `vr.minimal_hud` the buttons that the right frame paints can't be seen but still take clicks.


## Testing

We strive for a good test coverage of the project, and while we're not there yet, the current policy is to add tests for all the bugs we fix, as long as the fix is testable. E.g. graphical glitches are generally very hard to test, but we have the infrastructure to test game logic and small isolated classes.

Tests in OpenEnroth fall into two categories:
* Unit tests. These are a standard breed of tests, written using Google Test. You can see some examples in `src/Utility/Streams/Tests`.
* Game tests. If you're familiar with how testing is done these days for complex mobile apps, then you can consider game tests a variation of UI tests that's specific to our project. Game tests need game assets to run.

Game tests work by instrumenting the engine, and then running test code in between the game frames. This code usually sends events to the engine (e.g. mouse clicks), which are then processed by the engine in the next frame, but it can do pretty much anything else – all of engine's data is accessible and writable from inside the game test.

As typing out all the events to send inside the test code can be pretty tedious, we also have an event recording functionality, so that the test creation workflow usually looks as follows:
* Fix the bug.
* Load a save that used to reproduce the bug that you've just fixed.
* Press `Ctrl+Shift+R` to start recording an event trace. Check logs to make sure that trace recording has started.
* Perform the steps that used to reproduce the bug.
* Press `Ctrl+Shift+R` again to stop trace recording. You will get two files generated in the current folder – `trace00001.json` and `trace00001.mm7`, with
the number one past the highest existing one, so the freshest trace always sorts last.
* Rename them into something more suiting (e.g. `issue_XXX.json` and `issue_XXX.mm7`) and place them into `test/Data/`.
* Create a new test case in one of the game test files in [the game tests folder](https://github.com/OpenEnroth/OpenEnroth/tree/master/test/Bin/GameTest).
* Use `TestController::playTraceFromTestData` to play back your trace, and add the necessary checks around it.

If you need to record a trace with non-standard FPS (e.g. if an issue doesn't reproduce on lower FPS values), set `debug.trace_frame_time_ms` config value before starting OpenEnroth for recording.

To run all unit tests locally, build a `OpenEnroth_UnitTest` cmake target and run `<build-dir>/test/Bin/UnitTest/OpenEnroth_UnitTest`.

To run all game tests locally, set `OPENENROTH_MM7_PATH` environment variable to point to the location of the game assets, then build `Run_GameTest_Headless_Parallel` cmake target. Alternatively, you can build `OpenEnroth_GameTest`, and run it manually, passing the paths to both game assets and the test data via command line. Run `OpenEnroth_GameTest --help` for a list of options. Note that you can pass `--headless` to run tests in headless mode. Test data is located in `test/Data/`.

The headset path has a smoke test of its own, the `VrSmoke` suite in `test/Bin/GameTest/VrSmokeTest.cpp`, built with `OE_BUILD_VR`. It plays a session from the main menu to quitting, through walking and turning, looking up and down, a chest, a book, a house with its video and its shop, a level transition, saving and loading, both pages of the VR options and the spread HUD. It checks that the session got focused, that every frame sent to the runtime carried the layers it should, that the frame rate stays above 50 per second, and that the warnings and errors match a flat run of the same script. Build the `Run_VrSmoke` target with `OPENENROTH_VR_SMOKE` set to the OpenXR runtime json to run it on, for the PICO runtime `set OPENENROTH_VR_SMOKE=C:\Program Files\PICO Streaming Service\openxr_runtime_pc\PicoStreamingXRRuntime\picostreaming-openxr.json`. The target runs the suite flat and then in a headset session, with the runtime picked for that process alone through `XR_RUNTIME_JSON`, so the runtime the system uses stays as it is. Without the variable the suite is skipped. It needs a headset session, so the other game test targets leave it out, and its frame rate floor assumes an otherwise idle machine.

If you need to look closely at the recorded trace, you can play it by running `OpenEnroth play --speed 0.5 <path-to-trace.json>`. Alternatively, if you already have a unit test that runs the recorded trace, you can run `OpenEnroth_GameTest --speed 0.5 --gtest_filter=<test-suite-name>.<test-name> --test-path <path-to-test-data-folder>`. Note that `--gtest_filter` needs that `=` and won't work if you try passing the test name after a space. 


## How to deal with `Random state desynchronized`

Changing game logic might result in failures in game tests because they check the random number generator state after each frame, and this will show as `Random state desynchronized when playing back trace` message in test logs. This is intentional – we don't want accidental game logic changes. **If** the change was actually intentional, then you will need to either retrace or re-record the failing traces:
* To retrace, run `OpenEnroth retrace <path-to-trace.json>`. Note that you can pass multiple trace paths to this command. Use paths into `test/Data/` directly.
* Commit the updated traces alongside your code changes in the same PR.


## Scripting

We're using Lua as the scripting language, and all our scripts are currently located under the `resources/scripts` folder.


#### Lua Language Server
Script files undergo a syntax checking process during the build generation. If you intend to work with scripts, it is recommended to install the [Lua Language Server](https://github.com/LuaLS/lua-language-server) to run the style checker locally. Follow these steps to setup `LuaLS` locally:
- Install `LuaLS` through one of the [following methods](https://luals.github.io/#other-install). Ensure that the lua-language-server executable is available in the `PATH` environment variable.
- Generate the project.
- The `check_style` target is now including scripting in its tests.

Little note: If `LuaLS` is not found, everything still build but no checks will be run against the Lua scripts. This is worth checking before you push, as CI does run them. The devcontainer ships `LuaLS` already, so there's nothing to install there.

#### Tools
To go through a better experience while working with scripts it is strongly recommended to use [VS Code](https://code.visualstudio.com/) and [install the LuaLS extension](https://luals.github.io/#vscode-install).
Just be sure to open the root repository folder in `VS Code`. By doing so `LuaLS` reads the correct configuration file used by the project

#### Modding ?
Scripting is currently used only for debugging purposes. Modding support is not planned for the near future. You can check the [milestones](https://github.com/OpenEnroth/OpenEnroth/milestones) to get a better idea.


## Console Commands

The game features a console capable of executing various Lua script commands. To activate this feature, please follow these steps:

1. Launch the game.
2. Begin by loading an existing game or creating a new one.
3. Once in-game, press the `~` key to open the debug console.

Currently, the console is only available while in-game.


## Additional Resources

Old event decompiler and IDB files can be found [here](https://www.dropbox.com/sh/if4u3lphn633oit/AADUYMxNcrkAU6epJ50RskyXa?dl=0). Feel free to ping `zipi6616` on Discord for more info.

An archived snapshot of the codebase from before the first commit in this repository is kept at [OpenEnroth_PreHistory](https://github.com/OpenEnroth/OpenEnroth_PreHistory). That tree is still flat and carries the unsorted `mm7_*.cpp` decompilation dumps, so it's mostly useful for tracing where a piece of code originally came from.


## Support

Still having problems? Ask for help on our discord! [![](https://img.shields.io/badge/chat-on%20discord-green.svg)](https://discord.gg/jRCyPtq)
