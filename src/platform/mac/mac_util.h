// mac_util.h - small helpers shared by the macOS platform files:
// running a read-only system command, reading sysctl values and converting
// CoreFoundation strings.

#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace platform::mac {

// Runs a command and returns what it printed. Used only where macOS offers no
// public API (FileVault, firewall state, product name), and only with fixed
// command strings written in this source code. User input is never put into a
// command, so nothing can be injected.
//
// popen starts the command through the shell and gives us its output as a
// FILE*. Error output is sent to /dev/null so it does not appear in the
// middle of our own screen. A non-zero exit status is treated as "could not
// read", which becomes "not available" further up.
inline std::optional<std::string> runCommand(const std::string& command) {
  FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
  if (pipe == nullptr) return std::nullopt;
  std::string output;
  char buffer[512];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) output.append(buffer);
  const int status = pclose(pipe);
  if (status != 0) return std::nullopt;
  return output;
}

// sysctl is the kernel's key/value store for system facts ("hw.memsize",
// "kern.osversion", ...). sysctlbyname looks a value up by its name. As with
// many C APIs, we call it twice: once without a buffer to learn the size,
// then again to fetch the data.
inline std::optional<std::string> sysctlString(const char* name) {
  std::size_t size = 0;
  if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return std::nullopt;
  std::vector<char> buffer(size + 1, '\0');
  if (sysctlbyname(name, buffer.data(), &size, nullptr, 0) != 0) return std::nullopt;
  return std::string(buffer.data());
}

// For fixed-size values (int, uint64_t, struct timeval). The size check
// protects against reading a value whose type is not the one we expected.
template <typename T>
std::optional<T> sysctlValue(const char* name) {
  T value{};
  std::size_t size = sizeof(value);
  if (sysctlbyname(name, &value, &size, nullptr, 0) != 0 || size != sizeof(value)) {
    return std::nullopt;
  }
  return value;
}

// CoreFoundation has its own string type (CFStringRef). This copies one into
// a UTF-8 std::string so the rest of the program can use it.
inline std::optional<std::string> cfToString(CFStringRef text) {
  if (text == nullptr) return std::nullopt;
  const CFIndex length = CFStringGetLength(text);
  const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::vector<char> buffer(static_cast<std::size_t>(capacity), '\0');
  if (!CFStringGetCString(text, buffer.data(), capacity, kCFStringEncodingUTF8)) {
    return std::nullopt;
  }
  return std::string(buffer.data());
}

}
