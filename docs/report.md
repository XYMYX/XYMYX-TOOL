# report

Files: `src/core/report.h`, `src/core/report.cpp`

A `Report` collects the result of one feature. It solves one problem: what the user sees on screen and what ends up in the saved file must be the same text.

## Print and remember

Each method of `Report` does two things at once:

| Method | On screen | In the saved text |
| --- | --- | --- |
| `heading` | bold line | line with a dashed underline |
| `row` | grey label, value | `label: value` |
| `line` | as given | as given |
| `note` | yellow | as given |
| `status` | coloured `[OK]`, `[WARNING]` or `[NOT AVAILABLE]` | the same tag without colour |

The screen version contains colour codes, the remembered version does not. If the escape sequences were saved to the file, a text editor would show them as strange characters.

Because features only talk to `Report`, none of them has separate "print" and "export" code that could drift apart.

## Status lines

`status` is used by the security check and the hash comparison. The three values come from `platform::Status`:

- `Ok`: read, and fine.
- `Warning`: read, and worth a look.
- `NotAvailable`: could not be read.

`row` also checks whether a value starts with `not available` and paints it yellow, so missing data stands out.

## Saving

`saveText` is the only function in the program that creates a file. If you want to verify that the tool is read-only on the host, this is the function to read.

What it does:

1. Builds the folder path as `util::exeDir() / "reports"`. The folder of the executable is used, not the current working folder, so reports always land on the stick. See [util.md](util.md).
2. Creates the folder if needed.
3. Builds the file name from a prefix and a timestamp, for example `system-info_2026-01-31_14-05-09.txt`.
4. If that name already exists, appends `_2`, `_3`, and so on. An existing report is never overwritten.
5. Writes the file and checks that writing worked.

### Error handling without exceptions

The `std::filesystem` functions used here take a `std::error_code` argument. In that form they report a failure through the argument instead of throwing. A write-protected or full stick is an ordinary situation for a portable tool, so it is handled as an ordinary result: `saveText` returns `false` with a message, and the user sees "Not saved: ...".

### File names

`slug` turns a title into a safe file name: lower case, letters and digits kept, everything else becomes a dash. The timestamp uses dashes instead of colons because Windows does not allow `:` in file names.

## Privacy note

Reports contain IP addresses, MAC addresses, open ports and drive details. The project's `.gitignore` excludes `reports/` so they cannot be committed by accident.
