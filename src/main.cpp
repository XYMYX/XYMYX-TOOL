// main.cpp - entry point.
//
// Sets up the console, shows the logo and hands control to the menu loop.
// All real work lives in src/features (what to show) and src/platform
// (how to read it from the operating system).

#include <vector>

#include "core/console.h"
#include "core/menu.h"
#include "features/features.h"

int main() {
  con::init();
  printLogo();

  // The menu is just data: a label and the function to call. Adding a feature
  // means adding one line here. "Exit" is appended by runMenu itself.
  const std::vector<MenuItem> items = {
      {"System info", runSystemInfo},
      {"Network", runNetwork},
      {"File hash checker", runHashChecker},
      {"Quick security check", runSecurityCheck},
  };
  const int result = runMenu(items);

  con::println(con::paint(con::Color::Gray, "Goodbye."));
  // Put the console back the way we found it, so the terminal behaves
  // normally for whatever the user runs next.
  con::shutdown();
  return result;
}
