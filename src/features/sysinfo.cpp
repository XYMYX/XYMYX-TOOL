// sysinfo.cpp - menu item 1, system info.
//
// Asks the platform layer for a SystemInfo struct and prints it. Every field
// is a std::optional: if it is empty, the text "not available" is shown
// instead of a value.

#include <cstdio>

#include "core/report.h"
#include "core/util.h"
#include "features/features.h"
#include "platform/platform.h"

namespace {

std::string orUnavailable(const std::optional<std::string>& value) {
  return value ? *value : std::string(platform::kNotAvailable);
}

}

void runSystemInfo() {
  Report report("System info");
  const platform::SystemInfo info = platform::readSystemInfo();

  report.heading("Operating system");
  report.row("OS", orUnavailable(info.osName));
  report.row("Version", orUnavailable(info.osVersion));
  report.row("Uptime", info.uptimeSeconds ? util::formatDuration(*info.uptimeSeconds)
                                          : std::string(platform::kNotAvailable));

  report.heading("Processor");
  report.row("Model", orUnavailable(info.cpuModel));
  report.row("Physical cores", info.physicalCores ? std::to_string(*info.physicalCores)
                                                  : std::string(platform::kNotAvailable));
  report.row("Logical cores", info.logicalCores ? std::to_string(*info.logicalCores)
                                                : std::string(platform::kNotAvailable));

  report.heading("Memory");
  report.row("Total", info.ramTotal ? util::formatBytes(*info.ramTotal)
                                    : std::string(platform::kNotAvailable));
  if (info.ramUsed) {
    std::string used = util::formatBytes(*info.ramUsed);
    if (info.ramTotal && *info.ramTotal > 0) {
      char percent[16];
      std::snprintf(percent, sizeof(percent), " (%.0f%%)",
                    100.0 * static_cast<double>(*info.ramUsed) /
                        static_cast<double>(*info.ramTotal));
      used += percent;
    }
    report.row("Used", used);
  } else {
    report.row("Used", platform::kNotAvailable);
  }

  // Three different answers: could not ask, asked and got nothing, got a list.
  report.heading("Graphics");
  if (!info.gpus) {
    report.row("GPU", platform::kNotAvailable);
  } else if (info.gpus->empty()) {
    report.row("GPU", "none reported");
  } else {
    for (const std::string& gpu : *info.gpus) report.row("GPU", gpu);
  }

  report.heading("Drives");
  if (!info.drives) {
    report.row("Drives", platform::kNotAvailable);
  } else if (info.drives->empty()) {
    report.line("  none reported");
  } else {
    for (const platform::DriveInfo& drive : *info.drives) {
      std::string head = "  " + drive.name;
      if (!drive.label.empty()) head += "  [" + drive.label + "]";
      std::string meta;
      if (!drive.fileSystem.empty()) meta += drive.fileSystem;
      if (!drive.kind.empty()) meta += (meta.empty() ? "" : ", ") + drive.kind;
      if (!meta.empty()) head += "  (" + meta + ")";
      report.line(head);
      if (drive.totalBytes && drive.freeBytes) {
        report.line("      size " + util::formatBytes(*drive.totalBytes) + ", free " +
                    util::formatBytes(*drive.freeBytes));
      } else {
        report.line(std::string("      size and free space ") + platform::kNotAvailable);
      }
    }
  }

  offerSave(report);
}
