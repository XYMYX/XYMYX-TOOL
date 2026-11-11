// menu.cpp - prints the logo and runs the main menu loop.

#include "core/menu.h"

#include "core/console.h"
#include "core/util.h"

void printLogo() {
  // Raw string literals R"(...)" keep backslashes as they are. Without them
  // every "\" in the logo would have to be written as "\\".
  static const char* lines[] = {
      R"(| \                   / |)",
      R"(|   \               /   |)",
      R"(|     \           /     |)",
      R"(|       \       /       |)",
      R"(|         \   /         |)",
      R"(|           X           |)",
      R"(|         / | \         |)",
      R"(|       /   |   \       |)",
      R"(|     /     |     \     |)",
      R"(|   /               \   |)",
      R"(| /                   \ |)"};
  // The diagonals move two columns per line. Terminal characters are about
  // twice as tall as they are wide, so this keeps the X close to square.
  con::println();
  for (const char* line : lines) con::println(con::paint(con::Color::White, line));
  con::println();
  con::println(con::paint(con::Color::Gray,
                          std::string("  XYMYX Toolkit ") + XYMYX_VERSION +
                              "  |  portable, read-only, offline"));
}

int runMenu(const std::vector<MenuItem>& items) {
  for (;;) {
    con::println();
    con::println(con::paint(con::Color::Bold, "Main menu"));
    for (std::size_t i = 0; i < items.size(); ++i) {
      // Only the number is painted. The label uses the terminal's default
      // colour, so it stays readable on dark and on light backgrounds.
      con::println("  " + con::paint(con::Color::White, "[" + std::to_string(i + 1) + "]") + " " +
                   items[i].label);
    }
    con::println("  " + con::paint(con::Color::White, "[" + std::to_string(items.size() + 1) + "]") +
                 " Exit");
    std::string input;
    // readLine returns false when input has ended. Leaving here prevents an
    // endless loop that would print the menu forever.
    if (!con::readLine("\nChoose an option: ", input)) return 0;
    input = util::trim(input);
    if (input.empty()) continue;
    // The number is parsed by hand. std::stoi would throw an exception on
    // "abc" and accept "2x" as 2; this accepts digits only and never throws.
    std::size_t choice = 0;
    bool numeric = input.size() <= 3;
    for (char c : input) {
      if (c < '0' || c > '9') {
        numeric = false;
        break;
      }
      choice = choice * 10 + static_cast<std::size_t>(c - '0');
    }
    if (numeric && choice == items.size() + 1) return 0;
    if (!numeric || choice == 0 || choice > items.size()) {
      con::println(con::paint(con::Color::Red, "Please enter a number from the menu."));
      continue;
    }
    items[choice - 1].action();
    con::pause();
  }
}
