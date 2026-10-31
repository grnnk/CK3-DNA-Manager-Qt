# CK3 DNA Manager (Qt)

A native, local-first reimplementation of the Electron **CK3 DNA Manager** for
Windows. It stores Crusader Kings III DNA codes and portraits in a SQLite
library, converts between modern Base64 DNA, legacy Base64 DNA and Ruler
Designer code, and gives every gene a proper editor.

The interface is dark, warm and compact: a searchable library on the left, the
selected character on the right, and a dedicated gene editor for detailed work.

![Overview](screenshots/overview.png)

## Highlights

- **Local SQLite library.** No account, cloud service or telemetry. Data lives
  in `%APPDATA%` and can be backed up or moved like any other file.
- **Every important DNA format.** Paste modern Base64, legacy Base64 or a Ruler Designer block. Copy either Base64 or Ruler
  Designer text back to the clipboard.
- **Complete gene editing.** 104 vanilla Base64 slots plus modded extra slots,
  two alleles per morph gene, strength values, RGBA colours, and the
  Ruler-Designer-only beard/hairstyle genes.
- **Portraits.** Drag and drop PNG, JPG, WebP, BMP or GIF images, or paste one
  from the clipboard with `Ctrl+V`; thumbnails are
  generated automatically for the library.
- **Library tools.** Names, notes, favourites, live search, sorting,
  duplicate, delete, single-entry export and full-library export.
- **Ten languages.** English, German, French, Chinese, Russian, Spanish,
  Portuguese, Polish, Japanese and Korean, switchable at runtime without
  restarting.
- **Designed for the keyboard.** See the shortcut table below.

## Screenshots

### Gene editor

![Gene editor](screenshots/genes.png)

### Empty library and add dialog

| First launch | New DNA entry |
| --- | --- |
| ![Welcome](screenshots/welcome.png) | ![Add entry](screenshots/dialog-add.png) |

## Requirements

- Windows 10 or 11 (64-bit)
- Qt **6.5+** with the **Widgets**, **Sql** and **Svg** modules.
  The project is tested with **Qt 6.8.3 / MinGW 13.1**.
- CMake **3.21+** (3.25+ for the bundled presets)
- Ninja (default) or GNU Make (`mingw32-make` / `make`)
- A MinGW-w64 compiler matching the Qt build (tested: MinGW 13.1)

MSVC is not configured out of the box because the provided scripts target the
MinGW Qt kit. The source itself is compiler-agnostic.

WebP portraits are decoded through Qt's WebP image plugin when it is installed,
and otherwise through the Windows Imaging Component. On Windows 10, install the
free **Webp Image Extensions** from the Microsoft Store if WebP files cannot be
opened; Windows 11 normally includes WebP support. PNG, JPG, BMP and GIF always
work through Qt.

## Build

### Quick build (recommended)

From the project root:

```powershell
.\tools\build.ps1 -Config Release
```

The script finds Qt and MinGW in the usual places, configures CMake with Ninja
(or a make generator via `-Generator`), builds both the application and the
tests, and runs the test suite.

Useful options:

```powershell
.\tools\build.ps1 -Config Debug
.\tools\build.ps1 -Config Release -SkipTests
.\tools\build.ps1 -Config Release -Screenshots
.\tools\build.ps1 -Config Release -Generator "MinGW Makefiles"
.\tools\build.ps1 -QtPath C:\Qt\6.8.3\mingw_64 -MingwPath C:\Qt\Tools\mingw1310_64
```

Supported configurations are `Debug`, `Release`, `RelWithDebInfo` and
`MinSizeRel`. `-Generator` accepts `Ninja` (default), `MinGW Makefiles` and
`Unix Makefiles`. Ninja builds go to `build\<config>`; make builds go to
`build\make-<config>` (or `build\unix-make-<config>`) so the build folders are
never shared between generators. The executable is
`build\<config>\CK3 DNA Manager Qt.exe` or
`build\make-<config>\CK3 DNA Manager Qt.exe` respectively.

