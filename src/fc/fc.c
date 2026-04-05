/*
 * PROJECT:     filecheck CLI Application
 * LICENSE:     GPL2
 * PURPOSE:     Provides a feature-compatible command-line interface to fc.exe
 *              using the filecheck.h library.
 * COPYRIGHT:   Copyright 2025 Zafer Balkan
 */

#include "filecheck.h"
#include <stdlib.h>  // For wcstoul
#include <wchar.h>   // For wcsncmp, wcslen, swprintf_s
#include <ctype.h>   // For iswdigit, towupper
#include <strsafe.h> // For StringCchLengthW

/**
 * @brief Writes a wide-character string to a console handle.
 * @internal
 * @param hConsole The console handle (e.g., STD_OUTPUT_HANDLE or STD_ERROR_HANDLE).
 * @param msg      The null-terminated wide string to write.
 */
static void
ConPrintW(_In_ HANDLE hConsole, _In_z_ const WCHAR* msg)
{
	WriteConsoleW(hConsole, msg, (DWORD)wcslen(msg), NULL, NULL);
}

 /**
 * @struct TEXT_DIFF_USER_DATA
 * @brief User data structure for text diff callback to access configuration flags.
 */
typedef struct {
	UINT Flags;              /**< Configuration flags (e.g., FC_SHOW_LINE_NUMS). */
} TEXT_DIFF_USER_DATA;

/**
 * @brief Helper function to get a line from a buffer safely.
 * @internal
 * @param Buffer The line buffer.
 * @param Index The line index.
 * @return Pointer to the _FC_LINE struct, or NULL if index is out of bounds.
 */
static inline const _FC_LINE*
GetLine(_In_ const _FC_BUFFER* Buffer, _In_ size_t Index)
{
	if (Index >= Buffer->Count)
		return NULL;
	return (const _FC_LINE*)((const char*)Buffer->pData + Index * Buffer->ElementSize);
}

/**
 * @brief Prints a range of lines from a buffer to the console.
 * @internal
 */
static void
PrintLines(
	_In_ const _FC_BUFFER* Lines,
	_In_ size_t Start,
	_In_ size_t End,
	_In_ BOOL ShowLineNumbers)
{
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	for (size_t i = Start; i < End; ++i)
	{
		const _FC_LINE* line = GetLine(Lines, i);
		if (line != NULL && line->Text != NULL)
		{
			if (ShowLineNumbers)
			{
				WCHAR numBuf[16];
				swprintf_s(numBuf, 16, L"%5zu:  ", i + 1);
				ConPrintW(hOut, numBuf);
			}
			int wLen = MultiByteToWideChar(CP_UTF8, 0, line->Text, -1, NULL, 0);
			if (wLen > 0)
			{
				WCHAR* wText = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, (size_t)wLen * sizeof(WCHAR));
				if (wText != NULL)
				{
					MultiByteToWideChar(CP_UTF8, 0, line->Text, -1, wText, wLen);
					WriteConsoleW(hOut, wText, (DWORD)(wLen - 1), NULL, NULL);
					HeapFree(GetProcessHeap(), 0, wText);
				}
			}
			WriteConsoleW(hOut, L"\n", 1, NULL, NULL);
		}
	}
}

/**
 * @brief Callback for handling text-based differences in fc.exe compatible format.
 *
 * This function produces rich text diff output that matches the Windows fc.exe
 * format. It displays difference blocks showing lines from both files, with
 * proper block sectioning using asterisk markers.
 *
 * The output format is:
 * ***** filename1
 * <lines from file1 in the difference block>
 * ***** filename2
 * <lines from file2 in the difference block>
 * *****
 *
 * @param Context The user context, providing file paths and line buffers.
 * @param Block The difference block describing the change, deletion, or addition.
 */
