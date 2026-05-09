# CraftPacker

Native Windows app for assembling Minecraft mod lists: bulk search on Modrinth (and optionally CurseForge), dependency resolution, conflict hints, `.mrpack` export, and folder import—with Qt 6 / C++20 UI.

Downloads: [**Releases → v3**](https://github.com/helloworldx64/CraftPacker/releases/tag/v3) — **`CraftPacker_v3.exe`** (single static exe, recommended) or **`CraftPacker_v3_Portable.zip`** (shared-Qt bundle; extract entirely before running).

## Repository layout

See [**docs/REPOSITORY_LAYOUT.md**](docs/REPOSITORY_LAYOUT.md) for what lives in **`src/`**, **`docs/`**, **`scripts/`**, build entry points at the repo root, and what is intentionally **not** committed.

## Features

- **Modrinth-first workflow** — search, rank, resolve versions per loader/MC version (`User-Agent`: `helloworldx64/CraftPacker/3.0.0`).
- **Dependency engine** — pulls required mods into the queue when possible.
- **Conflict / overlap hints** — flags risky combinations before you launch the game.
- **Export** — server-oriented packs and Modrinth-style `.mrpack` where applicable.
- **Folder import** — rebuild a working list from an existing `mods` directory.
- **CurseForge (optional)** — set an API key in settings or bake one in at compile time ([build docs](./docs/BUILDING.md)).

## Quick start (users)

**Option A — one file**

1. Open [**Releases → v3 → Assets**](https://github.com/helloworldx64/CraftPacker/releases/tag/v3).
2. Download **`CraftPacker_v3.exe`**.
3. Run it (SmartScreen may warn on unsigned binaries — *More info* → *Run anyway*).

**Option B — portable ZIP (shared Qt build)**

1. Download **`CraftPacker_v3_Portable.zip`** from the same page.
2. **Extract the entire ZIP**, then run **`CraftPacker.exe`** in that folder so Qt DLLs and plugins stay beside the executable.

## Documentation

| Document | Purpose |
|----------|---------|
| [**docs/REPOSITORY_LAYOUT.md**](docs/REPOSITORY_LAYOUT.md) | Folder / file map (**`src/`**, **`scripts/`**, what is gitignored) |
| [**docs/BUILDING.md**](docs/BUILDING.md) | Static **and** developer builds (`build_static.bat`, vcpkg, `/MT`, optional `CF_API_KEY`) |
| [**docs/USER_GUIDE.md**](docs/USER_GUIDE.md) | Full UI and feature reference |

## Building from source (summary)

Maintainers publish a **static** binary using **vcpkg** `x64-windows-static` Qt plus **`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`**.

```bat
build_static.bat
```

Output: `dist\CraftPacker_v3.exe`. Detailed steps and verification (`dumpbin`) are in [**docs/BUILDING.md**](docs/BUILDING.md).

## Contributing

Issues and PRs are welcome: [Issues](https://github.com/helloworldx64/CraftPacker/issues).

## License

MIT — see [`LICENSE`](LICENSE).

## Acknowledgements

[Modrinth](https://modrinth.com/) for the API, and [Qt](https://www.qt.io/) for the toolkit.
