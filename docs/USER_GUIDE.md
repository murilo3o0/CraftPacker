# CraftPacker v3 — Complete User Manual

**CraftPacker v3** transforms a bulk mod downloader into a full Modpack Creation Studio. Below is every feature, where to find it in the UI, and what it does.

---

## 🔍 SEARCH & RESOLUTION ENGINE

**Location:** Center panel → "Search Results" tab

### Core search flow
1. Type mod names (one per line) in the **Mod List** text area (left panel)
2. Select **MC Version** (default `1.20.1`) and **Loader** (`fabric`, `forge`, `neoforge`, `quilt`)
3. Choose download directory (default `CraftPacker_Downloads` folder)
4. Click **🔍 Search Mods**

### What the search engine does:
- **Bracket stripping** — strips `(text)`, `[text]`, `{text}` before querying (so "JustEnoughItems (JEI) [1.20.1]" searches for "JustEnoughItems")
- **Alias table** — known mod names (e.g. "Fabric API", "REI", "JEI", "Sodium") resolve directly to API slugs without search
- **Smart ranking** — ranks results by: exact title > exact slug > similarity score > loader match > version match
- **Direct slug fallback** — if no search hits, tries the name as a direct project slug

### Status results per row:
| Status | Meaning | Color |
|--------|---------|-------|
| Available | Found with matching loader + version | Green |
| Soft Overlap | Multiple mods in same category (e.g. 2 minimaps) | Orange |
| Hard Conflict | Modrinth metadata says incompatible | Red |
| Missing Dependency | Required dependency not present | Red |
| Dependency Mismatch | Wrong loader/version for a required dep | Orange |
| Loader Incompatible | No version exists for the selected loader | Orange |
| Wrong Version | Exists but not for the selected MC version | Dark orange |
| Not Found | No match on Modrinth | Red |
| Needs Review | Can't be automatically classified | Yellow |

### Available search throughput:
- **Parallel execution** — all mods searched simultaneously via QThreadPool
- **Rate-limited** — 300 calls/minute maximum to avoid API bans
- **Auto-dependency resolution** — after search, required dependencies are fetched and added as `[Dependency]` rows

---

## 📥 DOWNLOADS

**Location:** Buttons below the results table

### Download buttons:
| Button | Action |
|--------|--------|
| ⬇ Download Selected | Downloads only the mods you've selected (click rows) |
| ⬇ Download All | Downloads every resolved mod |

### What happens:
1. **Dependency resolution** — scans all mod's `required` dependencies, resolves them against Modrinth API
2. **Deduplication** — skips mods already in the download set
3. **Parallel downloading** — each file downloads in its own thread
4. **Progress tracking** — switch to the **⚡ Task Manager** tab to see:
   - Overall progress bar
   - Per-file progress (%)
   - Download speed (MB/s)
5. **Already-downloaded detection** — files that exist skip re-download (shown as `✓ Already Downloaded`)

---

## 📁 IMPORT FROM FOLDER

**Location:** Left panel → "Import from Folder..." button, or **File** → **Import from Folder...**

1. Select a folder containing `.jar` files
2. The app extracts metadata from every JAR:
   - **Fabric mods**: reads `fabric.mod.json` (name, mod-id, version, loader)
   - **Forge/NeoForge mods**: reads `META-INF/mods.toml`
   - **Fallback**: extracts from `MANIFEST.MF` or parses filename
3. **Deduplicates** — same mod-id keeps the first occurrence, logs duplicates
4. **Auto-fills** the mod list and triggers search

---

## 🗄️ PROFILES

**Location:** Left panel → "Mod Profiles" section

| Button | Action |
|--------|--------|
| Save | Save current mod list with a profile name |
| Load | Load a saved profile and auto-fill the mod list |
| Delete | Delete a selected profile |

Profiles are stored as `.txt` files in `%APPDATA%\CraftPacker`.

---

## 📦 EXPORT MODPACKS

**Location:** **File** → **Export Pack** submenu

### Modrinth .mrpack export
- Generates `modrinth.index.json` with:
  - Mod hashes (SHA1/SHA512)
  - Download URLs
  - Environment tags (client/server)
- Zips everything into a `.mrpack` file

### CurseForge .zip export
- Generates `manifest.json` with project IDs and file IDs
- Includes `overrides/` folder for configs

### Generate Server Pack
- Automatically **filters out** client-only mods:
  - `client_side: required` + `server_side: unsupported` (e.g. shaders, minimaps, Optifine)
- Shows conflict warnings before export
- Outputs a clean `.zip` for server deployment

