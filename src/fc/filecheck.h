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

	// External NTDLL APIs
	NTSYSAPI RTL_PATH_TYPE NTAPI RtlDetermineDosPathNameType_U(_In_ PCWSTR Path);
	NTSYSAPI NTSTATUS NTAPI RtlDosPathNameToNtPathName_U_WithStatus(
		__in PCWSTR DosFileName,
		__out PUNICODE_STRING NtFileName,
		__deref_opt_out_opt PWSTR* FilePart,
		__reserved PVOID Reserved
	);

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
		FC_CompareFilesW(
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
		FC_CompareFilesUtf8(
			_In_z_ const char* Path1Utf8,
			_In_z_ const char* Path2Utf8,
			_In_ const FC_CONFIG* Config);

	/* -------------------- Internal Implementation (Private) -------------------- */

	// All functions and structs below are not part of the public API.

	typedef struct
	{
		char* Text;
		size_t Length;
		UINT Hash;
	} _FC_LINE;

	/*
 * Generic, reusable dynamic buffer for any data type.
 * This single structure replaces _FC_LINE_ARRAY and the _FC_CHAR_NODE linked list.
 */
	typedef struct
	{
		void* pData;       // Pointer to the block of memory holding the elements.
		size_t ElementSize; // The size of a single element (e.g., sizeof(char)).
		size_t Count;       // The number of elements currently in the buffer.
		size_t Capacity;    // The number of elements the buffer can hold before resizing.
	} _FC_BUFFER;

	// Initializes a new, empty buffer to hold elements of a specific size.
	static inline void
		_FC_BufferInit(
			_Inout_ _FC_BUFFER* pBuffer,
			_In_ size_t elementSize)
	{
		pBuffer->pData = NULL;
		pBuffer->ElementSize = elementSize;
		pBuffer->Count = 0;
		pBuffer->Capacity = 0;
	}

	// Frees the internal memory of the buffer.
	// NOTE: Does not free nested pointers within the elements themselves.
	static inline void
		_FC_BufferFree(
			_Inout_ _FC_BUFFER* pBuffer)
	{
		if (pBuffer->pData != NULL)
		{
			HeapFree(GetProcessHeap(), 0, pBuffer->pData);
		}
		pBuffer->pData = NULL;
		pBuffer->Count = 0;
		pBuffer->Capacity = 0;
	}

	// Ensures the buffer has enough capacity for at least 'additionalCount' new elements.
	// Returns FALSE on memory allocation failure.
	static inline BOOL
		_FC_BufferEnsureCapacity(
			_Inout_ _FC_BUFFER* pBuffer,
			_In_ size_t additionalCount)
	{
		if (additionalCount > SIZE_MAX - pBuffer->Count)
			return FALSE;
		if (pBuffer->Count + additionalCount > pBuffer->Capacity)
		{
			size_t newCapacity = pBuffer->Capacity > 0 ? pBuffer->Capacity : 8;
			if (pBuffer->Capacity > 0)
			{
				if (pBuffer->Capacity > SIZE_MAX / 2) // Check before doubling
					newCapacity = SIZE_MAX;
				else
					newCapacity *= 2;
			}

			if (newCapacity < pBuffer->Count + additionalCount)
				newCapacity = pBuffer->Count + additionalCount;
			if (pBuffer->ElementSize > 0 && newCapacity > SIZE_MAX / pBuffer->ElementSize) // Check before multiplication
				return FALSE;
			size_t newSizeInBytes = newCapacity * pBuffer->ElementSize;
			void* pNewData = pBuffer->pData
				? HeapReAlloc(GetProcessHeap(), 0, pBuffer->pData, newSizeInBytes)
				: HeapAlloc(GetProcessHeap(), 0, newSizeInBytes);
			if (pNewData == NULL)
				return FALSE;
			pBuffer->pData = pNewData;
			pBuffer->Capacity = newCapacity;
		}
		return TRUE;
	}

	// Appends a single element to the end of the buffer.
	static inline BOOL
		_FC_BufferAppend(
			_Inout_ _FC_BUFFER* pBuffer,
			_In_ const void* pElement)
	{
		if (!_FC_BufferEnsureCapacity(pBuffer, 1))
		{
			return FALSE;
		}

		void* pDestination = (char*)pBuffer->pData + (pBuffer->Count * pBuffer->ElementSize);
		memcpy(pDestination, pElement, pBuffer->ElementSize);
		pBuffer->Count++;
		return TRUE;
	}

	// Appends a range of elements to the end of the buffer.
	static inline BOOL
		_FC_BufferAppendRange(
			_Inout_ _FC_BUFFER* pBuffer,
			_In_ const void* pElements,
			_In_ size_t count)
	{
		if (count == 0) return TRUE;
		if (!_FC_BufferEnsureCapacity(pBuffer, count))
		{
			return FALSE;
		}

		void* pDestination = (char*)pBuffer->pData + (pBuffer->Count * pBuffer->ElementSize);
		memcpy(pDestination, pElements, count * pBuffer->ElementSize);
		pBuffer->Count += count;
		return TRUE;
	}

	// Returns a pointer to the element at a given index.
	static inline void*
		_FC_BufferGet(
			_In_ const _FC_BUFFER* pBuffer,
			_In_ size_t index)
	{
		if (index >= pBuffer->Count)
		{
			return NULL;
		}
		return (char*)pBuffer->pData + (index * pBuffer->ElementSize);
	}

	/**
	 * @brief Finds the first occurrence of a pattern within a buffer.
	 *
	 * @param pBuffer The buffer to search in.
	 * @param pPattern A pointer to the pattern to find.
	 * @param patternSize The number of elements in the pattern.
	 * @param startIndex The index to start searching from.
	 * @return The starting index of the found pattern, or (size_t)-1 if not found.
	 */
	static inline size_t
		_FC_BufferFind(
			_In_ const _FC_BUFFER* pBuffer,
			_In_ const void* pPattern,
			_In_ size_t patternSize,
			_In_ size_t startIndex)
	{
		if (patternSize == 0 || pBuffer->Count < patternSize || startIndex > pBuffer->Count - patternSize)
		{
			return (size_t)-1;
		}

		size_t searchLimit = pBuffer->Count - patternSize;
		for (size_t i = startIndex; i <= searchLimit; ++i)
		{
			void* pCurrent = (char*)pBuffer->pData + (i * pBuffer->ElementSize);
			if (memcmp(pCurrent, pPattern, patternSize * pBuffer->ElementSize) == 0)
			{
				return i;
			}
		}
		return (size_t)-1;
	}


	/**
	 * @brief Replaces all occurrences of a pattern with a new pattern, in-place. (Optimized Single-Pass)
	 *
	 * This function is highly efficient for pattern-based replacement and removal.
	 * It iterates through the source buffer only once.
	 *
	 * @return TRUE on success, FALSE on memory allocation failure.
	 */
	static inline BOOL
		_FC_BufferReplace(
			_Inout_ _FC_BUFFER* pBuffer,
			_In_ const void* pOldPattern,
			_In_ size_t oldPatternSize,
			_In_opt_ const void* pNewPattern,
			_In_ size_t newPatternSize)
	{
		if (oldPatternSize == 0 || pBuffer->Count == 0)
		{
			return TRUE; // Nothing to replace.
		}

		// First, check if any occurrences exist at all. If not, we can exit immediately.
		size_t firstOccurrence = _FC_BufferFind(pBuffer, pOldPattern, oldPatternSize, 0);
		if (firstOccurrence == (size_t)-1)
		{
			return TRUE; // No replacements needed.
		}

		_FC_BUFFER newBuffer;
		_FC_BufferInit(&newBuffer, pBuffer->ElementSize);

		// 1) Count total occurrences to compute final size
		size_t occurrences = 0;
		for (size_t idx = firstOccurrence;
			idx < pBuffer->Count;
			idx = _FC_BufferFind(pBuffer, pOldPattern, oldPatternSize, idx) + oldPatternSize)
		{
			if (idx - oldPatternSize >= pBuffer->Count) break;
			occurrences++;
		}

		// 2) Pre-reserve capacity, checking for overflow
		size_t delta_per = (newPatternSize > oldPatternSize)
			? (newPatternSize - oldPatternSize)
			: 0;
		size_t extra = occurrences * delta_per;
		// Prevent wrap‐around
		if (pBuffer->Count > SIZE_MAX - extra)
			goto cleanup;
		if (!_FC_BufferEnsureCapacity(&newBuffer, pBuffer->Count + extra))
			goto cleanup;

		// 3) Perform the replace loop
		size_t read_idx = 0;
		BOOL  success = TRUE;
		while (read_idx < pBuffer->Count)
		{
			// Match?
			if (read_idx + oldPatternSize <= pBuffer->Count &&
				memcmp((char*)pBuffer->pData + (read_idx * pBuffer->ElementSize),
					pOldPattern,
					oldPatternSize * pBuffer->ElementSize) == 0)
			{
				if (newPatternSize && pNewPattern)
				{
					if (!_FC_BufferAppendRange(&newBuffer, pNewPattern, newPatternSize))
					{
						success = FALSE;
						goto cleanup;
					}
				}
				read_idx += oldPatternSize;
			}
			else
			{
				void* pCurrent = (char*)pBuffer->pData + (read_idx * pBuffer->ElementSize);
				if (!_FC_BufferAppend(&newBuffer, pCurrent))
				{
					success = FALSE;
					goto cleanup;
				}
				read_idx++;
			}
		}

		// Copy any tail
		if (read_idx < pBuffer->Count)
		{
			void* pTail = (char*)pBuffer->pData + (read_idx * pBuffer->ElementSize);
			size_t tailCount = pBuffer->Count - read_idx;
			if (!_FC_BufferAppendRange(&newBuffer, pTail, tailCount))
			{
				success = FALSE;
				goto cleanup;
			}
		}

		// Swap in the new data, freeing the old
		_FC_BufferFree(pBuffer);
		*pBuffer = newBuffer;

	cleanup:
		if (!success)
			_FC_BufferFree(&newBuffer);
		return success;
	}

	// Convenience function to null-terminate and return a character buffer as a string.
	static inline char*
		_FC_BufferToString(
			_Inout_ _FC_BUFFER* pBuffer)
	{
		// Ensure it's a char buffer.
		if (pBuffer->ElementSize != sizeof(char)) return NULL;

		// Append a null terminator if not already present.
		if (pBuffer->Count == 0 || *((char*)_FC_BufferGet(pBuffer, pBuffer->Count - 1)) != '\0')
		{
			char nullChar = '\0';
			if (!_FC_BufferAppend(pBuffer, &nullChar))
			{
				// On failure, we can't return a valid string.
				_FC_BufferFree(pBuffer);
				return NULL;
			}
		}
		return (char*)pBuffer->pData;
	}

	static inline unsigned char
		_FC_ToLowerAscii(
			unsigned char Character)
	{
		if (Character >= 'A' && Character <= 'Z')
		{
			return Character + ('a' - 'A');
		}
		return Character;
	}

	static inline char*
		_FC_StringDuplicateRange(
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
		_FC_StringToLowerUnicode(
			_In_reads_(SourceLength) const char* Source,
			_In_ size_t SourceLength,
			_Out_ size_t* NewLength)
	{
		*NewLength = 0;
		char* DestBuffer = NULL;
		WCHAR* WideBuffer = NULL;
		BOOL   allocatedOnHeap = FALSE;

		if (SourceLength == 0)
		{
			// Empty input → return empty string
			return _FC_StringDuplicateRange("", 0);
		}

		// Determine required UTF-16 length
		int WideLength = MultiByteToWideChar(CP_UTF8, 0,
			Source, (int)SourceLength,
			NULL, 0);
		if (WideLength == 0)
			goto cleanup;

#define STACK_BUFFER_SIZE 512 // Use stack for up to 512 wide chars (1KB)
		WCHAR TmpStackBuffer[STACK_BUFFER_SIZE];
		WideBuffer = TmpStackBuffer;

		// If the required size is larger than our stack buffer, allocate from the heap.
		if (WideLength > STACK_BUFFER_SIZE)
		{
			WideBuffer = (WCHAR*)HeapAlloc(GetProcessHeap(), 0,
				(size_t)WideLength * sizeof(WCHAR));
			if (WideBuffer == NULL)
				goto cleanup;
			allocatedOnHeap = TRUE;
		}

		// Convert UTF-8 → UTF-16
		if (MultiByteToWideChar(CP_UTF8, 0,
			Source, (int)SourceLength,
			WideBuffer, WideLength) == 0)
			goto cleanup;

		// Lowercase in place
		CharLowerW(WideBuffer);

		// Determine required UTF-8 length
		int Utf8Length = WideCharToMultiByte(CP_UTF8, 0,
			WideBuffer, WideLength,
			NULL, 0, NULL, NULL);
		if (Utf8Length == 0)
			goto cleanup;

		// Allocate UTF-8 buffer
		DestBuffer = (char*)HeapAlloc(GetProcessHeap(), 0,
			(size_t)Utf8Length + 1);
		if (DestBuffer == NULL)
			goto cleanup;

		// Convert UTF-16 → UTF-8
		if (WideCharToMultiByte(CP_UTF8, 0,
			WideBuffer, WideLength,
			DestBuffer, Utf8Length,
			NULL, NULL) == 0)
		{
			HeapFree(GetProcessHeap(), 0, DestBuffer);
			DestBuffer = NULL;
			goto cleanup;
		}

		DestBuffer[Utf8Length] = '\0';
		*NewLength = (size_t)Utf8Length;

	cleanup:
		// Free the heap buffer if it was allocated
		if (allocatedOnHeap)
			HeapFree(GetProcessHeap(), 0, WideBuffer);

#undef STACK_BUFFER_SIZE
		return DestBuffer;
	}

	static inline UINT
		_FC_ComputeHash(
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
		_FC_HashLine(
			_In_reads_(Length) const char* String,
			_In_ size_t Length,
			_In_ const FC_CONFIG* Config)
	{
		UINT Flags = Config->Flags;

		// If case-insensitive and Unicode, lowercase using Unicode-aware conversion
		if ((Flags & FC_IGNORE_CASE) && Config->Mode == FC_MODE_TEXT_UNICODE)
		{
			size_t LowerLength;
			char* LowerString = _FC_StringToLowerUnicode(String, Length, &LowerLength);
			if (LowerString == NULL)
				return 0; // Fail-safe; hashing 0 for NULL is safe for comparison fallback
			UINT Hash = _FC_ComputeHash(LowerString, LowerLength, Flags);
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
				Temp[i] = (char)_FC_ToLowerAscii((unsigned char)String[i]);
			}
			Temp[Length] = '\0';
			Input = Temp;
		}

		UINT Hash = _FC_ComputeHash(Input, InputLen, Flags);

		if (Temp)
			HeapFree(GetProcessHeap(), 0, Temp);

		return Hash;
	}

	static void _FC_FreeLineBufferContents(_Inout_ _FC_BUFFER* pLineBuffer)
	{
		for (size_t i = 0; i < pLineBuffer->Count; ++i)
		{
			_FC_LINE* line = (_FC_LINE*)_FC_BufferGet(pLineBuffer, i);
			if (line && line->Text)
			{
				HeapFree(GetProcessHeap(), 0, line->Text);
			}
		}
		_FC_BufferFree(pLineBuffer);
	}

	static inline char*
		_FC_ExpandTabs(
			_In_reads_(SourceLength) const char* Source,
			_In_ size_t SourceLength,
			_Out_ size_t* NewLength)
	{
		const char tab = '\t';
		const char* spaces = "    ";
		const size_t TabWidth = 4;
		_FC_BUFFER buffer;

		// 1. Initialize a buffer and copy the source string into it.
		_FC_BufferInit(&buffer, sizeof(char));
		if (!_FC_BufferAppendRange(&buffer, Source, SourceLength))
		{
			*NewLength = 0;
			return NULL;
		}

		// 2. Perform the replacement in-place on the buffer.
		if (!_FC_BufferReplace(&buffer, &tab, 1, spaces, TabWidth))
		{
			// Free on failure
			_FC_BufferFree(&buffer);
			*NewLength = 0;
			return NULL;
		}

		// 3. Return the result.
		*NewLength = buffer.Count;
		return _FC_BufferToString(&buffer);
	}

	static FC_RESULT
		_FC_CompareLineArrays(
			_In_ const _FC_BUFFER* pBufferA, // Changed type
			_In_ const _FC_BUFFER* pBufferB, // Changed type
			_In_ const FC_CONFIG* Config)
	{
		if (pBufferA->Count != pBufferB->Count)
		{
			if (Config->Output)
				Config->Output(Config->UserData, "Files have different line counts", -1, -1);
			return FC_DIFFERENT;
		}
		// Compare each line one by one.
		// Why: Hash comparison is fast and covers most differences;
		// fallback to byte-wise memcmp is used only if hashes match and case must be preserved.
		for (size_t i = 0; i < pBufferA->Count; ++i)
		{
			const _FC_LINE* LineA = (_FC_LINE*)_FC_BufferGet(pBufferA, i);
			const _FC_LINE* LineB = (_FC_LINE*)_FC_BufferGet(pBufferB, i);

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


	static inline FC_RESULT
		_FC_ParseLines(
			_In_reads_(BufferLength) const char* Buffer,
			_In_ size_t BufferLength,
			_Inout_ _FC_BUFFER* pLineBuffer, // The buffer for _FC_LINE structs
			_In_ const FC_CONFIG* Config)
	{
		const char* Ptr = Buffer;
		const char* End = Buffer + BufferLength;

		while (Ptr < End)
		{
			// 1. Find the end of the current line.
			const char* Newline = Ptr;
			while (Newline < End && *Newline != '\n' && *Newline != '\r')
			{
				Newline++;
			}
			size_t LineLength = (size_t)(Newline - Ptr);

			// 2. Create a mutable buffer for the line's text.
			//    This buffer will be modified for tabs and whitespace.
			_FC_BUFFER textBuffer;
			_FC_BufferInit(&textBuffer, sizeof(char));
			if (!_FC_BufferAppendRange(&textBuffer, Ptr, LineLength))
			{
				_FC_BufferFree(&textBuffer);             // free on failure
				return FC_ERROR_MEMORY;
			}

			// 3. Perform text normalization based on config flags.

			// Handle tab expansion if FC_RAW_TABS is NOT set.
			if (!(Config->Flags & FC_RAW_TABS))
			{
				const char tab = '\t';
				const char* spaces = "    ";
				if (!_FC_BufferReplace(&textBuffer, &tab, 1, spaces, 4))
				{
					_FC_BufferFree(&textBuffer);         // free on failure
					return FC_ERROR_MEMORY;
				}
			}

			// Handle whitespace removal if FC_IGNORE_WS IS set.
			if (Config->Flags & FC_IGNORE_WS)
			{
				const char space = ' ';
				const char tab = '\t'; // remove tabs too

				if (!_FC_BufferReplace(&textBuffer, &space, 1, NULL, 0) ||
					!_FC_BufferReplace(&textBuffer, &tab, 1, NULL, 0))
				{
					_FC_BufferFree(&textBuffer);         // free on failure
					return FC_ERROR_MEMORY;
				}
			}

			// 4. Finalize the processed text from the buffer.
			size_t FinalLength = textBuffer.Count;
			char* FinalText = _FC_BufferToString(&textBuffer);

			if (FinalText == NULL)
			{
				_FC_BufferFree(&textBuffer); // Clean up the text buffer on failure.
				return FC_ERROR_MEMORY;
			}

			// 5. Hash the final, normalized line and append it to the line buffer.
			_FC_LINE line;
			line.Text = FinalText;
			line.Length = FinalLength;
			line.Hash = _FC_HashLine(FinalText, FinalLength, Config);

			if (!_FC_BufferAppend(pLineBuffer, &line))
			{
				HeapFree(GetProcessHeap(), 0, FinalText); // Free the text we just finalized.
				return FC_ERROR_MEMORY;
			}

			// 6. Advance the main pointer to the start of the next line.
			while (Newline < End && (*Newline == '\n' || *Newline == '\r'))
			{
				Newline++;
			}
			Ptr = Newline;
		}
		return FC_OK;
	}

	static inline char*
		_FC_ReadFileContents(
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
			return _FC_StringDuplicateRange("", 0);
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
		_FC_IsProbablyTextBuffer(const BYTE* Buffer, DWORD BufferLength)
	{
		const double textThreshold = 0.90;
		if (BufferLength == 0) return FALSE;

		// Detect UTF BOMs (early exit for known good encodings)
		if (BufferLength >= 3 && Buffer[0] == 0xEF && Buffer[1] == 0xBB && Buffer[2] == 0xBF) // UTF-8 BOM
			return TRUE;
		if (BufferLength >= 2 && Buffer[0] == 0xFF && Buffer[1] == 0xFE) // UTF-16 LE BOM
			return TRUE;
		if (BufferLength >= 2 && Buffer[0] == 0xFE && Buffer[1] == 0xFF) // UTF-16 BE BOM
			return TRUE;

		int printable = 0;
		for (DWORD i = 0; i < BufferLength; ++i)
		{
			BYTE c = Buffer[i];
			if ((c >= 32 && c <= 126) || c == 9 || c == 10 || c == 13) {
				printable++;
			}
			else if (c == 0) {
				return FALSE; // Null byte strongly suggests binary
			}
			// Could expand here with UTF-8 continuation byte validation, if needed
		}

		double ratio = (double)printable / BufferLength;
		return (ratio >= textThreshold);
	}

	static inline BOOL
		_FC_IsProbablyTextFileW(const WCHAR* Path) {
		HANDLE hFile = CreateFileW(
			Path,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);
		if (hFile == INVALID_HANDLE_VALUE) return FALSE;

#define bufferSize 4096

		BYTE* buffer = (BYTE*)HeapAlloc(GetProcessHeap(), 0, bufferSize);
		if (buffer == NULL)
		{
			// If heap allocation fails, we can't proceed.
			CloseHandle(hFile);
			return FALSE;
		}

		DWORD bytesRead = 0;
		BOOL success = ReadFile(hFile, buffer, bufferSize, &bytesRead, NULL);

		// Call the text-checking function.
		BOOL isText = _FC_IsProbablyTextBuffer(buffer, bytesRead);

		HeapFree(GetProcessHeap(), 0, buffer);
		CloseHandle(hFile);

		if (!success || bytesRead == 0) return FALSE;

		return isText;
	}

	static FC_RESULT
		_FC_CompareFilesText(
			_In_z_ const WCHAR* Path1,
			_In_z_ const WCHAR* Path2,
			_In_ const FC_CONFIG* Config)
	{
		FC_RESULT Result = FC_OK;
		size_t Length1 = 0, Length2 = 0;
		char* Buffer1 = NULL;
		char* Buffer2 = NULL;
		_FC_BUFFER BufferA, BufferB;

		// Initialize our generic buffers to hold _FC_LINE structs.
		_FC_BufferInit(&BufferA, sizeof(_FC_LINE));
		_FC_BufferInit(&BufferB, sizeof(_FC_LINE));

		Buffer1 = _FC_ReadFileContents(Path1, &Length1, &Result);
		if (!Buffer1) goto cleanup;

		Buffer2 = _FC_ReadFileContents(Path2, &Length2, &Result);
		if (!Buffer2) goto cleanup;

		Result = _FC_ParseLines(Buffer1, Length1, &BufferA, Config);
		if (Result != FC_OK) goto cleanup;

		Result = _FC_ParseLines(Buffer2, Length2, &BufferB, Config);
		if (Result != FC_OK) goto cleanup;

		Result = _FC_CompareLineArrays(&BufferA, &BufferB, Config);

	cleanup:
		if (Buffer1) HeapFree(GetProcessHeap(), 0, Buffer1);
		if (Buffer2) HeapFree(GetProcessHeap(), 0, Buffer2);
		// Free the buffers and their nested content.
		_FC_FreeLineBufferContents(&BufferA);
		_FC_FreeLineBufferContents(&BufferB);
		return Result;
	}

	static FC_RESULT
		_FC_CompareFilesBinary(
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
			int charsWritten;

			charsWritten = _snprintf_s(
				Message,
				sizeof(Message),
				_TRUNCATE,
				"Binary diff at offset 0x%zx",
				FirstDifference
			);

			if (charsWritten > 0)
			{
				Config->Output(Config->UserData, Message, -1, -1);
			}
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
		_FC_ToCanonicalPath(
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
		if(outPath == NULL)
		{
			// Memory allocation failed
			return FALSE;
		}
		if(outPath != NULL && outPath[0] == L'\0')
		{
			// If the path is empty after conversion, treat it as invalid.
			HeapFree(GetProcessHeap(), 0, outPath);
			return FALSE;
		}
		// Step 6: Return the canonical path
		*CanonicalPathOut = outPath;
		return TRUE;
	}

	static WCHAR* _FC_ConvertUtf8ToWide(const char* Utf8String)
	{
		if (Utf8String == NULL) return NULL;

		int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Utf8String, -1, NULL, 0);
		if (wideLength == 0) {
			return NULL; // Invalid characters or other error
		}

		WCHAR* wideBuffer = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, (size_t)wideLength * sizeof(WCHAR));
		if (wideBuffer == NULL) {
			return NULL; // Memory allocation failed
		}

		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Utf8String, -1, wideBuffer, wideLength) == 0) {
			HeapFree(GetProcessHeap(), 0, wideBuffer);
			return NULL; // Conversion failed
		}

		return wideBuffer;
	}

	static inline FC_RESULT
		_FC_CompareFilesInternal(
			_In_z_ const WCHAR* Path1,
			_In_z_ const WCHAR* Path2,
			_In_ const FC_CONFIG* Config)
	{
		switch (Config->Mode)
		{
		case FC_MODE_TEXT_ASCII:
		case FC_MODE_TEXT_UNICODE:
			// Corrected order: Path1, Path2
			return _FC_CompareFilesText(Path1, Path2, Config);

		case FC_MODE_BINARY:
			// Corrected order: Path1, Path2
			return _FC_CompareFilesBinary(Path1, Path2, Config);

		case FC_MODE_AUTO:
		default:
		{
			// Corrected order
			BOOL isText1 = _FC_IsProbablyTextFileW(Path1);
			BOOL isText2 = _FC_IsProbablyTextFileW(Path2);
			if (isText1 && isText2)
				return _FC_CompareFilesText(Path1, Path2, Config);
			else
				return _FC_CompareFilesBinary(Path1, Path2, Config);
		}
		}
	}

	//
	// Main Implementation
	//

	FC_RESULT
		FC_CompareFilesUtf8(
			_In_z_ const char* Path1Utf8,
			_In_z_ const char* Path2Utf8,
			_In_ const FC_CONFIG* Config)
	{
		FC_RESULT Result = FC_OK;
		WCHAR* WidePath1 = NULL;
		WCHAR* WidePath2 = NULL;

		if (Path1Utf8 == NULL || Path2Utf8 == NULL)
		{
			Result = FC_ERROR_INVALID_PARAM;
			goto cleanup;
		}

		// Convert paths
		WidePath1 = _FC_ConvertUtf8ToWide(Path1Utf8);
		WidePath2 = _FC_ConvertUtf8ToWide(Path2Utf8);

		if (WidePath1 == NULL || WidePath2 == NULL)
		{
			Result = (GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
				? FC_ERROR_INVALID_PARAM
				: FC_ERROR_MEMORY;
			goto cleanup;
		}

		// Chain to the primary public API, not the internal one.
		// FC_CompareFilesW will handle path validation and the actual comparison.
		Result = FC_CompareFilesW(WidePath1, WidePath2, Config);

	cleanup:
		// This function owns the wide paths, so it frees them here.
		// There is no more double-free bug.
		if (WidePath1) HeapFree(GetProcessHeap(), 0, WidePath1);
		if (WidePath2) HeapFree(GetProcessHeap(), 0, WidePath2);

		return Result;
	}

	FC_RESULT
		FC_CompareFilesW(
			_In_z_ const WCHAR* Path1,
			_In_z_ const WCHAR* Path2,
			_In_ const FC_CONFIG* Config)
	{
		FC_RESULT Result = FC_OK;
		WCHAR* CanonicalPath1 = NULL;
		WCHAR* CanonicalPath2 = NULL;

		if (!Path1 || !Path2 || !Config || !Config->Output)
		{
			Result = FC_ERROR_INVALID_PARAM;
			goto cleanup;
		}

		// Path preparation
		if (!_FC_ToCanonicalPath(Path1, &CanonicalPath1) ||
			!_FC_ToCanonicalPath(Path2, &CanonicalPath2))
		{
			Result = FC_ERROR_INVALID_PARAM;
			goto cleanup;
		}

		// Call the core logic function, which does NOT free the memory.
		Result = _FC_CompareFilesInternal(CanonicalPath1, CanonicalPath2, Config);

	cleanup:
		// This function is the owner of these pointers, so it frees them.
		if (CanonicalPath1) HeapFree(GetProcessHeap(), 0, CanonicalPath1);
		if (CanonicalPath2) HeapFree(GetProcessHeap(), 0, CanonicalPath2);

		return Result;
	}

#ifdef __cplusplus
}
#endif
