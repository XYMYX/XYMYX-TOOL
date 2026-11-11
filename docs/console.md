# console

Files: `src/core/console.h`, `src/core/console.cpp`

Everything that touches the terminal goes through this module. No other file calls `printf`, `std::cout` or `std::cin`.

## Why a separate module

Printing text sounds trivial, but three things differ between systems:

- how colours are switched on,
- which character encoding the terminal expects,
- how typed text arrives.

Keeping those differences in one file means the rest of the program can simply call `con::println`.

## Colours: ANSI escape sequences

A terminal shows the bytes a program sends. Some byte sequences are not shown but interpreted as commands. They start with the escape character (`\x1b`), for example:

| Sequence | Effect |
| --- | --- |
| `\x1b[97m` | switch to bright white |
| `\x1b[92m` | switch to bright green |
| `\x1b[1m` | switch to bold |
| `\x1b[0m` | reset everything |

`paint` wraps a piece of text in such a pair. If colours are off, it returns the text unchanged, so callers never need to check.

### Switching them on in Windows

macOS terminals understand these sequences by default. The Windows console only does so after it is told to. `init` does this:

1. `GetStdHandle` returns the handle for the program's output.
2. `GetConsoleMode` reads the current settings. It fails if the output is not a console (for example redirected into a file), and then colours stay off.
3. `SetConsoleMode` with `ENABLE_VIRTUAL_TERMINAL_PROCESSING` asks the console to interpret escape sequences. If that fails, the tool falls back to plain text.

`shutdown` restores the original mode. A program should leave the terminal the way it found it.

### When colours stay off

- Output is redirected to a file or pipe.
- The console refuses the mode (very old Windows consoles).
- The environment variable `NO_COLOR` is set. This is a common convention across many tools.

## Encoding

The program uses UTF-8 for all text. `init` calls `SetConsoleOutputCP(CP_UTF8)` so the Windows console decodes output as UTF-8. Without it, a drive label like `Zürich` would be shown with wrong characters.

## Reading input

`readLine` prints a prompt and returns one line.

On Windows it uses `ReadConsoleW`, which delivers UTF-16, and converts the result to UTF-8. The standard `std::getline(std::cin, ...)` would go through the old regional code page and lose characters that are not in it. That matters for the hash checker, where users type or drag in file paths.

If input is not a console (redirected from a file), or on macOS, `std::getline` is used.

`readLine` returns `false` when there is no more input. `runMenu` uses this to leave the loop instead of printing the menu forever.

## Small helpers

- `confirm` returns `true` only for `y` or `yes`. Everything else, including just pressing Enter, means no. Saving is the only action with a side effect, so the default is the safe one.
- `pause` waits for Enter so a result does not scroll away when the menu is printed again.

## Try it

Run the tool with `NO_COLOR` set and compare the output:

```bash
NO_COLOR=1 ./xymyx-toolkit
```
