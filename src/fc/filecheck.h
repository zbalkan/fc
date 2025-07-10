/*
 * PROJECT:     FileCheck Library
 * LICENSE:     GPL2
 * PURPOSE:     Header-only file comparison library for Windows.
 * COPYRIGHT:   Copyright 2025 Zafer Balkan
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include <winternl.h>

	//
	// Return codes for comparison operations.
	//
	typedef enum
	{
		FC_OK = 0,
		FC_DIFFERENT,
		FC_ERROR_IO,
		FC_ERROR_INVALID_PARAM,
		FC_ERROR_MEMORY
	} FC_RESULT;

	//
	// Specifies the comparison mode (text, binary, or auto).
	//
	typedef enum
	{
		FC_MODE_TEXT_ASCII,     // Plain text, ASCII/ANSI encoding
		FC_MODE_TEXT_UNICODE,   // Unicode text (UTF-8, UTF-16 w/ BOM)
		FC_MODE_BINARY,         // Raw binary comparison
		FC_MODE_AUTO            // Auto-detect based on file content
	} FC_MODE;

	//
	// Flags to modify comparison behavior.
	//
#define FC_IGNORE_CASE      0x0001  // Ignore case in text comparison.
#define FC_IGNORE_WS        0x0002  // Ignore whitespace in text comparison.
#define FC_SHOW_LINE_NUMS   0x0004  // Show line numbers in output.
#define FC_RAW_TABS         0x0008  // Do not expand tabs in text comparison.

/**
 * @enum RTL_PATH_TYPE
 * @brief Describes the type of a DOS-style path as interpreted by Windows internal path normalization routines.
 *
 * Used with the RtlDetermineDosPathNameType_U function in NTDLL to classify Win32 file path formats
 * before conversion to NT-native paths. Understanding these types is critical when validating or sanitizing
 * user-provided paths to prevent unintended access to devices, UNC shares, or object manager escape paths.
 *
 * This enum is not declared in the public Windows SDK and must be defined explicitly for use with Rtl* APIs.
 */
	typedef enum _RTL_PATH_TYPE {
		/**
		 * The path type could not be determined. Typically indicates malformed or empty input.
		 */
		RtlPathTypeUnknown = 0,

		/**
		 * A Universal Naming Convention (UNC) path, starting with two backslashes (\\server\share).
		 */
		RtlPathTypeUncAbsolute,

		/**
		 * A drive letter-based absolute path (e.g., C:\path\to\file).
		 */
		RtlPathTypeDriveAbsolute,

		/**
		 * A drive-relative path (e.g., C:folder\file.txt) — resolved against the current directory for the given drive.
		 */
		RtlPathTypeDriveRelative,

		/**
		 * A rooted path (e.g., \folder\file.txt) — interpreted relative to the current drive's root.
		 */
		RtlPathTypeRooted,

		/**
		 * A relative path (e.g., folder\file.txt) — resolved from the current working directory.
		 */
		RtlPathTypeRelative,

		/**
		 * A local device path using the \\.\ prefix (e.g., \\.\COM1) — accesses the DOS device namespace directly.
		 */
		RtlPathTypeLocalDevice,

		/**
		 * A root-local device path using the \\?\ prefix — bypasses Win32 normalization and accesses raw NT paths.
		 */
		RtlPathTypeRootLocalDevice
	} RTL_PATH_TYPE;

	EXTERN_C_START

		// External NTDLL APIs
		NTSYSAPI RTL_PATH_TYPE NTAPI RtlDetermineDosPathNameType_U(_In_ PCWSTR Path);
	NTSYSAPI NTSTATUS NTAPI RtlDosPathNameToNtPathName_U_WithStatus(
		__in PCWSTR DosFileName,
		__out PUNICODE_STRING NtFileName,
		__deref_opt_out_opt PWSTR* FilePart,
		__reserved PVOID Reserved
	);
	EXTERN_C_END

		/**
		 * @brief Callback function for reporting comparison differences.
		 *
		 * @param UserData  User-defined data passed from the FC_CONFIG struct.
		 * @param Message   A UTF-8 encoded string describing the difference.
		 * @param Line1     The line number in the first file, or -1 if not applicable.
		 * @param Line2     The line number in the second file, or -1 if not applicable.
		 */
		typedef void (*FC_OUTPUT_CALLBACK)(
			_In_opt_ void* UserData,
			_In_z_ const char* Message,
			_In_ int Line1,
			_In_ int Line2);

	//
	// Configuration structure for a file comparison operation.
	//
	typedef struct
	{
		FC_MODE Mode;               // Text, binary, or auto-detection mode.
		UINT Flags;                 // Option flags from FC_* defines.
		UINT ResyncLines;           // Reserved for future use.
		UINT BufferLines;           // Reserved for future use.
		FC_OUTPUT_CALLBACK Output;  // Callback function for diff messages.
		void* UserData;             // User-defined data passed to the callback.
	} FC_CONFIG;

	/* -------------------- Public API Functions -------------------- */

	/**
	 * @brief Compares two files using UTF-16 encoded paths. (Primary Function)
	 *
	 * Supports long paths and all modes (text, binary, auto).
	 *
	 * @param Path1 Path to the first file, UTF-16 encoded.
	 * @param Path2 Path to the second file, UTF-16 encoded.
	 * @param Config A pointer to the comparison configuration structure.
	 *
	 * @return An FC_RESULT code indicating the outcome of the comparison.
	 */
	FC_RESULT
		FileCheckCompareFilesW(
			_In_z_ const WCHAR* Path1,
			_In_z_ const WCHAR* Path2,
			_In_ const FC_CONFIG* Config);

	/**
	 * @brief Compares two files using UTF-8 encoded paths.
	 *
	 * This is a convenience wrapper that converts paths to UTF-16 before comparison.
	 *
	 * @param Path1Utf8 Path to the first file, UTF-8 encoded.
	 * @param Path2Utf8 Path to the second file, UTF-8 encoded.
	 * @param Config A pointer to the comparison configuration structure.
	 *
	 * @return An FC_RESULT code indicating the outcome of the comparison.
	 */
	FC_RESULT
		FileCheckCompareFilesUtf8(
			_In_z_ const char* Path1Utf8,
			_In_z_ const char* Path2Utf8,
			_In_ const FC_CONFIG* Config);

	/* -------------------- Internal Implementation (Private) -------------------- */

	// All functions and structs below are not part of the public API.

	typedef struct _FC_LINE
	{
		char* Text;
		size_t Length;
		UINT Hash;
	} FC_LINE;

	typedef struct _FC_LINE_ARRAY
	{
		FC_LINE* Lines;
		size_t Count;
		size_t Capacity;
	} FC_LINE_ARRAY;

	static inline unsigned char
		_FileCheckToLowerAscii(
			unsigned char Character)
	{
		if (Character >= 'A' && Character <= 'Z')
		{
			return Character + ('a' - 'A');
		}
		return Character;
	}

	static inline char*
		_FileCheckStringDuplicateRange(
			_In_reads_(Length) const char* String,
			_In_ size_t Length)
	{
		char* Output = (char*)HeapAlloc(GetProcessHeap(), 0, Length + 1);
		if (Output == NULL)
		{
			return NULL;
		}
		CopyMemory(Output, String, Length);
		Output[Length] = '\0';
		return Output;
	}

	//
	// Unicode-aware (UTF-8) string lowercasing function.
	// Returns a new, heap-allocated lowercase string. The caller must free it.
	//
	static inline char*
		_FileCheckStringToLowerUnicode(
			_In_reads_(SourceLength) const char* Source,
			_In_ size_t SourceLength,
			_Out_ size_t* NewLength)
	{
		*NewLength = 0;
		if (SourceLength == 0)
		{
			return _FileCheckStringDuplicateRange("", 0);
		}

		// Convert source UTF-8 to a temporary UTF-16 buffer
		int WideLength = MultiByteToWideChar(CP_UTF8, 0, Source, (int)SourceLength, NULL, 0);
		if (WideLength == 0) return NULL;

		WCHAR* WideBuffer = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, WideLength * sizeof(WCHAR));
		if (WideBuffer == NULL) return NULL;

		MultiByteToWideChar(CP_UTF8, 0, Source, (int)SourceLength, WideBuffer, WideLength);

		// Perform Unicode-aware lowercase conversion in-place on the UTF-16 buffer
		CharLowerW(WideBuffer);

		// Get the required buffer size for the final lowercase UTF-8 string
		int Utf8Length = WideCharToMultiByte(CP_UTF8, 0, WideBuffer, WideLength, NULL, 0, NULL, NULL);
		if (Utf8Length == 0)
		{
			HeapFree(GetProcessHeap(), 0, WideBuffer);
			return NULL;
		}

		char* DestBuffer = (char*)HeapAlloc(GetProcessHeap(), 0, (size_t)Utf8Length + 1);
		if (DestBuffer == NULL)
		{
			HeapFree(GetProcessHeap(), 0, WideBuffer);
			return NULL;
		}

		// Convert the lowercase UTF-16 buffer back to UTF-8
		WideCharToMultiByte(CP_UTF8, 0, WideBuffer, WideLength, DestBuffer, Utf8Length, NULL, NULL);
		DestBuffer[Utf8Length] = '\0'; // Null-terminate

		HeapFree(GetProcessHeap(), 0, WideBuffer);
		*NewLength = (size_t)Utf8Length;
		return DestBuffer;
	}

	static inline void
		_FileCheckIntegerToHex(
			size_t Value,
			_Out_writes_z_(17) char* OutputBuffer)
	{
		static const char HexDigits[] = "0123456789abcdef";
		char Temp[16];
		int i = 0;

		if (Value == 0)
		{
			OutputBuffer[0] = '0';
			OutputBuffer[1] = '\0';
			return;
		}

		while (Value && i < 16)
		{
			Temp[i++] = HexDigits[Value & 0xF];
			Value >>= 4;
		}

		for (int j = 0; j < i; ++j)
			OutputBuffer[j] = Temp[i - j - 1];

		OutputBuffer[i] = '\0';
	}

	static inline void
		_FileCheckLineArrayInit(
			_Inout_ FC_LINE_ARRAY* LineArray)
	{
		LineArray->Lines = NULL;
		LineArray->Count = 0;
		LineArray->Capacity = 0;
	}

	static inline void
		_FileCheckLineArrayFree(
			_Inout_ FC_LINE_ARRAY* LineArray)
	{
		if (LineArray->Lines != NULL)
		{
			for (size_t i = 0; i < LineArray->Count; ++i)
			{
				if (LineArray->Lines[i].Text != NULL)
				{
					HeapFree(GetProcessHeap(), 0, LineArray->Lines[i].Text);
				}
			}
			HeapFree(GetProcessHeap(), 0, LineArray->Lines);
		}
		LineArray->Lines = NULL;
		LineArray->Count = 0;
		LineArray->Capacity = 0;
	}

	static inline UINT
		_FileCheckComputeHash(
			_In_reads_(Length) const char* String,
			_In_ size_t Length,
			_In_ UINT Flags)
	{
		UINT Hash = 0;
		for (size_t i = 0; i < Length; ++i)
		{
			unsigned char Character = (unsigned char)String[i];
			if ((Flags & FC_IGNORE_WS) && (Character == ' ' || Character == '\t'))
				continue;
			Hash = Hash * 31 + Character;
		}
		return Hash;
	}

	static inline UINT
		_FileCheckHashLine(
			_In_reads_(Length) const char* String,
			_In_ size_t Length,
			_In_ const FC_CONFIG* Config)
	{
		UINT Flags = Config->Flags;

		// If case-insensitive and Unicode, lowercase using Unicode-aware conversion
		if ((Flags & FC_IGNORE_CASE) && Config->Mode == FC_MODE_TEXT_UNICODE)
		{
			size_t LowerLength;
			char* LowerString = _FileCheckStringToLowerUnicode(String, Length, &LowerLength);
			if (LowerString == NULL)
				return 0; // Fail-safe; hashing 0 for NULL is safe for comparison fallback
			UINT Hash = _FileCheckComputeHash(LowerString, LowerLength, Flags);
			HeapFree(GetProcessHeap(), 0, LowerString);
			return Hash;
		}

		// Otherwise, optionally ASCII-lower the input before hashing
		char* Temp = NULL;
		const char* Input = String;
		size_t InputLen = Length;

		if (Flags & FC_IGNORE_CASE)
		{
			Temp = (char*)HeapAlloc(GetProcessHeap(), 0, Length + 1);
			if (Temp == NULL)
				return 0;

			for (size_t i = 0; i < Length; ++i)
			{
				Temp[i] = (char)_FileCheckToLowerAscii((unsigned char)String[i]);
			}
			Temp[Length] = '\0';
			Input = Temp;
		}

		UINT Hash = _FileCheckComputeHash(Input, InputLen, Flags);

		if (Temp)
			HeapFree(GetProcessHeap(), 0, Temp);

		return Hash;
	}

	static inline BOOL
		_FileCheckLineArrayAppend(
			_Inout_ FC_LINE_ARRAY* LineArray,
			_In_ _Post_invalid_ char* Text, // Text ownership is transferred
			_In_ size_t Length,
			_In_ UINT Hash)
	{
		if (LineArray->Count + 1 > LineArray->Capacity)
		{
			size_t NewCapacity = LineArray->Capacity ? LineArray->Capacity * 2 : 64;
			if (LineArray->Count + 1 > LineArray->Capacity)
			{
				size_t NewCapacity = LineArray->Capacity ? LineArray->Capacity * 2 : 64;
				FC_LINE* Temp = NULL;
				if (LineArray->Lines == NULL)
				{
					Temp = (FC_LINE*)HeapAlloc(GetProcessHeap(), 0, NewCapacity * sizeof(FC_LINE));
				}
				else
				{
					Temp = (FC_LINE*)HeapReAlloc(GetProcessHeap(), 0, LineArray->Lines, NewCapacity * sizeof(FC_LINE));
				}
				if (Temp == NULL)
				{
					return FALSE;
				}
				LineArray->Lines = Temp;
				LineArray->Capacity = NewCapacity;
			}
		}
		LineArray->Lines[LineArray->Count].Text = Text;
		LineArray->Lines[LineArray->Count].Length = Length;
		LineArray->Lines[LineArray->Count].Hash = Hash;
		LineArray->Count++;
		return TRUE;
	}

	// Linked-list node holding a chunk of characters
	typedef struct _FileCheckCharNode {
		char* data;
		size_t length;
		struct _FileCheckCharNode* next;
	} _FileCheckCharNode;

	// Create a new node with a copy of the given data
	static _FileCheckCharNode*
		_FileCheckCreateNode(
			_In_reads_(length) const char* data,
			_In_ size_t length)
	{
		_FileCheckCharNode* node = (_FileCheckCharNode*)HeapAlloc(GetProcessHeap(), 0, sizeof(_FileCheckCharNode));
		if (!node)
			return NULL;
		node->data = (char*)HeapAlloc(GetProcessHeap(), 0, length + 1);
		if (!node->data) {
			HeapFree(GetProcessHeap(), 0, node);
			return NULL;
		}
		memcpy(node->data, data, length);
		node->data[length] = '\0';
		node->length = length;
		node->next = NULL;
		return node;
	}

	// Free the entire linked list
	static void
		_FileCheckFreeList(
			_In_opt_ _FileCheckCharNode* head)
	{
		while (head) {
			_FileCheckCharNode* next = head->next;
			HeapFree(GetProcessHeap(), 0, head->data);
			HeapFree(GetProcessHeap(), 0, head);
			head = next;
		}
	}

	// Build a linked list from the source string, splitting at tabs
	static _FileCheckCharNode*
		_FileCheckBuildList(
			_In_reads_(srcLen) const char* src,
			_In_ size_t srcLen)
	{
		_FileCheckCharNode* head = NULL;
		_FileCheckCharNode* tail = NULL;
		size_t i = 0;

		while (i < srcLen) {
			if (src[i] == '\t') {
				_FileCheckCharNode* node = _FileCheckCreateNode("\t", 1);
				if (!node) { _FileCheckFreeList(head); return NULL; }
				if (!head)
					head = tail = node;
				else {
					tail->next = node;
					tail = node;
				}
				i++;
			}
			else {
				size_t start = i;
				while (i < srcLen && src[i] != '\t')
					++i;
				size_t len = i - start;
				_FileCheckCharNode* node = _FileCheckCreateNode(src + start, len);
				if (!node) { _FileCheckFreeList(head); return NULL; }
				if (!head)
					head = tail = node;
				else {
					tail->next = node;
					tail = node;
				}
			}
		}
		return head;
	}

	// Replace each tab node with a node containing TabWidth spaces
	static void
		_FileCheckExpandTabsInList(
			_Inout_ _FileCheckCharNode** headPtr,
			_In_ size_t TabWidth)
	{
		_FileCheckCharNode* prev = NULL;
		_FileCheckCharNode* curr = *headPtr;

		while (curr) {
			if (curr->length == 1 && curr->data[0] == '\t') {
				// Allocate space buffer
				char* spaces = (char*)HeapAlloc(GetProcessHeap(), 0, TabWidth + 1);
				if (!spaces)
					return;
				memset(spaces, ' ', TabWidth);
				spaces[TabWidth] = '\0';

				_FileCheckCharNode* spaceNode = _FileCheckCreateNode(spaces, TabWidth);
				HeapFree(GetProcessHeap(), 0, spaces);
				if (!spaceNode)
					return;

				// Splice in the new node
				spaceNode->next = curr->next;
				if (prev)
					prev->next = spaceNode;
				else
					*headPtr = spaceNode;

				// Free the old tab node
				HeapFree(GetProcessHeap(), 0, curr->data);
				HeapFree(GetProcessHeap(), 0, curr);
				curr = spaceNode;
			}
			prev = curr;
			curr = curr->next;
		}
	}

	// Flatten the linked list back into a single string
	static char*
		_FileCheckFlattenList(
			_In_ _FileCheckCharNode* head,
			_Out_ size_t* newLength)
	{
		size_t total = 0;
		for (_FileCheckCharNode* n = head; n; n = n->next)
			total += n->length;

		char* dest = (char*)HeapAlloc(GetProcessHeap(), 0, total + 1);
		if (!dest)
			return NULL;

		size_t pos = 0;
		for (_FileCheckCharNode* n = head; n; n = n->next) {
			memcpy(dest + pos, n->data, n->length);
			pos += n->length;
		}
		dest[pos] = '\0';
		*newLength = pos;
		return dest;
	}

	// Wrapper matching original signature, using linked-list expansion
	static inline char*
		_FileCheckExpandTabs(
			_In_reads_(SourceLength) const char* Source,
			_In_ size_t SourceLength,
			_Out_ size_t* NewLength)
	{
		const size_t TabWidth = 4;
		_FileCheckCharNode* list = _FileCheckBuildList(Source, SourceLength);
		if (!list)
			return NULL;
		_FileCheckExpandTabsInList(&list, TabWidth);
		char* result = _FileCheckFlattenList(list, NewLength);
		_FileCheckFreeList(list);
		return result;
	}
    
	static inline FC_RESULT
		_FileCheckParseLines(
			_In_reads_(BufferLength) const char* Buffer,
			_In_ size_t BufferLength,
			_Inout_ FC_LINE_ARRAY* LineArray,
			_In_ const FC_CONFIG* Config)
	{
		_FileCheckLineArrayInit(LineArray);
		const char* Ptr = Buffer;
		const char* End = Buffer + BufferLength;

		while (Ptr < End)
		{
			const char* Newline = Ptr;
			while (Newline < End && *Newline != '\n' && *Newline != '\r')
			{
				Newline++;
			}

			size_t OriginalLength = (size_t)(Newline - Ptr);
			char* LineText = _FileCheckStringDuplicateRange(Ptr, OriginalLength);
			if (LineText == NULL)
			{
				_FileCheckLineArrayFree(LineArray);
				return FC_ERROR_MEMORY;
			}

			size_t FinalLength = OriginalLength;
			char* FinalText = LineText;

			// Expand tabs if FC_RAW_TABS is not set
			if (!(Config->Flags & FC_RAW_TABS))
			{
				char* Expanded = _FileCheckExpandTabs(LineText, OriginalLength, &FinalLength);

				if (Expanded == NULL)
				{
					_FileCheckLineArrayFree(LineArray);
					return FC_ERROR_MEMORY;
				}

				HeapFree(GetProcessHeap(), 0, LineText);
				FinalText = Expanded; // Always update FinalText!
			}

			UINT Hash = _FileCheckHashLine(FinalText, FinalLength, Config);
			if (!_FileCheckLineArrayAppend(LineArray, FinalText, FinalLength, Hash))
			{
				HeapFree(GetProcessHeap(), 0, FinalText);
				_FileCheckLineArrayFree(LineArray);
				return FC_ERROR_MEMORY;
			}

			while (Newline < End && (*Newline == '\n' || *Newline == '\r'))
			{
				Newline++;
			}
			Ptr = Newline;
		}
		return FC_OK;
	}

	static inline FC_RESULT
		_FileCheckCompareLineArrays(
			_In_ const FC_LINE_ARRAY* ArrayA,
			_In_ const FC_LINE_ARRAY* ArrayB,
			_In_ const FC_CONFIG* Config)
	{
		// First check: line count mismatch.
		// Why: Different counts imply files differ in structure, no need to compare hashes.
		if (ArrayA->Count != ArrayB->Count)
		{
			if (Config->Output)
				Config->Output(Config->UserData, "Files have different line counts", -1, -1);
			return FC_DIFFERENT;
		}

		// Compare each line one by one.
		// Why: Hash comparison is fast and covers most differences;
		// fallback to byte-wise memcmp is used only if hashes match and case must be preserved.
		for (size_t i = 0; i < ArrayA->Count; ++i)
		{
			const FC_LINE* LineA = &ArrayA->Lines[i];
			const FC_LINE* LineB = &ArrayB->Lines[i];

			// Fast hash mismatch check — high-probability early exit
			if (LineA->Hash != LineB->Hash)
			{
				return FC_DIFFERENT;
			}

			// If case is ignored, the original line contents may differ (e.g., "abc" vs "ABC").
			// We skip memcmp in that case because hashes already include normalization.
			if (!(Config->Flags & FC_IGNORE_CASE))
			{
				// Fallback exact match check
				// If FC_IGNORE_WS is set, skip whitespace in comparison
				if (Config->Flags & FC_IGNORE_WS)
				{
					const char* a = LineA->Text;
					const char* b = LineB->Text;
					size_t la = LineA->Length, lb = LineB->Length;
					size_t ia = 0, ib = 0;
					while (ia < la && ib < lb)
					{
						while (ia < la && (a[ia] == ' ' || a[ia] == '\t')) ++ia;
						while (ib < lb && (b[ib] == ' ' || b[ib] == '\t')) ++ib;
						if (ia < la && ib < lb)
						{
							if (a[ia] != b[ib])
							{
								return FC_DIFFERENT;
							}
							++ia; ++ib;
						}
					}
					// Skip trailing whitespace
					while (ia < la && (a[ia] == ' ' || a[ia] == '\t')) ++ia;
					while (ib < lb && (b[ib] == ' ' || b[ib] == '\t')) ++ib;
					if (ia != la || ib != lb)
					{
						return FC_DIFFERENT;
					}
				}
				else
				{
					if (LineA->Length != LineB->Length ||
						RtlCompareMemory(LineA->Text, LineB->Text, LineA->Length) != LineA->Length)
					{
						return FC_DIFFERENT;
					}
				}
			}
		}

		// All lines matched exactly or per hash+config — files are equal
		return FC_OK;
	}

	static inline char*
		_FileCheckReadFileContents(
			_In_z_ const WCHAR* Path,
			_Out_ size_t* OutputLength,
			_Out_ FC_RESULT* Result)
	{
		*OutputLength = 0;
		*Result = FC_ERROR_IO;

		HANDLE FileHandle = CreateFileW(
			Path,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);

		if (FileHandle == INVALID_HANDLE_VALUE)
			return NULL;

		LARGE_INTEGER FileSize;
		BOOL SizeSuccess = GetFileSizeEx(FileHandle, &FileSize);
		if (!SizeSuccess) {
			DWORD Err = GetLastError();
			CloseHandle(FileHandle);
			if (Err == ERROR_FILE_NOT_FOUND || Err == ERROR_PATH_NOT_FOUND)
			{
				*Result = FC_ERROR_INVALID_PARAM; // File not found
			}
			else
			{
				*Result = FC_ERROR_IO; // Other IO error
			}
			return NULL;
		}

		if (FileSize.QuadPart > (ULONGLONG)SIZE_MAX)
		{
			CloseHandle(FileHandle);
			return NULL;
		}

		size_t Length = (size_t)FileSize.QuadPart;
		if (Length == 0)
		{
			CloseHandle(FileHandle);
			*Result = FC_OK;
			return _FileCheckStringDuplicateRange("", 0);
		}

		char* Buffer = (char*)HeapAlloc(GetProcessHeap(), 0, Length);
		if (Buffer == NULL)
		{
			CloseHandle(FileHandle);
			*Result = FC_ERROR_MEMORY;
			return NULL;
		}

		DWORD BytesRead = 0;
		if (!ReadFile(FileHandle, Buffer, (DWORD)Length, &BytesRead, NULL) || BytesRead != Length)
		{
			HeapFree(GetProcessHeap(), 0, Buffer);
			CloseHandle(FileHandle);
			*Result = FC_ERROR_IO;
			return NULL;
		}

		CloseHandle(FileHandle);
		*OutputLength = Length;
		*Result = FC_OK;
		return Buffer;
	}

	static inline BOOL
		IsProbablyTextBuffer(const BYTE* buffer, DWORD length)
	{
		const double textThreshold = 0.90;
		if (length == 0) return FALSE;

		// Detect UTF BOMs (early exit for known good encodings)
		if (length >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF) // UTF-8 BOM
			return TRUE;
		if (length >= 2 && buffer[0] == 0xFF && buffer[1] == 0xFE) // UTF-16 LE BOM
			return TRUE;
		if (length >= 2 && buffer[0] == 0xFE && buffer[1] == 0xFF) // UTF-16 BE BOM
			return TRUE;

		int printable = 0;
		for (DWORD i = 0; i < length; ++i)
		{
			BYTE c = buffer[i];
			if ((c >= 32 && c <= 126) || c == 9 || c == 10 || c == 13) {
				printable++;
			}
			else if (c == 0) {
				return FALSE; // Null byte strongly suggests binary
			}
			// Could expand here with UTF-8 continuation byte validation, if needed
		}

		double ratio = (double)printable / length;
		return (ratio >= textThreshold);
	}

	static inline BOOL
		IsProbablyTextFileW(const WCHAR* filepath) {
		HANDLE hFile = CreateFileW(
			filepath,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);
		if (hFile == INVALID_HANDLE_VALUE) return FALSE;

#define bufferSize 4096

		BYTE buffer[bufferSize];
		DWORD bytesRead = 0;
		BOOL success = ReadFile(hFile, buffer, bufferSize, &bytesRead, NULL);
		CloseHandle(hFile);
		if (!success || bytesRead == 0) return FALSE;

		return IsProbablyTextBuffer(buffer, bytesRead);
	}

	static FC_RESULT
		_FileCheckCompareFilesText(
			_In_z_ const WCHAR* Path1,
			_In_z_ const WCHAR* Path2,
			_In_ const FC_CONFIG* Config)
	{
		FC_RESULT Result = FC_OK;
		size_t Length1 = 0, Length2 = 0;
		char* Buffer1 = NULL;
		char* Buffer2 = NULL;
		FC_LINE_ARRAY ArrayA, ArrayB;

		_FileCheckLineArrayInit(&ArrayA);
		_FileCheckLineArrayInit(&ArrayB);

		Buffer1 = _FileCheckReadFileContents(Path1, &Length1, &Result);
		if (!Buffer1) goto cleanup;

		Buffer2 = _FileCheckReadFileContents(Path2, &Length2, &Result);
		if (!Buffer2) goto cleanup;

		Result = _FileCheckParseLines(Buffer1, Length1, &ArrayA, Config);
		if (Result != FC_OK) goto cleanup;

		Result = _FileCheckParseLines(Buffer2, Length2, &ArrayB, Config);
		if (Result != FC_OK) goto cleanup;

		Result = _FileCheckCompareLineArrays(&ArrayA, &ArrayB, Config);

	cleanup:
		if (Buffer1) HeapFree(GetProcessHeap(), 0, Buffer1);
		if (Buffer2) HeapFree(GetProcessHeap(), 0, Buffer2);
		_FileCheckLineArrayFree(&ArrayA);
		_FileCheckLineArrayFree(&ArrayB);
		return Result;
	}

	static FC_RESULT
		_FileCheckCompareFilesBinary(
			_In_z_ const WCHAR* Path1,
			_In_z_ const WCHAR* Path2,
			_In_ const FC_CONFIG* Config)
	{
		HANDLE File1Handle = CreateFileW(Path1, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		HANDLE File2Handle = CreateFileW(Path2, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if (File1Handle == INVALID_HANDLE_VALUE || File2Handle == INVALID_HANDLE_VALUE)
		{
			if (File1Handle != INVALID_HANDLE_VALUE) CloseHandle(File1Handle);
			if (File2Handle != INVALID_HANDLE_VALUE) CloseHandle(File2Handle);
			return FC_ERROR_IO;
		}

		LARGE_INTEGER File1Size, File2Size;
		if (!GetFileSizeEx(File1Handle, &File1Size) || !GetFileSizeEx(File2Handle, &File2Size))
		{
			CloseHandle(File1Handle);
			CloseHandle(File2Handle);
			return FC_ERROR_IO;
		}

		if (File1Size.QuadPart != File2Size.QuadPart)
		{
			if (Config->Output != NULL)
			{
				Config->Output(Config->UserData, "Files are different sizes", -1, -1);
			}
			CloseHandle(File1Handle);
			CloseHandle(File2Handle);
			return FC_DIFFERENT;
		}

		size_t CompareSize = (size_t)File1Size.QuadPart;
		HANDLE Map1Handle = CreateFileMapping(File1Handle, NULL, PAGE_READONLY, 0, 0, NULL);
		HANDLE Map2Handle = CreateFileMapping(File2Handle, NULL, PAGE_READONLY, 0, 0, NULL);

		if (Map1Handle == NULL || Map2Handle == NULL)
		{
			if (Map1Handle != NULL) CloseHandle(Map1Handle);
			if (Map2Handle != NULL) CloseHandle(Map2Handle);
			CloseHandle(File1Handle);
			CloseHandle(File2Handle);
			return FC_ERROR_IO;
		}

		unsigned char* Buffer1 = (unsigned char*)MapViewOfFile(Map1Handle, FILE_MAP_READ, 0, 0, CompareSize);
		unsigned char* Buffer2 = (unsigned char*)MapViewOfFile(Map2Handle, FILE_MAP_READ, 0, 0, CompareSize);

		if (Buffer1 == NULL || Buffer2 == NULL)
		{
			if (Buffer1 != NULL) UnmapViewOfFile(Buffer1);
			if (Buffer2 != NULL) UnmapViewOfFile(Buffer2);
			CloseHandle(Map1Handle);
			CloseHandle(Map2Handle);
			CloseHandle(File1Handle);
			CloseHandle(File2Handle);
			return FC_ERROR_IO;
		}

		FC_RESULT Result = FC_OK;
		size_t FirstDifference = (size_t)-1;

		// Consolidated loop to find the first difference.
		for (size_t i = 0; i < CompareSize; ++i)
		{
			if (Buffer1[i] != Buffer2[i])
			{
				FirstDifference = i;
				Result = FC_DIFFERENT;
				break;
			}
		}

		if (Result == FC_DIFFERENT && Config->Output != NULL)
		{
			char Message[64] = "Binary diff at offset 0x";
			char HexBuffer[17];
			_FileCheckIntegerToHex(FirstDifference, HexBuffer);

			char* Ptr = Message;
			while (*Ptr) Ptr++;
			char* Source = HexBuffer;
			while (*Source) *Ptr++ = *Source++;
			*Ptr = '\0';

			Config->Output(Config->UserData, Message, -1, -1);
		}

		UnmapViewOfFile(Buffer1);
		UnmapViewOfFile(Buffer2);
		CloseHandle(Map1Handle);
		CloseHandle(Map2Handle);
		CloseHandle(File1Handle);
		CloseHandle(File2Handle);
		return Result;
	}

	static BOOL
		_FileCheckPreparePath(
			_In_z_ const WCHAR* InputPath,
			_Outptr_result_nullonfailure_ WCHAR** CanonicalPathOut)
	{
		if (!InputPath || !CanonicalPathOut)
			return FALSE;

		*CanonicalPathOut = NULL;

		// Step 1: Check if input path type is acceptable
		RTL_PATH_TYPE PathType = RtlDetermineDosPathNameType_U(InputPath);
		if (PathType == RtlPathTypeUnknown ||
			PathType == RtlPathTypeLocalDevice ||
			PathType == RtlPathTypeRootLocalDevice)
		{
			return FALSE; // reject raw \\.\ or \\?\ paths
		}

		// Step 2: Convert to full NT path via native call
		// TODO: Check if the path is already in NT format, if not, convert it
		// Note: RtlDosPathNameToRelativeNtPathName_U_WithStatus returns a relative NT path,
		// but we can use it to validate and canonicalize the input.
		// It will also handle long paths correctly.
		// Sample input: "C:\path\to\file.txt" or "\\?\C:\path\to\file.txt"
		// Sample output: "\??\C:\path\to\file.txt" or "\Device\HarddiskVolume1\path\to\file.txt"
		// Check PathType variable's value to ensure we handle it correctly.
		if (PathType == RtlPathTypeUncAbsolute ||
			PathType == RtlPathTypeDriveAbsolute ||
			PathType == RtlPathTypeDriveRelative ||
			PathType == RtlPathTypeRooted ||
			PathType == RtlPathTypeRelative)
		{
			// These are acceptable, proceed to conversion
		}
		else
		{
			return FALSE; // reject other types like UNC relative, etc.
		}

		UNICODE_STRING NtPath;
		NTSTATUS Status = RtlDosPathNameToNtPathName_U_WithStatus(
			InputPath,
			&NtPath,
			NULL, // FilePart not needed
			NULL); // Reserved

		if (!NT_SUCCESS(Status))
		{
			RtlFreeUnicodeString(&NtPath);
			return FALSE;
		}

		// Step 3: Detect risky NT path prefixes
		if (NtPath.Length >= 8 * sizeof(WCHAR))
		{
			const WCHAR* s = NtPath.Buffer;

			// Block named pipes
			if ((NtPath.Length >= 36 * sizeof(WCHAR) &&
				_wcsnicmp(s, L"\\Device\\NamedPipe\\", 18) == 0) ||
				_wcsnicmp(s, L"\\??\\PIPE\\", 9) == 0)
			{
				RtlFreeUnicodeString(&NtPath);
				return FALSE;
			}

			// Block all \Device\... raw paths
			if (_wcsnicmp(s, L"\\Device\\", 8) == 0)
			{
				RtlFreeUnicodeString(&NtPath);
				return FALSE;
			}
		}

		// Step 4: Reject reserved DOS device names
		const WCHAR* base = NtPath.Buffer;
		for (USHORT i = 0; i < NtPath.Length / sizeof(WCHAR); ++i)
			if (NtPath.Buffer[i] == L'\\')
				base = &NtPath.Buffer[i + 1];

		static const WCHAR* ReservedDevices[] = {
			L"CON", L"PRN", L"AUX", L"NUL",
			L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
			L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"
		};
		for (int i = 0; i < ARRAYSIZE(ReservedDevices); ++i)
		{
			if (_wcsicmp(base, ReservedDevices[i]) == 0)
			{
				RtlFreeUnicodeString(&NtPath);
				return FALSE;
			}
		}

		// Step 5: Allocate copy of canonical path
		size_t len = (NtPath.Length / sizeof(WCHAR)) + 1;
		WCHAR* outPath = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
		if (!outPath)
		{
			RtlFreeUnicodeString(&NtPath);
			return FALSE;
		}

		wcsncpy_s(outPath, len, NtPath.Buffer, _TRUNCATE);
		RtlFreeUnicodeString(&NtPath);

		*CanonicalPathOut = outPath;
		return TRUE;
	}

	//
	// Main Implementation
	//

	FC_RESULT
		FileCheckCompareFilesUtf8(
			_In_z_ const char* Path1Utf8,
			_In_z_ const char* Path2Utf8,
			_In_ const FC_CONFIG* Config)
	{
		WCHAR* WidePath1 = NULL;
		WCHAR* WidePath2 = NULL;
		FC_RESULT Result = FC_OK;

		if (Path1Utf8 == NULL || Path2Utf8 == NULL)
		{
			return FC_ERROR_INVALID_PARAM;
		}

		int WideLength1 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path1Utf8, -1, NULL, 0);
		if (WideLength1 == 0)
		{
			Result = FC_ERROR_INVALID_PARAM;
			goto cleanup;
		}
		WidePath1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, WideLength1 * sizeof(WCHAR));
		if (WidePath1 == NULL)
		{
			Result = FC_ERROR_MEMORY;
			goto cleanup;
		}
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path1Utf8, -1, WidePath1, WideLength1) == 0)
		{
			Result = FC_ERROR_INVALID_PARAM;
			goto cleanup;
		}

		int WideLength2 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path2Utf8, -1, NULL, 0);
		if (WideLength2 == 0)
		{
			Result = FC_ERROR_INVALID_PARAM;
			goto cleanup;
		}
		WidePath2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, WideLength2 * sizeof(WCHAR));
		if (WidePath2 == NULL)
		{
			Result = FC_ERROR_MEMORY;
			goto cleanup;
		}
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path2Utf8, -1, WidePath2, WideLength2) == 0)
		{
			Result = FC_ERROR_INVALID_PARAM;
			goto cleanup;
		}

		Result = FileCheckCompareFilesW(WidePath1, WidePath2, Config);

	cleanup:
		if (WidePath1 != NULL) HeapFree(GetProcessHeap(), 0, WidePath1);
		if (WidePath2 != NULL) HeapFree(GetProcessHeap(), 0, WidePath2);
		return Result;
	}

	FC_RESULT
		FileCheckCompareFilesW(
			_In_z_ const WCHAR* Path1,
			_In_z_ const WCHAR* Path2,
			_In_ const FC_CONFIG* Config)
	{
		if (!Path1 || !Path2 || !Config || !Config->Output)
			return FC_ERROR_INVALID_PARAM;

		WCHAR* CanonicalPath1 = NULL;
		WCHAR* CanonicalPath2 = NULL;

		if (!_FileCheckPreparePath(Path1, &CanonicalPath1) ||
			!_FileCheckPreparePath(Path2, &CanonicalPath2))
		{
			if (CanonicalPath1) HeapFree(GetProcessHeap(), 0, CanonicalPath1);
			if (CanonicalPath2) HeapFree(GetProcessHeap(), 0, CanonicalPath2);
			return FC_ERROR_INVALID_PARAM;
		}

		FC_RESULT Result;

		switch (Config->Mode)
		{
		case FC_MODE_TEXT_ASCII:
		case FC_MODE_TEXT_UNICODE:
			Result = _FileCheckCompareFilesText(CanonicalPath1, CanonicalPath2, Config);
			break;
		case FC_MODE_BINARY:
			Result = _FileCheckCompareFilesBinary(CanonicalPath1, CanonicalPath2, Config);
			break;
		case FC_MODE_AUTO:
		default: {
			BOOL isText1 = IsProbablyTextFileW(Path1);
			BOOL isText2 = IsProbablyTextFileW(Path2);
			if (isText1 && isText2)
				Result = _FileCheckCompareFilesText(CanonicalPath1, CanonicalPath2, Config);
			else
				Result = _FileCheckCompareFilesBinary(CanonicalPath1, CanonicalPath2, Config);
			break;
		}
		}

		HeapFree(GetProcessHeap(), 0, CanonicalPath1);
		HeapFree(GetProcessHeap(), 0, CanonicalPath2);

		return Result;
	}

#ifdef __cplusplus
}
#endif
