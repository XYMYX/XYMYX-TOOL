// console.h - everything that touches the terminal: colours, printing and
// reading a line of input. The rest of the program never calls printf or
// std::cin directly, so platform quirks stay in console.cpp.

#pragma once

#include <string>
#include <string_view>

namespace con {

// White is the accent colour (logo, menu numbers, titles). Green, Yellow and
// Red are reserved for results, so a colour always means the same thing.
enum class Color { White, Red, Green, Yellow, Gray, Bold };

void init();
void shutdown();
bool colorsEnabled();

// Wraps text in colour codes, or returns it unchanged when colours are off.
std::string paint(Color color, std::string_view text);

void print(std::string_view text);
void println(std::string_view text = {});

// Returns false when there is no more input (window closed, Ctrl+Z, end of a
// redirected file). Callers use that to stop instead of looping forever.
bool readLine(std::string_view prompt, std::string& out);
bool confirm(std::string_view prompt);
void pause();

}
