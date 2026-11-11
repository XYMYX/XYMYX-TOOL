// report.h - collects the result of one feature and can save it as text.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "platform/platform.h"

// Each method prints to the screen and records the same line, without
// colours, so that the saved report matches what the user saw.
class Report {
 public:
  explicit Report(std::string title);

  void heading(std::string_view text);
  void row(std::string_view key, std::string_view value);
  void line(std::string_view text);
  void note(std::string_view text);
  void status(platform::Status status, std::string_view name, std::string_view detail);
  void blank();

  const std::string& title() const { return title_; }
  const std::string& text() const { return text_; }

 private:
  std::string title_;
  std::string text_;
};

// Writes content to reports/<prefix>_<timestamp>.txt next to the executable.
// This is the only function in the whole program that creates a file.
bool saveText(const std::string& prefix, const std::string& content,
              std::filesystem::path& savedPath, std::string& error);
void offerSave(const Report& report);
