// menu.h - the logo and the numbered main menu.

#pragma once

#include <string>
#include <vector>

// A plain function pointer is enough here: every feature is a function that
// takes nothing and returns nothing.
struct MenuItem {
  std::string label;
  void (*action)();
};

void printLogo();
int runMenu(const std::vector<MenuItem>& items);
