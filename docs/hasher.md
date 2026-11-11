# hasher

Files: `src/features/hasher.cpp`, uses `src/core/sha256.cpp`

Menu item 3. Calculates the SHA-256 of one file or of every file in a folder.

This feature has no platform code. `std::filesystem` handles files and folders on both systems, and the hashing itself is plain arithmetic. For how the hash is calculated, see [sha256.md](sha256.md).

## What it is for

- **Check a download.** Paste the hash from the download page; the tool says `match` or `MISMATCH`.
- **Fingerprint a folder.** Export a list of all hashes. Run it again later and compare the two lists to see which files changed.

## Flow

`runHashChecker` asks for a path and then calls one of two functions:

| Path is | Function |
| --- | --- |
| a regular file | `hashSingleFile` |
| a folder | `hashFolder` |
| missing or something else | prints `not available (...)` |

## Cleaning the path: `cleanPathInput`

Dragging a file into the terminal window pastes its path, but with decorations:

- Windows wraps a path with spaces in double quotes.
- The macOS Terminal puts a backslash before spaces and other special characters.

`cleanPathInput` removes both, so a dragged path just works.

The cleaned text is turned into a path with `util::pathFromUtf8`, which keeps umlauts and other non-ASCII characters intact on Windows. See [util.md](util.md).

## Hashing a folder: `hashFolder`

### Step 1: collect

A `std::filesystem::recursive_directory_iterator` visits every entry below the folder. Three decisions are worth explaining.

**No exceptions.** Every filesystem call uses the overload that takes a `std::error_code`. The throwing versions would abort the entire scan at the first folder you are not allowed to open. With error codes, the scan continues and the problem is counted.

**`skip_permission_denied`.** This option tells the iterator to step over folders it cannot enter.

**Symbolic links are not followed.** A link can point anywhere: outside the folder you chose, or back to a parent folder, which would loop forever. Links are counted under "Skipped entries" instead.

### Step 2: sort

The order in which a file system lists a folder is not defined. Sorting makes two runs produce identical lists.

### Step 3: hash

Each file goes through `sha256File`. If it returns an empty optional, the file is added to an "unreadable" list and shown by name. It gets no hash line.

Paths are printed relative to the chosen folder, with forward slashes on both systems, so a list made on Windows can be compared with one made on macOS.

### Step 4: summary

The summary shows files hashed, total size, unreadable files and skipped entries. If the numbers do not add up to what you expected, the reason is visible.

## Comparing: `askExpectedHash` and `normalizeHash`

Hashes on websites come in upper or lower case, sometimes split into groups by spaces. `normalizeHash` removes whitespace and lower-cases the input. `isSha256Hex` then checks that exactly 64 hexadecimal characters remain. Anything else is rejected with a message rather than compared and reported as a mismatch.

For a folder, the pasted hash is compared against every file, and all matches are listed.

## Exporting the list

The export uses the format of the `sha256sum` tool: hash, two spaces, path.

```
ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  abc.txt
```

On Linux or WSL you can verify such a list later from inside the hashed folder:

```bash
sha256sum -c /path/to/hash-list.txt
```

The file is written by `saveText` into the `reports` folder. See [report.md](report.md).

## Limits

- Files you may not read are reported as unreadable.
- A file that is being written while it is hashed may give a hash that matches neither the old nor the new content.
- A matching hash proves the file equals the one the hash was made from. It says nothing about whether that original is trustworthy.
