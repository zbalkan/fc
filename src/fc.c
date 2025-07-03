/*
 * PROJECT:     FileCheck CLI Application
 * LICENSE:     GPL2
 * PURPOSE:     Provides a feature-compatible command-line interface to fc.exe
 *              using the FileCheck.h library.
 * COPYRIGHT:   Copyright 2025 Zafer Balkan
 */

#include "filecheck.h"
#include <stdio.h>
#include <stdlib.h> // For wcstoul
#include <wchar.h>  // For wcsncmp, wprintf
#include <ctype.h>  // For iswdigit, towupper

 //
 // Default callback to print comparison results to the console.
 //
static void
DefaultOutputCallback(
    _In_opt_ void* UserData,
    _In_ const char* Message,
    _In_ int Line1,
    _In_ int Line2)
{
    // UserData is not used in this implementation.
    (void)UserData;

    if (Line1 >= 0 && Line2 >= 0)
    {
        printf("%s (Line %d vs %d)\n", Message, Line1, Line2);
    }
    else
    {
        printf("%s\n", Message);
    }
}

//
// Prints the command-line usage instructions.
//
static void
PrintUsage(void)
{
    printf("Usage: fc.exe [options] file1 file2\n");
    printf("Options:\n");
    printf("  /B    Binary comparison\n");
    printf("  /C    Case-insensitive comparison\n");
    printf("  /W    Ignore whitespace differences\n");
    printf("  /L    ASCII text comparison (default)\n");
    printf("  /N    Show line numbers in text mode\n");
    printf("  /T    Do not expand tabs\n");
    printf("  /U    Unicode text comparison\n");
    printf("  /nnnn Set resync line threshold (default 2)\n");
    printf("  /LBn  Set internal buffer size for text lines (default 100)\n");
}

//
// Main entry point for the application.
// Using wmain to natively support Unicode command-line arguments.
//
int
wmain(
    _In_ int argc,
    _In_reads_(argc) WCHAR* argv[])
{
    if (argc < 3)
    {
        PrintUsage();
        return -1; // Syntax error
    }

    FC_CONFIG Config = { 0 }; // Initialize all fields to zero

    // Set defaults
    Config.Mode = FC_MODE_TEXT;
    Config.Flags = 0;
    Config.ResyncLines = 2;
    Config.BufferLines = 100;
    Config.Output = DefaultOutputCallback;
    Config.UserData = NULL;

    int ArgIndex = 1;
    for (; ArgIndex < argc - 2; ++ArgIndex)
    {
        WCHAR* Option = argv[ArgIndex];
        WCHAR* EndPtr; // For wcstoul error checking

        if (Option[0] == L'/' || Option[0] == L'-')
        {
            // Check for numeric resync line option (e.g., /20)
            if (iswdigit(Option[1]))
            {
                errno = 0; // Clear errno before call
                unsigned long Value = wcstoul(Option + 1, &EndPtr, 10);
                if (*EndPtr != L'\0' || Value == 0 || errno == ERANGE) // Must consume entire string, be non-zero, and not overflow/underflow
                {
                    wprintf(L"Invalid numeric option: %s\n", Option);
                    return -1;
                }
                Config.ResyncLines = (UINT)Value;
            }
            // Check for buffer line option (e.g., /LB100)
            else if (wcsncmp(Option + 1, L"LB", 2) == 0 && iswdigit(Option[3]))
            {
                errno = 0; // Clear errno before call
                unsigned long Value = wcstoul(Option + 3, &EndPtr, 10);
                if (*EndPtr != L'\0' || Value == 0 || errno == ERANGE) // Must consume entire string, be non-zero, and not overflow/underflow
                {
                    wprintf(L"Invalid numeric option: %s\n", Option);
                    return -1;
                }
                Config.BufferLines = (UINT)Value;
            }
            else
            {
                switch (towupper(Option[1]))
                {
                case L'B':
                    Config.Mode = FC_MODE_BINARY;
                    break;
                case L'C':
                    Config.Flags |= FC_IGNORE_CASE;
                    break;
                case L'W':
                    Config.Flags |= FC_IGNORE_WS;
                    break;
                case L'L':
                    Config.Mode = FC_MODE_TEXT;
                    break;
                case L'N':
                    Config.Flags |= FC_SHOW_LINE_NUMS;
                    break;
                case L'T':
                    Config.Flags |= FC_RAW_TABS;
                    break;
                case L'U':
                    Config.Flags |= FC_UNICODE_TEXT;
                    break;
                default:
                    wprintf(L"Invalid option: %s\n", Option);
                    return -1;
                }
            }
        }
        else
        {
            wprintf(L"Invalid argument: %s\n", Option);
            return -1;
        }
    }

    const WCHAR* File1 = argv[argc - 2];
    const WCHAR* File2 = argv[argc - 1];

    // Call the wide-character version of the comparison function for best performance
    FC_RESULT Result = FileCheckCompareFilesW(File1, File2, &Config);

    switch (Result)
    {
    case FC_OK:
        // Files are identical
        return 0;
    case FC_DIFFERENT:
        // Differences were found and printed by the callback
        return 1;
    case FC_ERROR_IO:
    case FC_ERROR_MEMORY:
        fprintf(stderr, "Error during comparison: %d\n", Result);
        return 2;
    default:
        // Invalid parameter or other syntax error
        return -1;
    }
}