static void
TextDiffCallback(
	_In_ const FC_USER_CONTEXT* Context,
	_In_ const FC_DIFF_BLOCK* Block)
{
	// Defensive: Validate input parameters
	if (Context == NULL || Block == NULL)
		return;

	const _FC_BUFFER* Lines1 = Context->Lines1;
	const _FC_BUFFER* Lines2 = Context->Lines2;

	// Defensive: Validate line buffers
	if (Lines1 == NULL || Lines2 == NULL)
		return;

	BOOL ShowLineNumbers = FALSE;
	if (Context->UserData != NULL)
		ShowLineNumbers = (((TEXT_DIFF_USER_DATA*)Context->UserData)->Flags & FC_SHOW_LINE_NUMS) != 0;

	if (Block->Type == FC_DIFF_TYPE_CHANGE ||
		Block->Type == FC_DIFF_TYPE_DELETE ||
		Block->Type == FC_DIFF_TYPE_ADD)
	{
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

		// Print first file block
		WriteConsoleW(hOut, L"***** ", 6, NULL, NULL);
		ConPrintW(hOut, Context->Path1);
		WriteConsoleW(hOut, L"\n", 1, NULL, NULL);

		if (Block->Type == FC_DIFF_TYPE_CHANGE || Block->Type == FC_DIFF_TYPE_DELETE)
			PrintLines(Lines1, Block->StartA, Block->EndA, ShowLineNumbers);

		// Print second file block
		WriteConsoleW(hOut, L"***** ", 6, NULL, NULL);
		ConPrintW(hOut, Context->Path2);
		WriteConsoleW(hOut, L"\n", 1, NULL, NULL);

		if (Block->Type == FC_DIFF_TYPE_CHANGE || Block->Type == FC_DIFF_TYPE_ADD)
			PrintLines(Lines2, Block->StartB, Block->EndB, ShowLineNumbers);

		// Print closing marker
		WriteConsoleW(hOut, L"*****\n", 6, NULL, NULL);
	}
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
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	WCHAR buf[128];

	if (Block->Type == FC_DIFF_TYPE_SIZE)
	{
		// Always report as "[longer_file] longer than [shorter_file]", matching Windows fc.exe.
		const WCHAR* LongerPath  = (Block->StartA > Block->StartB) ? Context->Path1 : Context->Path2;
		const WCHAR* ShorterPath = (Block->StartA > Block->StartB) ? Context->Path2 : Context->Path1;
		WriteConsoleW(hOut, L"FC: ", 4, NULL, NULL);
		ConPrintW(hOut, LongerPath);
		WriteConsoleW(hOut, L" longer than ", 13, NULL, NULL);
		ConPrintW(hOut, ShorterPath);
		WriteConsoleW(hOut, L"\n", 1, NULL, NULL);
	}
	else if (Block->Type == FC_DIFF_TYPE_CHANGE)
	{
		swprintf_s(buf, 128, L"%08zX: %02X %02X\n",
			Block->StartA, (unsigned int)(unsigned char)Block->EndA, (unsigned int)(unsigned char)Block->EndB);
		ConPrintW(hOut, buf);
	}
}

