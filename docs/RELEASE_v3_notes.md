CraftPacker **v3** — dependency resolution, conflict checks, `.mrpack` and CurseForge export, folder import, debug reports.

### Releases

Stable GitHub [**tag `v3`**](https://github.com/helloworldx64/CraftPacker/releases/tag/v3) ships **`CraftPacker_v3.exe`** (static MSVC) plus optional **`CraftPacker_v3_Portable.zip`** (shared Qt). **Rebuild from `main`** to match latest behaviour; CMake **`project(VERSION …)`** (see root **`CMakeLists.txt`**) drives the semver shown in **About**/`QApplication::applicationVersion` and in Modrinth-related **`User-Agent`** strings (**`helloworldx64/CraftPacker/`** + that version).

**3.0.1** Patch highlights: corrected Modrinth **`.mrpack`** index semantics (dependency keys, SHA1/SHA512, env, HTTPS downloads, overrides tree) and CurseForge **`manifest.json`** (**`projectID`**, **`fileID`**, loader **`id`**). Older **3.0.0** bullets still apply otherwise.

Unsigned Windows binaries: use **More info** → **Run anyway** when SmartScreen appears.
