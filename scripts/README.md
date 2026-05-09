# Scripts

| File | Use |
|------|-----|
| **`Build-DynamicRelease.ps1`** | PowerShell 7 — MSVC + shared Qt, `windeployqt`, outputs `dist/CraftPacker_Windows/` and `dist/CraftPacker_Windows_portable.zip` (rename to **`CraftPacker_v3_Portable.zip`** before uploading with the static exe if you ship both). |
| **`obfuscate_key.py`** | Invoked by CMake when **`CF_API_KEY`** is set — writes obfuscated **`api_keys.h`** into the build directory. |

Batch entry points live in the **repository root** (`build_static.bat`, `run_build.bat`). See [**docs/BUILDING.md**](../docs/BUILDING.md).
