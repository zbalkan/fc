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

	/**
	 * @enum FC_RESULT
	 * @brief Defines the return codes for file comparison operations.
	 *
	 * These values indicate the outcome of a comparison, distinguishing between
	 * successful comparisons, differences found, and various types of errors.
	 */
	typedef enum
	{
		FC_OK = 0,
		FC_DIFFERENT,
		FC_ERROR_IO,
		FC_ERROR_INVALID_PARAM,
		FC_ERROR_MEMORY
	} FC_RESULT;

	/**
	 * @enum FC_MODE
	 * @brief Specifies the comparison mode to be used.
	 *
	 * This determines whether the files are treated as text, binary, or if the library
	 * should attempt to auto-detect the appropriate mode.
	 */
	typedef enum
	{
		FC_MODE_TEXT_ASCII,     // Plain text, ASCII/ANSI encoding
		FC_MODE_TEXT_UNICODE,   // Unicode text (UTF-8, UTF-16 w/ BOM)
		FC_MODE_BINARY,         // Raw binary comparison
		FC_MODE_AUTO            // Auto-detect based on file content
	} FC_MODE;

	/**
	 * @defgroup FC_FLAGS Comparison Behavior Flags
	 * @brief Flags to modify the behavior of file comparisons.
	 * @{
	 */
