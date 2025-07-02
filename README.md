# FileCheck for Windows

**A modern, header-only file comparison library for Windows and a feature-compatible `fc.exe` command-line replacement.**

This project provides two main components:

1.  **`filecheck.h`**: A self-contained, header-only C library for performing high-performance binary and text-based file comparisons using the native Windows API.
2.  **`fc.c`**: A command-line application that uses the library to provide a powerful, feature-compatible alternative to the standard Windows `fc.exe` utility.

The entire codebase is written in pure C, adheres to the ReactOS/Windows coding style, and has no external dependencies beyond the standard Windows SDK.

## Features

*   **Header-Only Library**: Simply include `filecheck.h` in your C/C++ project to get started.
*   **High-Performance**:
    *   Uses memory-mapped I/O for fast binary comparisons.
    *   Uses efficient hashing for fast text-based comparisons.
*   **Windows Native**: Built entirely on the Windows API for maximum performance and compatibility. No C-runtime dependency required for the library itself.
*   **Robust Path Handling**: Full support for long file paths (`\\?\` prefix) and Unicode (UTF-16) filenames.
*   **Flexible API**: The library exposes a clean API that accepts both UTF-8 and native UTF-16 paths.
*   **Correct Unicode Support**: Provides a dedicated mode for proper, Unicode-aware case-insensitive comparisons for international text.
*   **`fc.exe` Compatibility**: The command-line app supports all major `fc.exe` options, including:
    *   `/B` - Binary comparison
    *   `/C` - Case-insensitive text comparison
    *   `/L` - Line-by-line text comparison (default)
    *   `/W` - Ignore whitespace differences
    *   `/N` - Display line numbers
    *   `/U` - Unicode-aware text comparison

## Getting Started

### Prerequisites

*   A C/C++ compiler for Windows (e.g., MSVC from Visual Studio, Clang, or MinGW-w64).
*   The Windows SDK.

### Building the `fc.exe` Replacement

You can compile `fc.c` from any standard developer command prompt.

**Using MSVC (from a Developer Command Prompt for VS):**

```sh
cl fc.c /O2 /W4 /Fe:fc.exe
```
*   `/O2`: Optimize for speed.
*   `/W4`: Enable high-level warnings.
*   `/Fe:fc.exe`: Name the output executable `fc.exe`.

**Using Clang:**

```sh
clang fc.c -O2 -Wall -o fc.exe -luser32
```

### Using the Command-Line Tool

The syntax is identical to the Windows `fc.exe` command.

**Basic Usage:**
```sh
fc.exe [options] <file1> <file2>
```

**Examples:**
```sh
# Perform a standard text comparison
fc.exe original.txt modified.txt

# Perform a binary comparison
fc.exe /B program_v1.dll program_v2.dll

# Perform a case-insensitive comparison on a UTF-8 file with non-ASCII characters
fc.exe /C /U german_doc1.txt german_doc2.txt
```

## Using the `filecheck.h` Library

To use the library in your own project, simply copy `filecheck.h` into your source tree and include it.

### Library API

The library provides two primary functions for maximum flexibility.

#### `FileCheckCompareFilesW` (Recommended)
This is the most efficient function, as it uses native Windows UTF-16 strings directly.

```c
FC_RESULT FileCheckCompareFilesW(
    _In_z_ const WCHAR* Path1,
    _In_z_ const WCHAR* Path2,
    _In_ const FC_CONFIG* Config);
```

#### `FileCheckCompareFilesUtf8`
A convenience wrapper for applications that work with UTF-8 strings.

```c
FC_RESULT FileCheckCompareFilesUtf8(
    _In_z_ const char* Path1Utf8,
    _In_z_ const char* Path2Utf8,
    _In_ const FC_CONFIG* Config);
```

### Example: Library Usage

Here is a simple example of how to use the library in your own C code.

```c
#include "filecheck.h"
#include <stdio.h>

// A simple callback to print messages to the console
void MyOutputCallback(void* UserData, const char* Message, int Line1, int Line2)
{
    (void)UserData; // Unused
    if (Line1 > 0) {
        printf("Difference on line %d: %s\n", Line1, Message);
    } else {
        printf("Info: %s\n", Message);
    }
}

int main(void)
{
    // 1. Configure the comparison
    FC_CONFIG config = {0};
    config.Mode = FC_MODE_TEXT;
    config.Flags = FC_IGNORE_CASE | FC_IGNORE_WS; // Ignore case and whitespace
    config.Output = MyOutputCallback;

    // 2. Define file paths (using wide strings for the native API)
    const WCHAR* file1 = L"C:\\docs\\report_v1.txt";
    const WCHAR* file2 = L"C:\\docs\\report_v2.txt";

    // 3. Perform the comparison
    FC_RESULT result = FileCheckCompareFilesW(file1, file2, &config);

    // 4. Check the result
    switch (result)
    {
        case FC_OK:
            printf("Files are identical.\n");
            break;
        case FC_DIFFERENT:
            printf("Files are different.\n");
            break;
        default:
            printf("An error occurred during comparison: %d\n", result);
            break;
    }

    return 0;
}
```

## License

This project is licensed under the **GPLv2**. Please see the `LICENSE` file for details.

## References

- `fc.exe` implementation of ReactOS.
