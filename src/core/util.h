// util.h - small helpers used everywhere: formatting numbers and times,
// trimming text, finding the folder of the executable and converting
// between UTF-8 and the wide strings Windows uses.

#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>

namespace util {

std::string formatBytes(std::uint64_t bytes);
std::string formatDuration(std::uint64_t seconds);
std::string formatTime(std::time_t t, const char* format);
std::string trim(std::string_view text);
std::string toLower(std::string_view text);
std::string padRight(std::string_view text, std::size_t width);

// Folder that contains the running executable. On the USB stick this is
// where the "reports" folder goes.
std::filesystem::path exeDir();

// All strings in this program are UTF-8. These two convert to and from
// std::filesystem::path correctly on both platforms.
std::filesystem::path pathFromUtf8(const std::string& text);
std::string pathToUtf8(const std::filesystem::path& path);

#ifdef _WIN32
std::string narrow(std::wstring_view wide);
std::wstring widen(std::string_view text);
#endif

}
