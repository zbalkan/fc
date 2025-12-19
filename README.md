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
    *   `/B` - Binary comparison
    *   `/C` - Case-insensitive text comparison
    *   `/L` - ASCII text comparison
    *   `/W` - Ignore whitespace differences
    *   `/N` - Display line numbers in text mode output
    *   `/U` - Unicode-aware text comparison
    *   `/T` - Do not expand tabs to spaces
*   **Text Diff Output**: The tool now displays line-by-line differences in a format compatible with Windows `fc.exe`, showing difference blocks with proper sectioning using asterisk markers.

## Getting Started

### Prerequisites

*   Microsoft Visual Studio with the "Desktop development with C++" workload installed.
*   The Windows SDK (usually included with Visual Studio).

### Building the Projects

The repository includes a Visual Studio solution at `src/fc.sln` which contains projects for both the `fc` command-line tool and the `test` suite.

1.  Open `src/fc.sln` in Visual Studio.
2.  Select a configuration (e.g., `Release` or `Debug`) and platform (e.g., `x64`).
3.  Build the solution by selecting **Build > Build Solution** from the menu (or by pressing `Ctrl+Shift+B`).

This will produce two executables:
*   `fc.exe` in the build output directory for the `fc` project.
*   `test.exe` in the build output directory for the `test` project.

**Note:** The projects are configured to link against `ntdll.lib` to use certain native Windows API functions for path canonicalization.

### Running the Tests

After building the solution, you can run the test suite to validate the library's functionality.

1.  Open a terminal or command prompt.
2.  Navigate to the build output directory (e.g., `src/x64/Release/`).
3.  Run the test executable:
    ```sh
    test.exe
    ```
The test executable will create temporary files, report the status of each test case, and finish with a summary of passed and failed tests.

### Using the Command-Line Tool

The syntax is designed to be compatible with the Windows `fc.exe` command.

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

# Display line numbers in text comparison
fc.exe /N file1.txt file2.txt
```

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

The `/N` flag will add line numbers to each line of output.

## Using the `filecheck.h` Library

To use the library in your own project, simply copy `src/fc/filecheck.h` into your source tree and include it.

**Important:** Because the library uses native API functions, any project that includes `filecheck.h` must be linked with `ntdll.lib`. In Visual Studio, you can add this in **Project Properties > Linker > Input > Additional Dependencies**.

### Library API

The library provides two primary functions for maximum flexibility.

#### `FC_CompareFilesW` (Recommended)
This is the most efficient function, as it uses native Windows UTF-16 strings directly.

```c
FC_RESULT FC_CompareFilesW(
    _In_z_ const WCHAR* Path1,
    _In_z_ const WCHAR* Path2,
    _In_ const FC_CONFIG* Config);
```

#### `FC_CompareFilesUtf8`
A convenience wrapper for applications that work with UTF-8 strings.

```c
FC_RESULT FC_CompareFilesUtf8(
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
    if (Line1 >= 0) {
        printf("Difference on line %d: %s\n", Line1, Message);
    } else {
        printf("Info: %s\n", Message);
    }
}

int main(void)
{
    // 1. Configure the comparison
    FC_CONFIG config = {0};
    config.Mode = FC_MODE_AUTO; // Let the library detect if files are text or binary
    config.Flags = FC_IGNORE_CASE | FC_IGNORE_WS; // Ignore case and whitespace for text files
    config.Output = MyOutputCallback;

    // 2. Define file paths (using wide strings for the native API)
    const WCHAR* file1 = L"C:\\docs\\report_v1.txt";
    const WCHAR* file2 = L"C:\\docs\\report_v2.txt";

    // 3. Perform the comparison
    FC_RESULT result = FC_CompareFilesW(file1, file2, &config);

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

## Future Enhancements

The following features are planned for future releases:
- **`/A` flag**: Abbreviated output showing only the first and last line of each difference block with resynchronization context
- Additional resynchronization indicators showing matching lines after difference blocks

## License

This project is licensed under the **GPL-2.0-only**. Please see the `LICENSE` file for details.

## References

- `fc.exe` implementation of ReactOS.