//
// Prints the command-line usage instructions.
//
static void
PrintUsage(void)
{
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	ConPrintW(hOut, L"Usage: fc.exe [options] file1 file2\n");
	ConPrintW(hOut, L"Options:\n");
	ConPrintW(hOut, L"  /B    Binary comparison\n");
	ConPrintW(hOut, L"  /C    Case-insensitive comparison\n");
	ConPrintW(hOut, L"  /W    Ignore whitespace differences\n");
	ConPrintW(hOut, L"  /L    ASCII text comparison (default)\n");
	ConPrintW(hOut, L"  /N    Show line numbers in text mode\n");
	ConPrintW(hOut, L"  /T    Do not expand tabs\n");
	ConPrintW(hOut, L"  /U    Unicode text comparison\n");
	ConPrintW(hOut, L"  /nnnn Set resync line threshold (default 2)\n");
	ConPrintW(hOut, L"  /LBn  Set internal buffer size for text lines (default 100)\n");
	ConPrintW(hOut, L"(If neither L, B or U is specified, auto-detect is used)\n");
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
		HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
		WCHAR buf[256];
		swprintf_s(buf, 256, L"Invalid numeric option: %s\n", OptionString);
		ConPrintW(hErr, buf);
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

/**
 * @brief Checks whether a path contains a wildcard character ('*' or '?').
 * @param Path The path to check.
 * @return TRUE if the path contains a wildcard, FALSE otherwise.
 */
static BOOL
ContainsWildcard(_In_z_ const WCHAR* Path)
{
	if (Path == NULL)
		return FALSE;
	for (size_t i = 0; Path[i] != L'\0'; i++)
	{
		if (Path[i] == L'*' || Path[i] == L'?')
			return TRUE;
	}
	return FALSE;
}

/**
 * @struct WILDCARD_EXPANSION
 * @brief Holds an array of file paths that matched a wildcard pattern.
 */
typedef struct {
	WCHAR** Paths;     /**< Array of heap-allocated file path strings. */
	size_t  Count;     /**< Number of valid entries in Paths. */
	size_t  Capacity;  /**< Allocated capacity of Paths. */
} WILDCARD_EXPANSION;

/**
 * @brief Builds a full path by combining a directory prefix with a file name.
 *
 * Given a pattern such as "dir\\*.c", extracts the directory component
 * "dir\\" and appends the supplied file name to produce "dir\\file.c".
 * If the pattern contains no directory separator, the file name is used as-is.
 *
 * @param Pattern  The original wildcard pattern.
 * @param FileName The file name returned by FindFirstFileW / FindNextFileW.
 * @param OutBuf   Buffer that receives the combined path (MAX_PATH wide chars).
 */
static void
BuildFullPath(
	_In_z_  const WCHAR* Pattern,
	_In_z_  const WCHAR* FileName,
	_Out_writes_z_(MAX_PATH) WCHAR* OutBuf)
{
	// Find the last path separator in the pattern.
	const WCHAR* LastSep = NULL;
	for (const WCHAR* p = Pattern; *p; p++)
	{
		if (*p == L'\\' || *p == L'/')
			LastSep = p;
	}

	if (LastSep != NULL)
	{
		// Copy the directory prefix (including the separator).
		size_t PrefixLen = (size_t)(LastSep - Pattern) + 1;
		if (PrefixLen >= MAX_PATH)
			PrefixLen = MAX_PATH - 1;
		CopyMemory(OutBuf, Pattern, PrefixLen * sizeof(WCHAR));
		OutBuf[PrefixLen] = L'\0';

		// Append the file name, truncating if necessary.
		size_t NameLen;
		if (FAILED(StringCchLengthW(FileName, MAX_PATH, &NameLen)))
			NameLen = 0;
		if (PrefixLen + NameLen >= MAX_PATH)
			NameLen = MAX_PATH - PrefixLen - 1;
		CopyMemory(OutBuf + PrefixLen, FileName, NameLen * sizeof(WCHAR));
		OutBuf[PrefixLen + NameLen] = L'\0';
	}
	else
	{
		// No directory component – use the file name directly.
		size_t NameLen;
		if (FAILED(StringCchLengthW(FileName, MAX_PATH, &NameLen)))
			NameLen = 0;
		if (NameLen >= MAX_PATH)
			NameLen = MAX_PATH - 1;
		CopyMemory(OutBuf, FileName, NameLen * sizeof(WCHAR));
		OutBuf[NameLen] = L'\0';
	}
}

/**
 * @brief Expands a wildcard pattern into a list of matching file paths.
 *
 * Uses FindFirstFileW / FindNextFileW to enumerate all files that match
 * the supplied pattern.  Directories are skipped.
 *
 * @param Pattern The wildcard pattern (may include '*' and '?').
 * @return A heap-allocated WILDCARD_EXPANSION, or NULL on allocation failure.
 *         The caller must free this with FreeWildcardExpansion().
 *         On success the structure's Count may be 0 if no files matched.
 */
static WILDCARD_EXPANSION*
ExpandWildcardPattern(_In_z_ const WCHAR* Pattern)
{
	WILDCARD_EXPANSION* Exp = (WILDCARD_EXPANSION*)HeapAlloc(
		GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WILDCARD_EXPANSION));
	if (Exp == NULL)
		return NULL;

	const size_t InitialCapacity = 16;
	Exp->Paths = (WCHAR**)HeapAlloc(
		GetProcessHeap(), HEAP_ZERO_MEMORY, InitialCapacity * sizeof(WCHAR*));
	if (Exp->Paths == NULL)
	{
		HeapFree(GetProcessHeap(), 0, Exp);
		return NULL;
	}
	Exp->Capacity = InitialCapacity;

	WIN32_FIND_DATAW FindData;
	HANDLE hFind = FindFirstFileW(Pattern, &FindData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		// No matches – return an empty expansion.
		return Exp;
	}

	do
	{
		// Skip directories.
		if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		// Build the full path from the pattern directory and the found name.
		WCHAR FullPath[MAX_PATH];
		BuildFullPath(Pattern, FindData.cFileName, FullPath);

		// Grow the array if needed.
		if (Exp->Count >= Exp->Capacity)
		{
			size_t NewCapacity = Exp->Capacity * 2;
			WCHAR** NewPaths = (WCHAR**)HeapReAlloc(
				GetProcessHeap(), 0,
				Exp->Paths, NewCapacity * sizeof(WCHAR*));
			if (NewPaths == NULL)
			{
				FindClose(hFind);
				// Partial results are still returned; caller will deal with them.
				return Exp;
			}
			Exp->Paths = NewPaths;
			Exp->Capacity = NewCapacity;
		}

		// Duplicate the full path onto the heap.
		size_t PathLen;
		if (FAILED(StringCchLengthW(FullPath, MAX_PATH, &PathLen)))
			continue;

		WCHAR* Dup = (WCHAR*)HeapAlloc(
			GetProcessHeap(), 0, (PathLen + 1) * sizeof(WCHAR));
		if (Dup == NULL)
		{
			FindClose(hFind);
			return Exp;
		}
		CopyMemory(Dup, FullPath, (PathLen + 1) * sizeof(WCHAR));

		Exp->Paths[Exp->Count++] = Dup;

	} while (FindNextFileW(hFind, &FindData));

	FindClose(hFind);
	return Exp;
}

