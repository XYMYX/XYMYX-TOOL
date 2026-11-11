// platform.h - the contract between the features and the operating system.
//
// The features include only this header. Each function declared at the bottom
// is implemented twice: once in src/platform/win and once in src/platform/mac.
// CMake compiles exactly one of those folders, so the rest of the program
// never needs to know which system it runs on.
//
// The most important rule of the tool is encoded in the types here: a value
// that could not be read is an empty std::optional, never a made-up default.
// The features turn an empty optional into the text "not available".

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace platform {

// Shared wording, so both platforms and all features say exactly the same.
inline constexpr const char* kNeedsAdmin = "not available (requires admin)";
inline constexpr const char* kNotAvailable = "not available";

enum class Status { Ok, Warning, NotAvailable };

struct DriveInfo {
  std::string name;
  std::string label;
  std::string fileSystem;
  std::string kind;
  // Empty when the drive exists but has no medium, e.g. an empty card reader.
  std::optional<std::uint64_t> totalBytes;
  std::optional<std::uint64_t> freeBytes;
};

struct SystemInfo {
  std::optional<std::string> osName;
  std::optional<std::string> osVersion;
  std::optional<std::string> cpuModel;
  std::optional<unsigned> physicalCores;
  std::optional<unsigned> logicalCores;
  std::optional<std::uint64_t> ramTotal;
  std::optional<std::uint64_t> ramUsed;
  // optional<vector>: an empty optional means "could not ask", an empty
  // vector means "asked, and the system reported none". Those are different
  // answers and are shown differently.
  std::optional<std::vector<std::string>> gpus;
  std::optional<std::vector<DriveInfo>> drives;
  std::optional<std::uint64_t> uptimeSeconds;
};

struct AdapterInfo {
  std::string name;
  std::string description;
  std::string type;
  std::string mac;
  bool up = false;
  bool loopback = false;
  // Text form with prefix length, e.g. "192.168.1.20/24".
  std::vector<std::string> addresses;
};

struct SocketEntry {
  std::string protocol;
  bool ipv6 = false;
  std::string localAddress;
  std::uint16_t localPort = 0;
  std::string remoteAddress;
  std::uint16_t remotePort = 0;
  std::string state;
  // True for TCP sockets in LISTEN state and for bound UDP sockets.
  bool listening = false;
  std::optional<std::uint32_t> pid;
  // If the name could not be read, process stays empty and processNote says
  // why (kNeedsAdmin or kNotAvailable).
  std::optional<std::string> process;
  std::string processNote;
};

struct SocketTable {
  bool ok = false;
  std::vector<SocketEntry> entries;
  // Things the user must know about gaps in the table, e.g. how many
  // processes could not be inspected without admin rights.
  std::vector<std::string> notes;
};

struct Finding {
  std::string name;
  Status status = Status::NotAvailable;
  std::string detail;
};

SystemInfo readSystemInfo();
std::optional<std::vector<AdapterInfo>> readAdapters();
SocketTable readSockets();
std::vector<Finding> readSecurity();

}