### CMake presets

Set the two environment variables, then use the presets:

```powershell
$env:QT_ROOT    = "C:/Qt/6.8.3/mingw_64"
$env:MINGW_ROOT = "C:/Qt/Tools/mingw1310_64"

cmake --preset release
cmake --build --preset release
ctest --preset release
```

Use `--preset debug` for a debug build. The presets write to `build/debug` and
`build/release`.

The bundled `make-debug` and `make-release` presets use the **MinGW Makefiles**
generator and Qt's `mingw32-make`:

```powershell
cmake --preset make-release
cmake --build --preset make-release
ctest --preset make-release
```

They write to `build/make-debug` and `build/make-release`.

### GNU Make

To build with `make` directly, configure once with a Makefiles generator and
then call `make` inside the build folder:

```powershell
cmake -S . -B build/make-release -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64 `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_RC_COMPILER=C:/Qt/Tools/mingw1310_64/bin/windres.exe `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe `
  -DBUILD_TESTING=ON

make -C build/make-release -j
```

`cmake --build build/make-release --parallel` works as well and picks the
generator from the cache. On Windows, `MinGW Makefiles` works with both
`mingw32-make.exe` and GNU `make.exe` (for example from w64devkit). The
`Unix Makefiles` generator additionally needs `sh.exe` on `PATH`.

Do not point a make generator at an existing Ninja build folder (or vice
versa); CMake caches the generator and refuses to mix them.

### Manual configure

```powershell
cmake -S . -B build/release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64 `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_RC_COMPILER=C:/Qt/Tools/mingw1310_64/bin/windres.exe `
  -DCMAKE_MAKE_PROGRAM=E:/w64devkit/bin/ninja.exe `
  -DBUILD_TESTING=ON

cmake --build build/release --parallel
```

Use forward slashes in CMake paths on Windows.

### Tests and development helpers

The test suite covers the Base64/Ruler codecs, legacy conversion, modded extra
slots, the SQLite layer, thumbnail generation and translation completeness.

```powershell
build\release\ck3_tests.exe -o build\release\test.log,txt
Get-Content build\release\test.log
```

Build the offscreen screenshot helper with `-DBUILD_SCREENSHOTS=ON` (or
`build.ps1 -Screenshots`), then:

```powershell
build\release\ck3_screenshots.exe --out screenshots\overview.png --entry "Countess Adela of Vermandois"
build\release\ck3_screenshots.exe --out screenshots\genes.png --tab genes
build\release\ck3_screenshots.exe --out screenshots\welcome.png --empty
build\release\ck3_screenshots.exe --out screenshots\dialog-add.png --dialog add
```

The helper uses Qt's test-mode data directory and never touches the real
library.

## Run

```powershell
build\release\CK3 DNA Manager Qt.exe
```

The application starts in English unless a different language was selected
previously. Qt DLLs must be on `PATH` when running from a build tree; the
deployed `dist` folder is self-contained.

## Data storage

| Item | Location |
| --- | --- |
| DNA library | `%APPDATA%\CK3 DNA Manager Qt\dna.db` |
| SQLite journal files | `dna.db-wal`, `dna.db-shm` next to the database |
| Settings | `HKEY_CURRENT_USER\Software\CK3 DNA Manager Qt\CK3 DNA Manager Qt` |
| Portraits | PNG/thumbnail BLOBs inside `dna.db` |

The app migrates an existing library and settings from the pre-1.0.1
`%APPDATA%\Welshtokki\CK3 DNA Manager` location on first launch.

`File > Open data folder` opens the library directory. To back up the library,
close the application first and copy all three `dna.db*` files. A copy made
while the app is running may omit recent WAL data.

The database schema is intentionally simple and can be inspected with any
SQLite tool. Do not store other files in that folder if you plan to replace the
folder wholesale during a restore.

## Importing DNA

### Adding a single entry

Use **New entry** (`Ctrl+N`) and paste either:

