// security_mac.cpp - the quick security check on macOS.
//
// Two sources are used. Settings stored as preferences are read with
// CFPreferences, the same API the system itself uses for .plist files.
// Where macOS has no public API (firewall state, FileVault), a built-in
// command is run and its output is read. All of them only report state.
//
// Not compiled or tested yet. See the note in README.md.

#include <unistd.h>

#include <ctime>

#include "core/util.h"
#include "platform/mac/mac_util.h"
#include "platform/platform.h"

namespace platform {

namespace {

// Reads one value from the system-wide preferences in /Library/Preferences.
// "AnyUser" + "CurrentHost" selects that location instead of the settings of
// the logged-in user. The result is null if the key does not exist. Whatever
// is returned must be released with CFRelease, which the typed helpers below
// take care of. They also check the type before converting, because a plist
// value can be a boolean, a number, a string or a date.
CFPropertyListRef systemPreference(CFStringRef domain, CFStringRef key) {
  return CFPreferencesCopyValue(key, domain, kCFPreferencesAnyUser, kCFPreferencesCurrentHost);
}

std::optional<bool> preferenceBool(CFStringRef domain, CFStringRef key) {
  CFPropertyListRef value = systemPreference(domain, key);
  if (value == nullptr) return std::nullopt;
  std::optional<bool> result;
  if (CFGetTypeID(value) == CFBooleanGetTypeID()) {
    result = CFBooleanGetValue(static_cast<CFBooleanRef>(value)) != 0;
  } else if (CFGetTypeID(value) == CFNumberGetTypeID()) {
    long long number = 0;
    if (CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberLongLongType, &number)) {
      result = number != 0;
    }
  }
  CFRelease(value);
  return result;
}

std::optional<long long> preferenceInt(CFStringRef domain, CFStringRef key) {
  CFPropertyListRef value = systemPreference(domain, key);
  if (value == nullptr) return std::nullopt;
  std::optional<long long> result;
  if (CFGetTypeID(value) == CFNumberGetTypeID()) {
    long long number = 0;
    if (CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberLongLongType, &number)) {
      result = number;
    }
  }
  CFRelease(value);
  return result;
}

std::optional<std::string> preferenceString(CFStringRef domain, CFStringRef key) {
  CFPropertyListRef value = systemPreference(domain, key);
  if (value == nullptr) return std::nullopt;
  std::optional<std::string> result;
  if (CFGetTypeID(value) == CFStringGetTypeID()) {
    result = mac::cfToString(static_cast<CFStringRef>(value));
  }
  CFRelease(value);
  return result;
}

std::optional<std::time_t> preferenceDate(CFStringRef domain, CFStringRef key) {
  CFPropertyListRef value = systemPreference(domain, key);
  if (value == nullptr) return std::nullopt;
  std::optional<std::time_t> result;
  if (CFGetTypeID(value) == CFDateGetTypeID()) {
    const CFAbsoluteTime absolute = CFDateGetAbsoluteTime(static_cast<CFDateRef>(value));
    result = static_cast<std::time_t>(absolute + kCFAbsoluteTimeIntervalSince1970);
  }
  CFRelease(value);
  return result;
}

Finding firewall() {
  const std::string name = "Firewall";
  // socketfilterfw is Apple's own command-line tool for the application
  // firewall. "--getglobalstate" only prints the state. Older macOS versions
  // kept this in a plist file, but that file no longer exists on current
  // versions, so the command is the way that works everywhere.
  const char* tool = "/usr/libexec/ApplicationFirewall/socketfilterfw";
  if (access(tool, X_OK) != 0) return {name, Status::NotAvailable, kNotAvailable};
  const auto output = mac::runCommand(std::string(tool) + " --getglobalstate");
  if (!output) return {name, Status::NotAvailable, kNotAvailable};
  const std::string text = util::toLower(*output);
  if (text.find("state = 0") != std::string::npos || text.find("disabled") != std::string::npos) {
    return {name, Status::Warning, "macOS application firewall is off"};
  }
  if (text.find("state = 1") != std::string::npos || text.find("state = 2") != std::string::npos ||
      text.find("enabled") != std::string::npos) {
    return {name, Status::Ok, "macOS application firewall is on"};
  }
  return {name, Status::NotAvailable, kNotAvailable};
}

