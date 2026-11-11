# XYMYX Toolkit
**A portable, read-only system and security check that runs straight from a USB stick.**

XYMYX Toolkit is a small command-line tool for Windows 10/11 (x64) and macOS (Apple Silicon). You start it from a USB stick, read the result, and optionally save it as a text report on the same stick.

It needs no installation and no admin rights. It changes no settings and writes nothing to the computer it runs on. It makes no network calls and has no telemetry.

The code is commented for beginners and comes with a [docs](docs/architecture.md) folder that explains how each part works.

Written by XYMYX. C++20, built with CMake. No runtime dependencies.

> **Status:** the Windows build is compiled and tested with MSVC and MinGW-w64. The macOS code is written but has not been compiled or run yet. Treat it as untested until this note is removed.

## What it does

| Menu | Shows |
| --- | --- |
| 1. System info | OS and version, CPU, RAM, GPU, drives with size and free space, uptime |
| 2. Network | Local IP addresses, adapters, listening ports with process name, active connections |
| 3. File hash checker | SHA-256 of a file or a whole folder, compare with a pasted hash, export the hash list |
| 4. Quick security check | Firewall, OS updates, disk encryption (BitLocker / FileVault), auto-login, guest account |
| 5. Exit | |

Security findings are shown as **OK**, **Warning** or **Not available**. A value that could not be read is never guessed.

After every result you can save it as `reports/<name>_<date>_<time>.txt` next to the executable.

## Screenshots

_Screenshots will be added here._

## Build

You need CMake 3.16 or newer and a C++20 compiler. The executable ends up in `<build folder>/bin/`.

```bash
git clone https://github.com/XYMYX/XYMYX-TOOL.git
cd XYMYX-TOOL
```

### Windows, MSVC

In the **x64 Native Tools Command Prompt for VS** (2019 or newer):

```bash
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-msvc
```

### Windows, MinGW-w64

In the **MSYS2 UCRT64** shell:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

### macOS, Apple Silicon

With the Xcode command line tools (`xcode-select --install`) and CMake:

```bash
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac
```

## Usage from a USB stick

1. Format the stick as exFAT so Windows and macOS can both write to it.
2. Copy `xymyx-toolkit.exe` (Windows) and `xymyx-toolkit` (macOS) to the stick. They can share a folder.
3. **Windows:** double-click `xymyx-toolkit.exe`. The file is **unsigned**, so SmartScreen may warn. If you trust the source, choose "More info" and then "Run anyway".
4. **macOS:** open Terminal and run it by path, for example `/Volumes/STICK/xymyx-toolkit`.
5. Type the number of a menu entry and press Enter.

The macOS binary is **unsigned** too. If Gatekeeper blocks it, right-click the file in Finder and choose **Open** (on macOS 15 or newer: **System Settings → Privacy & Security → Open Anyway**), or remove the quarantine attribute:

```bash
xattr -d com.apple.quarantine /Volumes/STICK/xymyx-toolkit
```

Reports contain IP addresses, open ports and drive details. Treat them as private.

## Limitations

The tool never asks for admin rights. Without them:

| Area | Limit |
| --- | --- |
| Ports on macOS | Only your own processes can be inspected. The rest is counted and shown as `not available (requires admin)`. |
| BitLocker | Only the state of each drive. No encryption method, no key protectors. |
| FileVault, guest account | Can be unreadable on managed or domain-joined computers. |
| Hashing | Files you may not read are listed as unreadable. |

Also:

- **Updates** are judged from local data only. The tool does not ask Microsoft or Apple whether newer updates exist.
- **Firewall and encryption** checks cover the built-in products only.
- **It is a quick check, not an audit.** "OK" means one setting looks fine, not that the computer is secure.

## Learn how it works

Start with the architecture page, then pick the module you are curious about.

| Page | Topic |
| --- | --- |
| [architecture.md](docs/architecture.md) | The three layers and how a menu choice travels through them |
| [platform.md](docs/platform.md) | The Windows/macOS split and common OS API patterns |
| [console.md](docs/console.md) | Colours, encodings and keyboard input |
| [menu.md](docs/menu.md) | The menu loop |
| [report.md](docs/report.md) | Printing and saving results |
| [util.md](docs/util.md) | Helpers, UTF-8 and UTF-16 |
| [sha256.md](docs/sha256.md) | SHA-256 step by step |
| [sysinfo.md](docs/sysinfo.md) | Where the system information comes from |
| [network.md](docs/network.md) | Adapters, ports and process names |
| [hasher.md](docs/hasher.md) | Hashing files and folders |
| [security.md](docs/security.md) | The five security checks |

## License

MIT. See [LICENSE](LICENSE).
