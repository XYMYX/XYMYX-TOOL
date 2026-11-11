# Architecture

This page explains how XYMYX Toolkit is put together. Read it first; the other pages in this folder each cover one module.

## The idea in one picture

```
            main.cpp
               │
           core/menu ──────────────► core/console   (screen and keyboard)
               │
        ┌──────┴───────────────────────────┐
        ▼                                  ▼
   features/*.cpp ───────────────► core/report      (print and save)
   (what to show)                  core/sha256      (hashing)
        │                          core/util        (small helpers)
        ▼
   platform/platform.h             (the contract)
        │
   ┌────┴─────┐
   ▼          ▼
platform/win  platform/mac         (how to read it from the OS)
```

There are three layers:

| Layer | Folder | Knows about the OS? | Job |
| --- | --- | --- | --- |
| Core | `src/core` | Only `console` and `util`, in small `#ifdef` blocks | Menu, output, reports, hashing, helpers |
| Features | `src/features` | No | Decide what is shown and how it is laid out |
| Platform | `src/platform` | Yes, entirely | Read facts from Windows or macOS |

## Folder structure

```
XYMYX-TOOL/
├─ CMakeLists.txt
├─ src/
│  ├─ main.cpp                 starts the tool, builds the menu
│  ├─ core/
│  │  ├─ console.*             colours, ANSI setup, keyboard input
│  │  ├─ menu.*                logo and numbered menu
│  │  ├─ report.*              result output and saving to reports/
│  │  ├─ sha256.*              SHA-256 implementation
│  │  └─ util.*                formatting, paths, text conversion
│  ├─ features/
│  │  ├─ features.h            one function per menu item
│  │  ├─ sysinfo.cpp           1. System info
│  │  ├─ network.cpp           2. Network
│  │  ├─ hasher.cpp            3. File hash checker
│  │  └─ security.cpp          4. Quick security check
│  └─ platform/
│     ├─ platform.h            shared data types, four read functions
│     ├─ win/                  Windows implementation
│     └─ mac/                  macOS implementation
├─ docs/                       these pages
└─ reports/                    created at runtime, next to the executable
```

## How one platform is chosen

Most cross-platform C++ projects sprinkle `#ifdef _WIN32` through the code. This project avoids that with one trick in `CMakeLists.txt`: it compiles **either** `src/platform/win/*.cpp` **or** `src/platform/mac/*.cpp`, never both.

Both folders implement the same four functions declared in `platform.h`:

```cpp
SystemInfo readSystemInfo();
std::optional<std::vector<AdapterInfo>> readAdapters();
SocketTable readSockets();
std::vector<Finding> readSecurity();
```

A feature calls `platform::readSystemInfo()` and gets back a plain struct. It never sees a Windows or macOS type. See [platform.md](platform.md).

## What happens when you press "1"

1. `main` in `main.cpp` builds a list of `MenuItem` and calls `runMenu`.
2. `runMenu` in `core/menu.cpp` prints the menu, reads a line with `con::readLine` and calls the chosen function, here `runSystemInfo`.
3. `runSystemInfo` in `features/sysinfo.cpp` creates a `Report` and calls `platform::readSystemInfo`.
4. `readSystemInfo` in `platform/win/sysinfo_win.cpp` (or the macOS file) asks the operating system and fills a `SystemInfo` struct.
5. Back in the feature, each field is passed to `Report::row`, which prints it and remembers it.
6. `offerSave` asks whether to save. On "y", `saveText` writes the remembered text to the `reports` folder.

Every feature follows the same six steps.

## Three rules the code is built around

### 1. Never report a value that was not read

Every value that can fail to load is a `std::optional`. An empty optional is shown as `not available`. There are no default values such as `0 GB` or `"Unknown CPU"` that could be mistaken for real data.

Where the reason is known to be missing rights, the text is `not available (requires admin)`. That wording is only used when the operating system actually answered "access denied".

### 2. Read-only on the host

The program contains exactly one function that creates a file: `saveText` in `core/report.cpp`. It writes only into `reports/` next to the executable, and only after the user typed "y". Everything else reads.

No function changes a setting, starts a service, or opens a network connection. Where an API offers both reading and writing or network access, the comments in the code say which part is used and why (for example `lastInstalledUpdate` in `security_win.cpp`).

### 3. One self-contained file

The Windows build links the C++ runtime into the `.exe`, so it needs no installer on the host. Only libraries that ship with Windows or macOS are used. SHA-256 is implemented in `core/sha256.cpp` instead of pulling in a crypto library.

## Strings: UTF-8 everywhere

All `std::string` values in the program are UTF-8. Windows APIs use UTF-16 (`wchar_t`), so text is converted at the border with `util::narrow` and `util::widen`. That keeps the feature code simple and makes file names with umlauts or emoji work. See [util.md](util.md).

## Where to go next

| Page | Covers |
| --- | --- |
| [console.md](console.md) | Colours and input |
| [menu.md](menu.md) | The menu loop |
| [report.md](report.md) | Printing and saving results |
| [sha256.md](sha256.md) | How the hash is calculated |
| [util.md](util.md) | Helpers and text conversion |
| [platform.md](platform.md) | The platform contract and common API patterns |
| [sysinfo.md](sysinfo.md) | System info |
| [network.md](network.md) | Network |
| [hasher.md](hasher.md) | File hash checker |
| [security.md](security.md) | Quick security check |