---

## ⚠️ CONFLICT DETECTION

**Runs automatically** after every search and dependency resolution.

### What it detects:
| Type | Example | Severity |
|------|---------|----------|
| Feature overlap | Multiple minimaps, multiple recipe viewers | Soft Overlap |
| API incompatible | Modrinth metadata says A is incompatible with B | Hard Conflict |
| Dependency-linked suppressed | Sodium Extra depends on Sodium → no false conflict | Suppressed |

### Conflict display:
- Rows show "Soft Overlap" (orange) or "Hard Conflict" (red)
- Right panel shows conflict details when you click a row
- Status bar shows conflict count summary

### Dependency-aware suppression:
- If mod B depends on mod A (required or optional), the pair is NOT flagged as a conflict
- Only explicit `incompatible` dependency metadata creates a Hard Conflict
- Example: Sodium + Sodium Extra + Reese's Sodium Options → all compatible (they depend on each other)

---

## 🔧 DEBUG TOOLS

### Debug Local Folder
**Location:** **Tools** → **Debug Local Folder...**

Analyzes a local folder of `.jar` files and displays:
- Extracted metadata for each JAR
- Loader detection (Fabric/Forge/NeoForge)
- Version info
- Duplicate detection
- Identity confidence level

### Export Search Results Debug
**Location:** **Help** → **Export Search Results Debug...**

Saves a full diagnostic report as `.txt` file with:

1. **PACK SETTINGS** — MC version, loader, download directory
2. **MOD LIST** — raw input
3. **RESULTS TABLE** — per-row: name, status, reason, ModInfo, ModEntry
4. **TOTAL COUNTS** — breakdown by status
5. **CANONICAL OVERLAP GROUPS** — e.g. `group:minimap | Soft Overlap | mods: ...`
6. **CANONICAL HARD CONFLICTS** — individual hard conflict rows
7. **DEPENDENCY DEBUG** — per-mod: required/optional/incompatible deps + presence status
8. **VALIDATION** — integrity checks

---

## 🔄 UPDATES & MIGRATION

### Check for Updates
**Location:** **Tools** → **Check for Updates**

- Pings the Modrinth API for every resolved mod
- Checks if newer versions exist for the current MC version
- Shows green/yellow/red indicators

### Migrate Profile
**Location:** **Tools** → **Migrate Profile...**

- Duplicates current profile to a new MC version (e.g. 1.20.1 → 1.21)
- Auto-queries API for matching mod versions
- Highlights which mods couldn't be migrated

---

## ⚙️ SETTINGS

**Location:** **File** → **Settings...**

| Setting | Description |
|---------|-------------|
| Max Concurrent Threads | Parallel search/download threads (default = CPU count) |
| CurseForge API Key | Paste your own CF API key (optional, stored encrypted) |

---

## 🖱️ INTERACTIONS & UX

### Drag & Drop
- Drag a `mods` folder (from a Minecraft instance) onto the app
- Drag individual `.jar` files
- Automatically parses and appends mod names to the list

### Context Menu (right-click any row)
| Option | Available for |
|--------|--------------|
| Open on Modrinth | Found mods |
| Copy Name | All rows |
| Search on CurseForge | Not-found mods |
| Copy All Names | All rows |

### Right panel (mod details)
Click any row to see:
- Mod icon (fetched from Modrinth)
- Title + Author
- Description
- Environment (Client/Server)
- Conflict warnings (if any)

### Animations
- **Fade-in** on startup
- **Completion pulse** when mods finish downloading
- **Jump bounce** on download-all button after completion

---

## 🧠 ARCHITECTURE NOTES

### Performance
- **Zero UI blocking** — all network I/O, searches, and dependency resolution run on background threads
- **Local caching** — API responses are cached in-memory for the session
- **Parallel workers** — downloads use thread-per-file, searches use thread-per-mod

### Security
- CurseForge API key is XOR-encrypted at build time (byte array in generated header)
- Decrypted only in memory at call time
- User can override with their own key in Settings

### Build
- **Fully static** (no DLL dependencies)
- Single standalone `.exe` (~24.5 MB)
- No installer needed — just run it
- Built with Qt 6 + MSVC 2022 + static linking

---

## KEYBOARD SHORTCUTS

While there are no explicit keyboard shortcuts, the UI is fully keyboard-navigable via Tab/Enter.

---

## LIMITATIONS

- **Modrinth only** for mod metadata (CurseForge support prepared but requires API key setup)
- **Internet required** for search and download
- Search quality depends on Modrinth API availability