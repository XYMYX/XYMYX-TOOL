// util.cpp - formatting helpers, text conversion and "where am I running
// from". Nothing in here knows about features; it is the bottom layer.

#include "core/util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <mach-o/dyld.h>
#endif

namespace util {

std::string formatBytes(std::uint64_t bytes) {
  static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 5) {
    value /= 1024.0;
    ++unit;
  }
  char buffer[48];
  if (unit == 0) {
    std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
  }
  return buffer;
}

std::string formatDuration(std::uint64_t seconds) {
  const std::uint64_t days = seconds / 86400;
  const std::uint64_t hours = (seconds % 86400) / 3600;
  const std::uint64_t minutes = (seconds % 3600) / 60;
  char buffer[64];
  if (days > 0) {
    std::snprintf(buffer, sizeof(buffer), "%llud %lluh %llum",
                  static_cast<unsigned long long>(days),
                  static_cast<unsigned long long>(hours),
                  static_cast<unsigned long long>(minutes));
  } else if (hours > 0) {
    std::snprintf(buffer, sizeof(buffer), "%lluh %llum",
                  static_cast<unsigned long long>(hours),
                  static_cast<unsigned long long>(minutes));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%llum %llus",
                  static_cast<unsigned long long>(minutes),
                  static_cast<unsigned long long>(seconds % 60));
  }
  return buffer;
}

std::string formatTime(std::time_t t, const char* format) {
  // Plain localtime() returns a pointer to one shared internal struct, which
  // is easy to overwrite by accident. localtime_s (Windows) and localtime_r
  // (macOS) fill a struct we own instead. Same job, different argument order.
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buffer[64];
  if (std::strftime(buffer, sizeof(buffer), format, &tm) == 0) return {};
  return buffer;
}

std::string trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return std::string(text.substr(begin, end - begin));
}

std::string toLower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string padRight(std::string_view text, std::size_t width) {
  std::string out(text);
  if (out.size() < width) out.append(width - out.size(), ' ');
  return out;
}

#ifdef _WIN32

// Windows APIs speak UTF-16 (wchar_t). This program keeps every string as
// UTF-8 in std::string and converts only at the border to the Windows API.
// narrow() goes UTF-16 -> UTF-8, widen() the other way.
//
// WideCharToMultiByte / MultiByteToWideChar are called twice each: the first
// call with no output buffer returns the size needed, the second converts.
std::string narrow(std::wstring_view wide) {
  if (wide.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                       nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size,
                      nullptr, nullptr);
  return out;
}

std::wstring widen(std::string_view text) {
  if (text.empty()) return {};
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

// Why not std::filesystem::current_path()? The current folder is wherever the
// program was started from, which may be the user's home folder on the host.
// Reports must land on the stick, so we ask for the location of the .exe.
//
// GetModuleFileNameW(nullptr, ...) returns the full path of the running
// executable. It does not tell us the needed size up front. If the result
// fills the whole buffer it was probably cut off, so we retry with a bigger
// one.
std::filesystem::path exeDir() {
  std::vector<wchar_t> buffer(512);
  for (;;) {
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) return std::filesystem::current_path();
    if (length < buffer.size()) {
      return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
}

// On Windows, building a path from a narrow string would interpret the bytes
// in the legacy code page and break names with umlauts or emoji. Going
// through UTF-16 avoids that.
std::filesystem::path pathFromUtf8(const std::string& text) {
  return std::filesystem::path(widen(text));
}

std::string pathToUtf8(const std::filesystem::path& path) {
  return narrow(path.wstring());
}

#else

std::filesystem::path exeDir() {
  // _NSGetExecutablePath is the macOS way to ask "where is my executable".
  // Called with no buffer it reports the size needed. The path it returns may
  // contain symlinks or "..", so canonical() cleans it up afterwards.
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size + 1, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) return std::filesystem::current_path();
  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::canonical(buffer.data(), ec);
  if (ec) resolved = std::filesystem::path(buffer.data());
  return resolved.parent_path();
}

std::filesystem::path pathFromUtf8(const std::string& text) {
  return std::filesystem::path(text);
}

std::string pathToUtf8(const std::filesystem::path& path) {
  return path.string();
}

#endif

}
