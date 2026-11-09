# Compiled dependency provenance

The build files are the source of truth for dependency revisions. The current
release inputs are:

- Dear ImGui 1.92.2 WIP: vendored in `lib/libs/lib/imgui_lib`. The ordered
  SHA-256 manifest of the seven compiled ImGui source files is
  `3e85486c2d75d55bd90c0cac023afafb95bfb2590762ab37fd92ce068853226b`.
- Glaze: `3bfd53e70db09247f3726a65abd7c7c2414a1601`.
- Zydis: `bffbb610cfea643b98e87658b9058382f7522807`.
- SafetyHook: `f44cc070a8340f2f26649553c49533475417304d`.
- Lua: 5.4.8 archive SHA-256
  `4f18ddae154e793e46eeab727c59ef1c0c2b744e7b94219710d76f530629ae`.
- oxorany (MIT): vendored at `src/oxorany` (from
  https://github.com/llxiaoyuan/oxorany). Compile-time constant obfuscator for
  the license identity table. `obfuscate.h` (AY, public domain) at `src/`
  obfuscates the remaining string literals.
- Geode 6.0.0-alpha.1 and its configured support libraries: exact source revisions are
  emitted into `SOURCE_MANIFEST.txt` by the source-bundle script. Their BSL,
  BSD, MIT, and Apache notices are packaged from `LICENSES/`.

`scripts/make-source-bundle.sh` copies the exact configured dependency source
trees and the Geode SDK source into the corresponding-source archive.
