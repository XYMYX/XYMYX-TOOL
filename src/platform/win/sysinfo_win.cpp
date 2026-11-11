// sysinfo_win.cpp - reads system information on Windows.
//
// Everything here is available to a normal user. Each read function fills
// its part of SystemInfo and simply leaves a field empty if a call fails.

#include <algorithm>
#include <vector>

#include "platform/platform.h"
#include "platform/win/win_util.h"

namespace platform {

namespace {

const wchar_t* kCurrentVersion = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

void readOs(SystemInfo& info) {
  // The obvious function, GetVersionEx, does not tell the truth: for
  // compatibility it reports Windows 8 to any program without a special
  // manifest. RtlGetVersion in ntdll.dll returns the real numbers.
  //
  // ntdll.dll is loaded into every Windows process, so GetModuleHandleW just
  // finds it. GetProcAddress then looks the function up by name at runtime.
  // That way we need no extra import library, which keeps the MinGW and MSVC
  // builds identical.
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  bool haveVersion = false;
  if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
    auto fn = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (fn != nullptr && fn(&version) == 0) haveVersion = true;
  }

  std::optional<std::string> product = win::regString(HKEY_LOCAL_MACHINE, kCurrentVersion,
                                                      L"ProductName");
  if (product) {
    // Microsoft never updated this registry text: Windows 11 still says
    // "Windows 10 Pro" here. Build 22000 was the first Windows 11 build, so
    // the build number we really read decides which name is correct.
    if (haveVersion && version.dwBuildNumber >= 22000) {
      const std::string old = "Windows 10";
      const std::size_t pos = product->find(old);
      if (pos != std::string::npos) product->replace(pos, old.size(), "Windows 11");
    }
    info.osName = product;
  }

  if (haveVersion) {
    std::string text = std::to_string(version.dwMajorVersion) + "." +
                       std::to_string(version.dwMinorVersion) + "." +
                       std::to_string(version.dwBuildNumber);
    // UBR ("update build revision") is the last number of the version shown
    // by winver. It rises with every monthly update.
    if (auto ubr = win::regDword(HKEY_LOCAL_MACHINE, kCurrentVersion, L"UBR")) {
      text += "." + std::to_string(*ubr);
    }
    if (auto display = win::regString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"DisplayVersion")) {
      text = *display + " (" + text + ")";
    }
    info.osVersion = text;
  }
}

void readCpu(SystemInfo& info) {
  if (auto name = win::regString(HKEY_LOCAL_MACHINE,
                                 L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                 L"ProcessorNameString")) {
    info.cpuModel = util::trim(*name);
  }

  // The CPU name above comes from the registry, where Windows stores it at
  // boot. That is simpler than WMI and needs no COM.
  //
  // GetLogicalProcessorInformationEx lists one record per physical core.
  // The first call with no buffer only reports the size needed. The records
  // have different lengths, so we walk the buffer using each record's Size
  // field instead of treating it as an array.
  DWORD length = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
  if (length > 0) {
    std::vector<BYTE> buffer(length);
    if (GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()), &length)) {
      unsigned cores = 0;
      DWORD offset = 0;
      while (offset < length) {
        auto* item =
            reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (item->Size == 0) break;
        if (item->Relationship == RelationProcessorCore) ++cores;
        offset += item->Size;
      }
      if (cores > 0) info.physicalCores = cores;
    }
  }

  // Logical cores include hyper-threading. ALL_PROCESSOR_GROUPS matters on
  // machines with more than 64 logical cores, which Windows splits into groups.
  const DWORD logical = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  if (logical > 0) info.logicalCores = logical;
}

void readMemory(SystemInfo& info) {
  // GlobalMemoryStatusEx reports physical memory. Many Windows structs must
  // carry their own size in the first field; the API uses it to know which
  // version of the struct it was given, and fails if it is not set.
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) {
    info.ramTotal = status.ullTotalPhys;
    info.ramUsed = status.ullTotalPhys - status.ullAvailPhys;
  }
}

void readGpus(SystemInfo& info) {
  std::vector<std::string> names;
  bool any = false;
  // EnumDisplayDevicesW is called with a rising index until it fails. It was
  // chosen over DXGI or WMI because it is a single plain function. It returns
  // one entry per monitor output, so a card with four outputs appears four
  // times; duplicates are filtered by name.
  for (DWORD index = 0;; ++index) {
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) break;
    any = true;
    if (device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) continue;
    const std::string name = util::trim(util::narrow(device.DeviceString));
    if (name.empty()) continue;
    if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
  }
  if (any) info.gpus = names;
}

const char* driveKind(UINT type) {
  switch (type) {
    case DRIVE_FIXED: return "fixed";
    case DRIVE_REMOVABLE: return "removable";
    case DRIVE_REMOTE: return "network";
    case DRIVE_CDROM: return "optical";
    case DRIVE_RAMDISK: return "ram disk";
    default: return "unknown";
  }
}

void readDrives(SystemInfo& info) {
  // GetLogicalDriveStringsW returns all drive roots in one buffer, each ended
  // by a zero and the whole list ended by a second zero: "C:\0D:\0\0".
  const DWORD length = GetLogicalDriveStringsW(0, nullptr);
  if (length == 0) return;
  std::vector<wchar_t> buffer(length + 1, L'\0');
  if (GetLogicalDriveStringsW(length, buffer.data()) == 0) return;

  // Asking an empty card reader or DVD drive for its size can make Windows
  // pop up an "insert a disk" dialog. SEM_FAILCRITICALERRORS turns that
  // dialog into a normal error for this thread. The old mode is restored
  // below.
  DWORD oldMode = 0;
  SetThreadErrorMode(SEM_FAILCRITICALERRORS, &oldMode);
  std::vector<DriveInfo> drives;
  for (const wchar_t* root = buffer.data(); *root != L'\0'; root += wcslen(root) + 1) {
    DriveInfo drive;
    drive.name = util::narrow(root);
    drive.kind = driveKind(GetDriveTypeW(root));
    wchar_t label[MAX_PATH + 1] = {};
    wchar_t fileSystem[MAX_PATH + 1] = {};
    if (GetVolumeInformationW(root, label, MAX_PATH + 1, nullptr, nullptr, nullptr, fileSystem,
                              MAX_PATH + 1)) {
      drive.label = util::narrow(label);
      drive.fileSystem = util::narrow(fileSystem);
    }
    // Fails for a drive without a medium. The two optionals then stay empty
    // and the drive is listed with "not available" instead of 0 bytes.
    ULARGE_INTEGER freeToCaller{}, total{}, totalFree{};
    if (GetDiskFreeSpaceExW(root, &freeToCaller, &total, &totalFree)) {
      drive.totalBytes = total.QuadPart;
      drive.freeBytes = totalFree.QuadPart;
    }
    drives.push_back(std::move(drive));
  }
  SetThreadErrorMode(oldMode, nullptr);
  info.drives = std::move(drives);
}

}

SystemInfo readSystemInfo() {
  SystemInfo info;
  readOs(info);
  readCpu(info);
  readMemory(info);
  readGpus(info);
  readDrives(info);
  // Milliseconds since the system started. Note for readers: with Windows
  // "Fast Startup", shutting down hibernates the kernel, so this counter is
  // not reset by a normal shutdown, only by a restart.
  info.uptimeSeconds = GetTickCount64() / 1000;
  return info;
}

}
