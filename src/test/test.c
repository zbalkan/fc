#include <windows.h>    // WinAPI: HANDLE, DWORD, BOOL, WCHAR, CreateFileW, WriteFile, CloseHandle,
// GetTempPathW, CreateDirectoryW, GetStdHandle, WriteConsoleA, WriteConsoleW,
// WideCharToMultiByte, ExitProcess
#include <strsafe.h>     // StringCchLengthA/W, StringCchCopyW, StringCchCatW
#include <pathcch.h>     // PathCchCombine, PathCchAddBackslash
#include "../fc/filecheck.h"   // FC_CONFIG, FC_RESULT, FC_OK, FC_DIFFERENT, FC_MODE_*, FC_IGNORE_*,
// FileCheckCompareFilesUtf8()

#pragma comment(lib, "Pathcch.lib") // Link Pathcch
#define MAX_LONG_PATH 32768
const int UTF8_BUFFER_SIZE = MAX_LONG_PATH * 4;
#define LONG_PATH_PREFIX L"\\\\?\\"
int FAILURE = 0;
int SUCCESS = 0;

#define ASSERT_TRUE(expr) \
    do { \
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); \
        DWORD w; \
        WCHAR funcNameW[128]; \
        MultiByteToWideChar(CP_ACP, 0, __FUNCTION__, -1, funcNameW, 128); \
        if (!(expr)) { \
            WriteConsoleW(h, L"Test FAILED: ", 13, &w, NULL); \
            WriteConsoleW(h, funcNameW, (DWORD)wcslen(funcNameW), &w, NULL); \
            WriteConsoleW(h, L"\n  Assertion failed: " L#expr L"\n\n", (DWORD)(22 + wcslen(L#expr)), &w, NULL); \
			FAILURE++; \
            /*ExitProcess(1);*/ \
        } else { \
            WriteConsoleW(h, L"Test PASSED: ", 13, &w, NULL); \
            WriteConsoleW(h, funcNameW, (DWORD)wcslen(funcNameW), &w, NULL); \
            WriteConsoleW(h, L"\n", 1, &w, NULL); \
			SUCCESS++; \
        } \
    } while(0)

// Abort on error
static void Throw(_In_z_ const WCHAR* msg, _In_opt_z_ const WCHAR* path)
{
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD w;
	WriteConsoleW(h, msg, (DWORD)wcslen(msg), &w, NULL);
	if (path) { WriteConsoleW(h, L": ", 2, &w, NULL); WriteConsoleW(h, path, (DWORD)wcslen(path), &w, NULL); }
	WriteConsoleW(h, L"\r\n", 2, &w, NULL);
	ExitProcess(1);
}

static BOOL WriteDataFile(_In_z_ const WCHAR* path, _In_reads_(size) const void* data, _In_ DWORD size)
{
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return FALSE;
	DWORD w; BOOL ok = WriteFile(h, data, size, &w, NULL) && w == size; CloseHandle(h); return ok;
}

// Combine directory and filename into extended-length path
static void MakePath(
	_In_z_ const WCHAR* baseDir,
	_In_z_ const WCHAR* name,
	_Out_writes_z_(MAX_LONG_PATH) WCHAR* out)
{
	WCHAR* ext = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	if (ext == NULL)
	{
		Throw(L"MakePath failed: HeapAlloc", NULL);
	}

	if (FAILED(StringCchCopyW(ext, MAX_LONG_PATH, LONG_PATH_PREFIX)) ||
		FAILED(StringCchCatW(ext, MAX_LONG_PATH, baseDir)) ||
		FAILED(PathCchAddBackslash(ext, MAX_LONG_PATH)))
	{
		HeapFree(GetProcessHeap(), 0, ext);
		Throw(L"Invalid base path", baseDir);
	}

	if (FAILED(PathCchCombine(out, MAX_LONG_PATH, ext, name)))
	{
		HeapFree(GetProcessHeap(), 0, ext);
		Throw(L"Invalid path combine", name);
	}

	HeapFree(GetProcessHeap(), 0, ext);
}

// Default callback printing differences
static void WINAPI TestCallback(
	_In_opt_ void* UserData,
	_In_z_ const char* Message,
	_In_ int Line1,
	_In_ int Line2)
{
	/*HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD written;
	size_t len;
	if (SUCCEEDED(StringCchLengthA(Message, STRSAFE_MAX_CCH, &len))) {
		WriteConsoleA(h, Message, (DWORD)len, &written, NULL);
		WriteConsoleA(h, "\r\n", 2, &written, NULL);
	}*/
}

#define WRITE_STR_FILE(path, str) do { size_t cch; if (FAILED(StringCchLengthA(str, STRSAFE_MAX_CCH, &cch))) Throw(L"Bad string", NULL); \
    if (!WriteDataFile(path, str, (DWORD)cch)) Throw(L"Write failed", path); } while(0)