- **Base64 DNA** — the code used by current CK3 versions.
- **Ruler Designer code** — a `genes={ ... }` block copied from the game.

The format selector defaults to **Detect automatically**. A live validation
line and OK state tell you whether the pasted code can be saved.

### Importing libraries and characters

Use **Import library or character** (`Ctrl+Shift+O`) and select either an
original `CK3_DNA_DB.json` or a JSON file exported by this app.

For the original Electron app’s `CK3_DNA_DB.json`, the importer:

- reads entries from the JSON array or from its `data` array;
- converts each legacy Base64 payload to modern gene order;
- looks for portraits in an `img` folder next to the JSON file, using
  `img\<id>.png` first and then the other supported extensions
  (`.webp`, `.jpg`, `.jpeg`, `.bmp`, `.gif`);
- skips entries marked `remove` or with empty DNA and reports warnings at the
  end.

The original `img` folder must stay next to the JSON file during the import.
Portraits are copied into the SQLite database, so the original folder is not
needed afterwards.

For an app export (`"format": "ck3-dna-manager-qt"`), the importer:

- reads the `entries` array;
- prefers `rulerDesigner` and `rulerMeta` so modded keys and Ruler-only genes
  survive the round trip, falling back to `base64`;
- restores names, notes, favourite status, timestamps and the embedded
  `portraitPng`;
- regenerates thumbnails and reports any malformed entries as warnings.

## Exporting

- **Export current entry** (`Ctrl+E`) writes a JSON document with the name,
  notes, favourite flag, Base64 DNA, Ruler Designer code, Ruler metadata,
  timestamps, gene counts and an optional Base64-encoded portrait PNG.
- **Export all entries** (`Ctrl+Shift+E`) writes the same format for the whole
  library, under an `entries` array.
- **Copy Base64** (`Ctrl+Shift+C`) and **Copy Ruler code** (`Ctrl+Shift+R`)
  place the current code on the clipboard.

The export is intended for backup and interchange; it is not a direct CK3 save
file format.

## Modded DNA and the gene catalog

The bundled catalog is generated from **CK3 1.20.1.2 (Crozier)**:

- 106 gene definitions in 12 categories;
- 104 genes in the vanilla Base64 payload;
- 2 Ruler-Designer-only genes (`beards`, `hairstyles`) with no Base64 slot;
- 3 leading colour genes (`hair_color`, `skin_color`, `eye_color`) storing four
  raw RGBA values instead of allele pairs.

The accessory gene indices come straight from the game files: for
`eye_accessory`, `0` is `no_eyes` and `1` is `normal_eyes`; for
`teeth_accessory`, `0` is `no_teeth` and `1` is `normal_teeth`. Using the wrong
index here makes a character render without eyeballs or teeth in game.

Each morph gene occupies four bytes:

```text
[allele 1 variant, allele 1 strength, allele 2 variant, allele 2 strength]
```

A vanilla payload is therefore 416 bytes (104 × 4). Modded payloads may append
extra genes. CBO (Carnalitas) adds 13 genes, so its characters carry 117 slots
and 624-character Base64 strings. The application keeps unknown trailing slots
verbatim, exposes their four bytes in the **Non-vanilla & Ruler-only genes**
section of the editor, and writes them back unchanged when copying. Never
assume that a longer code is invalid.

Cheat Engine tables that build their DNA rows from the vanilla
`common\genes\*.txt` files only know the 104 vanilla genes. On a CBO character
they therefore label the DNA row "raw bytes only"; the raw-byte paste still
works, and the 13 CBO slots are left untouched.

Ruler Designer imports preserve every key in the `genes={ ... }` block, including
modded keys that are not in the bundled catalog. Their exact key names, option
names, strengths and order are stored alongside the DNA and restored when the
Ruler code is copied again. Vanilla Ruler-only fields (`beards`, `hairstyles`,
`clothes`) and the `override`/`entity` blocks are preserved in the Ruler code but
are not appended to the Base64 payload. Base64 itself cannot carry modded key
names; use **Copy Ruler code** when the names matter.

