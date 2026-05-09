# CraftPacker v3

A fast, native desktop tool for **building, validating, and exporting Minecraft modpacks**.

CraftPacker started as a bulk mod downloader, but v3 turns it into a full **modpack creation studio**: paste a list of mods, resolve dependencies automatically, detect incompatibilities, generate server-safe packs, and export detailed debug reports that explain exactly why a mod was accepted or flagged.

Catch missing dependencies, wrong loaders, and mod conflicts *before* you launch the game.

### Showcase video

https://github.com/user-attachments/assets/13ba675e-2505-4fea-8aab-e0b8199d04c3

## Key features

* **Bulk search and download:** Paste dozens of mod names (even messy ones) from a text list and search them all at once.
* **Automatic dependency resolution:** Finds, resolves, and flags required and optional dependencies against your chosen Minecraft version and loader.
* **Compatibility and conflict engine:** Detects soft overlaps (multiple minimaps) and hard conflicts (explicit incompatibilities, wrong loaders, wrong game versions).
* **Import from folder:** Build a fresh list by scanning an existing local Minecraft `mods` folder—great for migrating or auditing old packs.
* **Server pack generation:** Filters client-only mods (shaders, OptiFine-style extras, minimaps, etc.) to export a cleaner, server-ready bundle.
* **Debug and diagnostic reports:** Export plain-text reports explaining conflicts, missing dependencies, and how overlapping mods were classified.
* **Customizable targets:** Pick the Minecraft version and loader (Fabric, Forge, NeoForge, Quilt).
* **Single-file Windows build:** The recommended release is **`CraftPacker_v3.exe`**—a static MSVC build (Qt and CRT linked in). **No separate install folder of Qt/MSVC redistributable DLLs** (optional portable ZIP is only for the shared-Qt fallback). On Windows, Qt may still **import** **`icuuc.dll` / `icuin.dll`**, which the OS supplies from **`System32`** on supported versions—see [**docs/BUILDING.md**](docs/BUILDING.md). **No Python or Java runtime.**
  *Optional:* **`CraftPacker_v3_Portable.zip`** is a shared-Qt layout (extract the whole folder, then run **`CraftPacker.exe`** inside it).

**CurseForge** search is optional and may require an API key in settings (or a key baked in at build time—see [docs/BUILDING.md](docs/BUILDING.md)).

## How is this different from Prism Launcher?

Prism Launcher is an incredible tool, but CraftPacker is not trying to replace it.

**Prism Launcher** is a *launcher*: instances, play, and installing mods into a profile you actually run.

**CraftPacker** is a *builder and diagnostic tool*: assemble a pack from scratch, analyze it for broken dependencies or overlaps, clean up old mod folders, and prepare server exports.

Use CraftPacker to decide what belongs in your pack and catch problems early; use Prism (or your launcher of choice) to play.

## Getting started

### For users

1. Open the [**Releases**](https://github.com/helloworldx64/CraftPacker/releases) page (current v3: [**tag v3**](https://github.com/helloworldx64/CraftPacker/releases/tag/v3)).
2. Under **Assets**, download **`CraftPacker_v3.exe`** and run it—no extraction step.
3. If Windows SmartScreen warns about an unsigned app, use **More info** → **Run anyway** if you trust the project.

**Alternative:** Download **`CraftPacker_v3_Portable.zip`**, extract **everything** to a folder, and run **`CraftPacker.exe`** there (Qt DLLs and plugins must stay next to the executable).

### Repository layout (for contributors)

See [**docs/REPOSITORY_LAYOUT.md**](docs/REPOSITORY_LAYOUT.md) for how **`src/`**, **`docs/`**, **`scripts/`**, and build scripts are organized, and what build outputs are gitignored.

### For developers (building from source)

**Prerequisites**

* C++ toolchain (**MSVC** is what we test on Windows; MinGW may work but is unsupported here)
* **Qt 6** (Widgets + Network minimum)
* **CMake 3.20+** (matches `CMakeLists.txt`)

**Quick clone**

```bash
git clone https://github.com/helloworldx64/CraftPacker.git
cd CraftPacker
```

**Recommended reading:** [**docs/BUILDING.md**](docs/BUILDING.md) explains:

* **`build_static.bat`** — static **`dist\CraftPacker_v3.exe`** via **vcpkg** `x64-windows-static` and `/MT`
* **`run_build.bat`** / CMake + **shared Qt** for fast local iteration
* **`scripts/Build-DynamicRelease.ps1`** (PowerShell 7) — `windeployqt` + portable ZIP

Minimal shared-Qt configure example (adjust `CMAKE_PREFIX_PATH` to your Qt install):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64
cmake --build build --config Release
```

Then use Qt’s **`windeployqt`** on **`build/CraftPacker.exe`** before running outside your dev tree (details in [**docs/BUILDING.md**](docs/BUILDING.md)).

## Documentation

| Document | Purpose |
|----------|---------|
| [docs/BUILDING.md](docs/BUILDING.md) | Static and dynamic builds, vcpkg, optional `CF_API_KEY` |
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | Full UI reference |
| [docs/REPOSITORY_LAYOUT.md](docs/REPOSITORY_LAYOUT.md) | Source tree overview |

## How to use

1. **Configure settings:** Minecraft version, loader (Fabric / Forge / NeoForge / Quilt), and a **download** directory for `.jar` files.
2. **Provide a mod list:** paste names (one per line) or **Import from folder…** to scan an existing `mods` directory.
3. **Search:** click **Search Mods**. Use the status column (**Available**, **Soft Overlap**, **Hard Conflict**, **Loader incompatible**, etc.) and the side/details panel for explanations.
4. **Export or download:** **Download** selected or all mods, or use export options (e.g. `.mrpack`, server-oriented pack)—see [**docs/USER_GUIDE.md**](docs/USER_GUIDE.md) for every control.

Outbound Modrinth-facing HTTP requests identify the application as **`helloworldx64/CraftPacker/`** suffixed by the semver in **`CMakeLists.txt`** **`project(VERSION …)`** (currently **3.0.1**), consistent with Modrinth's API guidelines. The same **`User-Agent`** is used where appropriate for Fabric/Quilt meta and other downloader paths.

## Support the project

If CraftPacker saves you time debugging crashes, tips are appreciated.

[![Donate with PayPal](https://raw.githubusercontent.com/stefan-niedermann/paypal-donate-button/master/paypal-donate-button.png)](https://www.paypal.com/donate/?business=4UZWFGSW6C478&no_recurring=0&item_name=Donate+to+helloworldx64&currency_code=USD)

## Contributing

Issues and PRs are welcome:

* [Open an issue](https://github.com/helloworldx64/CraftPacker/issues)
* Fork and submit a pull request

## License

This project is licensed under the MIT License. See [`LICENSE`](LICENSE).

## Acknowledgements

* [**Modrinth**](https://modrinth.com/) for the platform and API.
* [**Qt**](https://www.qt.io/) for the framework used in this C++ rewrite.