static void ConvertWideToUtf8OrExit(_In_z_ const WCHAR* wp, char* buf, int sz)
{
	int req = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wp, -1, NULL, 0, NULL, NULL);
	if (req <= 0 || req > sz) Throw(L"Conv fail", wp);
	int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wp, -1, buf, sz, NULL, NULL);
	if (n <= 0 || n > sz) Throw(L"Conv fail", wp);
}

static void Test_TextAsciiIdentical(const WCHAR* baseDir)
{
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"ascii_id1.txt", p1);
	MakePath(baseDir, L"ascii_id2.txt", p2);
	WRITE_STR_FILE(p1, "Line1\nLine2\n"); WRITE_STR_FILE(p2, "Line1\nLine2\n");
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_ASCII;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_OK);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}

static void Test_TextAsciiDifferentContent(const WCHAR* baseDir)
{
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"ascii_diff1.txt", p1);
	MakePath(baseDir, L"ascii_diff2.txt", p2);
	WRITE_STR_FILE(p1, "Line1\nLine2\n");
	WRITE_STR_FILE(p2, "LineX\nLineY\n");
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_ASCII;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_CaseSensitivity_Sensitive(const WCHAR* baseDir)
{
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"case1.txt", p1);
	MakePath(baseDir, L"case2.txt", p2);
	WRITE_STR_FILE(p1, "Hello World\n");
	WRITE_STR_FILE(p2, "hello world\n");
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_ASCII;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);	cfg.Flags = 0;
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_CaseSensitivity_Insensitive(const WCHAR* baseDir)
{
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"case1.txt", p1);
	MakePath(baseDir, L"case2.txt", p2);
	WRITE_STR_FILE(p1, "Hello World\n");
	WRITE_STR_FILE(p2, "hello world\n");
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_ASCII;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	cfg.Flags = FC_IGNORE_CASE;
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_OK);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_Whitespace_Sensitive(const WCHAR* baseDir)
{
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"ws1.txt", p1);
	MakePath(baseDir, L"ws2.txt", p2);
	WRITE_STR_FILE(p1, "Test\n");
	WRITE_STR_FILE(p2, "  Test  \n");
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_ASCII;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	cfg.Flags = 0;
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_Whitespace_Insensitive(const WCHAR* baseDir)
{
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"ws1.txt", p1);
	MakePath(baseDir, L"ws2.txt", p2);
	WRITE_STR_FILE(p1, "Test\n");
	WRITE_STR_FILE(p2, "  Test  \n");
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_ASCII;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	cfg.Flags = FC_IGNORE_WS;
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_OK);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_Tabs_Expanded(const WCHAR* baseDir)
{
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"tab1.txt", p1);
	MakePath(baseDir, L"tab2.txt", p2);
	WRITE_STR_FILE(p1, "A\tB\n");
	WRITE_STR_FILE(p2, "A    B\n"); // 4 spaces
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_ASCII;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	cfg.Flags = 0;
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_OK);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_Tabs_Raw(const WCHAR* baseDir)
{
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"tab1.txt", p1);
	MakePath(baseDir, L"tab2.txt", p2);
	WRITE_STR_FILE(p1, "A\tB\n");
	WRITE_STR_FILE(p2, "A    B\n"); // 4 spaces
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_ASCII;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	cfg.Flags = FC_RAW_TABS;
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}

