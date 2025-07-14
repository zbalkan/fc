/*
 * PROJECT:     filecheck CLI Application
 * LICENSE:     GPL2
 * PURPOSE:     Provides a feature-compatible command-line interface to fc.exe
 *              using the filecheck.h library.
 * COPYRIGHT:   Copyright 2025 Zafer Balkan
 */

#include "filecheck.h"
#include <stdio.h>
#include <stdlib.h> // For wcstoul
#include <wchar.h>  // For wcsncmp, wprintf
#include <ctype.h>  // For iswdigit, towupper

 /**
	  * @brief Callback for handling text-based differences.
	  * @note This is currently a placeholder and does not produce rich output.
	  */
static void
TextDiffCallback(
	_In_ const FC_USER_CONTEXT* Context,
	_In_ const FC_DIFF_BLOCK* Block)
{
	// This is a placeholder for rich text diff output, which is a future task.
	printf("Difference block of type %d found.\n", Block->Type);
}

/**
 * @brief Callback for handling binary differences.
 *
 * This function formats and prints binary differences to the console in a
 * style compatible with the standard fc.exe utility. It handles both
 * byte-level mismatches and file size differences.
 *
 * @param Context The user context, providing file paths.
 * @param Block The difference block. For byte mismatches, the fields are
 * repurposed: StartA holds the offset, EndA holds the byte from file 1,
 * and EndB holds the byte from file 2. For size mismatches, StartA and
 * StartB hold the respective file sizes.
 */
static void
BinaryDiffCallback(
	_In_ const FC_USER_CONTEXT* Context,
	_In_ const FC_DIFF_BLOCK* Block)
{
	if (Block->Type == FC_DIFF_TYPE_SIZE)
	{
		if (Block->StartA > Block->StartB)
			wprintf(L"FC: %s longer than %s\n", Context->Path1, Context->Path2);
		else
			wprintf(L"FC: %s shorter than %s\n", Context->Path1, Context->Path2);
	}
	else if (Block->Type == FC_DIFF_TYPE_CHANGE)
	{
		printf("%08zX: %02X %02X\n", Block->StartA, (unsigned char)Block->EndA, (unsigned char)Block->EndB);
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
	printf("(If neither L, B or U is specified, auto-detect is used)\n");
}

_Success_(return == TRUE)
static BOOL
ParseNumericOption(
	_In_z_ const WCHAR * OptionString,
	_Out_ UINT * Value,
	_In_ UINT MinValue,
	_In_ UINT MaxValue)
{
	*Value = 0;
	WCHAR* EndPtr;
	errno = 0;
	unsigned long ParsedValue = wcstoul(OptionString, &EndPtr, 10);

	if (*EndPtr != L'\0' || ParsedValue < MinValue || ParsedValue > MaxValue || errno == ERANGE)
	{
		wprintf(L"Invalid numeric option: %s\n", OptionString);
		return FALSE;
	}

	*Value = (UINT)ParsedValue;
	return TRUE;
}

typedef struct {
	WCHAR OptionChar;    // Uppercase single-char option (e.g., 'B', 'C', etc.)
	UINT FlagToSet;      // Config.Flags to OR
	FC_MODE ModeToSet;   // If not 0, sets Config.Mode
} OPTION_MAP;

static const OPTION_MAP g_OptionMap[] = {
	{ L'B', 0, FC_MODE_BINARY },
	{ L'C', FC_IGNORE_CASE, 0 },
	{ L'W', FC_IGNORE_WS, 0 },
	{ L'L', 0, FC_MODE_TEXT_ASCII },
	{ L'N', FC_SHOW_LINE_NUMS, 0 },
	{ L'T', FC_RAW_TABS, 0 },
	{ L'U', 0, FC_MODE_TEXT_UNICODE },
	{ 0, 0, 0 } // Sentinel
};

//
// Main entry point for the application.
// Using wmain to natively support Unicode command-line arguments.
//
int
wmain(
	_In_ int argc,
	_In_reads_(argc) WCHAR * argv[])
{
	if (argc < 3)
	{
		PrintUsage();
		return -1; // Syntax error
	}

	FC_CONFIG Config = { 0 }; // Initialize all fields to zero

	// Set defaults
	Config.Mode = FC_MODE_AUTO;
	Config.Flags = 0;
	Config.ResyncLines = 2;
	Config.BufferLines = 100;
	Config.UserData = NULL;

	int ArgIndex = 1;
	for (; ArgIndex < argc - 2; ++ArgIndex)
	{
		WCHAR* Option = argv[ArgIndex];

		if (Option[0] == L'/' || Option[0] == L'-')
		{
			// Check for numeric resync line option (e.g., /20)
			if (iswdigit(Option[1]))
			{
				if (!ParseNumericOption(Option + 1, &Config.ResyncLines, 1, UINT_MAX))
					return -1;
			}
			// Check for buffer line option (e.g., /LB100)
			else if (wcsncmp(Option + 1, L"LB", 2) == 0 && iswdigit(Option[3]))
			{
				if (!ParseNumericOption(Option + 3, &Config.BufferLines, 1, UINT_MAX))
					return -1;
			}
			else
			{
				WCHAR OptionChar = towupper(Option[1]);
				BOOL Handled = FALSE;

				for (int i = 0; g_OptionMap[i].OptionChar != 0; ++i)
				{
					if (OptionChar == g_OptionMap[i].OptionChar)
					{
						if (g_OptionMap[i].FlagToSet)
							Config.Flags |= g_OptionMap[i].FlagToSet;
						if (g_OptionMap[i].ModeToSet)
							Config.Mode = g_OptionMap[i].ModeToSet;
						Handled = TRUE;
						break;
					}
				}

				if (!Handled)
				{
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

	if (Config.Mode == FC_MODE_BINARY)
		Config.DiffCallback = BinaryDiffCallback;
	else
		Config.DiffCallback = TextDiffCallback;

	const WCHAR* File1 = argv[argc - 2];
	const WCHAR* File2 = argv[argc - 1];

	wprintf(L"Comparing files %s and %s\n", File1, File2);

	FC_RESULT Result = FC_CompareFilesW(File1, File2, &Config);

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