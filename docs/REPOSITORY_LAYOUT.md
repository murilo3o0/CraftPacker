# Repository layout

High-level map of committed files (generated outputs like `build/` are gitignored).

```text
CraftPacker-main/
├── CMakeLists.txt          Project + targets (Qt 6, C++20, optional CurseForge key generation)
├── LICENSE
├── README.md               Overview, features, links
├── CraftPacker.cpp         Main UI and application wiring (large single TU)
├── CraftPacker.h
├── api_keys.h.in           CMake/template input when CF_API_KEY is set at build time
├── resources.qrc           Qt resource bundle
├── build_static.bat        One-shot **static** Windows release (/MT + vcpkg x64-windows-static Qt)
├── run_build.bat           Local **dynamic** Qt + Ninja dev build (edit paths inside)
├── build.ps1               Optional PowerShell build helper (if present)
├── docs/
│   ├── BUILDING.md         Static build, dev build, PowerShell `Build-DynamicRelease.ps1`
│   ├── REPOSITORY_LAYOUT.md
│   └── USER_GUIDE.md       Full UI / feature reference
├── dist/                   Packaging helpers (IExpress SED, etc.); release binaries are **not** committed
├── resources/              Icons, `known_conflicts.json`, Windows `.rc` if used
├── scripts/
│   ├── README.md           What each script does
│   ├── Build-DynamicRelease.ps1   PowerShell 7: MSVC + shared Qt + windeployqt + zip
│   └── obfuscate_key.py    Build-time CurseForge key obfuscation
└── src/
    ├── AsyncWorker.cpp / .h
    ├── CacheManager.cpp / .h
    ├── ConflictDetector.cpp / .h
    ├── CurseForgeAPI.cpp / .h
    ├── DebuggerDashboard.cpp / .h
    ├── FolderDebugger.cpp / .h
    ├── JarMetadataExtractor.cpp / .h
    ├── ModrinthAPI.cpp / .h
    ├── PackExporter.cpp / .h
    ├── UpdateChecker.cpp / .h
    ├── StringCleaner.h
    ├── ThemeManager.h
    ├── miniz.h / miniz_impl.cpp   ZIP handling (single-file miniz build)
    └── …
```

## What is not in Git

| Path / pattern | Reason |
|----------------|--------|
| `build/`, `build_static/` | CMake / Ninja output |
| `dist/CraftPacker_Windows/` | `windeployqt` staging folder |
| `deploy_release/` | Local packaging scratch |
| `*.exe`, `*.zip` | Binaries and archives (use **Releases**) |
| `api_keys.h` | Generated under the build directory |

## Release artifacts (GitHub)

| Asset | Meaning |
|-------|---------|
| **`CraftPacker_v3.exe`** | Static MSVC build: one file, no Qt DLLs (see **`docs/BUILDING.md`**) |
| **`CraftPacker_v3_Portable.zip`** | Optional: shared-Qt bundle (exe + Qt DLLs + plugins); matches recent `main` when rebuilt |
