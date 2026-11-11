// console.cpp - terminal setup, coloured output and keyboard input.
//
// Colours are done with ANSI escape sequences: short invisible byte strings
// such as "\x1b[92m" that tell the terminal "switch to green". macOS
// terminals understand them out of the box. The Windows console has to be
// asked first, which is most of what init() does.

#include "core/console.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "core/util.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace con {

namespace {

bool g_colors = false;

#ifdef _WIN32
// Remembered so shutdown() can restore the console exactly.
HANDLE g_out = INVALID_HANDLE_VALUE;
DWORD g_outMode = 0;
bool g_modeChanged = false;
UINT g_oldOutputCp = 0;
#endif

const char* code(Color color) {
  switch (color) {
    case Color::Red: return "\x1b[91m";
    case Color::Green: return "\x1b[92m";
    case Color::Yellow: return "\x1b[93m";
    case Color::Cyan: return "\x1b[96m";
    case Color::Magenta: return "\x1b[95m";
    case Color::Gray: return "\x1b[90m";
    case Color::Bold: return "\x1b[1m";
  }
  return "";
}

}

void init() {
#ifdef _WIN32
  // GetStdHandle gives us the handle Windows uses for this program's output.
  g_out = GetStdHandle(STD_OUTPUT_HANDLE);

  // All text in this program is UTF-8. The Windows console defaults to an old
  // regional code page, which would turn names like "Zürich" into garbage.
  // SetConsoleOutputCP switches it to UTF-8; the old value is restored in
  // shutdown().
  g_oldOutputCp = GetConsoleOutputCP();
  SetConsoleOutputCP(CP_UTF8);

  // GetConsoleMode only succeeds if the output really is a console window.
  // If the output was redirected into a file it fails, and colours stay off
  // so the file does not fill up with escape codes.
  DWORD mode = 0;
  if (g_out != INVALID_HANDLE_VALUE && GetConsoleMode(g_out, &mode)) {
    g_outMode = mode;
    // ENABLE_VIRTUAL_TERMINAL_PROCESSING is the switch that makes the console
    // interpret ANSI escape sequences. Very old consoles reject it; then the
    // call fails and we fall back to plain text.
    if (SetConsoleMode(g_out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
      g_modeChanged = true;
      g_colors = true;
    }
  }
#else
  // isatty answers "is the output a real terminal?" (as opposed to a file or
  // a pipe). TERM=dumb is how a terminal says it cannot do colours.
  g_colors = isatty(STDOUT_FILENO) != 0;
  const char* term = std::getenv("TERM");
  if (term == nullptr || std::string_view(term) == "dumb") g_colors = false;
#endif
  // NO_COLOR is a widely used convention (no-color.org): if the variable
  // exists, programs should not print colours.
  if (std::getenv("NO_COLOR") != nullptr) g_colors = false;
}

void shutdown() {
#ifdef _WIN32
  if (g_modeChanged) SetConsoleMode(g_out, g_outMode);
  if (g_oldOutputCp != 0) SetConsoleOutputCP(g_oldOutputCp);
#endif
}

bool colorsEnabled() {
  return g_colors;
}

std::string paint(Color color, std::string_view text) {
  if (!g_colors) return std::string(text);
  std::string out = code(color);
  out.append(text);
  // "\x1b[0m" resets all attributes, so the colour does not leak into
  // whatever is printed next.
  out.append("\x1b[0m");
  return out;
}

void print(std::string_view text) {
  std::fwrite(text.data(), 1, text.size(), stdout);
  // Flush right away: a prompt without a trailing newline would otherwise sit
  // in the buffer and the user would be asked for input without seeing why.
  std::fflush(stdout);
}

void println(std::string_view text) {
  std::fwrite(text.data(), 1, text.size(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

bool readLine(std::string_view prompt, std::string& out) {
  print(prompt);
  out.clear();
#ifdef _WIN32
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  // Same test as in init(): is the input a real keyboard console?
  if (in != INVALID_HANDLE_VALUE && GetConsoleMode(in, &mode)) {
    // ReadConsoleW delivers the typed text as UTF-16. std::getline would go
    // through the legacy code page and lose characters outside it, which
    // matters when someone types or drags in a path like "C:\Fotos\Zürich".
    std::wstring wide;
    wchar_t buffer[1024];
    for (;;) {
      DWORD read = 0;
      if (!ReadConsoleW(in, buffer, 1024, &read, nullptr) || read == 0) {
        if (wide.empty()) return false;
        break;
      }
      wide.append(buffer, read);
      // A very long line arrives in several pieces. The newline marks the end.
      if (wide.back() == L'\n') break;
    }
    // Ctrl+Z then Enter is the Windows way to say "end of input". It arrives
    // as the character 0x1A.
    if (!wide.empty() && wide.front() == L'\x1a') return false;
    while (!wide.empty() && (wide.back() == L'\n' || wide.back() == L'\r')) wide.pop_back();
    out = util::narrow(wide);
    return true;
  }
#endif
  // Input is not a console (redirected from a file, or macOS where the
  // terminal already delivers UTF-8): the standard library is fine.
  if (!std::getline(std::cin, out)) return false;
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return true;
}

bool confirm(std::string_view prompt) {
  std::string answer;
  // Anything other than an explicit yes counts as no. Saving a file is the
  // only thing this tool ever writes, so it must never happen by accident.
  if (!readLine(prompt, answer)) return false;
  answer = util::toLower(util::trim(answer));
  return answer == "y" || answer == "yes";
}

void pause() {
  std::string ignored;
  readLine(paint(Color::Gray, "\nPress Enter to return to the menu..."), ignored);
}

}