static void Test_UnicodeUtf8Match(const WCHAR* baseDir)
{
	// Identical UTF-8 Unicode content
	const char* utf8 = "cafÃ©\n"; // "café"
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"unicode_u8_1.txt", p1);
	MakePath(baseDir, L"unicode_u8_2.txt", p2);
	// write raw UTF-8
	if (!WriteDataFile(p1, utf8, (DWORD)strlen(utf8))) Throw(L"UTF8 write failed", p1);
	if (!WriteDataFile(p2, utf8, (DWORD)strlen(utf8))) Throw(L"UTF8 write failed", p2);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_UNICODE;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_OK);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_UnicodeDiacritics(const WCHAR* baseDir)
{
	// Differ by diacritic
	const char* a = "cafe\n";
	const char* b = "cafÃ©\n"; // "café"
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"unicode_diac1.txt", p1);
	MakePath(baseDir, L"unicode_diac2.txt", p2);
	if (!WriteDataFile(p1, a, (DWORD)strlen(a))) Throw(L"write failed", p1);
	if (!WriteDataFile(p2, b, (DWORD)strlen(b))) Throw(L"write failed", p2);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_UNICODE;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_UnicodeEmojiMultiline(const WCHAR* baseDir)
{
	const char* content = "Line1 😃\nLine2 🚀\n"; // emoji in UTF-8
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"unicode_emoji1.txt", p1);
	MakePath(baseDir, L"unicode_emoji2.txt", p2);
	if (!WriteDataFile(p1, content, (DWORD)strlen(content))) Throw(L"write failed", p1);
	if (!WriteDataFile(p2, content, (DWORD)strlen(content))) Throw(L"write failed", p2);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_UNICODE;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_OK);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_UnicodeBomEquivalence(const WCHAR* baseDir)
{
	// BOM present vs absent
	const unsigned char bom[] = { 0xEF,0xBB,0xBF };
	const char* text = "Hello\n";
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"bom1.txt", p1);
	MakePath(baseDir, L"bom2.txt", p2);

	// with BOM
	if (!WriteDataFile(p1, bom, sizeof(bom))) Throw(L"write BOM failed", p1);
	if (!WriteDataFile(p1, text, (DWORD)strlen(text))) Throw(L"write text failed", p1);

	// without BOM
	WRITE_STR_FILE(p2, text);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_TEXT_UNICODE;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_OK);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_BinaryExactMatch(const WCHAR* baseDir)
{
	const unsigned char data[] = { 0x00,0xFF,0x7F,0x80 };
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"bin1.dat", p1);
	MakePath(baseDir, L"bin2.dat", p2);
	if (!WriteDataFile(p1, data, sizeof(data))) Throw(L"write bin failed", p1);
	if (!WriteDataFile(p2, data, sizeof(data))) Throw(L"write bin failed", p2);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_BINARY;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_OK);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_BinaryMiddleDiff(const WCHAR* baseDir)
{
	unsigned char d1[] = { 1,2,3,4,5 };
	unsigned char d2[] = { 1,2,99,4,5 };
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"bin_mid1.dat", p1);
	MakePath(baseDir, L"bin_mid2.dat", p2);
	if (!WriteDataFile(p1, d1, sizeof(d1))) Throw(L"write bin failed", p1);
	if (!WriteDataFile(p2, d2, sizeof(d2))) Throw(L"write bin failed", p2);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_BINARY;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_BinarySizeDiff(const WCHAR* baseDir)
{
	unsigned char d1[] = { 1,2,3 };
	unsigned char d2[] = { 1,2,3,4 };
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"bin_sz1.dat", p1);
	MakePath(baseDir, L"bin_sz2.dat", p2);
	if (!WriteDataFile(p1, d1, sizeof(d1))) Throw(L"write bin failed", p1);
	if (!WriteDataFile(p2, d2, sizeof(d2))) Throw(L"write bin failed", p2);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_BINARY;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_AutoAsciiVsBinary(const WCHAR* baseDir)
{
	// ASCII text file and a binary file
	const char* text = "Hello\n";
	unsigned char bin[] = { 0x00,0x01,0x02 };
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"auto_text.txt", p1);
	MakePath(baseDir, L"auto_bin.dat", p2);
	WRITE_STR_FILE(p1, text);
	if (!WriteDataFile(p2, bin, sizeof(bin))) Throw(L"write failed", p2);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_AUTO;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_AutoUnicodeVsBinary(const WCHAR* baseDir)
{
	// UTF-8 text file and a binary file
	const char* utf8 = "cafÃ©\n";
	unsigned char bin[] = { 0xAA,0xBB };
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"auto_unicode.txt", p1);
	MakePath(baseDir, L"auto_bin2.dat", p2);
	if (!WriteDataFile(p1, utf8, (DWORD)strlen(utf8))) Throw(L"write failed", p1);
	if (!WriteDataFile(p2, bin, sizeof(bin))) Throw(L"write failed", p2);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_AUTO;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_AutoBinaryVsEmpty(const WCHAR* baseDir)
{
	// binary file vs empty file
	unsigned char bin[] = { 0xDE,0xAD,0xBE,0xEF };
	WCHAR* p1 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	WCHAR* p2 = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	char* u1 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);
	char* u2 = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * 4);

	// Always check for allocation failure in production code.
	if (!p1 || !p2 || !u1 || !u2)
	{
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}

	MakePath(baseDir, L"auto_bin3.dat", p1);
	MakePath(baseDir, L"auto_empty.bin", p2);
	if (!WriteDataFile(p1, bin, sizeof(bin))) Throw(L"write failed", p1);
	// create empty file
	HANDLE h = CreateFileW(p2, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) Throw(L"create empty failed", p2);
	CloseHandle(h);
	FC_CONFIG cfg = { 0 }; cfg.Output = TestCallback; cfg.Mode = FC_MODE_AUTO;

	ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(p2, u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(u1, u2, &cfg) == FC_DIFFERENT);

	HeapFree(GetProcessHeap(), 0, p1);
	HeapFree(GetProcessHeap(), 0, p2);
	HeapFree(GetProcessHeap(), 0, u1);
	HeapFree(GetProcessHeap(), 0, u2);
}
static void Test_UTF8WrapperValidPath(const WCHAR* baseDir)
{
	// Valid UTF-8 path conversion
	WCHAR pWide[MAX_LONG_PATH];
    char pUtf8[MAX_LONG_PATH*4 + 1] = {0};
	// Filename with non-ASCII character
	MakePath(baseDir, L"ünicode.txt", pWide);
	WRITE_STR_FILE(pWide, "X\n");
	ConvertWideToUtf8OrExit(pWide, pUtf8, sizeof(pUtf8));
    FC_CONFIG cfg = {0};
    cfg.Output = TestCallback;
    cfg.Mode = FC_MODE_TEXT_ASCII;
	ASSERT_TRUE(FC_CompareFilesUtf8(pUtf8, pUtf8, &cfg) == FC_OK);
}
static void Test_UTF8WrapperInvalidPath(const WCHAR* baseDir)
{
    const char bad[] = { (char)0xC3, (char)0x28, 0 };
    FC_CONFIG cfg = {0};
	cfg.Output = TestCallback;
	cfg.Mode = FC_MODE_TEXT_ASCII;
	ASSERT_TRUE(FC_CompareFilesUtf8(bad, bad, &cfg) == FC_ERROR_INVALID_PARAM);
}
static void Test_ErrorNullPathPointer(const WCHAR* baseDir)
{
	FC_CONFIG cfg = { 0 };
	cfg.Output = TestCallback;
	cfg.Mode = FC_MODE_TEXT_ASCII;
	ASSERT_TRUE(FC_CompareFilesUtf8("", NULL, &cfg) == FC_ERROR_INVALID_PARAM);
}
static void Test_ErrorNullConfigPointer(const WCHAR* baseDir)
{
	const char* utf = "X\n";
	ASSERT_TRUE(FC_CompareFilesUtf8(utf, utf, NULL) == FC_ERROR_INVALID_PARAM);
}

