# Grape

Grape is a Geometry Dash bot for recording, replaying, rendering, frame stepping, trajectory tools, and gameplay assists.

Grape is an independent implementation, released under the [Grape Open Source License](LICENSE). You may use, modify and distribute it freely; if you distribute it, keep a pinned link to the original sources: https://github.com/okhx/grapebot

## Build

The project targets Geode `6.0.0-alpha.1` and Geometry Dash `2.2091`. The public CI builds macOS, iOS and Android packages. The Windows configuration is under `pc/`.

macOS (arm64):

```sh
export GEODE_SDK=/path/to/GeodeSDK
cmake -S . -B build-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DGEODE_DONT_INSTALL_MODS=ON \
  -DGEODE_CLI="$(command -v geode)"
cmake --build build-macos --parallel
```

The macOS desktop recorder loads FFmpeg from `Grape/libraries`, `/opt/homebrew/lib`,
`/usr/local/lib`, or the system `dlopen` search path at runtime. The menu and the
rest of the mod work without it; only video capture is disabled until the dylibs
are installed (for example with `brew install ffmpeg`).

Windows:

```sh
export GEODE_SDK=/path/to/GeodeSDK
cmake -S pc -B build -G Ninja
cmake --build build
```

## Macros and input modes

Macros store the input timing mode they were recorded with: CBF (the
`syzzi.click_between_frames` mod), the mod's click-on-steps option, and the
game's click-between-steps / click-on-steps options. On playback Grape restores
that mode, so a macro recorded with CBF or CBS replays with the same input
timing instead of being forced back to plain frame input.

`geode.node-ids` is optional. Grape sets the few node IDs it needs itself (pause
button, pause menus, end-level layer and hex input), so it runs without the
NodeIDs mod and is not blocked by the fact that its released build still targets
2.2081. If NodeIDs is installed, Grape uses its IDs and skips the fallbacks.

## Corresponding source

Create the source archive for a built release with:

```sh
GEODE_SDK=/path/to/GeodeSDK scripts/make-source-bundle.sh build output/grape-source.tar.gz
```

Ship that archive and its generated SHA-256 file with the matching binary.
See [NOTICE](NOTICE) for provenance and third-party licensing information.
