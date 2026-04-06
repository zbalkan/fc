# FileCheck for Windows

**This is a proof of concept tool and not yet a feature-complete replacement for `fc.exe`.**

---

A modern, header-only file comparison library for Windows and a feature-compatible `fc.exe` command-line replacement.

This project provides three main components organized within the `src` directory:

1.  **`filecheck.h`**: A self-contained, header-only C library located in `src/fc/`.
2.  **`fc.c`**: A command-line application that uses the library, located in `src/fc/`.
3.  **`test.c`**: A comprehensive test suite located in `src/test/`.

The entire codebase is written in pure C and has no external dependencies beyond the standard Windows SDK and its libraries.

## Features

*   **Header-Only Library**: Simply include `filecheck.h` in your C/C++ project to get started.
*   **High-Performance**:
    *   Uses memory-mapped I/O for fast binary comparisons.
    *   Uses efficient hashing and buffer management for fast text-based comparisons.
*   **Windows Native**: Built entirely on the Windows API for maximum performance and compatibility. It uses undocumented native functions for robust path handling.
*   **Robust Path Handling**: Full support for long file paths (`\\?\` prefix) and Unicode (UTF-16) filenames.
*   **Flexible API**: The library exposes a clean API that accepts both UTF-8 and native UTF-16 paths.
*   **Correct Unicode Support**: Provides a dedicated mode for proper, Unicode-aware case-insensitive comparisons for international text.
*   **`fc.exe` Compatibility**: The command-line app supports all major `fc.exe` options.
    *   `/A` - Abbreviated output (first and last line of each difference block)
    *   `/B` - Binary comparison
    *   `/C` - Case-insensitive text comparison
    *   `/L` - ASCII text comparison
    *   `/LBn` - Set internal line buffer size (default: 100)
    *   `/N` - Display line numbers in text mode output
    *   `/T` - Do not expand tabs to spaces
    *   `/U` - Unicode-aware text comparison
    *   `/W` - Ignore whitespace differences
    *   `/nnnn` - Set resync line threshold (default: 2)
*   **Text Diff Output**: Displays line-by-line differences in a format compatible with Windows `fc.exe`, showing difference blocks with proper sectioning using asterisk markers.
*   **Wildcard Support**: Both input file arguments may use `*` or `?` wildcards to compare matching pairs (see examples below).

---

## Usage

### Command-Line Tool

The syntax is designed to be compatible with the Windows `fc.exe` command.

**Basic Usage:**
```sh
fc.exe [options] <file1> <file2>
```

**Options:**

| Option  | Description |
|---------|-------------|
| `/A`    | Abbreviated output: show only the first and last line of each difference block |
| `/B`    | Binary comparison |
| `/C`    | Case-insensitive text comparison |
| `/L`    | ASCII text comparison |
| `/LBn`  | Set maximum line-index distance for resync matching (e.g., `/LB200`; default: 100) |
| `/N`    | Show line numbers in text mode |
| `/T`    | Do not expand tabs to spaces |
| `/U`    | Unicode-aware text comparison |
| `/W`    | Ignore whitespace differences |
| `/nnnn` | Set resync line threshold (e.g., `/5`; default: 2) |
| `/?`    | Display help |

> **Design note — default mode differs from Windows `fc.exe`:**
> The standard `fc.exe` defaults to text mode (`/L`) when no mode flag is given.
> This tool instead **auto-detects** whether each file is binary or text by inspecting
> the first 4 KB of its content. The heuristic is applied in order: if a file begins
> with a recognised UTF BOM, it is treated as text; otherwise, if it contains a null
> byte (`0x00`), it is treated as binary; otherwise, it is treated as text only if at
> least 90 % of its bytes are printable ASCII characters (including TAB, CR, LF).
> If either file is classified as binary, binary comparison is used for the pair.
> This is an intentional design decision to improve safety and correctness when
> comparing files without an explicit mode flag. Use `/L`, `/U`, or `/B` to override
> the automatic selection.

**Examples:**
```sh
# Perform a standard text comparison
fc.exe original.txt modified.txt

# Perform a binary comparison
fc.exe /B program_v1.dll program_v2.dll

# Perform a case-insensitive comparison on a UTF-8 file with non-ASCII characters
fc.exe /C /U german_doc1.txt german_doc2.txt

# Display line numbers in text comparison
fc.exe /N file1.txt file2.txt

# Abbreviated output, showing only the first and last line of each diff block
fc.exe /A file1.txt file2.txt

# Wildcard comparison (compares matching file pairs across directories)
fc.exe /B dir1\*.dll dir2\*.dll

# Error handling for files not found
fc.exe *.xyz *.abc   # Will report “FC: no files found for ...” if no matches
```

### Exit Codes

| Code | Meaning |
|------|---------|
| `0`  | No differences found |
| `1`  | Differences found |
| `2`  | I/O or memory error during comparison |
| `-1` | Invalid arguments or usage error |

### Example Text Diff Output

When comparing two text files with differences, the output will show:
```
Comparing files file1.txt and file2.txt

***** file1.txt
Line from file 1
Another line from file 1
***** file2.txt
Modified line in file 2
Different content in file 2
*****
```
The `/N` flag adds line numbers to each line of output. The `/A` flag abbreviates long diff blocks to show only the first and last line with `...` for omitted content.

### Using the `filecheck.h` Library

To use the library in your own project, copy `src/fc/filecheck.h` into your source tree and include it.

**Important:** Because the library uses native API functions, any project that includes `filecheck.h` must be linked with `ntdll.lib`.

#### Library API

The library provides two primary functions for maximum flexibility.

##### `FC_CompareFilesW` (Recommended)
This is the most efficient function, as it uses native Windows UTF-16 strings directly.

```c
FC_RESULT FC_CompareFilesW(
    _In_z_ const WCHAR* Path1,
    _In_z_ const WCHAR* Path2,
    _In_ const FC_CONFIG* Config);
```

##### `FC_CompareFilesUtf8`
A convenience wrapper for applications that work with UTF-8 strings.

```c
FC_RESULT FC_CompareFilesUtf8(
    _In_z_ const char* Path1Utf8,
    _In_z_ const char* Path2Utf8,
    _In_ const FC_CONFIG* Config);
```

#### Example

Here is a simple example of how to use the library in your own C code.

```c
#include "filecheck.h"
#include <stdio.h>

int main(void)
{
    // 1. Configure the comparison
    FC_CONFIG config = {0};
    config.Mode = FC_MODE_AUTO; // Let the library detect if files are text or binary
    config.Flags = FC_IGNORE_CASE | FC_IGNORE_WS; // Ignore case and whitespace for text files

    // 2. Define file paths (using wide strings for the native API)
    const WCHAR* file1 = L"C:\\docs\\report_v1.txt";
    const WCHAR* file2 = L"C:\\docs\\report_v2.txt";

    // 3. Perform the comparison
    FC_RESULT result = FC_CompareFilesW(file1, file2, &config);

    // 4. Check the result
    switch (result)
    {
        case FC_OK:      wprintf(L"Files are identical.\n");  break;
        case FC_DIFFERENT: wprintf(L"Files are different.\n"); break;
        default:         wprintf(L"Error: %d\n", result);     break;
    }

    return 0;
}
```

---

## Development

### Prerequisites

*   Microsoft Visual Studio with the "Desktop development with C++" workload installed.
*   The Windows SDK (usually included with Visual Studio).

### Project Structure

```
src/
├── fc.sln                  # Visual Studio solution
├── fc/
│   ├── filecheck.h         # Header-only library
│   ├── fc.c                # Command-line application
│   └── fc.vcxproj
└── test/
    ├── test.c              # Test suite
    └── fc.test.vcxproj
```

### Building

The repository includes a Visual Studio solution at `src/fc.sln` which contains projects for both the `fc` command-line tool and the `test` suite.

1.  Open `src/fc.sln` in Visual Studio.
2.  Select a configuration (e.g., `Release` or `Debug`) and platform (e.g., `x64`).
3.  Build the solution by selecting **Build > Build Solution** (or press `Ctrl+Shift+B`).

This will produce two executables in the build output directory (e.g., `src/x64/Release/`):
*   `fc.exe` — the command-line tool.
*   `fc.test.exe` — the test suite.

**Note:** The projects are configured to link against `ntdll.lib` for native Windows API functions used in path canonicalization.

### Running the Tests

After building the solution, run the test suite to validate the library's functionality:

```sh
fc.test.exe
```

The test executable creates temporary files, reports the status of each test case, and finishes with a summary of passed and failed tests.

CI runs automatically on every push and pull request using GitHub Actions (MSVC / x64, Release configuration). See `.github/workflows/test.yml` for the workflow definition.

### Contributing

*   All code is written in pure C using Windows-native APIs (`HeapAlloc`/`HeapFree`, `WriteConsoleW`, `StringCchLengthW`, etc.) — avoid standard C library equivalents where a Windows API alternative exists.
*   SAL 2.0 annotations (e.g., `_In_z_`, `_Outptr_opt_result_maybenull_`) are used throughout for static analysis.
*   The library is header-only — all logic lives in `filecheck.h`.

---

## Future Enhancements

- Additional resynchronization indicators showing matching lines after difference blocks.

## Documented Differences from Windows `fc.exe`

To keep behavior explicit for maintainers and users, this project records intentional (or currently accepted) differences from Microsoft `fc.exe`:

- **Wildcard matching**: Both file arguments support wildcards and match by file "stem"; error/warning reporting aligns to Windows behavior, but may differ for partial matches or ordering.
  - If both wildcard sets expand successfully but no stem pairs match at all, CLI now reports: `FC: no matching stem pairs found for <pattern1> and <pattern2>` and exits non-zero.
- **Output redirection**: Output is fully compatible with piping/redirection to files or CI environments, falling back to UTF-8 output if no console is present.
- **`/OFF` and `/OFFLINE`**: Accepted as compatibility switches but currently act as no-ops in the CLI implementation.
- **`/LBn`**: Implemented as a bounded resynchronization window heuristic in the LCS matcher, not as a strict legacy internal text-buffer emulation.
- **`FC_MODE_AUTO`**: Uses an ordered, content-based heuristic, not extension-based defaults: BOM recognized as text, null bytes as binary, ≥90% printable as text. This is a modernized/safer behavior compared to Windows.
- **Alternate Data Streams**: Files or paths containing `:` (ADS) are explicitly rejected.
- **Tab Handling**: Expands tab characters to true 8-column stops (not fixed 4-spaces), matching ReactOS and Windows results.
- **Whitespace Ignore**: `/W` compresses internal runs of whitespace, trims ends, and matches file content accordingly (not simply strips all whitespace).
- **Case Ignoring and Unicode**: Unicode-aware, locale-neutral case folding for non-ASCII text when `/U` and `/C` are used together.
- **Error output**: Exit codes and error reporting fit POSIX conventions, not just Windows.

When these behaviors change, please update this section and the corresponding inline code notes in `src/fc/fc.c` and `src/fc/filecheck.h`.

## License

This project is licensed under the **GPL-2.0-only**. Please see the `LICENSE` file for details.

## References

- `fc.exe` implementation of ReactOS.
