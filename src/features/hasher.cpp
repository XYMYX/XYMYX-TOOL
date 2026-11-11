// hasher.cpp - menu item 3, the file hash checker.
//
// Works the same on both platforms, so there is no platform code behind it:
// std::filesystem walks the folders and core/sha256 does the maths.

#include <algorithm>
#include <ctime>
#include <system_error>
#include <vector>

#include "core/console.h"
#include "core/report.h"
#include "core/sha256.h"
#include "core/util.h"
#include "features/features.h"
#include "platform/platform.h"

namespace {

namespace fs = std::filesystem;

struct HashedFile {
  std::string relative;
  std::string hash;
};

// Dragging a file into the window pastes its path, but each system decorates
// it differently. Windows wraps paths with spaces in quotes. The macOS
// Terminal puts a backslash before spaces and other special characters.
// Both are undone here so the user does not have to clean up by hand.
std::string cleanPathInput(std::string input) {
  input = util::trim(input);
  if (input.size() >= 2 && ((input.front() == '"' && input.back() == '"') ||
                            (input.front() == '\'' && input.back() == '\''))) {
    input = input.substr(1, input.size() - 2);
  }
#ifndef _WIN32
  std::string unescaped;
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\\' && i + 1 < input.size()) ++i;
    unescaped.push_back(input[i]);
  }
  input = unescaped;
#endif
  return input;
}

// Websites print hashes in upper or lower case, sometimes in groups with
// spaces. Removing whitespace and lower-casing makes all of them comparable.
std::string normalizeHash(const std::string& input) {
  std::string out;
  for (char c : input) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    out.push_back(c);
  }
  return util::toLower(out);
}

