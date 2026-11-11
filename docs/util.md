# util

Files: `src/core/util.h`, `src/core/util.cpp`

Small helpers that every other module uses. Nothing in here knows about features.

## Formatting

| Function | Example result |
| --- | --- |
| `formatBytes` | `476.3 GB` |
| `formatDuration` | `3d 4h 12m` |
| `formatTime` | `2026-01-31_14-05-09` |
| `padRight` | pads text with spaces to line up columns |

`formatBytes` divides by 1024, the way Windows Explorer does. A drive sold as "500 GB" (counted in steps of 1000) therefore shows up as about 465 GB.

`formatTime` uses `localtime_s` on Windows and `localtime_r` on macOS. Plain `localtime` returns a pointer to one shared internal struct; the `_s` and `_r` versions fill a struct you provide. They do the same job but take their arguments in opposite order, which is why there is an `#ifdef`.

## Where is the executable: `exeDir`

Reports must be saved next to the tool on the stick. The obvious `std::filesystem::current_path()` is wrong for this: the current folder is wherever the program was started from. Started from a terminal on a Mac, that is usually the home folder on the host, exactly where the tool must not write.

So the tool asks the operating system where its own file is:

| System | Call |
| --- | --- |
| Windows | `GetModuleFileNameW(nullptr, ...)` |
| macOS | `_NSGetExecutablePath` |

### A common C API pattern

Neither call returns a `std::string`. You pass a buffer and its size. If the buffer was too small, you allocate a bigger one and call again. You will see this pattern in almost every file under `src/platform`. See [platform.md](platform.md).

## UTF-8 and UTF-16

This is the part most likely to surprise someone new to Windows programming.

- This program stores all text as **UTF-8** in `std::string`.
- Windows APIs come in two flavours. The "A" versions use an old regional code page and cannot represent all characters. The "W" versions use **UTF-16** in `wchar_t`. Only the W versions are used here.
- macOS uses UTF-8 natively.

`narrow` (UTF-16 to UTF-8) and `widen` (UTF-8 to UTF-16) convert at the border. They wrap `WideCharToMultiByte` and `MultiByteToWideChar`, each called twice: once to learn the output size, once to convert.

The rule of thumb used in this project: convert as late as possible on the way into a Windows call, and as early as possible on the way out. Everything in between is UTF-8.

### Paths

`pathFromUtf8` and `pathToUtf8` exist for the same reason. On Windows, constructing a `std::filesystem::path` from a narrow string interprets the bytes in the regional code page. A file called `Zürich.txt` would not be found. Going through UTF-16 avoids that. On macOS the two functions are one-liners.