int wmain(void)
{
	WCHAR tempDir[MAX_LONG_PATH]; DWORD len = GetTempPathW(MAX_LONG_PATH, tempDir);
	if (!len || len >= MAX_LONG_PATH) Throw(L"TempPath fail", NULL);
	WCHAR testDir[MAX_LONG_PATH]; if (FAILED(PathCchCombine(testDir, MAX_LONG_PATH, tempDir, L"FileCheckTests"))) Throw(L"Combine fail", NULL);
	CreateDirectoryW(testDir, NULL);

	Test_TextAsciiIdentical(testDir);
	Test_TextAsciiDifferentContent(testDir);
	Test_CaseSensitivity_Sensitive(testDir);
	Test_CaseSensitivity_Insensitive(testDir);
	Test_Whitespace_Sensitive(testDir);
	Test_Whitespace_Insensitive(testDir);
	Test_Tabs_Expanded(testDir);
	Test_Tabs_Raw(testDir);
	Test_UnicodeUtf8Match(testDir);
	Test_UnicodeDiacritics(testDir);
	Test_UnicodeEmojiMultiline(testDir);
	Test_UnicodeBomEquivalence(testDir);
	Test_BinaryExactMatch(testDir);
	Test_BinaryMiddleDiff(testDir);
	Test_BinarySizeDiff(testDir);
	Test_AutoAsciiVsBinary(testDir);
	Test_AutoUnicodeVsBinary(testDir);
	Test_AutoBinaryVsEmpty(testDir);
	Test_UTF8WrapperValidPath(testDir);
	Test_UTF8WrapperInvalidPath(testDir);
	Test_ErrorNullPathPointer(testDir);
	Test_ErrorNullConfigPointer(testDir);

	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	WriteConsoleW(hConsole, L"\n\n", 2, NULL, NULL);
	WriteConsoleW(hConsole, L"Tests completed\n", 17, NULL, NULL);

	if (FAILURE == 0) {
		WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), L"All tests passed successfully.\n", 30, NULL, NULL);
		return 0;
	}
	else {
		int total = FAILURE + SUCCESS;
		WCHAR msg[128];
		swprintf_s(msg, sizeof(msg) / sizeof(WCHAR), L"%d/%d failed\n", FAILURE, total);
		WriteConsoleW(hConsole, msg, (DWORD)wcslen(msg), NULL, NULL);

		swprintf_s(msg, sizeof(msg) / sizeof(WCHAR), L"%d/%d passed\n", SUCCESS, total);
		WriteConsoleW(hConsole, msg, (DWORD)wcslen(msg), NULL, NULL);
		return 1;
	}
}
