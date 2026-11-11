// security.cpp - menu item 4, the quick security check.
//
// This file only presents findings. Deciding what is OK or a Warning happens
// in the platform files (security_win.cpp, security_mac.cpp), because the
// rules differ per operating system.

#include "core/report.h"
#include "features/features.h"
#include "platform/platform.h"

void runSecurityCheck() {
  Report report("Quick security check");
  report.note("Read-only check. Nothing on this computer is changed.");

  const std::vector<platform::Finding> findings = platform::readSecurity();

  report.heading("Findings");
  int ok = 0;
  int warnings = 0;
  int unavailable = 0;
  for (const platform::Finding& finding : findings) {
    report.status(finding.status, finding.name, finding.detail);
    if (finding.status == platform::Status::Ok) {
      ++ok;
    } else if (finding.status == platform::Status::Warning) {
      ++warnings;
    } else {
      ++unavailable;
    }
  }

  // "Not available" gets its own count on purpose. Hiding unreadable checks
  // would make the summary look better than what was actually verified.
  report.heading("Summary");
  report.row("OK", std::to_string(ok));
  report.row("Warning", std::to_string(warnings));
  report.row("Not available", std::to_string(unavailable));

  offerSave(report);
}
