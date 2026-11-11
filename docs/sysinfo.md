# sysinfo

Files: `src/features/sysinfo.cpp`, `src/platform/win/sysinfo_win.cpp`, `src/platform/mac/sysinfo_mac.cpp`

Menu item 1. Shows operating system, CPU, memory, graphics cards, drives and uptime.

## Feature side

`runSystemInfo` calls `platform::readSystemInfo` once and prints the returned `SystemInfo` section by section. For each field it prints either the value or `not available`.

There is no OS-specific code in this file.

## Where each value comes from

| Value | Windows | macOS |
| --- | --- | --- |
| OS name | registry `ProductName` | `sw_vers -productName` |
| OS version | `RtlGetVersion`, registry `DisplayVersion` and `UBR` | sysctl `kern.osproductversion`, `kern.osversion` |
| CPU model | registry `ProcessorNameString` | sysctl `machdep.cpu.brand_string` |
| Cores | `GetLogicalProcessorInformationEx`, `GetActiveProcessorCount` | sysctl `hw.physicalcpu`, `hw.logicalcpu` |
| RAM | `GlobalMemoryStatusEx` | sysctl `hw.memsize`, `host_statistics64` |
| GPU | `EnumDisplayDevicesW` | IOKit registry |
| Drives | `GetLogicalDriveStringsW`, `GetVolumeInformationW`, `GetDiskFreeSpaceExW` | `getmntinfo` |
| Uptime | `GetTickCount64` | sysctl `kern.boottime` |

## Windows notes

### The version number that lies

`readOs` does not use the well-known `GetVersionEx`. Since Windows 8.1 that function reports an old version to any program that does not declare, in a manifest, which Windows versions it supports. It is a compatibility feature for old software.

`RtlGetVersion` lives one layer lower, in `ntdll.dll`, and returns the real numbers. The code looks it up at runtime with `GetModuleHandleW` and `GetProcAddress`.

### "Windows 10" on Windows 11

The registry value `ProductName` still says `Windows 10` on Windows 11. `readOs` corrects the name using the build number it read: build 22000 was the first Windows 11 release.

### The registry as a data source

The registry is a tree of keys (like folders) holding named values. Windows stores a lot of hardware and version facts there at boot. Reading from `HKEY_LOCAL_MACHINE` is allowed for normal users; writing is not, and this tool never tries.

### Cores

`readCpu` counts physical cores by asking for one record per core. The records have different sizes, so the loop advances by each record's own `Size` field rather than indexing an array.

### Graphics cards

`EnumDisplayDevicesW` returns one entry per monitor output. A card with four outputs appears four times, so `readGpus` filters duplicate names.

### Drives without a disk

Asking an empty card reader for its size can make Windows show an "insert a disk" dialog. `readDrives` suppresses that with `SetThreadErrorMode(SEM_FAILCRITICALERRORS, ...)` and restores the previous mode afterwards. The drive is still listed, with size `not available`.

### Uptime and Fast Startup

`GetTickCount64` counts milliseconds since the kernel started. With Windows "Fast Startup", shutting down hibernates the kernel, so the counter keeps running across a shutdown. Only "Restart" resets it. If uptime looks too long, that is why.

## macOS notes

The macOS code has not been compiled or tested yet.

### sysctl

`sysctl` is a key/value store inside the kernel. Names look like paths: `hw.memsize`, `kern.osversion`. `sysctlString` and `sysctlValue` in `mac_util.h` wrap the lookups.

### Memory "used"

macOS has no single "used memory" number. The kernel reports page counts per category through `host_statistics64`. `readMemory` adds active, wired and compressed pages and multiplies by the page size. That is close to what Activity Monitor shows as "Memory Used", but the two numbers will not match exactly.

### Graphics

IOKit keeps a tree of all hardware, the I/O Registry. `readGpus` asks for all entries of class `IOAccelerator` and reads their `model` property, which may be a string or raw bytes depending on the Mac.

### Drives

`getmntinfo` returns the mount table. macOS mounts several internal volumes that Finder hides; they are flagged `MNT_DONTBROWSE` and `readDrives` skips them, except for `/`.

## Available without admin?

Everything in this feature is readable by a normal user on both systems.
