# CraftPacker

Native Windows app for assembling Minecraft mod lists: bulk search on Modrinth (and optionally CurseForge), dependency resolution, conflict hints, `.mrpack` export, and folder import—with a **standalone** Qt 6 / C++20 UI.

Downloads: [**Releases**](https://github.com/helloworldx64/CraftPacker/releases) — use **`CraftPacker_v3.exe`** (single executable, no extra DLLs).

## Features

- **Modrinth-first workflow** — search, rank, resolve versions per loader/MC version (`User-Agent`: `helloworldx64/CraftPacker/3.0.0`).
- **Dependency engine** — pulls required mods into the queue when possible.
- **Conflict / overlap hints** — flags risky combinations before you launch the game.
- **Export** — server-oriented packs and Modrinth-style `.mrpack` where applicable.
- **Folder import** — rebuild a working list from an existing `mods` directory.
- **CurseForge (optional)** — set an API key in settings or bake one in at compile time ([build docs](./docs/BUILDING.md)).

## Quick start (users)

1. Open **[Releases](https://github.com/helloworldx64/CraftPacker/releases)** and download **`CraftPacker_v3.exe`** from **Assets**.
2. Run it (Windows SmartScreen may warn on unsigned binaries — choose *More info* → *Run anyway* if you trust the source).

No Qt install, ZIP extraction for the main exe, or Python runtime required for the official static build.

## Documentation

| Document | Purpose |
|----------|---------|
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
