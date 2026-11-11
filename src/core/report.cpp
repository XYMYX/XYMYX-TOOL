// report.cpp - shows results on screen and saves them as .txt files.
//
// This file contains the only code in the program that writes to disk
// (saveText). Keeping it in one place makes the "read-only on the host"
// promise easy to check.

#include "core/report.h"

#include <ctime>
#include <fstream>
#include <system_error>

#include "core/console.h"
#include "core/util.h"

namespace {

// Turns "Quick security check" into "quick-security-check". Only letters,
// digits and dashes survive, so the file name is valid on exFAT, NTFS and
// APFS alike.
std::string slug(std::string_view title) {
  std::string out;
  for (char c : title) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out.push_back(c);
    } else if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else if (!out.empty() && out.back() != '-') {
      out.push_back('-');
    }
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  return out.empty() ? "report" : out;
}

bool isUnavailable(std::string_view value) {
  return value.rfind(platform::kNotAvailable, 0) == 0;
}

}

// Every method below does the same two things: print the line (with colour)
// and append it (without colour) to text_. Escape codes in a saved file
// would show up as garbage in a text editor.
Report::Report(std::string title) : title_(std::move(title)) {
  con::println();
  con::println(con::paint(con::Color::Cyan, "=== " + title_ + " ==="));
}

void Report::heading(std::string_view text) {
  con::println();
  con::println(con::paint(con::Color::Bold, text));
  text_.append("\n").append(text).append("\n");
  text_.append(std::string(text.size(), '-')).append("\n");
}

void Report::row(std::string_view key, std::string_view value) {
  const std::string label = "  " + util::padRight(std::string(key) + ":", 18) + " ";
  con::println(con::paint(con::Color::Gray, label) +
               (isUnavailable(value) ? con::paint(con::Color::Yellow, value) : std::string(value)));
  text_.append(label).append(value).append("\n");
}

void Report::line(std::string_view text) {
  con::println(text);
  text_.append(text).append("\n");
}

void Report::note(std::string_view text) {
  con::println(con::paint(con::Color::Yellow, text));
  text_.append(text).append("\n");
}

void Report::status(platform::Status status, std::string_view name, std::string_view detail) {
  const char* label = "[NOT AVAILABLE]";
  con::Color color = con::Color::Gray;
  if (status == platform::Status::Ok) {
    label = "[OK]";
    color = con::Color::Green;
  } else if (status == platform::Status::Warning) {
    label = "[WARNING]";
    color = con::Color::Yellow;
  }
  const std::string tag = util::padRight(label, 16);
  const std::string rest = util::padRight(name, 24) + " " + std::string(detail);
  con::println("  " + con::paint(color, tag) + " " + rest);
  text_.append("  ").append(tag).append(" ").append(rest).append("\n");
}

void Report::blank() {
  con::println();
  text_.append("\n");
}

bool saveText(const std::string& prefix, const std::string& content,
              std::filesystem::path& savedPath, std::string& error) {
  // Anchored to the executable, not the current folder: see exeDir().
  const std::filesystem::path dir = util::exeDir() / "reports";
  // The error_code overloads report failure instead of throwing. A stick
  // that is write-protected or full is an expected situation, not a crash.
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    error = "could not create the reports folder: " + ec.message();
    return false;
  }
  const std::string stamp = util::formatTime(std::time(nullptr), "%Y-%m-%d_%H-%M-%S");
  std::filesystem::path path = dir / (prefix + "_" + stamp + ".txt");
  // Two saves within the same second would get the same name. Add a counter
  // rather than overwrite an existing report.
  for (int n = 2; std::filesystem::exists(path, ec) && n < 100; ++n) {
    path = dir / (prefix + "_" + stamp + "_" + std::to_string(n) + ".txt");
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "could not open the report file for writing";
    return false;
  }
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  // Close before checking: buffered data is only really written here, so a
  // full stick often shows up at this point and not at write().
  out.close();
  if (!out) {
    error = "writing the report file failed";
    return false;
  }
  savedPath = path;
  return true;
}

void offerSave(const Report& report) {
  con::println();
  if (!con::confirm("Save this result as a .txt report? [y/N]: ")) return;
  std::string content = "XYMYX Toolkit report\n";
  content += "Title:   " + report.title() + "\n";
  content += "Created: " + util::formatTime(std::time(nullptr), "%Y-%m-%d %H:%M:%S") + "\n";
  content += "========================================\n";
  content += report.text();
  std::filesystem::path path;
  std::string error;
  if (saveText(slug(report.title()), content, path, error)) {
    con::println(con::paint(con::Color::Green, "Saved: " + util::pathToUtf8(path)));
  } else {
    con::println(con::paint(con::Color::Red, "Not saved: " + error));
  }
}
