# Building CraftPacker

CraftPacker is a Windows Qt 6 desktop app (C++20). Two build modes are supported:

| Mode | Output | Typical use |
|------|--------|---------------|
| **Static (release)** | One `CraftPacker.exe`; no Qt/MSVC redistributable DLLs | Shipping to users |
| **Dynamic (development)** | Small `.exe` + Qt DLLs (via `windeployqt`) | Day-to-day development |

---

## Standalone executable (recommended for releases)

Everything is statically linked:

1. **Qt 6** from vcpkg as **x64-windows-static** libraries (`.lib`), so widgets, networking, and internal Qt code live inside the `.exe`.
2. **MSVC runtime with `/MT`** (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`), so you do **not** need `MSVCP140.dll` / `VCRUNTIME140.dll`.

### Prerequisites (install once)

| Tool | Typical location | Role |
|------|------------------|------|
| VS 2022 Build Tools | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\` | MSVC compiler (`cl.exe`) |
| vcpkg (clone next to CraftPacker) | **`..\\..\vcpkg\`** assumed by **`build_static.bat`** | Downloads/builds **`vcpkg.json`** manifest deps |

Static Qt uses the repo **`vcpkg.json`**: **`qtbase`** is built with **`default-features`** disabled and curated features (**no **`icu`** from vcpkg**). Dependencies install into **`CraftPacker-main\vcpkg_installed\`** / **`build_static\vcpkg_installed\`** automatically on first CMake configure.

Prefetch (optional):

```bat
..\vcpkg\vcpkg install --triplet x64-windows-static
```

(run from **`CraftPacker-main`**.)

### One-command build

From the repository root (`CraftPacker-main`):

```bat
build_static.bat
```

The script activates MSVC **`x64`**, configures CMake with the vcpkg toolchain and **`MultiThreaded`**, builds **Release**, runs **`dumpbin /dependents`** to list DLLs, then copies the binary to **`dist\CraftPacker_v3.exe`**.

Bundled **`resources\`** beside the exe is **not required**: conflict data ships inside the binary via **`resources.qrc`**.

### Equivalent manual commands

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

cmake -B build_static -S . ^
  -DCMAKE_TOOLCHAIN_FILE=C:/projects/craftpacker/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DCMAKE_BUILD_TYPE=Release

cmake --build build_static --config Release
```

With **Ninja** (common), the executable is **`build_static\CraftPacker.exe`** (no **`Release`** subfolder). With **VS generators**, expect **`build_static\Release\CraftPacker.exe`**.

Adjust **`CMAKE_TOOLCHAIN_FILE`** if your **`vcpkg`** clone lives elsewhere (`%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake`).

Artifacts:

| Path | Description |
|------|-------------|
| `build_static\CraftPacker.exe` or `build_static\Release\CraftPacker.exe` | Immediate build output (depends on CMake generator) |
| `dist\CraftPacker_v3.exe` | Copy ready to publish |

### Dependencies

Publish **only `dist\\CraftPacker_v3.exe`**: you do not ship **`resources\\`**, **`vcpkg_installed\\`**, or Qt/OpenSSL DLLs.

**Qt and ICU on Windows.** Qt enables **winsdkicu**, so **`dumpbin /dependents`** often lists **`icuuc.dll`** / **`icuin.dll`**. Those are Windows system ICU binaries (under **`%SystemRoot%\\System32`** on supported Windows releases), not files you publish next to CraftPacker. You still must not ship third‑party MSVC/Qt redistributables copied out of vcpkg.

Expect entries like **`KERNEL32.dll`**, **`USER32.dll`** — **not** `Qt6Core.dll`.

### Verification

```bat
dumpbin /dependents dist\CraftPacker_v3.exe | findstr /i "\.dll"
```

### Optional: CurseForge API key at build time

If you want CurseForge search enabled without prompting for a key in the UI:

```bat
set CF_API_KEY=your_key_here
build_static.bat
```

A Python step XOR-obfuscates the key into generated **`api_keys.h`** under the build tree. That header is **not** committed.

---

## Development build (shared Qt DLLs)

Faster iteration when Qt is installed as shared libs (official installer):

1. Configure and build **`build\`** using **`run_build.bat`** (updates paths inside for your machine) or:

   ```bat
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64
   cmake --build build --config Release
   ```

2. Deploy DLLs beside the exe:

   ```bat
   windeployqt --release build\CraftPacker.exe
   ```

### PowerShell 7 script (recommended on Windows)

From **PowerShell 7**, after editing `-QtPath` if your kit differs:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build-DynamicRelease.ps1
```

Optional:

```powershell
.\scripts\Build-DynamicRelease.ps1 -QtPath 'C:\Qt\6.11.0\msvc2022_64' -CMakePath 'C:\path\to\cmake.exe'
```

This runs **vcvars64**, configures **Ninja** + **Release**, builds, copies **`CraftPacker.exe`** and **`resources\`**, runs **`windeployqt --compiler-runtime`**, writes **`dist\CraftPacker_Windows\`**, and zips **`dist\CraftPacker_Windows_portable.zip`**.

See the [Qt deployment documentation](https://doc.qt.io/qt-6/windows-deployment.html) for details.

---

## Release checklist

1. Bump version strings if needed (`CMakeLists.txt`, app metadata).
2. Preferred: run **`build_static.bat`**; confirm **`dist\CraftPacker_v3.exe`** and **`dumpbin`** output (no third‑party DLLs).
3. Optional: run **`scripts\Build-DynamicRelease.ps1`**; it writes **`dist\CraftPacker_Windows_portable.zip`** and a duplicate **`dist\CraftPacker_v3_Portable.zip`** for GitHub uploads.
4. On GitHub **Releases**, attach **`CraftPacker_v3.exe`** (primary download) and the portable ZIP as a fallback. GitHub automatically provides **Source code (zip)** / **tar.gz** for the tag snapshot.
