// sysinfo_mac.cpp - reads system information on macOS.
//
// Most values come from sysctl, the kernel's key/value store. Memory usage
// comes from the Mach kernel interface, the GPU name from the IOKit device
// registry and the drives from the mount table.
//
// Not compiled or tested yet. See the note in README.md.

#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/time.h>

#include <algorithm>
#include <ctime>

#include "core/util.h"
#include "platform/mac/mac_util.h"
#include "platform/platform.h"

namespace platform {

namespace {

void readOs(SystemInfo& info) {
  // The product name ("macOS") has no sysctl key, so the built-in sw_vers
  // command is asked. The version and build numbers below do have keys.
  if (auto name = mac::runCommand("/usr/bin/sw_vers -productName")) {
    const std::string trimmed = util::trim(*name);
    if (!trimmed.empty()) info.osName = trimmed;
  }
  if (auto version = mac::sysctlString("kern.osproductversion")) {
    std::string text = *version;
    if (auto build = mac::sysctlString("kern.osversion")) text += " (" + *build + ")";
    info.osVersion = text;
  }
}

void readCpu(SystemInfo& info) {
  if (auto brand = mac::sysctlString("machdep.cpu.brand_string")) info.cpuModel = util::trim(*brand);
  if (auto physical = mac::sysctlValue<int>("hw.physicalcpu")) {
    if (*physical > 0) info.physicalCores = static_cast<unsigned>(*physical);
  }
  if (auto logical = mac::sysctlValue<int>("hw.logicalcpu")) {
    if (*logical > 0) info.logicalCores = static_cast<unsigned>(*logical);
  }
}

void readMemory(SystemInfo& info) {
  if (auto total = mac::sysctlValue<std::uint64_t>("hw.memsize")) info.ramTotal = *total;

  // macOS counts memory in pages, not bytes. host_statistics64 returns how
  // many pages are in each state. There is no single "used" number, so we
  // add active pages, wired pages (which can never be moved to disk) and
  // compressed pages. That is close to "Memory Used" in Activity Monitor.
  vm_size_t pageSize = 0;
  const mach_port_t host = mach_host_self();
  if (host_page_size(host, &pageSize) != KERN_SUCCESS) return;
  vm_statistics64_data_t stats{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  if (host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) !=
      KERN_SUCCESS) {
    return;
  }
  const std::uint64_t pages = static_cast<std::uint64_t>(stats.active_count) +
                              static_cast<std::uint64_t>(stats.wire_count) +
                              static_cast<std::uint64_t>(stats.compressor_page_count);
  info.ramUsed = pages * static_cast<std::uint64_t>(pageSize);
}

void readGpus(SystemInfo& info) {
  // IOKit keeps a tree of all hardware, the "I/O Registry". We ask for every
  // entry of class IOAccelerator (the graphics drivers) and read its "model"
  // property. The alternative, system_profiler, takes seconds to run.
  //
  // The property may sit on the entry itself or on a parent, so the search
  // looks through the parents too. On Apple Silicon it is a string, on Intel
  // Macs raw bytes; both are handled. Everything IOKit and CoreFoundation
  // hand out must be released again, hence the Release calls.
  io_iterator_t iterator = 0;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"),
                                   &iterator) != KERN_SUCCESS) {
    return;
  }
  std::vector<std::string> names;
  io_object_t service = 0;
  while ((service = IOIteratorNext(iterator)) != 0) {
    CFTypeRef model = IORegistryEntrySearchCFProperty(
        service, kIOServicePlane, CFSTR("model"), kCFAllocatorDefault,
        kIORegistryIterateRecursively | kIORegistryIterateParents);
    if (model != nullptr) {
      std::string name;
      if (CFGetTypeID(model) == CFStringGetTypeID()) {
        if (auto text = mac::cfToString(static_cast<CFStringRef>(model))) name = *text;
      } else if (CFGetTypeID(model) == CFDataGetTypeID()) {
        CFDataRef data = static_cast<CFDataRef>(model);
        const char* bytes = reinterpret_cast<const char*>(CFDataGetBytePtr(data));
        const std::size_t length = static_cast<std::size_t>(CFDataGetLength(data));
        name.assign(bytes, length);
        const std::size_t zero = name.find('\0');
        if (zero != std::string::npos) name.resize(zero);
      }
      CFRelease(model);
      name = util::trim(name);
      if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
        names.push_back(name);
      }
    }
    IOObjectRelease(service);
  }
  IOObjectRelease(iterator);
  info.gpus = names;
}

void readDrives(SystemInfo& info) {
  // getmntinfo returns the table of mounted file systems. MNT_NOWAIT uses the
  // values the kernel already has; without it, an unreachable network share
  // could make the call hang. The array belongs to the system and must not
  // be freed by us.
  struct statfs* mounts = nullptr;
  const int count = getmntinfo(&mounts, MNT_NOWAIT);
  if (count <= 0 || mounts == nullptr) return;
  std::vector<DriveInfo> drives;
  for (int i = 0; i < count; ++i) {
    const struct statfs& mount = mounts[i];
    const std::string point = mount.f_mntonname;
    // macOS mounts many internal volumes (Preboot, VM, Data, ...) and marks
    // them "don't browse" so Finder hides them. We hide them too, except "/".
    if ((mount.f_flags & MNT_DONTBROWSE) != 0 && point != "/") continue;
    if (mount.f_blocks == 0) continue;
    DriveInfo drive;
    drive.name = point;
    drive.fileSystem = mount.f_fstypename;
    drive.kind = (mount.f_flags & MNT_LOCAL) != 0 ? "local" : "network";
    drive.totalBytes = static_cast<std::uint64_t>(mount.f_blocks) * mount.f_bsize;
    drive.freeBytes = static_cast<std::uint64_t>(mount.f_bavail) * mount.f_bsize;
    drives.push_back(std::move(drive));
  }
  info.drives = std::move(drives);
}

void readUptime(SystemInfo& info) {
  // The kernel stores the moment it booted. Uptime is "now minus that".
  if (auto boot = mac::sysctlValue<struct timeval>("kern.boottime")) {
    const std::time_t now = std::time(nullptr);
    if (boot->tv_sec > 0 && now >= boot->tv_sec) {
      info.uptimeSeconds = static_cast<std::uint64_t>(now - boot->tv_sec);
    }
  }
}

}

SystemInfo readSystemInfo() {
  SystemInfo info;
  readOs(info);
  readCpu(info);
  readMemory(info);
  readGpus(info);
  readDrives(info);
  readUptime(info);
  return info;
}

}