#define FC_IGNORE_CASE      0x0001  // Ignore case in text comparison.
#define FC_IGNORE_WS        0x0002  // Ignore whitespace in text comparison.
#define FC_SHOW_LINE_NUMS   0x0004  // Show line numbers in output.
#define FC_RAW_TABS         0x0008  // Do not expand tabs in text comparison.
	 /** @} */

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
	 * @brief Defines the function pointer for a callback that reports comparison differences.
	 *
	 * An application can implement a function of this type and pass it in the FC_CONFIG
	 * structure to receive detailed messages when a difference is found.
	 *
	 * @param UserData  A pointer to user-defined data, passed through from the FC_CONFIG struct.
	 * @param Message   A UTF-8 encoded, null-terminated string describing the difference.
	 * @param Line1     The line number in the first file where the difference occurred, or -1 if not applicable (e.g., in binary mode).
	 * @param Line2     The line number in the second file where the difference occurred, or -1 if not applicable.
	 */
	typedef void (*FC_OUTPUT_CALLBACK)(
		_In_opt_ void* UserData,
		_In_z_ const char* Message,
		_In_ int Line1,
		_In_ int Line2);

	/**
	 * @enum FC_DIFF_TYPE
	 * @brief Describes the type of a difference found between two files.
	 */
	typedef enum {
		FC_DIFF_TYPE_NONE,      /**< Should not be used. */
		FC_DIFF_TYPE_CHANGE,    /**< A block of lines was changed from file A to file B. */
		FC_DIFF_TYPE_DELETE,    /**< A block of lines from file A was deleted (not present in file B). */
		FC_DIFF_TYPE_ADD,       /**< A block of lines from file B was added (not present in file A). */
		FC_DIFF_TYPE_SIZE       /**< A special type indicating that two binary files have different sizes. */
	} FC_DIFF_TYPE;

	/**
	 * @struct FC_DIFF_BLOCK
	 * @brief Contains information about a single block of differences.
	 *
	 * For text comparisons, this describes a contiguous set of additions, deletions,
	 * or changes. The indices are 0-based and exclusive of the end.
	 */
	typedef struct {
		FC_DIFF_TYPE Type;      /**< The type of difference. */
		size_t StartA;          /**< The starting line index in file A's line buffer. */
		size_t EndA;            /**< The ending line index (exclusive) in file A's line buffer. */
		size_t StartB;          /**< The starting line index in file B's line buffer. */
		size_t EndB;            /**< The ending line index (exclusive) in file B's line buffer. */
	} FC_DIFF_BLOCK;

	/**
 * @struct _FC_BUFFER
 * @brief A generic, reusable dynamic buffer for storing contiguous elements.
 *
 * This structure provides a type-agnostic, dynamic array implementation, used
 * throughout the library to manage collections of lines (_FC_LINE) and characters (char).
 * It handles its own memory management, growing as needed.
 *
 * @internal
 */
	typedef struct
	{
		void* pData;       // Pointer to the block of memory holding the elements.
		size_t ElementSize; // The size of a single element (e.g., sizeof(char)).
		size_t Count;       // The number of elements currently in the buffer.
		size_t Capacity;    // The number of elements the buffer can hold before resizing.
	} _FC_BUFFER;

	/**
	 * @struct FC_USER_CONTEXT
	 * @brief Provides the callback with full context about the comparison operation.
	 *
	 * This structure is passed to the `FC_DIFF_CALLBACK` and gives it access to the
	 * original file paths, the parsed line buffers, and any user-defined data.
	 */
	typedef struct {
		const WCHAR* Path1;         /**< The canonical path to the first file. */
		const WCHAR* Path2;         /**< The canonical path to the second file. */
		const _FC_BUFFER* Lines1;   /**< A pointer to the buffer of _FC_LINE structs for file 1. */
		const _FC_BUFFER* Lines2;   /**< A pointer to the buffer of _FC_LINE structs for file 2. */
		void* UserData;             /**< The user-defined data pointer from FC_CONFIG. */
	} FC_USER_CONTEXT;

	/**
	 * @brief Defines the function pointer for a callback that reports structured differences.
	 *
	 * An application implements a function of this type to receive detailed information
	 * about each block of differences found by the comparison algorithm.
	 *
	 * @param Context A pointer to the FC_USER_CONTEXT, providing full access to file paths and line data.
	 * @param Block   A pointer to the FC_DIFF_BLOCK, describing the specific difference found.
	 */
	typedef void (*FC_DIFF_CALLBACK)(_In_ const FC_USER_CONTEXT* Context, _In_ const FC_DIFF_BLOCK* Block);

	/**
		 * @struct FC_CONFIG
		 * @brief Holds the configuration settings for a file comparison operation.
		 *
		 * An instance of this structure must be initialized and passed to the main
		 * comparison functions to control their behavior.
		 */
	typedef struct
	{
		FC_MODE Mode;                   /**< Text, binary, or auto-detection mode. */
		UINT Flags;                     /**< Option flags from FC_* defines. */
		UINT ResyncLines;               /**< The number of matching lines to declare a resynchronization. */
		UINT BufferLines;               /**< Reserved for future use. */
		FC_DIFF_CALLBACK DiffCallback;  /**< The mandatory callback function for receiving structured diff reports. */
		void* UserData;                 /**< A user-defined pointer passed to the callback function's context. */
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
	 * @param Path1 Path to the first file, UTF-8 encoded.
	 * @param Path2 Path to the second file, UTF-8 encoded.
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

	static const WCHAR* const g_ReservedDevices[] = {
		L"CON", L"PRN", L"AUX", L"NUL",
		L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
		L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"
	};

	/**
	 * @struct _FC_LINE
	 * @brief Internal representation of a single line of text from a file.
	 *
	 * This structure stores the processed text of a line, its length, and a pre-computed
	 * hash value for fast comparisons. The text is normalized according to the
	 * active comparison flags (e.g., whitespace removal, tab expansion).
	 *
	 * @internal
	 */
	typedef struct
	{
		char* Text;
		size_t Length;
		UINT Hash;
	} _FC_LINE;

	/**
	 * @struct _FC_MATCH
	 * @brief Represents a potential match of a line from file A in file B.
	 *
	 * This is used to build a singly-linked list of all occurrences of a line
	 * with a specific hash in file B.
	 * @internal
	 */
	typedef struct _FC_MATCH {
		size_t IndexInB;         /**< The 0-based line index in file B. */
		struct _FC_MATCH* Next;  /**< Pointer to the next match with the same hash. */
	} _FC_MATCH;

	/**
	 * @struct _FC_LCS_CONTEXT
	 * @brief Holds the intermediate data structures for the Hunt-McIlroy LCS algorithm.
	 * @internal
	 */
	typedef struct {
		size_t* Thresholds;     /**< Stores the smallest ending line index in B for an LCS of a given length. */
		size_t* PredecessorsA;  /**< For each line in A, stores the previous line's index in B from the LCS path. */
		size_t* PredecessorsB;  /**< For each line in A, stores its own line index in B if it's part of a potential LCS path. */
	} _FC_LCS_CONTEXT;

	/**
	 * @struct _FC_HASH_MAP_ENTRY
	 * @brief An entry in the hash map's bucket list.
	 *
	 * Contains the hash value, a pointer to the head of the match list for that hash,
	 * and a pointer to the next entry in the same bucket (to handle collisions).
	 * @internal
	 */
	typedef struct _FC_HASH_MAP_ENTRY {
		UINT Hash;
		_FC_MATCH* MatchHead;
		struct _FC_HASH_MAP_ENTRY* Next;
	} _FC_HASH_MAP_ENTRY;

	/**
	 * @struct _FC_HASH_MAP
	 * @brief A simple hash map implementation for finding line matches.
	 *
	 * Uses open addressing with chaining for collision resolution. It pre-allocates
	 * all necessary entry and match nodes from pools for performance, avoiding
	 * repeated small allocations.
	 * @internal
	 */
	typedef struct {
		_FC_HASH_MAP_ENTRY** Buckets;      /**< Array of pointers to bucket chains. */
		size_t NumBuckets;                 /**< The number of buckets in the hash table. */
		_FC_HASH_MAP_ENTRY* EntryPool;     /**< A pre-allocated pool of hash map entries. */
		size_t EntryPoolIndex;             /**< The next available index in the entry pool. */
	} _FC_HASH_MAP;

	/**
	 * @brief Creates and initializes a hash map.
	 * @internal
	 * @param Map A pointer to the _FC_HASH_MAP structure to initialize.
	 * @param InitialCapacity The number of entries to pre-allocate in the pool.
	 * @return TRUE on success, FALSE on memory allocation failure.
	 */
	static BOOL _FC_HashMapCreate(_Inout_ _FC_HASH_MAP* Map, _In_ size_t InitialCapacity) {
		Map->NumBuckets = 1021; // A reasonably sized prime number
		Map->Buckets = (_FC_HASH_MAP_ENTRY**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Map->NumBuckets * sizeof(_FC_HASH_MAP_ENTRY*));
		if (!Map->Buckets) return FALSE;
		Map->EntryPool = (_FC_HASH_MAP_ENTRY*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InitialCapacity * sizeof(_FC_HASH_MAP_ENTRY));
		if (!Map->EntryPool) { HeapFree(GetProcessHeap(), 0, Map->Buckets); return FALSE; }
		Map->EntryPoolIndex = 0;
		return TRUE;
	}

	/**
	 * @brief Frees all memory associated with a hash map.
	 * @internal
	 * @param Map A pointer to the _FC_HASH_MAP to free.
	 */
	static void _FC_HashMapFree(_Inout_ _FC_HASH_MAP* Map) {
		if (Map->Buckets) HeapFree(GetProcessHeap(), 0, Map->Buckets);
		if (Map->EntryPool) HeapFree(GetProcessHeap(), 0, Map->EntryPool);
	}

	/**
	 * @brief Finds an entry in the hash map by its hash value.
	 * @internal
	 * @param Map A pointer to the hash map.
	 * @param Hash The hash value to find.
	 * @return A pointer to the found _FC_HASH_MAP_ENTRY, or NULL if not found.
	 */
	static _FC_HASH_MAP_ENTRY* _FC_HashMapFind(_In_ _FC_HASH_MAP* Map, _In_ UINT Hash) {
		size_t bucketIndex = Hash % Map->NumBuckets;
		_FC_HASH_MAP_ENTRY* entry = Map->Buckets[bucketIndex];
		while (entry) { if (entry->Hash == Hash) return entry; entry = entry->Next; }
		return NULL;
	}

	/**
	 * @brief Inserts a new hash value into the map, or finds the existing entry.
	 * @internal
	 * @param Map A pointer to the hash map.
	 * @param Hash The hash value to insert.
	 * @return A pointer to the new or existing _FC_HASH_MAP_ENTRY, or NULL on failure.
	 */
	static _FC_HASH_MAP_ENTRY* _FC_HashMapInsert(_Inout_ _FC_HASH_MAP* Map, _In_ UINT Hash) {
		_FC_HASH_MAP_ENTRY* entry = _FC_HashMapFind(Map, Hash);
		if (entry) return entry;
		entry = &Map->EntryPool[Map->EntryPoolIndex++];
		entry->Hash = Hash;
		entry->MatchHead = NULL;
		size_t bucketIndex = Hash % Map->NumBuckets;
		entry->Next = Map->Buckets[bucketIndex];
		Map->Buckets[bucketIndex] = entry;
		return entry;
	}

	/**
	 * @brief Initializes a new, empty buffer for elements of a specific size.
	 * @internal
	 * @param pBuffer A pointer to the _FC_BUFFER to initialize.
	 * @param elementSize The size in bytes of each element the buffer will hold.
	 */
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

	/**
	 * @brief Frees the internal memory block owned by the buffer.
	 * @internal
	 * @note This function does not free any nested pointers within the elements themselves.
	 *       The caller is responsible for freeing such data before calling this.
	 * @param pBuffer A pointer to the _FC_BUFFER to free.
	 */
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

	/**
	 * @brief Ensures the buffer has enough capacity for a specified number of new elements.
	 *
	 * If the current capacity is insufficient, it reallocates the internal memory,
	 * typically doubling the size.
	 * @internal
	 * @param pBuffer A pointer to the _FC_BUFFER.
	 * @param additionalCount The number of additional elements to make space for.
	 * @return TRUE if the capacity is sufficient or reallocation succeeded, FALSE on failure.
	 */
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

	/**
	 * @brief Appends a single element to the end of the buffer.
	 * @internal
	 * @param pBuffer A pointer to the _FC_BUFFER.
	 * @param pElement A pointer to the element to be copied into the buffer.
	 * @return TRUE on success, FALSE on memory allocation failure.
	 */
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

	/**
	 * @brief Appends a range of elements from an array to the end of the buffer.
	 * @internal
	 * @param pBuffer A pointer to the _FC_BUFFER.
	 * @param pElements A pointer to the first element in the array to append.
	 * @param count The number of elements to append from the array.
	 * @return TRUE on success, FALSE on memory allocation failure.
	 */
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

	/**
	 * @brief Retrieves a pointer to the element at a specific index.
	 * @internal
	 * @param pBuffer A pointer to the _FC_BUFFER.
	 * @param index The zero-based index of the element to retrieve.
	 * @return A pointer to the element, or NULL if the index is out of bounds.
	 */
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
	 * @brief Finds the first occurrence of a pattern of elements within a buffer.
	 * @internal
	 * @param pBuffer The buffer to search within.
	 * @param pPattern A pointer to the pattern of elements to find.
	 * @param patternSize The number of elements in the pattern.
	 * @param startIndex The index in the buffer from which to start the search.
	 * @return The starting index of the found pattern, or (size_t)-1 if the pattern is not found.
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
	 * @brief Replaces all occurrences of a pattern with a new pattern.
	 *
	 * This function performs a single-pass replacement. It first calculates the
	 * required size for the new buffer, allocates it, and then iterates through the
	 * source buffer, copying non-matching parts and inserting the new pattern where
	 * matches occur. If the new pattern is NULL or its size is 0, it effectively
	 * removes all occurrences of the old pattern.
	 * @internal
	 * @param pBuffer A pointer to the _FC_BUFFER to modify. The original buffer's data is freed and replaced.
	 * @param pOldPattern A pointer to the sequence of elements to be replaced.
	 * @param oldPatternSize The number of elements in the old pattern.
	 * @param pNewPattern A pointer to the new sequence of elements to insert. Can be NULL to indicate removal.
	 * @param newPatternSize The number of elements in the new pattern.
	 * @return TRUE on success, FALSE on memory allocation or parameter validation failure.
	 */
	static inline BOOL
		_FC_BufferReplace(
			_Inout_ _FC_BUFFER* pBuffer,
			_In_ const void* pOldPattern,
			_In_ size_t oldPatternSize,
			_In_opt_ const void* pNewPattern,
			_In_ size_t newPatternSize)
	{
		// 1. Defensive Input Validation
		if (!pBuffer || !pOldPattern)
			return FALSE;
		if (newPatternSize > 0 && !pNewPattern)
			return FALSE;
		if (pBuffer->Count > 0 && !pBuffer->pData) // Check for inconsistent buffer state
			return FALSE;
		if (pBuffer->ElementSize == 0 && pBuffer->Count > 0) // Reject degenerate buffer
			return FALSE;

		if (oldPatternSize == 0 || pBuffer->Count == 0)
			return TRUE;

		size_t count = 0;
		// Pass 1: count occurrences
		for (size_t i = 0; i + oldPatternSize <= pBuffer->Count; )
		{
			if (memcmp((char*)pBuffer->pData + i * pBuffer->ElementSize,
				pOldPattern,
				oldPatternSize * pBuffer->ElementSize) == 0)
			{
				if (count == SIZE_MAX)
					return FALSE; // Too many occurrences to count
				count++;
				i += oldPatternSize;
			}
			else
			{
				i++;
			}
		}

		if (count == 0)
			return TRUE;

		// 2. Safely calculate new buffer size using pure unsigned arithmetic
		size_t newCount;
		if (newPatternSize >= oldPatternSize) {
			// Growth path
			size_t delta = newPatternSize - oldPatternSize;
			if (delta > 0 && count > (SIZE_MAX - pBuffer->Count) / delta)
				return FALSE; // Overflow
			newCount = pBuffer->Count + count * delta;
		}
		else {
			// Shrink path
			size_t delta = oldPatternSize - newPatternSize;
			size_t totalShrink = count * delta;
			if (totalShrink > pBuffer->Count)
				return FALSE;  // Logic error: cannot shrink more than the buffer size
			newCount = pBuffer->Count - totalShrink;
		}

		_FC_BUFFER newBuf;
		_FC_BufferInit(&newBuf, pBuffer->ElementSize);

		// Handle the edge case of replacing with nothing, resulting in an empty buffer
		if (newCount == 0)
		{
			_FC_BufferFree(pBuffer);
			*pBuffer = newBuf; // newBuf is already an empty, valid buffer
			return TRUE;
		}

		// 3. Check for allocation size overflow before allocating
		if (pBuffer->ElementSize > 0 && newCount > SIZE_MAX / pBuffer->ElementSize)
			return FALSE;
		size_t newSizeInBytes = newCount * pBuffer->ElementSize;

		newBuf.pData = HeapAlloc(GetProcessHeap(), 0, newSizeInBytes);
		if (!newBuf.pData)
			return FALSE;
		newBuf.Capacity = newCount;

		// Pass 2: build the new buffer
		size_t read_idx = 0, write_idx = 0;
		const size_t elemSize = pBuffer->ElementSize;
		char* const dstBase = (char*)newBuf.pData;
		const char* const srcBase = (char*)pBuffer->pData;

		while (read_idx < pBuffer->Count)
		{
			if (read_idx + oldPatternSize <= pBuffer->Count &&
				memcmp(srcBase + read_idx * elemSize, pOldPattern, oldPatternSize * elemSize) == 0)
			{
				if (newPatternSize > 0)
				{
					if (write_idx + newPatternSize > newBuf.Capacity) goto cleanup;
					memcpy(dstBase + write_idx * elemSize, pNewPattern, newPatternSize * elemSize);
					write_idx += newPatternSize;
				}
				read_idx += oldPatternSize;
			}
			else
			{
				if (write_idx + 1 > newBuf.Capacity) goto cleanup;
				memcpy(dstBase + write_idx * elemSize, srcBase + read_idx * elemSize, elemSize);
				write_idx++;
				read_idx++;
			}
		}
		newBuf.Count = write_idx;

		// Swap in the new buffer
		HeapFree(GetProcessHeap(), 0, pBuffer->pData);
		*pBuffer = newBuf;
		return TRUE;

	cleanup:
		// This cleanup path is a safeguard against logic errors in the write loop
		HeapFree(GetProcessHeap(), 0, newBuf.pData);
		return FALSE;
	}

	/**
	 * @brief Converts a character buffer to a null-terminated string.
	 *
	 * Ensures the buffer is of type `char`, appends a null terminator if one is not
	 * already present, and returns a pointer to the raw data.
	 * @internal
	 * @note On failure, this function may free the buffer's internal data.
	 * @param pBuffer A pointer to the character `_FC_BUFFER`.
	 * @return A pointer to the null-terminated string, or NULL on failure or if the buffer is not a char buffer.
	 */
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

	/**
	 * @brief Converts an ASCII character to its lowercase equivalent.
	 * @internal
	 * @param Character The input character.
	 * @return The lowercase version of the character if it's an uppercase letter, otherwise the character itself.
	 */
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

	/**
	 * @brief Creates a new, null-terminated heap-allocated string from a character range.
	 * @internal
	 * @param String A pointer to the source character array.
	 * @param Length The number of characters to copy from the source.
	 * @return A pointer to the new heap-allocated string, or NULL on failure. The caller must free this memory.
	 */
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

	/**
	 * @brief Converts a UTF-8 encoded string to its lowercase equivalent.
	 *
	 * This function handles multi-byte UTF-8 characters correctly by converting the
	 * string to UTF-16, using the Windows `CharLowerW` function, and then converting
	 * it back to a new UTF-8 string.
	 * @internal
	 * @param Source A pointer to the source UTF-8 string.
	 * @param SourceLength The length in bytes of the source string.
	 * @param[out] NewLength A pointer to a size_t that will receive the length of the new lowercase string.
	 * @return A pointer to a new, heap-allocated, null-terminated lowercase UTF-8 string, or NULL on failure. The caller must free this memory.
	 */
	static inline char*
		_FC_StringToLowerUnicode(
			_In_reads_(SourceLength) const char* Source,
			_In_ size_t SourceLength,
			_Out_ size_t* NewLength)
	{
		*NewLength = 0;
		char* DestBuffer = NULL;
		WCHAR* WideBuffer = NULL;

		if (SourceLength == 0)
		{
			// Empty input → return empty string
			return _FC_StringDuplicateRange("", 0);
		}

		// The MultiByteToWideChar API uses an int for length, so we must respect that limit.
		if (SourceLength > INT_MAX)
		{
			return NULL; // Input too large for the API.
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
			if ((size_t)WideLength > SIZE_MAX / sizeof(WCHAR)) // Check for overflow
				goto cleanup;
			WideBuffer = (WCHAR*)HeapAlloc(GetProcessHeap(), 0,
				(size_t)WideLength * sizeof(WCHAR));
			if (WideBuffer == NULL)
				goto cleanup;
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
		if (WideBuffer != TmpStackBuffer && WideBuffer != NULL)
			HeapFree(GetProcessHeap(), 0, WideBuffer);

#undef STACK_BUFFER_SIZE
		return DestBuffer;
	}

	/**
	 * @brief Computes a hash for a character string using a simple algorithm (djb2-like).
	 *
	 * This function can optionally ignore case and whitespace for ASCII strings.
	 * @internal
	 * @param String The string to hash.
	 * @param Length The length of the string.
	 * @param Flags A bitmask of comparison flags (FC_IGNORE_CASE, FC_IGNORE_WS).
	 * @return The computed 32-bit hash value.
	 */
	static inline UINT
		_FC_ComputeHash(
			_In_reads_(Length) const char* String,
			_In_ size_t Length,
			_In_ UINT Flags)
	{
		UINT Hash = 0;
		BOOL const ignoreCase = (Flags & FC_IGNORE_CASE);
		BOOL const ignoreWs = (Flags & FC_IGNORE_WS);

		for (size_t i = 0; i < Length; ++i)
		{
			unsigned char Character = (unsigned char)String[i];
			if (ignoreWs && (Character == ' ' || Character == '\t'))
				continue;
			if (ignoreCase)
				Character = _FC_ToLowerAscii(Character);
			Hash = Hash * 31 + Character;
		}
		return Hash;
	}

	/**
	 * @brief Computes a hash for a line, handling Unicode case-insensitivity if required.
	 *
	 * This is a wrapper around `_FC_ComputeHash`. If Unicode case-insensitivity is
	 * requested, it first converts the line to lowercase using the Unicode-aware
	 * `_FC_StringToLowerUnicode` function before hashing. Otherwise, it calls
	 * `_FC_ComputeHash` directly.
	 * @internal
	 * @param String The line's text to hash.
	 * @param Length The length of the line's text.
	 * @param Config A pointer to the main comparison configuration.
	 * @return The computed hash value for the line.
	 */
	static inline UINT
		_FC_HashLine(
			_In_reads_(Length) const char* String,
			_In_ size_t Length,
			_In_ const FC_CONFIG* Config)
	{
		UINT Flags = Config->Flags;

		// If case-insensitive and Unicode, we must use the slower, Unicode-aware path.
		if ((Flags & FC_IGNORE_CASE) && Config->Mode == FC_MODE_TEXT_UNICODE)
		{
			size_t LowerLength;
			char* LowerString = _FC_StringToLowerUnicode(String, Length, &LowerLength);
			if (LowerString == NULL)
				return 0; // Fail-safe; hashing 0 for NULL is safe for comparison fallback

			// We pass the original flags, but the string is already lowercase, so
			// the case-insensitivity path in _FC_ComputeHash will be redundant but harmless.
			UINT Hash = _FC_ComputeHash(LowerString, LowerLength, Flags);
			HeapFree(GetProcessHeap(), 0, LowerString);
			return Hash;
		}

		// For all other cases (ASCII, or case-sensitive), compute the hash directly.
		// _FC_ComputeHash now handles ASCII case-insensitivity on the fly.
		return _FC_ComputeHash(String, Length, Flags);
	}

	/**
	 * @brief Frees all memory associated with the lines stored in a line buffer.
	 *
	 * This iterates through each `_FC_LINE` in the buffer, frees the `Text` pointer
	 * for each line, and then frees the buffer's main data block itself.
	 * @internal
	 * @param pLineBuffer A pointer to the `_FC_BUFFER` containing `_FC_LINE` elements.
	 */
	static void
		_FC_FreeLineBufferContents(_Inout_ _FC_BUFFER* pLineBuffer)
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

	/**
	 * @brief Filters an LCS result to enforce a resynchronization threshold.
	 *
	 * This function processes an optimal LCS result and keeps only "stable anchors"—
	 * runs of consecutive matches that are at least `ResyncLines` long. This emulates
	 * the behavior of fc.exe's /nnnn switch by discarding shorter, potentially
	 * coincidental matches and consolidating differences into larger blocks.
	 * @internal
	 *
	 * @param LcsA              The array of LCS indices for file A.
	 * @param LcsB              The array of LCS indices for file B.
	 * @param LcsLength         The length of the input LCS arrays.
	 * @param ResyncLines       The minimum number of consecutive lines to be considered a stable anchor.
	 * @param[out] pFilteredLcsA A pointer to receive the new, heap-allocated filtered array of indices for file A. The caller must free this.
	 * @param[out] pFilteredLcsB A pointer to receive the new, heap-allocated filtered array of indices for file B. The caller must free this.
	 * @return The length of the new filtered LCS. Returns 0 on memory allocation failure.
	 */
	static size_t
		_FC_FilterLcsForResync(
			_In_ const size_t* LcsA,
			_In_ const size_t* LcsB,
			_In_ size_t LcsLength,
			_In_ UINT ResyncLines,
			_Outptr_result_buffer_maybenull_(*pFilteredLcsA) size_t** pFilteredLcsA,
			_Outptr_result_buffer_maybenull_(*pFilteredLcsB) size_t** pFilteredLcsB)
	{
		*pFilteredLcsA = NULL;
		*pFilteredLcsB = NULL;

		// If no resync threshold is set (or is 1), or if there's nothing to filter,
		// just make a copy of the original LCS.
		if (LcsLength == 0 || ResyncLines <= 1)
		{
			if (LcsLength > 0)
			{
				*pFilteredLcsA = (size_t*)HeapAlloc(GetProcessHeap(), 0, LcsLength * sizeof(size_t));
				*pFilteredLcsB = (size_t*)HeapAlloc(GetProcessHeap(), 0, LcsLength * sizeof(size_t));
				if (!*pFilteredLcsA || !*pFilteredLcsB) return 0; // Allocation failed
				memcpy(*pFilteredLcsA, LcsA, LcsLength * sizeof(size_t));
				memcpy(*pFilteredLcsB, LcsB, LcsLength * sizeof(size_t));
			}
			return LcsLength;
		}

		_FC_BUFFER FilteredA, FilteredB;
		_FC_BufferInit(&FilteredA, sizeof(size_t));
		_FC_BufferInit(&FilteredB, sizeof(size_t));

		for (size_t i = 0; i < LcsLength; )
		{
			size_t runStart = i;
			size_t runEnd = i;
			// Find the end of the current run of consecutive matching lines.
			while (runEnd + 1 < LcsLength &&
				LcsA[runEnd + 1] == LcsA[runEnd] + 1 &&
				LcsB[runEnd + 1] == LcsB[runEnd] + 1)
			{
				runEnd++;
			}

			size_t runLength = (runEnd - runStart) + 1;
			if (runLength >= ResyncLines)
			{
				// This run is a stable anchor, so keep it.
				if (!_FC_BufferAppendRange(&FilteredA, &LcsA[runStart], runLength) ||
					!_FC_BufferAppendRange(&FilteredB, &LcsB[runStart], runLength))
				{
					// On failure, free what we've allocated and return 0.
					_FC_BufferFree(&FilteredA);
					_FC_BufferFree(&FilteredB);
					return 0;
				}
			}

			i = runEnd + 1;
		}

		// Transfer ownership of the buffer's data to the output pointers.
		*pFilteredLcsA = (size_t*)FilteredA.pData;
		*pFilteredLcsB = (size_t*)FilteredB.pData;
		return FilteredA.Count;
	}

	/**
	 * @brief Processes the final LCS result to report differences via the callback.
	 * @internal
	 * @param Context The user context containing file paths and line buffers.
	 * @param Config The main comparison configuration, containing the callback pointer.
	 * @param LcsA Array of matching line indices from file A.
	 * @param LcsB Array of matching line indices from file B.
	 * @param LcsLength The number of matching lines in the LCS.
	 * @return FC_OK if files are identical, FC_DIFFERENT otherwise.
	 */
	static FC_RESULT
		_FC_ProcessLcs(
			_In_ const FC_USER_CONTEXT* Context,
			_In_ const FC_CONFIG* Config, // Add this parameter
			_In_ const size_t* LcsA,
			_In_ const size_t* LcsB,
			_In_ size_t LcsLength)
	{
		const _FC_BUFFER* pBufferA = Context->Lines1;
		const _FC_BUFFER* pBufferB = Context->Lines2;

		if (LcsLength == pBufferA->Count && LcsLength == pBufferB->Count) return FC_OK;

		size_t IndexA = 0, IndexB = 0;
		for (size_t i = 0; i <= LcsLength; ++i) {
			size_t LcsLineA = (i < LcsLength) ? LcsA[i] : pBufferA->Count;
			size_t LcsLineB = (i < LcsLength) ? LcsB[i] : pBufferB->Count;

			BOOL hasAdds = (IndexB < LcsLineB);
			BOOL hasDeletes = (IndexA < LcsLineA);

			if (hasAdds || hasDeletes) {
				FC_DIFF_BLOCK block = { 0 };
				if (hasAdds && hasDeletes) block.Type = FC_DIFF_TYPE_CHANGE;
				else if (hasAdds) block.Type = FC_DIFF_TYPE_ADD;
				else block.Type = FC_DIFF_TYPE_DELETE;

				block.StartA = IndexA;
				block.EndA = LcsLineA;
				block.StartB = IndexB;
				block.EndB = LcsLineB;
				// CORRECTED: Call the callback from the Config struct, not the Context.
				Config->DiffCallback(Context, &block);
			}
			IndexA = LcsLineA + 1;
			IndexB = LcsLineB + 1;
		}
		return FC_DIFFERENT;
	}

	/**
		 * @brief Implements the Hunt-McIlroy algorithm to find the Longest Common Subsequence.
		 * @internal
		 * @param Context The user context containing file paths, line buffers, and user data.
		 * @param Config The main comparison	configuration.
		 * @return FC_OK if files are identical, FC_DIFFERENT if they differ, or an error code.
		 */
	static FC_RESULT
		_FC_FindLcs(_In_ const FC_USER_CONTEXT* Context, _In_ const FC_CONFIG* Config) {
		const _FC_BUFFER* pBufferA = Context->Lines1;
		const _FC_BUFFER* pBufferB = Context->Lines2;

		FC_RESULT Result = FC_OK;
		_FC_LCS_CONTEXT Ctx = { 0 };
		_FC_MATCH* MatchPool = NULL;
		_FC_HASH_MAP MapB = { 0 };

		size_t* LcsA = NULL;
		size_t* LcsB = NULL;
		size_t* FilteredLcsA = NULL;
		size_t* FilteredLcsB = NULL;

		if (pBufferA->Count == 0 && pBufferB->Count == 0) return FC_OK;
		if (pBufferA->Count > 0 && pBufferB->Count == 0) return FC_DIFFERENT;
		if (pBufferA->Count == 0 && pBufferB->Count > 0) return FC_DIFFERENT;

		if (!_FC_HashMapCreate(&MapB, pBufferB->Count)) return FC_ERROR_MEMORY;

		MatchPool = (_FC_MATCH*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pBufferB->Count * sizeof(_FC_MATCH));
		if (!MatchPool) { Result = FC_ERROR_MEMORY; goto cleanup; }

		for (size_t i = 0; i < pBufferB->Count; ++i) {
			UINT hash = ((_FC_LINE*)_FC_BufferGet(pBufferB, i))->Hash;
			_FC_HASH_MAP_ENTRY* entry = _FC_HashMapInsert(&MapB, hash);
			_FC_MATCH* newMatch = &MatchPool[i];
			newMatch->IndexInB = i;
			newMatch->Next = entry->MatchHead;
			entry->MatchHead = newMatch;
		}

		Ctx.Thresholds = (size_t*)HeapAlloc(GetProcessHeap(), 0, (pBufferA->Count + 1) * sizeof(size_t));
		Ctx.PredecessorsA = (size_t*)HeapAlloc(GetProcessHeap(), 0, pBufferA->Count * sizeof(size_t));
		Ctx.PredecessorsB = (size_t*)HeapAlloc(GetProcessHeap(), 0, pBufferA->Count * sizeof(size_t));
		if (!Ctx.Thresholds || !Ctx.PredecessorsA || !Ctx.PredecessorsB) { Result = FC_ERROR_MEMORY; goto cleanup; }

		size_t LcsLength = 0;
		Ctx.Thresholds[0] = (size_t)-1;
		for (size_t i = 1; i <= pBufferA->Count; ++i) Ctx.Thresholds[i] = SIZE_MAX;

		for (size_t i = 0; i < pBufferA->Count; ++i) {
			UINT hashA = ((_FC_LINE*)_FC_BufferGet(pBufferA, i))->Hash;
			_FC_HASH_MAP_ENTRY* entry = _FC_HashMapFind(&MapB, hashA);
			if (entry) {
				for (_FC_MATCH* match = entry->MatchHead; match != NULL; match = match->Next) {
					size_t k = 0, low = 1, high = LcsLength;
					while (low <= high) {
						size_t mid = low + (high - low) / 2;
						if (match->IndexInB > Ctx.Thresholds[mid]) low = mid + 1;
						else high = mid - 1;
					}
					k = low;

					if (match->IndexInB < Ctx.Thresholds[k]) {
						Ctx.Thresholds[k] = match->IndexInB;
						Ctx.PredecessorsA[i] = (k > 1) ? Ctx.Thresholds[k - 1] : (size_t)-1;
						Ctx.PredecessorsB[i] = match->IndexInB;
						if (k > LcsLength) LcsLength = k;
					}
				}
			}
		}

		if (LcsLength > 0) {
			LcsA = (size_t*)HeapAlloc(GetProcessHeap(), 0, LcsLength * sizeof(size_t));
			LcsB = (size_t*)HeapAlloc(GetProcessHeap(), 0, LcsLength * sizeof(size_t));
			if (!LcsA || !LcsB) { Result = FC_ERROR_MEMORY; goto cleanup; }

			size_t currentB = Ctx.Thresholds[LcsLength];
			size_t currentA = 0;
			for (size_t i = pBufferA->Count - 1; i != (size_t)-1; --i) {
				if (Ctx.PredecessorsB[i] == currentB) {
					currentA = i;
					break;
				}
			}

			for (size_t i = LcsLength; i > 0; --i) {
				LcsA[i - 1] = currentA;
				LcsB[i - 1] = currentB;
				size_t prevB = Ctx.PredecessorsA[currentA];
				currentB = prevB;
				if (currentB == (size_t)-1) break;

				for (size_t j = currentA - 1; j != (size_t)-1; --j) {
					if (Ctx.PredecessorsB[j] == currentB) {
						currentA = j;
						break;
					}
				}
			}
		}

		if (LcsLength == pBufferA->Count && LcsLength == pBufferB->Count) Result = FC_OK;
		else {
			size_t FilteredLcsLength = _FC_FilterLcsForResync(
				LcsA, LcsB, LcsLength, Config->ResyncLines, &FilteredLcsA, &FilteredLcsB);

			if (LcsLength > 0 && FilteredLcsLength == 0 && (!FilteredLcsA || !FilteredLcsB))
			{
				// This indicates a memory allocation failure inside the filter function.
				Result = FC_ERROR_MEMORY;
				goto cleanup;
			}

			// Process the (potentially filtered) results to show differences.
			Result = _FC_ProcessLcs(Context, Config, FilteredLcsA, FilteredLcsB, FilteredLcsLength);
		}
	cleanup:
		_FC_HashMapFree(&MapB);
		if (MatchPool) HeapFree(GetProcessHeap(), 0, MatchPool);
		if (Ctx.Thresholds) HeapFree(GetProcessHeap(), 0, Ctx.Thresholds);
		if (Ctx.PredecessorsA) HeapFree(GetProcessHeap(), 0, Ctx.PredecessorsA);
		if (Ctx.PredecessorsB) HeapFree(GetProcessHeap(), 0, Ctx.PredecessorsB);
		if (LcsA) HeapFree(GetProcessHeap(), 0, LcsA);
		if (LcsB) HeapFree(GetProcessHeap(), 0, LcsB);
		// The filtered arrays are now owned by these pointers, so we must free them.
		if (FilteredLcsA) HeapFree(GetProcessHeap(), 0, FilteredLcsA);
		if (FilteredLcsB) HeapFree(GetProcessHeap(), 0, FilteredLcsB);
		return Result;
	}

	/**
	 * @brief Parses a raw memory buffer into a structured array of lines.
	 *
	 * This function iterates through the input buffer, identifying line breaks. For each
	 * line, it performs normalization based on the `FC_CONFIG` flags (e.g., tab expansion,
	 * whitespace removal), computes a hash of the normalized text, and stores the result
	 * as an `_FC_LINE` in the output buffer.
	 * @internal
	 * @param Buffer The raw character buffer containing the file's content.
	 * @param BufferLength The length of the raw buffer.
	 * @param[out] pLineBuffer The output buffer where `_FC_LINE` structs will be stored.
	 * @param Config A pointer to the comparison configuration.
	 * @return FC_OK on success, or FC_ERROR_MEMORY on allocation failure.
	 */
	static inline FC_RESULT
		_FC_ParseLines(
			_In_reads_(BufferLength) const char* Buffer,
			_In_ size_t BufferLength,
			_Inout_ _FC_BUFFER* pLineBuffer,
			_In_ const FC_CONFIG* Config)
	{
		const char* Ptr = Buffer;
		const char* End = Buffer + BufferLength;

		while (Ptr <= End)
		{
			// This condition handles the last line if the file doesn't end with a newline.
			if (Ptr == End)
			{
				if (BufferLength == 0 || Buffer[BufferLength - 1] == '\n' || Buffer[BufferLength - 1] == '\r')
				{
					break; // Don't process a zero-length line at the very end.
				}
			}

			const char* Newline = Ptr;
			while (Newline < End && *Newline != '\n' && *Newline != '\r')
			{
				Newline++;
			}
			size_t LineLength = (size_t)(Newline - Ptr);

			_FC_BUFFER textBuffer;
			_FC_BufferInit(&textBuffer, sizeof(char));
			if (!_FC_BufferAppendRange(&textBuffer, Ptr, LineLength))
			{
				_FC_BufferFree(&textBuffer);
				return FC_ERROR_MEMORY;
			}

			if (!(Config->Flags & FC_RAW_TABS))
			{
				const char tab = '\t';
				const char* spaces = "    ";
				if (!_FC_BufferReplace(&textBuffer, &tab, 1, spaces, 4))
				{
					_FC_BufferFree(&textBuffer);
					return FC_ERROR_MEMORY;
				}
			}

			if (Config->Flags & FC_IGNORE_WS)
			{
				const char space = ' ';
				const char tab = '\t';
				if (!_FC_BufferReplace(&textBuffer, &space, 1, NULL, 0) ||
					!_FC_BufferReplace(&textBuffer, &tab, 1, NULL, 0))
				{
					_FC_BufferFree(&textBuffer);
					return FC_ERROR_MEMORY;
				}
			}

			size_t FinalLength = textBuffer.Count;
			char* FinalText = _FC_BufferToString(&textBuffer);

			if (FinalText == NULL)
			{
				return FC_ERROR_MEMORY; // _FC_BufferToString frees on failure
			}

			// If ignoring whitespace and the line becomes empty, discard it.
			if ((Config->Flags & FC_IGNORE_WS) && FinalLength == 0)
			{
				HeapFree(GetProcessHeap(), 0, FinalText);
			}
			else
			{
				_FC_LINE line;
				line.Text = FinalText;
				line.Length = FinalLength;
				line.Hash = _FC_HashLine(FinalText, FinalLength, Config);

				if (!_FC_BufferAppend(pLineBuffer, &line))
				{
					HeapFree(GetProcessHeap(), 0, FinalText);
					return FC_ERROR_MEMORY;
				}
			}

			if (Newline >= End)
			{
				break; // Reached end of buffer
			}

			Ptr = Newline;
			while (Ptr < End && (*Ptr == '\n' || *Ptr == '\r'))
			{
				Ptr++;
			}
		}
		return FC_OK;
	}

	/**
	 * @brief Reads the entire contents of a file into a new heap-allocated buffer.
	 * @internal
	 * @param Path The wide character path to the file to read.
	 * @param[out] OutputLength A pointer to a size_t that will receive the number of bytes read.
	 * @param[out] Result A pointer to an FC_RESULT that will be set to indicate the outcome.
	 * @return A pointer to a new, null-terminated buffer containing the file's content, or NULL on failure. The caller must free this memory.
	 */
	static inline char*
		_FC_ReadFileContents(
			_In_z_ const WCHAR* Path,
			_Out_ size_t* OutputLength,
			_Out_ FC_RESULT* Result)
	{
		*OutputLength = 0;
		*Result = FC_OK;
		HANDLE FileHandle = INVALID_HANDLE_VALUE;
		char* Buffer = NULL;

		FileHandle = CreateFileW(
			Path,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);

		if (FileHandle == INVALID_HANDLE_VALUE)
		{
			*Result = FC_ERROR_IO;
			return NULL;
		}

		LARGE_INTEGER FileSize;
		if (!GetFileSizeEx(FileHandle, &FileSize))
		{
			*Result = FC_ERROR_IO;
			goto cleanup;
		}

		if (FileSize.QuadPart > (ULONGLONG)SIZE_MAX - 1)
		{
			*Result = FC_ERROR_MEMORY; // File too large
			goto cleanup;
		}

		size_t Length = (size_t)FileSize.QuadPart;
		if (Length == 0)
		{
			// Return a valid, empty, null-terminated string for zero-length files.
			*Result = FC_OK;
			Buffer = _FC_StringDuplicateRange("", 0);
			goto cleanup;
		}
		if (Length > MAXDWORD)
		{
			*Result = FC_ERROR_MEMORY; // File too large for a single ReadFile call
			goto cleanup;
		}

		Buffer = (char*)HeapAlloc(GetProcessHeap(), 0, Length + 1);
		if (Buffer == NULL)
		{
			*Result = FC_ERROR_MEMORY;
			goto cleanup;
		}

		DWORD BytesRead = 0;
		if (!ReadFile(FileHandle, Buffer, (DWORD)Length, &BytesRead, NULL) || BytesRead != Length)
		{
			*Result = FC_ERROR_IO;
			HeapFree(GetProcessHeap(), 0, Buffer);
			Buffer = NULL; // Ensure we return NULL on failure
			goto cleanup;
		}

		Buffer[Length] = '\0'; // Add the null terminator
		*OutputLength = Length;
		// *Result is already FC_OK

	cleanup:
		if (FileHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(FileHandle);
		}
		return Buffer;
	}

	/**
	 * @brief Analyzes a byte buffer to determine if it likely contains text.
	 *
	 * This heuristic checks for UTF BOMs and calculates the ratio of printable ASCII
	 * characters to total characters. A null byte is considered a strong indicator of binary content.
	 * @internal
	 * @param Buffer A pointer to the byte buffer to inspect.
	 * @param BufferLength The length of the buffer in bytes.
	 * @return TRUE if the buffer content is likely text, FALSE otherwise.
	 */
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

	/**
	 * @brief Reads the beginning of a file to determine if it is likely a text file.
	 *
	 * This function opens the specified file, reads the first chunk of data, and uses
	 * `_FC_IsProbablyTextBuffer` to analyze its content.
	 * @internal
	 * @param Path The wide character path to the file to check.
	 * @return TRUE if the file is likely a text file, FALSE otherwise or on error.
	 */
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

#define BUFFER_SIZE 4096

		BYTE* buffer = (BYTE*)HeapAlloc(GetProcessHeap(), 0, BUFFER_SIZE);
		if (buffer == NULL)
		{
			// If heap allocation fails, we can't proceed.
			CloseHandle(hFile);
			return FALSE;
		}

		DWORD bytesRead = 0;
		BOOL success = ReadFile(hFile, buffer, BUFFER_SIZE, &bytesRead, NULL);

		// Call the text-checking function.
		BOOL isText = _FC_IsProbablyTextBuffer(buffer, bytesRead);

		HeapFree(GetProcessHeap(), 0, buffer);
		CloseHandle(hFile);

#undef BUFFER_SIZE

		if (!success || bytesRead == 0) return FALSE;

		return isText;
	}

	/**
	 * @brief Compares two files in text mode.
	 *
	 * This function orchestrates the text comparison process. It reads both files,
	 * parses them into normalized line arrays using `_FC_ParseLines`, and then
	 * compares the resulting arrays with `_FC_CompareLineArrays`.
	 * @internal
	 * @param Path1 The path to the first file.
	 * @param Path2 The path to the second file.
	 * @param Config A pointer to the comparison configuration.
	 * @return An FC_RESULT code indicating the outcome.
	 */
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

		FC_USER_CONTEXT UserCtx = { Path1, Path2, &BufferA, &BufferB, Config->UserData };
		Result = _FC_FindLcs(&UserCtx, Config);

	cleanup:
		if (Buffer1) HeapFree(GetProcessHeap(), 0, Buffer1);
		if (Buffer2) HeapFree(GetProcessHeap(), 0, Buffer2);
		// Free the buffers and their nested content.
		_FC_FreeLineBufferContents(&BufferA);
		_FC_FreeLineBufferContents(&BufferB);
		return Result;
	}

	/**
	 * @brief Compares two files in binary mode.
	 *
	 * This function performs a byte-for-byte comparison of two files using memory-mapped I/O
	 * for efficiency. It first checks if the file sizes are different and reports that via
	 * the callback if so. If they are the same, it maps both files into memory and
	 * iterates through them, calling the callback for every mismatched byte.
	 * @internal
	 * @param Path1 The path to the first file.
	 * @param Path2 The path to the second file.
	 * @param Config A pointer to the comparison configuration.
	 * @return An FC_RESULT code indicating the outcome.
	 */
	static FC_RESULT
		_FC_CompareFilesBinary(
			_In_z_ const WCHAR* Path1,
			_In_z_ const WCHAR* Path2,
			_In_ const FC_CONFIG* Config)
	{
		HANDLE File1Handle = INVALID_HANDLE_VALUE;
		HANDLE File2Handle = INVALID_HANDLE_VALUE;
		HANDLE Map1Handle = NULL;
		HANDLE Map2Handle = NULL;
		unsigned char* Buffer1 = NULL;
		unsigned char* Buffer2 = NULL;
		FC_RESULT Result = FC_ERROR_IO; // Default to error

		File1Handle = CreateFileW(Path1, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		File2Handle = CreateFileW(Path2, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if (File1Handle == INVALID_HANDLE_VALUE || File2Handle == INVALID_HANDLE_VALUE)
		{
			goto cleanup;
		}

		LARGE_INTEGER File1Size, File2Size;
		if (!GetFileSizeEx(File1Handle, &File1Size) || !GetFileSizeEx(File2Handle, &File2Size))
		{
			goto cleanup;
		}

		if (File1Size.QuadPart != File2Size.QuadPart)
		{
			if (Config->DiffCallback != NULL)
			{
				FC_DIFF_BLOCK block = { FC_DIFF_TYPE_SIZE, (size_t)File1Size.QuadPart, (size_t)File1Size.QuadPart, (size_t)File2Size.QuadPart, (size_t)File2Size.QuadPart };
				FC_USER_CONTEXT BinContext = { Path1, Path2, NULL, NULL, Config->UserData };
				Config->DiffCallback(&BinContext, &block);
			}
			Result = FC_DIFFERENT;
			goto cleanup;
		}

		if (File1Size.QuadPart == 0)
		{
			Result = FC_OK; // Empty files are identical
			goto cleanup;
		}

		size_t CompareSize = (size_t)File1Size.QuadPart;
		Map1Handle = CreateFileMappingW(File1Handle, NULL, PAGE_READONLY, 0, 0, NULL);
		Map2Handle = CreateFileMappingW(File2Handle, NULL, PAGE_READONLY, 0, 0, NULL);

		if (Map1Handle == NULL || Map2Handle == NULL)
		{
			goto cleanup;
		}

		Buffer1 = (unsigned char*)MapViewOfFile(Map1Handle, FILE_MAP_READ, 0, 0, CompareSize);
		Buffer2 = (unsigned char*)MapViewOfFile(Map2Handle, FILE_MAP_READ, 0, 0, CompareSize);

		if (Buffer1 == NULL || Buffer2 == NULL)
		{
			goto cleanup;
		}

		Result = FC_OK;
		for (size_t i = 0; i < CompareSize; ++i)
		{
			if (Buffer1[i] != Buffer2[i])
			{
				if (Result == FC_OK) Result = FC_DIFFERENT;
				if (Config->DiffCallback != NULL)
				{
					// Repurpose block fields for byte-level diffs:
					// StartA = Offset, EndA = Byte from File 1, EndB = Byte from File 2
					FC_DIFF_BLOCK block = { FC_DIFF_TYPE_CHANGE, i, Buffer1[i], i, Buffer2[i] };
					FC_USER_CONTEXT BinContext = { Path1, Path2, NULL, NULL, Config->UserData };
					Config->DiffCallback(&BinContext, &block);
				}
			}
		}

	cleanup:
		if (Buffer1) UnmapViewOfFile(Buffer1);
		if (Buffer2) UnmapViewOfFile(Buffer2);
		if (Map1Handle) CloseHandle(Map1Handle);
		if (Map2Handle) CloseHandle(Map2Handle);
		if (File1Handle != INVALID_HANDLE_VALUE) CloseHandle(File1Handle);
		if (File2Handle != INVALID_HANDLE_VALUE) CloseHandle(File2Handle);
		return Result;
	}

	/**
	 * @brief Converts a Win32 path to a canonical NT path and validates it.
	 *
	 * This function uses the native `RtlDosPathNameToNtPathName_U_WithStatus` API to
	 * resolve a DOS-style path into its underlying NT object manager path. It performs
	 * security checks to reject dangerous paths, such as those pointing directly to
	 * devices (`\\.\`, `\\?\`) or reserved DOS device names (CON, PRN, etc.).
	 * @internal
	 * @param InputPath The Win32-style path to process.
	 * @param[out] CanonicalPathOut A pointer to a WCHAR* that will receive the new, heap-allocated canonical path string. The caller must free this memory.
	 * @return TRUE on success, FALSE if the path is invalid, dangerous, or a memory allocation fails.
	 */
	static BOOL
		_FC_ToCanonicalPath(
			_In_z_ const WCHAR* InputPath,
			_Outptr_result_nullonfailure_ WCHAR** CanonicalPathOut)
	{
		if (!InputPath || !CanonicalPathOut)
			return FALSE;

		*CanonicalPathOut = NULL;

		UNICODE_STRING NtPath = { 0 };
		WCHAR* outPath = NULL;
		BOOL ntPathInitialized = FALSE;
		BOOL success = FALSE;

		// Step 1: Check if input path type is acceptable
		RTL_PATH_TYPE PathType = RtlDetermineDosPathNameType_U(InputPath);
		if (PathType == RtlPathTypeUnknown ||
			PathType == RtlPathTypeLocalDevice ||
			PathType == RtlPathTypeRootLocalDevice)
		{
			goto cleanup; // reject raw \\.\ or \\?\ paths
		}

		// Step 2: Convert to full NT path via native call
		if (!(PathType == RtlPathTypeUncAbsolute ||
			PathType == RtlPathTypeDriveAbsolute ||
			PathType == RtlPathTypeDriveRelative ||
			PathType == RtlPathTypeRooted ||
			PathType == RtlPathTypeRelative))
		{
			goto cleanup; // reject other types like UNC relative, etc.
		}

		NTSTATUS Status = RtlDosPathNameToNtPathName_U_WithStatus(
			InputPath,
			&NtPath,
			NULL, // FilePart not needed
			NULL); // Reserved

		if (!NT_SUCCESS(Status))
		{
			goto cleanup;
		}
		ntPathInitialized = TRUE;

		// Step 3: Detect risky NT path prefixes
		if (NtPath.Length >= 8 * sizeof(WCHAR))
		{
			const WCHAR* s = NtPath.Buffer;
			// Block named pipes and raw device paths
			if (_wcsnicmp(s, L"\\Device\\", 8) == 0 ||
				_wcsnicmp(s, L"\\??\\PIPE\\", 9) == 0)
			{
				goto cleanup;
			}
		}

		// Step 4: Reject reserved DOS device names
		const WCHAR* base = wcsrchr(NtPath.Buffer, L'\\');
		if (base == NULL)
		{
			// No backslash found, the whole path is the base component.
			base = NtPath.Buffer;
		}
		else
		{
			// Move past the backslash to the start of the name.
			base++;
		}

		for (int i = 0; i < ARRAYSIZE(g_ReservedDevices); ++i)
		{
			if (_wcsicmp(base, g_ReservedDevices[i]) == 0)
			{
				goto cleanup;
			}
		}

		// Step 5: Allocate and copy canonical path
		size_t len = (NtPath.Length / sizeof(WCHAR)) + 1;
		outPath = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
		if (!outPath)
		{
			goto cleanup;
		}

		errno_t err = wcsncpy_s(outPath, len, NtPath.Buffer, _TRUNCATE);
		if (err != 0 || outPath[0] == L'\0')
		{
			goto cleanup;
		}

		// Step 6: Success - set output parameter
		*CanonicalPathOut = outPath;
		success = TRUE;

	cleanup:
		if (ntPathInitialized)
		{
			RtlFreeUnicodeString(&NtPath);
		}
		if (!success && outPath)
		{
			HeapFree(GetProcessHeap(), 0, outPath);
		}
		return success;
	}

	/**
	 * @brief Converts a UTF-8 encoded string to a new wide (UTF-16) string.
	 * @internal
	 * @param Utf8String The null-terminated UTF-8 string to convert.
	 * @return A pointer to a new, heap-allocated, null-terminated wide string, or NULL on failure. The caller must free this memory.
	 */
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

	/**
	 * @brief The internal core comparison dispatcher.
	 *
	 * This function selects the appropriate comparison strategy (text or binary) based
	 * on the `Config->Mode`. For `FC_MODE_AUTO`, it uses `_FC_IsProbablyTextFileW` to
	 * decide which strategy to use.
	 * @internal
	 * @param Path1 The canonical path to the first file.
	 * @param Path2 The canonical path to the second file.
	 * @param Config A pointer to the comparison configuration.
	 * @return An FC_RESULT code indicating the outcome.
	 */
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

	/**
	 * @brief Compares two files using UTF-8 encoded paths.
	 *
	 * This is a convenience wrapper function that takes UTF-8 encoded file paths,
	 * converts them to wide (UTF-16) strings, and then calls the primary `FC_CompareFilesW`
	 * function to perform the comparison.
	 *
	 * @param Path1 A null-terminated, UTF-8 encoded path to the first file.
	 * @param Path2 A null-terminated, UTF-8 encoded path to the second file.
	 * @param Config A pointer to the comparison configuration structure. This must not be NULL.
	 *
	 * @return An FC_RESULT code indicating the outcome of the comparison.
	 * @retval FC_ERROR_INVALID_PARAM if any of the required pointers are NULL or if the path strings contain invalid UTF-8 sequences.
	 * @retval FC_ERROR_MEMORY if memory allocation for path conversion fails.
	 * @retval Other FC_RESULT codes as returned by `FC_CompareFilesW`.
	 */
	FC_RESULT
		FC_CompareFilesUtf8(
			_In_z_ const char* Path1,
			_In_z_ const char* Path2,
			_In_ const FC_CONFIG* Config)
	{
		FC_RESULT Result = FC_OK;
		WCHAR* WidePath1 = NULL;
		WCHAR* WidePath2 = NULL;

		if (!Path1 || !Path2 || !Config || !Config->DiffCallback)
		{
			return FC_ERROR_INVALID_PARAM; // No cleanup needed, return directly.
		}

		// Convert paths, checking each one immediately.
		WidePath1 = _FC_ConvertUtf8ToWide(Path1);
		if (WidePath1 == NULL)
		{
			Result = (GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
				? FC_ERROR_INVALID_PARAM
				: FC_ERROR_MEMORY;
			goto cleanup;
		}

		WidePath2 = _FC_ConvertUtf8ToWide(Path2);
		if (WidePath2 == NULL)
		{
			Result = (GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
				? FC_ERROR_INVALID_PARAM
				: FC_ERROR_MEMORY;
			goto cleanup;
		}

		// Chain to the primary public API.
		Result = FC_CompareFilesW(WidePath1, WidePath2, Config);

	cleanup:
		if (WidePath1) HeapFree(GetProcessHeap(), 0, WidePath1);
		if (WidePath2) HeapFree(GetProcessHeap(), 0, WidePath2);

		return Result;
	}

	/**
	 * @brief Compares two files using wide (UTF-16) encoded paths. (Primary Function)
	 *
	 * This is the main entry point of the library. It takes two file paths and a
	 * configuration structure, performs path canonicalization and validation, and then
	 * dispatches to the appropriate internal comparison routine (text or binary)
	 * based on the configuration. This function supports long file paths.
	 *
	 * @param Path1 A null-terminated, wide (UTF-16) encoded path to the first file.
	 * @param Path2 A null-terminated, wide (UTF-16) encoded path to the second file.
	 * @param Config A pointer to the comparison configuration structure. This must not be NULL.
	 *
	 * @return An FC_RESULT code indicating the outcome of the comparison.
	 * @retval FC_OK if the files are identical.
	 * @retval FC_DIFFERENT if the files differ.
	 * @retval FC_ERROR_INVALID_PARAM if any required pointers are NULL or if the paths are determined to be invalid or unsafe.
	 * @retval FC_ERROR_IO if a file cannot be read.
	 * @retval FC_ERROR_MEMORY if a memory allocation fails during the operation.
	 */
	FC_RESULT
		FC_CompareFilesW(
			_In_z_ const WCHAR* Path1,
			_In_z_ const WCHAR* Path2,
			_In_ const FC_CONFIG* Config)
	{
		FC_RESULT Result = FC_OK;
		WCHAR* CanonicalPath1 = NULL;
		WCHAR* CanonicalPath2 = NULL;

		if (!Path1 || !Path2 || !Config || !Config->DiffCallback) {
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