/**
 * @brief Frees a WILDCARD_EXPANSION previously returned by ExpandWildcardPattern().
 * @param Expansion The structure to free. May be NULL.
 */
static void
FreeWildcardExpansion(_In_opt_ WILDCARD_EXPANSION* Expansion)
{
	if (Expansion == NULL)
		return;

	for (size_t i = 0; i < Expansion->Count; i++)
	{
		if (Expansion->Paths[i] != NULL)
			HeapFree(GetProcessHeap(), 0, Expansion->Paths[i]);
	}

	if (Expansion->Paths != NULL)
		HeapFree(GetProcessHeap(), 0, Expansion->Paths);

	HeapFree(GetProcessHeap(), 0, Expansion);
}

/**
 * @brief Performs file comparisons for wildcard-expanded patterns.
 *
 * Expands both arguments (one or both may contain wildcards) and compares
 * each matched file pair in order.  If one side is a literal path it is
 * paired with every file on the wildcard side.
 *
 * @param Pattern1 First file argument (may contain wildcards).
 * @param Pattern2 Second file argument (may contain wildcards).
 * @param Config   The already-configured FC_CONFIG to use for each comparison.
 * @return 0 if all pairs are identical, 1 if any differ, 2 on I/O or memory
 *         error, -1 on argument/usage errors.
 */
