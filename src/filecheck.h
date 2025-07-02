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
#include <stdint.h>
#include <stddef.h>

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
// Specifies the comparison mode (text or binary).
//
typedef enum
{
    FC_MODE_TEXT,
    FC_MODE_BINARY
} FC_MODE;

//
// Flags to modify comparison behavior.
//
#define FC_IGNORE_CASE      0x0001  // Ignore case in text comparison.
#define FC_IGNORE_WS        0x0002  // Ignore whitespace in text comparison.
#define FC_SHOW_LINE_NUMS   0x0004  // Show line numbers in output.
#define FC_RAW_TABS         0x0008  // Do not expand tabs in text comparison.
#define FC_UNICODE_TEXT     0x0010  // Treat files as UTF-16 text.

/**
 * @brief Callback function for reporting comparison differences.
 *
 * @param UserData  User-defined data passed from the FC_CONFIG struct.
 * @param Message   A UTF-8 encoded string describing the difference.
 * @param Line1     The line number in the first file, or -1 if not applicable.
 * @param Line2     The line number in the second file, or -1 if not applicable.
 */
typedef void (*FC_OUTPUT_CALLBACK)(
    void* UserData,
    const char* Message,
    int Line1,
    int Line2);

//
// Configuration structure for a file comparison operation.
//
typedef struct
{
    FC_MODE Mode;               // Text or binary comparison mode.
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
 * This is the most efficient function, avoiding string conversions. It supports long paths.
 *
 * @param Path1 Path to the first file, UTF-16 encoded.
 * @param Path2 Path to the second file, UTF-16 encoded.
 * @param Config A pointer to the comparison configuration structure.
 *
 * @return An FC_RESULT code indicating the outcome of the comparison.
 */
FC_RESULT
FileCheckCompareFilesW(
    _In_ const WCHAR* Path1,
    _In_ const WCHAR* Path2,
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
    _In_ const char* Path1Utf8,
    _In_ const char* Path2Utf8,
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

// Forward declaration for the main implementation function.
static FC_RESULT
_FileCheckCompareFilesImpl(
    _In_ const WCHAR* Path1,
    _In_ const WCHAR* Path2,
    _In_ const FC_CONFIG* Config);

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

static inline void
_FileCheckIntegerToHex(
    size_t Value,
    char* OutputBuffer)
{
    const char* HexDigits = "0123456789abcdef";
    char TempBuffer[16];
    int Index = 0;

    if (Value == 0)
    {
        OutputBuffer[0] = '0';
        OutputBuffer[1] = '\0';
        return;
    }

    do
    {
        TempBuffer[Index++] = HexDigits[Value % 16];
        Value /= 16;
    } while (Value > 0);

    int OutputIndex = 0;
    while (Index > 0)
    {
        OutputBuffer[OutputIndex++] = TempBuffer[--Index];
    }
    OutputBuffer[OutputIndex] = '\0';
}

static inline WCHAR*
_FileCheckCreateLongPathW(
    _In_ const WCHAR* Path)
{
    size_t Length = 0;
    const WCHAR* Ptr = Path;
    while (*Ptr++) Length++;

    // Allocate for "\\?\" + path + null terminator
    WCHAR* LongPath = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, (Length + 5) * sizeof(WCHAR));
    if (LongPath == NULL)
    {
        return NULL;
    }

    LongPath[0] = L'\\';
    LongPath[1] = L'\\';
    LongPath[2] = L'?';
    LongPath[3] = L'\\';

    Ptr = Path;
    WCHAR* Destination = LongPath + 4;
    while (*Ptr)
    {
        *Destination++ = *Ptr++;
    }
    *Destination = L'\0';

    return LongPath;
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
_FileCheckHashLine(
    _In_ const char* String,
    _In_ size_t Length,
    _In_ UINT Flags)
{
    UINT Hash = 0;
    for (size_t i = 0; i < Length; ++i)
    {
        unsigned char Character = (unsigned char)String[i];
        if ((Flags & FC_IGNORE_WS) && (Character == ' ' || Character == '\t'))
        {
            continue;
        }
        if (Flags & FC_IGNORE_CASE)
        {
            Character = _FileCheckToLowerAscii(Character);
        }
        Hash = Hash * 31 + Character;
    }
    return Hash;
}

static inline char*
_FileCheckStringDuplicateRange(
    _In_ const char* String,
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

static inline BOOL
_FileCheckLineArrayAppend(
    _Inout_ FC_LINE_ARRAY* LineArray,
    _In_ char* Text,
    _In_ size_t Length,
    _In_ UINT Hash)
{
    if (LineArray->Count + 1 > LineArray->Capacity)
    {
        size_t NewCapacity = LineArray->Capacity ? LineArray->Capacity * 2 : 64;
        FC_LINE* Temp = (FC_LINE*)HeapReAlloc(GetProcessHeap(),
                                              0,
                                              LineArray->Lines,
                                              NewCapacity * sizeof(FC_LINE));
        if (Temp == NULL)
        {
            return FALSE;
        }
        LineArray->Lines = Temp;
        LineArray->Capacity = NewCapacity;
    }
    LineArray->Lines[LineArray->Count].Text = Text;
    LineArray->Lines[LineArray->Count].Length = Length;
    LineArray->Lines[LineArray->Count].Hash = Hash;
    LineArray->Count++;
    return TRUE;
}

static inline FC_RESULT
_FileCheckParseLines(
    _In_ const char* Buffer,
    _In_ size_t BufferLength,
    _Inout_ FC_LINE_ARRAY* LineArray,
    _In_ UINT Flags)
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

        size_t Length = (size_t)(Newline - Ptr);
        char* Text = _FileCheckStringDuplicateRange(Ptr, Length);
        if (Text == NULL)
        {
            _FileCheckLineArrayFree(LineArray);
            return FC_ERROR_MEMORY;
        }

        UINT Hash = _FileCheckHashLine(Text, Length, Flags);
        if (!_FileCheckLineArrayAppend(LineArray, Text, Length, Hash))
        {
            HeapFree(GetProcessHeap(), 0, Text);
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
    size_t Count = min(ArrayA->Count, ArrayB->Count);
    for (size_t i = 0; i < Count; ++i)
    {
        const FC_LINE* LineA = &ArrayA->Lines[i];
        const FC_LINE* LineB = &ArrayB->Lines[i];
        if (LineA->Hash != LineB->Hash ||
            LineA->Length != LineB->Length ||
            RtlCompareMemory(LineA->Text, LineB->Text, LineA->Length) != LineA->Length)
        {
            if (Config->Output != NULL)
            {
                Config->Output(Config->UserData, "Line differs", (int)(i + 1), (int)(i + 1));
            }
            return FC_DIFFERENT;
        }
    }

    if (ArrayA->Count != ArrayB->Count)
    {
        if (Config->Output != NULL)
        {
            Config->Output(Config->UserData, "Files have different line counts", -1, -1);
        }
        return FC_DIFFERENT;
    }

    return FC_OK;
}

static inline char*
_FileCheckReadFileContents(
    _In_ const WCHAR* Path,
    _Out_ size_t* OutputLength,
    _Out_ FC_RESULT* Result)
{
    HANDLE FileHandle = CreateFileW(Path,
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
    if (!GetFileSizeEx(FileHandle, &FileSize) || FileSize.QuadPart > (LONGLONG)-1)
    {
        CloseHandle(FileHandle);
        *Result = FC_ERROR_IO;
        return NULL;
    }

    size_t Length = (size_t)FileSize.QuadPart;
    char* Buffer = (char*)HeapAlloc(GetProcessHeap(), 0, Length);
    if (Buffer == NULL)
    {
        CloseHandle(FileHandle);
        *Result = FC_ERROR_MEMORY;
        return NULL;
    }

    DWORD BytesRead;
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

//
// Main Implementation
//

FC_RESULT
FileCheckCompareFilesUtf8(
    _In_ const char* Path1Utf8,
    _In_ const char* Path2Utf8,
    _In_ const FC_CONFIG* Config)
{
    if (Path1Utf8 == NULL || Path2Utf8 == NULL)
    {
        return FC_ERROR_INVALID_PARAM;
    }

    int WideLength1 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path1Utf8, -1, NULL, 0);
    if (WideLength1 == 0) return FC_ERROR_INVALID_PARAM;
    WCHAR* WidePath1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, WideLength1 * sizeof(WCHAR));
    if (WidePath1 == NULL) return FC_ERROR_MEMORY;
    // Use MB_ERR_INVALID_CHARS and check for failure
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             Path1Utf8, -1,
                             WidePath1, WideLength1) == 0)
    {
        HeapFree(GetProcessHeap(), 0, WidePath1);
        return FC_ERROR_INVALID_PARAM;
    }
    int WideLength2 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path2Utf8, -1, NULL, 0);
    if (WideLength2 == 0)
    {
        HeapFree(GetProcessHeap(), 0, WidePath1);
        return FC_ERROR_INVALID_PARAM;
    }
    WCHAR* WidePath2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, WideLength2 * sizeof(WCHAR));
    if (WidePath2 == NULL)
    {
        HeapFree(GetProcessHeap(), 0, WidePath1);
        return FC_ERROR_MEMORY;
    }
    // Likewise validate the second conversion
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             Path2Utf8, -1,
                             WidePath2, WideLength2) == 0)
    {
        HeapFree(GetProcessHeap(), 0, WidePath1);
        HeapFree(GetProcessHeap(), 0, WidePath2);
        return FC_ERROR_INVALID_PARAM;
    }
    FC_RESULT Result = FileCheckCompareFilesW(WidePath1, WidePath2, Config);

    HeapFree(GetProcessHeap(), 0, WidePath1);
    HeapFree(GetProcessHeap(), 0, WidePath2);
    return Result;
}