bool isSha256Hex(const std::string& text) {
  if (text.size() != 64) return false;
  return std::all_of(text.begin(), text.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

bool askExpectedHash(std::string& expected) {
  for (;;) {
    std::string input;
    if (!con::readLine("\nPaste an expected SHA-256 to compare (Enter to skip): ", input)) {
      return false;
    }
    expected = normalizeHash(input);
    if (expected.empty()) return false;
    if (isSha256Hex(expected)) return true;
    con::println(con::paint(con::Color::Red,
                            "That is not a SHA-256 hash (64 hexadecimal characters)."));
  }
}

void hashSingleFile(Report& report, const fs::path& path) {
  std::error_code ec;
  const std::uintmax_t size = fs::file_size(path, ec);
  report.heading("File");
  report.row("Path", util::pathToUtf8(path));
  report.row("Size", ec ? std::string(platform::kNotAvailable) : util::formatBytes(size));
  const std::optional<std::string> hash = sha256File(path);
  if (!hash) {
    report.row("SHA-256", "not available (file could not be read)");
    return;
  }
  report.row("SHA-256", *hash);

  std::string expected;
  if (askExpectedHash(expected)) {
    report.heading("Comparison");
    report.row("Expected", expected);
    if (expected == *hash) {
      report.status(platform::Status::Ok, "Hash comparison", "match");
    } else {
      report.status(platform::Status::Warning, "Hash comparison", "MISMATCH");
    }
  }
}

void hashFolder(Report& report, const fs::path& root) {
  std::vector<fs::path> files;
  std::size_t skippedEntries = 0;
  // Step 1: collect the file list. skip_permission_denied makes the iterator
  // step over folders we may not open instead of stopping. All calls use the
  // error_code overloads, because the throwing versions would abort the whole
  // scan on the first unreadable entry.
  std::error_code ec;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  if (ec) {
    report.row("Folder", "not available (folder could not be read)");
    return;
  }
  while (it != end) {
    std::error_code entryError;
    // Symbolic links are not followed. A link can point outside the chosen
    // folder or back to a parent, which would hash files the user did not
    // ask for or loop forever. They are counted as skipped instead.
    const bool isLink = it->is_symlink(entryError);
    if (!entryError && isLink) {
      if (it->is_directory(entryError)) it.disable_recursion_pending();
      ++skippedEntries;
    } else if (!entryError && it->is_regular_file(entryError) && !entryError) {
      files.push_back(it->path());
    }
    it.increment(ec);
    if (ec) {
      ++skippedEntries;
      break;
    }
  }
  // The order a folder is listed in depends on the file system. Sorting makes
  // two runs over the same folder produce identical lists that can be diffed.
  std::sort(files.begin(), files.end());

  report.heading("Folder");
  report.row("Path", util::pathToUtf8(root));
  report.row("Files found", std::to_string(files.size()));

  report.heading("SHA-256 hashes");
  std::vector<HashedFile> hashed;
  std::vector<std::string> unreadable;
  std::uint64_t totalBytes = 0;
  for (const fs::path& file : files) {
    std::error_code relError;
    fs::path relative = fs::relative(file, root, relError);
    if (relError) relative = file;
    // Paths are stored relative to the chosen folder and with forward
    // slashes, so a list made on Windows matches one made on macOS.
    std::string relativeText = util::pathToUtf8(relative);
    std::replace(relativeText.begin(), relativeText.end(), '\\', '/');
    const std::optional<std::string> hash = sha256File(file);
    // A file we may not read (locked, no permission) is remembered by name
    // and reported. It never gets a hash line it did not earn.
    if (!hash) {
      unreadable.push_back(relativeText);
      continue;
    }
    std::error_code sizeError;
    const std::uintmax_t size = fs::file_size(file, sizeError);
    if (!sizeError) totalBytes += size;
    report.line(*hash + "  " + relativeText);
    hashed.push_back({relativeText, *hash});
  }
  if (hashed.empty()) report.line("  no files hashed");

  report.heading("Summary");
  report.row("Files hashed", std::to_string(hashed.size()));
  report.row("Data hashed", util::formatBytes(totalBytes));
  report.row("Unreadable files", std::to_string(unreadable.size()));
  report.row("Skipped entries", std::to_string(skippedEntries) + " (symbolic links, read errors)");
  for (const std::string& name : unreadable) {
    report.note("  not available (could not be read): " + name);
  }

  if (hashed.empty()) return;

  std::string expected;
  if (askExpectedHash(expected)) {
    report.heading("Comparison");
    report.row("Expected", expected);
    bool found = false;
    for (const HashedFile& file : hashed) {
      if (file.hash == expected) {
        report.status(platform::Status::Ok, "Match", file.relative);
        found = true;
      }
    }
    if (!found) {
      report.status(platform::Status::Warning, "No match",
                    "no hashed file in this folder has that SHA-256");
    }
  }

  con::println();
  if (con::confirm("Export the hash list to the reports folder? [y/N]: ")) {
    // "<hash><two spaces><path>" is the format the sha256sum tool writes and
    // reads, so the exported list can be checked later with "sha256sum -c".
    std::string content;
    for (const HashedFile& file : hashed) content += file.hash + "  " + file.relative + "\n";
    fs::path saved;
    std::string error;
    if (saveText("hash-list", content, saved, error)) {
      con::println(con::paint(con::Color::Green, "Saved: " + util::pathToUtf8(saved)));
    } else {
      con::println(con::paint(con::Color::Red, "Not saved: " + error));
    }
  }
}

}

void runHashChecker() {
  Report report("File hash checker");
  con::println("Enter the path of a file or a folder. Folders are hashed recursively.");
  con::println(con::paint(con::Color::Gray, "Tip: you can drag a file into this window."));

  std::string input;
  if (!con::readLine("\nPath: ", input)) return;
  input = cleanPathInput(input);
  if (input.empty()) {
    con::println(con::paint(con::Color::Yellow, "No path entered."));
    return;
  }

  const fs::path path = util::pathFromUtf8(input);
  std::error_code ec;
  const fs::file_status status = fs::status(path, ec);
  if (ec || !fs::exists(status)) {
    report.row("Path", input);
    report.row("Result", "not available (path does not exist or cannot be accessed)");
  } else if (fs::is_directory(status)) {
    hashFolder(report, path);
  } else if (fs::is_regular_file(status)) {
    hashSingleFile(report, path);
  } else {
    report.row("Path", input);
    report.row("Result", "not available (not a regular file or folder)");
  }

  offerSave(report);
}