static int
WildcardFileCompare(
	_In_z_ const WCHAR* Pattern1,
	_In_z_ const WCHAR* Pattern2,
	_In_   FC_CONFIG*   Config)
{
	WILDCARD_EXPANSION* Exp1 = ExpandWildcardPattern(Pattern1);
	WILDCARD_EXPANSION* Exp2 = ExpandWildcardPattern(Pattern2);

	if (Exp1 == NULL || Exp2 == NULL)
	{
		FreeWildcardExpansion(Exp1);
		FreeWildcardExpansion(Exp2);
		ConPrintW(GetStdHandle(STD_ERROR_HANDLE), L"Error: memory allocation failure during wildcard expansion.\n");
		return 2;
	}

	if (Exp1->Count == 0 && Exp2->Count == 0)
	{
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		WriteConsoleW(hOut, L"FC: no files found for ", 23, NULL, NULL);
		ConPrintW(hOut, Pattern1);
		WriteConsoleW(hOut, L" or ", 4, NULL, NULL);
		ConPrintW(hOut, Pattern2);
		WriteConsoleW(hOut, L"\n", 1, NULL, NULL);
		FreeWildcardExpansion(Exp1);
		FreeWildcardExpansion(Exp2);
		return -1;
	}

	if (Exp1->Count == 0)
	{
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		WriteConsoleW(hOut, L"FC: no files found for ", 23, NULL, NULL);
		ConPrintW(hOut, Pattern1);
		WriteConsoleW(hOut, L"\n", 1, NULL, NULL);
		FreeWildcardExpansion(Exp1);
		FreeWildcardExpansion(Exp2);
		return -1;
	}

	if (Exp2->Count == 0)
	{
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		WriteConsoleW(hOut, L"FC: no files found for ", 23, NULL, NULL);
		ConPrintW(hOut, Pattern2);
		WriteConsoleW(hOut, L"\n", 1, NULL, NULL);
		FreeWildcardExpansion(Exp1);
		FreeWildcardExpansion(Exp2);
		return -1;
	}

	// Determine the number of pairs to compare.
	// If counts differ, compare up to the minimum and warn about the rest.
	size_t PairCount;
	if (Exp1->Count != Exp2->Count)
	{
		PairCount = Exp1->Count < Exp2->Count ? Exp1->Count : Exp2->Count;
		WCHAR buf[128];
		swprintf_s(buf, 128, L"FC: file count mismatch (%zu vs %zu); comparing first %zu pair(s).\n",
			Exp1->Count, Exp2->Count, PairCount);
		ConPrintW(GetStdHandle(STD_OUTPUT_HANDLE), buf);
	}
	else
	{
		PairCount = Exp1->Count;
	}

	int OverallResult = 0; // 0 = all identical so far

	for (size_t i = 0; i < PairCount; i++)
	{
		const WCHAR* File1 = Exp1->Paths[i];
		const WCHAR* File2 = Exp2->Paths[i];

		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		WriteConsoleW(hOut, L"Comparing files ", 16, NULL, NULL);
		ConPrintW(hOut, File1);
		WriteConsoleW(hOut, L" and ", 5, NULL, NULL);
		ConPrintW(hOut, File2);
		WriteConsoleW(hOut, L"\n", 1, NULL, NULL);

		FC_RESULT Result = FC_CompareFilesW(File1, File2, Config);

		switch (Result)
		{
		case FC_OK:
			// Identical – keep OverallResult as-is (0 or already 1).
			break;
		case FC_DIFFERENT:
			if (OverallResult < 1)
				OverallResult = 1;
			break;
		case FC_ERROR_IO:
		case FC_ERROR_MEMORY:
		{
			HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
			WriteConsoleW(hErr, L"Error during comparison of ", 27, NULL, NULL);
			ConPrintW(hErr, File1);
			WriteConsoleW(hErr, L" and ", 5, NULL, NULL);
			ConPrintW(hErr, File2);
			WCHAR errBuf[32];
			swprintf_s(errBuf, 32, L": %d\n", Result);
			ConPrintW(hErr, errBuf);
			if (OverallResult < 2)
				OverallResult = 2;
			break;
		}
		default:
			if (OverallResult == 0)
				OverallResult = -1;
			break;
		}
	}

	FreeWildcardExpansion(Exp1);
	FreeWildcardExpansion(Exp2);
	return OverallResult;
}

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
	TEXT_DIFF_USER_DATA TextUserData = { 0 }; // User data for text diff callback

	// Set non-zero defaults
	Config.Mode = FC_MODE_AUTO;
	Config.ResyncLines = 2;
	Config.BufferLines = 100;

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
					HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
					WCHAR buf[256];
					swprintf_s(buf, 256, L"Invalid option: %s\n", Option);
					ConPrintW(hErr, buf);
					return -1;
				}
			}
		}
		else
		{
			HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
			WCHAR buf[256];
			swprintf_s(buf, 256, L"Invalid argument: %s\n", Option);
			ConPrintW(hErr, buf);
			return -1;
		}
	}

	if (Config.Mode == FC_MODE_BINARY)
	{
		Config.DiffCallback = BinaryDiffCallback;
	}
	else
	{
		Config.DiffCallback = TextDiffCallback;
		// Pass flags to the text diff callback through UserData
		TextUserData.Flags = Config.Flags;
		Config.UserData = &TextUserData;
	}

	const WCHAR* File1 = argv[argc - 2];
	const WCHAR* File2 = argv[argc - 1];

	if (ContainsWildcard(File1) || ContainsWildcard(File2))
	{
		return WildcardFileCompare(File1, File2, &Config);
	}

	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	WriteConsoleW(hOut, L"Comparing files ", 16, NULL, NULL);
	ConPrintW(hOut, File1);
	WriteConsoleW(hOut, L" and ", 5, NULL, NULL);
	ConPrintW(hOut, File2);
	WriteConsoleW(hOut, L"\n", 1, NULL, NULL);

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
	{
		HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
		WCHAR errBuf[64];
		swprintf_s(errBuf, 64, L"Error during comparison: %d\n", Result);
		ConPrintW(hErr, errBuf);
		return 2;
	}
	default:
		// Invalid parameter or other syntax error
		return -1;
	}
}