### Regenerating the catalogs

`resources/genes.json` (modern catalog):

```powershell
python tools\build_gene_catalog.py "D:\SteamLibrary\steamapps\common\Crusader Kings III"
```

`resources/legacy_genes.json` (option mapping for the old app):

```powershell
node tools\build_legacy_catalog.js path\to\CK3_DNA_Manager\js\gene_index.js
```

Both scripts are development tools; the generated JSON is embedded at build
time, so end users do not need Python or Node.

## Keyboard shortcuts

| Action | Shortcut |
| --- | --- |
| New DNA entry | `Ctrl+N` |
| Save details / genes | `Ctrl+S` |
| Import library or character | `Ctrl+Shift+O` |
| Export current entry | `Ctrl+E` |
| Export all entries | `Ctrl+Shift+E` |
| Copy Base64 DNA | `Ctrl+Shift+C` |
| Copy Ruler Designer code | `Ctrl+Shift+R` |
| Duplicate entry | `Ctrl+D` |
| Search library | `Ctrl+F` |
| Quit | `Ctrl+Q` |

`Ctrl+V` also pastes a portrait from the clipboard (an image, or a copied
`.png`/`.webp`/`.jpg` file). When the clipboard contains only an image this
works even while a text field has focus; if it also contains text, normal text
pasting wins. On the welcome screen, `Ctrl+V` opens a new entry with the
screenshot already attached. Both the detail view and the new-entry dialog also
have an explicit **Paste image** button.

## Project layout

```text
src/
  main.cpp              application bootstrap
  core/                 codec, catalog, database, i18n, image and import logic
  ui/                   main window, gene editor, dialogs, theme and layouts
resources/
  genes.json            modern CK3 gene catalog
  legacy_genes.json     legacy option mapping
  i18n/translations.json
  theme.qss             dark gold theme
  icons/                SVG icons and application icon
tests/
  test_core.cpp         QtTest suite
  screenshot.cpp        offscreen UI screenshot helper
tools/
  build.ps1             configure, build and test
  deploy.ps1            self-contained Windows deployment with windeployqt
  build_gene_catalog.py
  build_legacy_catalog.js
```

## Packaging

```powershell
.\tools\deploy.ps1 -Config Release
```

This builds the app, creates a clean `dist` folder, copies the executable and
documentation, and runs `windeployqt` for a self-contained Qt deployment. It
verifies the platform, SQLite and image/icon plugins afterwards.

To also produce a zip archive:

```powershell
.\tools\deploy.ps1 -Config Release -Zip -Version 1.0.0
```

The archive is written to the project root as
`CK3-DNA-Manager-1.0.0-win64.zip`. Use `-SkipBuild` to redeploy an existing
build, or `-SkipTests` to skip the test run during deployment. Pass
`-Generator "MinGW Makefiles"` to deploy a make-generated build; with
`-SkipBuild`, the matching `build\make-<config>` folder is used automatically.

## Localization

All UI chrome is loaded from `resources/i18n/translations.json`. The catalog
also stores the native language names used in the language menu and combo box.
To add a language, add its code, native name and complete key set to that file;
`I18n` falls back to English for missing keys, and the test suite checks key
parity.

Gene keys, gene display names and CK3 option identifiers are technical game
data and intentionally remain in English. Categories are localized. This keeps
the editor consistent with the CK3 files that modders work with.

## Credits

Inspired by the original
[CK3 DNA Manager](https://github.com/Welshtokki/CK3_DNA_Manager) by Welshtokki.
Gene definitions are derived from Crusader Kings III game data.

Crusader Kings III is a trademark of Paradox Interactive. This is an unofficial
fan tool and is not affiliated with or endorsed by Paradox Interactive.

Built with [Qt](https://www.qt.io) and SQLite.

## License

ISC. See [LICENSE](LICENSE). The original project also used the ISC license.
