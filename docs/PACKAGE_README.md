# CK3 DNA Manager Qt

This package contains CK3 DNA Manager Qt, a local library for Crusader Kings
III DNA codes and portraits. Official release packages are self-contained.

## Run it

**Windows**

Extract the ZIP and run `CK3 DNA Manager Qt.exe`.

**Linux**

- AppImage: make it executable and run it:

  ```bash
  chmod +x CK3-DNA-Manager-*.AppImage
  ./CK3-DNA-Manager-*.AppImage
  ```

- Tarball: extract it and run the `ck3-dna-manager-qt` binary. In the
  self-contained release archive it is under `usr/bin/`.

No installer, account, cloud service or telemetry is required.

## Data storage

| Item | Location |
| --- | --- |
| DNA library (Windows) | `%APPDATA%\CK3 DNA Manager Qt\dna.db` |
| DNA library (Linux) | `~/.local/share/CK3 DNA Manager Qt/dna.db` (or `$XDG_DATA_HOME`) |
| Settings (Windows) | `HKEY_CURRENT_USER\Software\CK3 DNA Manager Qt\CK3 DNA Manager Qt` |
| Settings (Linux) | `~/.config/CK3 DNA Manager Qt/CK3 DNA Manager Qt.conf` (or `$XDG_CONFIG_HOME`) |
| Portraits | PNG/thumbnail BLOBs inside `dna.db` |

Use **File > Open data folder** to open the library directory. To back up the
library, close the application first and copy all three `dna.db*` files. A copy
made while the app is running may omit recent WAL data.

On Windows, the app migrates an existing library and settings from the
pre-1.0.1 `%APPDATA%\Welshtokki\CK3 DNA Manager` location on first launch.

## Documentation

The full documentation, web version and development instructions are at:

<https://github.com/grnnk/CK3-DNA-Manager-Qt>

## License

See the `LICENSE` file included with this package.
