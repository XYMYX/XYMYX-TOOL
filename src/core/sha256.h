// sha256.h - SHA-256 written from scratch, so the tool needs no crypto
// library. See docs/sha256.md for the background.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// Usage: create one, call update() as often as you like with pieces of the
// data, then call hex() once to get the result. Feeding the data in pieces
// is what lets us hash a 50 GB file with 1 MB of memory.
class Sha256 {
 public:
  Sha256();
  void update(const std::uint8_t* data, std::size_t size);
  std::string hex();

 private:
  void transform(const std::uint8_t* block);

  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t totalBytes_ = 0;
};

std::string sha256Text(const std::string& text);

// Empty optional if the file could not be opened or a read failed.
std::optional<std::string> sha256File(const std::filesystem::path& path);
