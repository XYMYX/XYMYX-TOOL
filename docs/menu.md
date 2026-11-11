# menu

Files: `src/core/menu.h`, `src/core/menu.cpp`, `src/main.cpp`

## The menu is data

A menu entry is a label plus a function to call:

```cpp
struct MenuItem {
  std::string label;
  void (*action)();
};
```

`void (*action)()` is a function pointer: the address of a function that takes no arguments and returns nothing. `main` fills a vector with four of these and passes it to `runMenu`.

The benefit: `runMenu` contains no `switch` with one case per feature. To add a feature you write the function and add one line to the list in `main`. The numbering and the "Exit" entry adjust on their own.

## The loop

`runMenu` repeats four steps:

1. Print the entries, numbered from 1. "Exit" is added as the last number.
2. Read a line with `con::readLine`.
3. Check the input.
4. Call the chosen function, then `con::pause`.

### Checking input

User input is never trusted to be a number. `runMenu` accepts only digits and converts them by hand.

Why not `std::stoi`? It throws an exception for input like `abc`, and it silently accepts `2x` as `2`. For a beginner-friendly tool, "please enter a number from the menu" is the better answer in both cases.

### Leaving the loop

There are two ways out:

- the user chooses the Exit number,
- `con::readLine` returns `false` because input has ended.

The second case is easy to forget. Without it, a closed input would make the loop spin forever.

## The logo

`printLogo` prints the XYMYX symbol as eleven lines of ASCII art: two vertical outer lines, two diagonals that cross in an X, and a short vertical line from the centre down. Only `|`, `/`, `\` and `X` are used, so it renders the same in the Windows console and the macOS Terminal.

The lines are written as raw string literals, `R"(...)"`. In a normal string every backslash would have to be doubled, which makes ASCII art unreadable in the source.

The diagonals move two columns per line. Terminal characters are about twice as tall as they are wide, so that keeps the X close to square.

## Colours

The logo, the menu numbers and report titles use bright white as the accent colour. Menu labels are not painted at all: they use the terminal's default foreground colour and stay readable on light backgrounds. Green, yellow and red are kept for results.

The version shown under the logo comes from `XYMYX_VERSION`. That is not defined in any source file: `CMakeLists.txt` passes it to the compiler from the `project(... VERSION ...)` line, so the version number exists in one place only.