FC_RESULT
FileCheckCompareFilesW(
    _In_ const WCHAR* Path1,
    _In_ const WCHAR* Path2,
    _In_ const FC_CONFIG* Config)
{
    if (Path1 == NULL || Path2 == NULL || Config == NULL)
    {
        return FC_ERROR_INVALID_PARAM;
    }

    WCHAR* LongPath1 = _FileCheckCreateLongPathW(Path1);
    WCHAR* LongPath2 = _FileCheckCreateLongPathW(Path2);
    if (LongPath1 == NULL || LongPath2 == NULL)
    {
        if (LongPath1 != NULL) HeapFree(GetProcessHeap(), 0, LongPath1);
        if (LongPath2 != NULL) HeapFree(GetProcessHeap(), 0, LongPath2);
        return FC_ERROR_MEMORY;
    }

    FC_RESULT Result = _FileCheckCompareFilesImpl(LongPath1, LongPath2, Config);

    HeapFree(GetProcessHeap(), 0, LongPath1);
    HeapFree(GetProcessHeap(), 0, LongPath2);
    return Result;
}

static FC_RESULT
_FileCheckCompareFilesImpl(
    _In_ const WCHAR* Path1,
    _In_ const WCHAR* Path2,
    _In_ const FC_CONFIG* Config)
{
    if (Config->Mode == FC_MODE_BINARY)
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
        if (RtlCompareMemory(Buffer1, Buffer2, CompareSize) != CompareSize)
        {
            Result = FC_DIFFERENT;
            if (Config->Output != NULL)
            {
                for (size_t i = 0; i < CompareSize; ++i)
                {
                    if (Buffer1[i] != Buffer2[i])
                    {
                        char Message[64] = "Binary diff at offset 0x";
                        char HexBuffer[17];
                        _FileCheckIntegerToHex(i, HexBuffer);

                        char* Ptr = Message;
                        while (*Ptr) Ptr++;
                        char* Source = HexBuffer;
                        while (*Source) *Ptr++ = *Source++;
                        *Ptr = '\0';

                        Config->Output(Config->UserData, Message, -1, -1);
                        break;
                    }
                }
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
    else // FC_MODE_TEXT
    {
        size_t Length1, Length2;
        FC_RESULT Result1, Result2;

        char* Buffer1 = _FileCheckReadFileContents(Path1, &Length1, &Result1);
        if (Buffer1 == NULL)
        {
            return Result1;
        }

        char* Buffer2 = _FileCheckReadFileContents(Path2, &Length2, &Result2);
        if (Buffer2 == NULL)
        {
            HeapFree(GetProcessHeap(), 0, Buffer1);
            return Result2;
        }

        FC_LINE_ARRAY ArrayA, ArrayB;
        Result1 = _FileCheckParseLines(Buffer1, Length1, &ArrayA, Config->Flags);
        HeapFree(GetProcessHeap(), 0, Buffer1);
        if (Result1 != FC_OK)
        {
            HeapFree(GetProcessHeap(), 0, Buffer2);
            return FC_ERROR_MEMORY;
        }

        Result2 = _FileCheckParseLines(Buffer2, Length2, &ArrayB, Config->Flags);
        HeapFree(GetProcessHeap(), 0, Buffer2);
        if (Result2 != FC_OK)
        {
            _FileCheckLineArrayFree(&ArrayA);
            return FC_ERROR_MEMORY;
        }

        FC_RESULT Result = _FileCheckCompareLineArrays(&ArrayA, &ArrayB, Config);
        _FileCheckLineArrayFree(&ArrayA);
        _FileCheckLineArrayFree(&ArrayB);
        return Result;
    }
}

#ifdef __cplusplus
}
#endif
