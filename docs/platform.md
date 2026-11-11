# platform

Files: `src/platform/platform.h`, `src/platform/win/win_util.h`, `src/platform/mac/mac_util.h`

`platform.h` is the contract between the features and the operating system. This page also collects the patterns you will meet again and again when calling OS APIs, so the feature pages can stay short.

## The contract

`platform.h` declares plain structs and four functions:

| Function | Returns | Used by |
| --- | --- | --- |
| `readSystemInfo` | `SystemInfo` | [sysinfo](sysinfo.md) |
| `readAdapters` | list of `AdapterInfo` | [network](network.md) |
| `readSockets` | `SocketTable` | [network](network.md) |
| `readSecurity` | list of `Finding` | [security](security.md) |

Each function exists twice, once under `platform/win` and once under `platform/mac`. `CMakeLists.txt` compiles only one folder, so the linker finds exactly one definition.

The structs contain only standard C++ types. No `HANDLE`, no `CFStringRef`. That is what lets the feature files compile unchanged on both systems.

## Empty means "not read"

Look at `SystemInfo`: almost every member is a `std::optional`. A platform function fills in what it could read and leaves the rest empty. It never invents a fallback value.

For lists there is a second level:

```cpp
std::optional<std::vector<std::string>> gpus;
```

- empty optional: the question could not be asked,
- empty vector: the system answered "none",
- filled vector: here they are.

Those are three different facts, and the feature prints three different texts.

The two shared texts `kNotAvailable` and `kNeedsAdmin` are defined once in `platform.h`, so the wording is identical everywhere.

## Patterns you will see in OS code

### Ask for the size, then ask for the data

C APIs cannot return a `std::vector`. You pass a buffer. Since you do not know how big it must be, many functions can be called with no buffer just to learn the size:

```cpp
DWORD size = 0;
RegGetValueW(..., nullptr, &size);   // "how big?"
std::wstring buffer(...);
RegGetValueW(..., buffer.data(), &size);   // "now give it to me"
```

Examples: `regString` in `win_util.h`, `sysctlString` in `mac_util.h`, `fetchTcp` in `network_win.cpp`.

Some data can grow between the two calls (a new connection opens). Those places retry in a small loop.

### Structs that carry their own size

Many Windows structs start with a size field that you must set before the call:

```cpp
MEMORYSTATUSEX status{};
status.dwLength = sizeof(status);
```

Windows uses it to tell which version of the struct your program was built with. Forgetting it makes the call fail.

### Error codes instead of exceptions

OS APIs do not throw. They return a value that says whether the call worked:

| Style | Success looks like | Used by |
| --- | --- | --- |
| `BOOL` | non-zero, details via `GetLastError()` | most Win32 functions |
| `LONG` / `DWORD` status | `ERROR_SUCCESS` or `NO_ERROR` | registry, IP Helper |
| `HRESULT` | `SUCCEEDED(hr)` | COM |
| `int` | `0`, details via `errno` | macOS / POSIX |

Every call in this project checks its result. On failure the field stays empty.

### Whoever receives it must release it

The OS often hands out resources that you must give back with a matching call:

| Got it from | Give it back with |
| --- | --- |
| `OpenProcess`, `CreateToolhelp32Snapshot` | `CloseHandle` |
| `RegOpenKeyExW` | `RegCloseKey` |
| `NetUserEnum` | `NetApiBufferFree` |
| COM objects | `Release` |
| CoreFoundation `...Copy...` / `...Create...` functions | `CFRelease` |
| `getifaddrs` | `freeifaddrs` |

For COM, `win_util.h` contains a small `ComPtr` class that calls `Release` in its destructor. This is the C++ idea called RAII: tie the clean-up to the lifetime of an object so it cannot be forgotten on an early `return`.

### Least privilege

When an API lets you say what you intend to do, the code asks for the minimum:

- `OpenProcess` with `PROCESS_QUERY_LIMITED_INFORMATION`, not full access,
- `RegOpenKeyExW` with `KEY_QUERY_VALUE`, not `KEY_ALL_ACCESS`.

Asking for less succeeds more often without admin rights, and makes it obvious that nothing is being changed.

## Helpers

### `win_util.h`

- `regString`, `regDword`: read one registry value, or return an empty optional.
- `ComPtr`: releases a COM object automatically.

### `mac_util.h`

- `sysctlString`, `sysctlValue`: read kernel values by name.
- `cfToString`: convert a CoreFoundation string to `std::string`.
- `runCommand`: run a built-in read-only command and capture its output.

`runCommand` is the fallback for the few things macOS exposes only through a command-line tool. The command strings are fixed text in the source. User input is never inserted into them, so there is nothing to inject.

## Without admin rights

The tool never asks for elevation. When the OS refuses a call, the code distinguishes:

- the OS said "access denied": show `not available (requires admin)`,
- any other failure: show `not available`.

It does not assume. A process that ended a millisecond ago also makes `OpenProcess` fail, and calling that "requires admin" would be a guess.