Finding updates() {
  const std::string name = "OS updates";
  // macOS remembers the result of its last update check in this preference
  // domain. Reading it is purely local. Running "softwareupdate -l" would
  // give a fresher answer but contacts Apple's servers, which this tool
  // never does. Any of the three values may be missing; only the ones that
  // exist are reported.
  const CFStringRef domain = CFSTR("com.apple.SoftwareUpdate");
  const auto automatic = preferenceBool(domain, CFSTR("AutomaticCheckEnabled"));
  const auto pending = preferenceInt(domain, CFSTR("LastRecommendedUpdatesAvailable"));
  const auto lastCheck = preferenceDate(domain, CFSTR("LastFullSuccessfulDate"));

  if (!automatic && !pending && !lastCheck) return {name, Status::NotAvailable, kNotAvailable};

  Status status = Status::Ok;
  std::string detail;
  auto add = [&](const std::string& part) {
    if (!detail.empty()) detail += "; ";
    detail += part;
  };
  if (automatic) {
    add(std::string("automatic check ") + (*automatic ? "on" : "off"));
    if (!*automatic) status = Status::Warning;
  }
  if (pending) {
    add(std::to_string(*pending) + " recommended updates pending at last check");
    if (*pending > 0) status = Status::Warning;
  }
  if (lastCheck) {
    add("last successful check " + util::formatTime(*lastCheck, "%Y-%m-%d"));
    const double ageDays = std::difftime(std::time(nullptr), *lastCheck) / 86400.0;
    if (ageDays > 35.0) status = Status::Warning;
  }
  return {name, status, detail};
}

Finding diskEncryption() {
  const std::string name = "Disk encryption";
  if (access("/usr/bin/fdesetup", X_OK) != 0) return {name, Status::NotAvailable, kNotAvailable};
  // fdesetup is the built-in FileVault tool; "status" prints one line such as
  // "FileVault is On." and changes nothing. If the command fails, the most
  // likely reason is that it was refused, so the finding says admin rights
  // are required.
  const auto output = mac::runCommand("/usr/bin/fdesetup status");
  if (!output) return {name, Status::NotAvailable, kNeedsAdmin};
  const std::string text = util::toLower(*output);
  if (text.find("filevault is on") != std::string::npos) {
    if (text.find("decryption in progress") != std::string::npos) {
      return {name, Status::Warning, "FileVault decryption in progress"};
    }
    return {name, Status::Ok, "FileVault on"};
  }
  if (text.find("filevault is off") != std::string::npos) {
    if (text.find("encryption in progress") != std::string::npos) {
      return {name, Status::Warning, "FileVault encryption in progress"};
    }
    return {name, Status::Warning, "FileVault off"};
  }
  return {name, Status::NotAvailable, kNotAvailable};
}

// CFPreferences returns null both for "key not set" and for "file not
// readable". For the login settings that difference matters: a missing key
// means the feature is off, an unreadable file means we do not know. So we
// first check with access() that the file can be read at all.
bool loginWindowReadable() {
  return access("/Library/Preferences/com.apple.loginwindow.plist", R_OK) == 0;
}

Finding autoLogin() {
  const std::string name = "Auto-login";
  if (!loginWindowReadable()) return {name, Status::NotAvailable, kNotAvailable};
  const auto user = preferenceString(CFSTR("com.apple.loginwindow"), CFSTR("autoLoginUser"));
  if (user && !user->empty()) {
    return {name, Status::Warning, "enabled for user \"" + *user + "\""};
  }
  return {name, Status::Ok, "disabled"};
}

Finding guestAccount() {
  const std::string name = "Guest account";
  if (!loginWindowReadable()) return {name, Status::NotAvailable, kNotAvailable};
  const auto enabled = preferenceBool(CFSTR("com.apple.loginwindow"), CFSTR("GuestEnabled"));
  if (!enabled) return {name, Status::Ok, "disabled (no guest setting stored)"};
  return {name, *enabled ? Status::Warning : Status::Ok, *enabled ? "enabled" : "disabled"};
}

}

std::vector<Finding> readSecurity() {
  std::vector<Finding> findings;
  findings.push_back(firewall());
  findings.push_back(updates());
  findings.push_back(diskEncryption());
  findings.push_back(autoLogin());
  findings.push_back(guestAccount());
  return findings;
}

}
