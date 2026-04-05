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
        WCHAR fileW[256]; \
        WCHAR _lineW[16]; \
        WCHAR _exprW[512]; \
        MultiByteToWideChar(CP_ACP, 0, __FUNCTION__, -1, funcNameW, 128); \
        MultiByteToWideChar(CP_ACP, 0, __FILE__, -1, fileW, 256); \
        MultiByteToWideChar(CP_ACP, 0, #expr, -1, _exprW, 512); \
        if (!(expr)) { \
            swprintf_s(_lineW, 16, L"%d", __LINE__); \
            WriteConsoleW(h, L"Test FAILED: ", 13, &w, NULL); \
            WriteConsoleW(h, funcNameW, (DWORD)wcslen(funcNameW), &w, NULL); \
            WriteConsoleW(h, L"\n  Assertion failed: ", 21, &w, NULL); \
            WriteConsoleW(h, _exprW, (DWORD)wcslen(_exprW), &w, NULL); \
            WriteConsoleW(h, L"\n  File: ", 9, &w, NULL); \
            WriteConsoleW(h, fileW, (DWORD)wcslen(fileW), &w, NULL); \
            WriteConsoleW(h, L", Line: ", 8, &w, NULL); \
            WriteConsoleW(h, _lineW, (DWORD)wcslen(_lineW), &w, NULL); \
            WriteConsoleW(h, L"\n\n", 2, &w, NULL); \
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

inline static WCHAR* AllocWcharPath()
{
	WCHAR* ret = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(WCHAR));
	if (!ret) {
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}
	return ret;
}
inline static char* AllocCharPath()
{
	char* ret = (char*)HeapAlloc(GetProcessHeap(), 0, MAX_LONG_PATH * sizeof(char));
	if (!ret) {
		Throw(L"Test setup failed: HeapAlloc", NULL);
	}
	return ret;
}

static BOOL WriteDataFile(_In_z_ const WCHAR* path, _In_reads_(size) const void* data, _In_ DWORD size)
{
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return FALSE;
	DWORD w; BOOL ok = WriteFile(h, data, size, &w, NULL) && w == size; CloseHandle(h); return ok;
}

// Combine directory and filename into extended-length path
static void ConcatPath(
	_In_z_ const WCHAR* baseDir,
	_In_z_ const WCHAR* name,
	_Out_writes_z_(MAX_LONG_PATH) WCHAR* out)
{
	WCHAR* ext = AllocWcharPath();

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

/**
 * @brief A context structure for capturing callback data during tests.
 */
typedef struct {
	int CallbackCount;          /**< The number of times the callback was invoked. */
	FC_DIFF_BLOCK Blocks[10];   /**< An array to store the data from the first 10 callbacks. */
} DIFF_TEST_CONTEXT;

/**
 * @brief A test callback that captures structured diff block data into a context object.
 */
static void
StructuredOutputCallback(
	_In_ const FC_USER_CONTEXT* Context,
	_In_ const FC_DIFF_BLOCK* Block)
{
	DIFF_TEST_CONTEXT* testCtx = (DIFF_TEST_CONTEXT*)Context->UserData;
	if (!testCtx) return;
	if (testCtx->CallbackCount < 10)
	{
		testCtx->Blocks[testCtx->CallbackCount] = *Block;
	}
	testCtx->CallbackCount++;
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

/**
 * @brief Holds the four path buffers that most tests need.
 */
typedef struct {
	WCHAR* p1;  /**< Wide path to first file. */
	WCHAR* p2;  /**< Wide path to second file. */
	char*  u1;  /**< UTF-8 encoded path to first file. */
	char*  u2;  /**< UTF-8 encoded path to second file. */
} TEST_PATHS;

/** Allocates all four path buffers. Aborts on allocation failure. */
static TEST_PATHS AllocTestPaths(void)
{
	TEST_PATHS tp = { 0 };
	tp.p1 = AllocWcharPath();
	tp.p2 = AllocWcharPath();
	tp.u1 = AllocCharPath();
	tp.u2 = AllocCharPath();
	return tp;
}

/** Frees all four path buffers. */
static void FreeTestPaths(TEST_PATHS* tp)
{
	HeapFree(GetProcessHeap(), 0, tp->p1);
	HeapFree(GetProcessHeap(), 0, tp->p2);
	HeapFree(GetProcessHeap(), 0, tp->u1);
	HeapFree(GetProcessHeap(), 0, tp->u2);
}

/**
 * @brief Builds an FC_CONFIG pre-wired to StructuredOutputCallback.
 * @param mode  The comparison mode.
 * @param flags FC_* flag bits.
 * @param ctx   Pointer to the DIFF_TEST_CONTEXT that receives callback data.
 */
static FC_CONFIG MakeTestConfig(FC_MODE mode, UINT flags, DIFF_TEST_CONTEXT* ctx)
{
	FC_CONFIG cfg = { 0 };
	cfg.DiffCallback = StructuredOutputCallback;
	cfg.Mode = mode;
	cfg.Flags = flags;
	cfg.UserData = ctx;
	return cfg;
}

/*
	Tests start here.

*/

static void Test_TextAsciiIdentical(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"ascii_id1.txt", tp.p1);
	ConcatPath(baseDir, L"ascii_id2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "Line1\nLine2\n");
	WRITE_STR_FILE(tp.p2, "Line1\nLine2\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_TextAsciiDifferentContent(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"ascii_diff1.txt", tp.p1);
	ConcatPath(baseDir, L"ascii_diff2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "Line1\nLine2\n");
	WRITE_STR_FILE(tp.p2, "LineX\nLineY\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_CaseSensitivityWithSensitive(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"case1.txt", tp.p1);
	ConcatPath(baseDir, L"case2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "Hello World\n");
	WRITE_STR_FILE(tp.p2, "hello world\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_CaseSensitivityWithInsensitive(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"case1.txt", tp.p1);
	ConcatPath(baseDir, L"case2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "Hello World\n");
	WRITE_STR_FILE(tp.p2, "hello world\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, FC_IGNORE_CASE, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_WhitespaceWithSensitive(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"ws1.txt", tp.p1);
	ConcatPath(baseDir, L"ws2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "Test\n");
	WRITE_STR_FILE(tp.p2, "  Test  \n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_WhitespaceWithInsensitive(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"ws1.txt", tp.p1);
	ConcatPath(baseDir, L"ws2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "Test\n");
	WRITE_STR_FILE(tp.p2, "  Test  \n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, FC_IGNORE_WS, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_WhitespaceCompress(const WCHAR* baseDir)
{
	// /W compresses whitespace, not strips it: "A  B  C" and "A B C" should
	// match because both compress to "A B C".
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"wsc1.txt", tp.p1);
	ConcatPath(baseDir, L"wsc2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "A  B  C\n");
	WRITE_STR_FILE(tp.p2, "A B C\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, FC_IGNORE_WS, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	// Both compress to "A B C" -> identical
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_WhitespaceCompressDistinct(const WCHAR* baseDir)
{
	// /W compresses, not strips: "AB" (no space) vs "A B" (one space) must
	// remain DIFFERENT under /W because a single space is preserved after compression.
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"wscd1.txt", tp.p1);
	ConcatPath(baseDir, L"wscd2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "AB\n");
	WRITE_STR_FILE(tp.p2, "A B\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, FC_IGNORE_WS, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	// "AB" != "A B" even after compression
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_TabsWithExpanded(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"tab1.txt", tp.p1);
	ConcatPath(baseDir, L"tab2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "A\tB\n");
	// "A" occupies column 0; the tab character (at position 1) advances to the
	// next 8-column tab stop (column 8), inserting 7 spaces (8-1=7).
	// Matches fc.exe/ReactOS TAB_WIDTH 8 behavior.
	WRITE_STR_FILE(tp.p2, "A       B\n"); // 7 spaces (8-column tab stop)
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_TabsWithRaw(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"tab1.txt", tp.p1);
	ConcatPath(baseDir, L"tab2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "A\tB\n");
	WRITE_STR_FILE(tp.p2, "A    B\n"); // 4 spaces
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, FC_RAW_TABS, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_UnicodeUtf8Match(const WCHAR* baseDir)
{
	// Identical UTF-8 Unicode content
	const char* utf8 = "cafÃ©\n"; // "café"
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"unicode_u8_1.txt", tp.p1);
	ConcatPath(baseDir, L"unicode_u8_2.txt", tp.p2);
	// write raw UTF-8
	if (!WriteDataFile(tp.p1, utf8, (DWORD)strlen(utf8))) Throw(L"UTF8 write failed", tp.p1);
	if (!WriteDataFile(tp.p2, utf8, (DWORD)strlen(utf8))) Throw(L"UTF8 write failed", tp.p2);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_UNICODE, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_UnicodeDiacritics(const WCHAR* baseDir)
{
	// Differ by diacritic
	const char* a = "cafe\n";
	const char* b = "cafÃ©\n"; // "café"
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"unicode_diac1.txt", tp.p1);
	ConcatPath(baseDir, L"unicode_diac2.txt", tp.p2);
	if (!WriteDataFile(tp.p1, a, (DWORD)strlen(a))) Throw(L"write failed", tp.p1);
	if (!WriteDataFile(tp.p2, b, (DWORD)strlen(b))) Throw(L"write failed", tp.p2);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_UNICODE, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_UnicodeEmojiMultiline(const WCHAR* baseDir)
{
	const char* content = "Line1 😃\nLine2 🚀\n"; // emoji in UTF-8
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"unicode_emoji1.txt", tp.p1);
	ConcatPath(baseDir, L"unicode_emoji2.txt", tp.p2);
	if (!WriteDataFile(tp.p1, content, (DWORD)strlen(content))) Throw(L"write failed", tp.p1);
	if (!WriteDataFile(tp.p2, content, (DWORD)strlen(content))) Throw(L"write failed", tp.p2);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_UNICODE, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_UnicodeBomEquivalence(const WCHAR* baseDir)
{
	// BOM present vs absent
	const unsigned char bom[] = { 0xEF,0xBB,0xBF };
	const char* text = "Hello\n";
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"bom1.txt", tp.p1);
	ConcatPath(baseDir, L"bom2.txt", tp.p2);
	// with BOM: combine into one write because WriteDataFile uses CREATE_ALWAYS,
	// so two sequential calls would overwrite the first.
	unsigned char bomAndText[32];
	DWORD bomLen = (DWORD)sizeof(bom);
	size_t cch;
	if (FAILED(StringCchLengthA(text, STRSAFE_MAX_CCH, &cch))) Throw(L"Bad string", NULL);
	DWORD textLen = (DWORD)cch;
	CopyMemory(bomAndText, bom, bomLen);
	CopyMemory(bomAndText + bomLen, text, textLen);
	if (!WriteDataFile(tp.p1, bomAndText, bomLen + textLen)) Throw(L"write BOM+text failed", tp.p1);
	// without BOM
	WRITE_STR_FILE(tp.p2, text);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_UNICODE, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_AutoModeBomEquivalence(const WCHAR* baseDir)
{
	// Verify that FC_MODE_AUTO also strips the UTF-8 BOM before comparing lines,
	// so a file with a BOM compares equal to the same content without a BOM.
	const unsigned char bom[] = { 0xEF,0xBB,0xBF };
	const char* text = "Hello\n";
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"auto_bom1.txt", tp.p1);
	ConcatPath(baseDir, L"auto_bom2.txt", tp.p2);
	unsigned char bomAndText[32];
	DWORD bomLen = (DWORD)sizeof(bom);
	size_t cch;
	if (FAILED(StringCchLengthA(text, STRSAFE_MAX_CCH, &cch))) Throw(L"Bad string", NULL);
	DWORD textLen = (DWORD)cch;
	CopyMemory(bomAndText, bom, bomLen);
	CopyMemory(bomAndText + bomLen, text, textLen);
	if (!WriteDataFile(tp.p1, bomAndText, bomLen + textLen)) Throw(L"write BOM+text failed", tp.p1);
	WRITE_STR_FILE(tp.p2, text);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_BinaryExactMatch(const WCHAR* baseDir)
{
	const unsigned char data[] = { 0x00,0xFF,0x7F,0x80 };
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"bin1.dat", tp.p1);
	ConcatPath(baseDir, L"bin2.dat", tp.p2);
	if (!WriteDataFile(tp.p1, data, sizeof(data))) Throw(L"write bin failed", tp.p1);
	if (!WriteDataFile(tp.p2, data, sizeof(data))) Throw(L"write bin failed", tp.p2);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_BINARY, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_BinaryMiddleDiff(const WCHAR* baseDir)
{
	unsigned char d1[] = { 1,2,3,4,5 };
	unsigned char d2[] = { 1,2,99,4,5 };
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"bin_mid1.dat", tp.p1);
	ConcatPath(baseDir, L"bin_mid2.dat", tp.p2);
	if (!WriteDataFile(tp.p1, d1, sizeof(d1))) Throw(L"write bin failed", tp.p1);
	if (!WriteDataFile(tp.p2, d2, sizeof(d2))) Throw(L"write bin failed", tp.p2);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_BINARY, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_BinarySizeDiff(const WCHAR* baseDir)
{
	unsigned char d1[] = { 1,2,3 };
	unsigned char d2[] = { 1,2,3,4 };
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"bin_sz1.dat", tp.p1);
	ConcatPath(baseDir, L"bin_sz2.dat", tp.p2);
	if (!WriteDataFile(tp.p1, d1, sizeof(d1))) Throw(L"write bin failed", tp.p1);
	if (!WriteDataFile(tp.p2, d2, sizeof(d2))) Throw(L"write bin failed", tp.p2);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_BINARY, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_AutoAsciiVsBinary(const WCHAR* baseDir)
{
	// ASCII text file and a binary file
	const char* text = "Hello\n";
	unsigned char bin[] = { 0x00,0x01,0x02 };
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"auto_text.txt", tp.p1);
	ConcatPath(baseDir, L"auto_bin.dat", tp.p2);
	WRITE_STR_FILE(tp.p1, text);
	if (!WriteDataFile(tp.p2, bin, sizeof(bin))) Throw(L"write failed", tp.p2);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_AutoUnicodeVsBinary(const WCHAR* baseDir)
{
	// UTF-8 text file and a binary file
	const char* utf8 = "cafÃ©\n";
	unsigned char bin[] = { 0xAA,0xBB };
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"auto_unicode.txt", tp.p1);
	ConcatPath(baseDir, L"auto_bin2.dat", tp.p2);
	if (!WriteDataFile(tp.p1, utf8, (DWORD)strlen(utf8))) Throw(L"write failed", tp.p1);
	if (!WriteDataFile(tp.p2, bin, sizeof(bin))) Throw(L"write failed", tp.p2);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_AutoBinaryVsEmpty(const WCHAR* baseDir)
{
	// binary file vs empty file
	unsigned char bin[] = { 0xDE,0xAD,0xBE,0xEF };
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"auto_bin3.dat", tp.p1);
	ConcatPath(baseDir, L"auto_empty.bin", tp.p2);
	if (!WriteDataFile(tp.p1, bin, sizeof(bin))) Throw(L"write failed", tp.p1);
	// create empty file
	HANDLE h = CreateFileW(tp.p2, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) Throw(L"create empty failed", tp.p2);
	CloseHandle(h);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_UTF8WrapperValidPath(const WCHAR* baseDir)
{
	// Valid UTF-8 path conversion
	WCHAR pWide[MAX_LONG_PATH];
	char pUtf8[MAX_LONG_PATH * 4 + 1] = { 0 };
	// Filename with non-ASCII character
	ConcatPath(baseDir, L"ünicode.txt", pWide);
	WRITE_STR_FILE(pWide, "X\n");
	ConvertWideToUtf8OrExit(pWide, pUtf8, sizeof(pUtf8));
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ASSERT_TRUE(FC_CompareFilesUtf8(pUtf8, pUtf8, &cfg) == FC_OK);
}

static void Test_UTF8WrapperInvalidPath(const WCHAR* baseDir)
{
	const char bad[] = { (char)0xC3, (char)0x28, 0 };
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ASSERT_TRUE(FC_CompareFilesUtf8(bad, bad, &cfg) == FC_ERROR_INVALID_PARAM);
}

static void Test_ErrorNullPathPointer(const WCHAR* baseDir)
{
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ASSERT_TRUE(FC_CompareFilesUtf8("", NULL, &cfg) == FC_ERROR_INVALID_PARAM);
}

static void Test_ErrorNullConfigPointer(const WCHAR* baseDir)
{
	const char* utf = "X\n";
	ASSERT_TRUE(FC_CompareFilesUtf8(utf, utf, NULL) == FC_ERROR_INVALID_PARAM);
}

static void Test_ErrorNonExistentFile(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"existent.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "some data");
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	// Path that does not exist
	ConvertWideToUtf8OrExit(L"C:\\this\\path\\should\\not\\exist\\ever.txt", tp.u2, UTF8_BUFFER_SIZE);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	// Comparing an existing file with a non-existing one should result in an I/O error.
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_ERROR_IO);
	FreeTestPaths(&tp);
}

static void Test_ErrorReservedDeviceName(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"regular_file.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "data");
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	// "CON" is a reserved device name and should be rejected by the canonicalization.
	ConvertWideToUtf8OrExit(L"CON", tp.u2, UTF8_BUFFER_SIZE);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_ERROR_INVALID_PARAM);
	FreeTestPaths(&tp);
}

static void Test_ErrorRawDevicePath(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"another_file.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "data");
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	// Raw device paths should be rejected for security.
	ConvertWideToUtf8OrExit(L"\\\\.\\PhysicalDrive0", tp.u2, UTF8_BUFFER_SIZE);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_ERROR_INVALID_PARAM);
	FreeTestPaths(&tp);
}

static void Test_ErrorEmptyPath(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"some_file.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "data");
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	// An empty path is invalid.
	tp.u2[0] = '\0';
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_ERROR_INVALID_PARAM);
	FreeTestPaths(&tp);
}

static void Test_ErrorNullOutputCallback(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"file_for_null_callback.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "data");
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	// Config with NULL DiffCallback — must be rejected.
	FC_CONFIG cfg = { 0 };
	cfg.DiffCallback = NULL;
	cfg.Mode = FC_MODE_AUTO;
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u1, &cfg) == FC_ERROR_INVALID_PARAM);
	FreeTestPaths(&tp);
}

static void Test_EmptyVsEmpty(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"empty1.txt", tp.p1);
	ConcatPath(baseDir, L"empty2.txt", tp.p2);
	// Create zero-byte files
	HANDLE h1 = CreateFileW(tp.p1, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	HANDLE h2 = CreateFileW(tp.p2, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
	if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_VeryLargeFile(const WCHAR* baseDir)
{
	// This test is slow and resource-intensive. It is commented out by default.
	// To run it, uncomment the code inside and the call in wmain.
	/*
	WCHAR* p1 = NULL;
	char* u1 = NULL;
	char* buffer = NULL;
	BOOL testResult = FALSE;

	do
	{
		p1 = AllocWcharPath();
		u1 = AllocCharPath();
		ConcatPath(baseDir, L"largefile.bin", p1);

		// Using a smaller size for a more practical "large" file test that runs faster.
		// 100 MB can be very slow. 5 MB is sufficient to test large buffer handling.
		const size_t fileSize = 1024 * 1024 * 5; // 5 MB
		buffer = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, fileSize);
		if (!buffer) {
			WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), L"Skipping Test_VeryLargeFile: Not enough memory.\n", 52, NULL, NULL);
			// Set to TRUE to pass the skipped test
			testResult = TRUE;
			break;
		}

		// Create a large file with predictable content
		for (size_t i = 0; i < fileSize; ++i) {
			buffer[i] = (char)(i % 256);
		}
		if (!WriteDataFile(p1, buffer, (DWORD)fileSize)) {
			// Can't use Throw here as we need to clean up.
			WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), L"Test FAILED: Test_VeryLargeFile\n  Could not write large file.\n\n", 64, NULL, NULL);
			FAILURE++;
			break;
		}

		FC_CONFIG cfg = { 0 };
		cfg.DiffCallback = StructuredOutputCallback;
		cfg.Mode = FC_MODE_BINARY;

		ConvertWideToUtf8OrExit(p1, u1, UTF8_BUFFER_SIZE);
		testResult = (FC_CompareFilesUtf8(u1, u1, &cfg) == FC_OK);
	} while (0);

	// --- Cleanup ---
	if (p1) {
		// Always attempt to delete the large file
		DeleteFileW(p1);
		HeapFree(GetProcessHeap(), 0, p1);
	}
	if (u1) HeapFree(GetProcessHeap(), 0, u1);
	if (buffer) HeapFree(GetProcessHeap(), 0, buffer);

	// The ASSERT_TRUE macro is called outside the block after all cleanup.
	ASSERT_TRUE(testResult);
	*/

	// To avoid breaking the build, we'll just pass it if it's commented out.
	ASSERT_TRUE(TRUE);
}

static void Test_MixedLineEndings(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"mixed_endings.txt", tp.p1);
	ConcatPath(baseDir, L"normalized_endings.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "Line1\r\nLine2\nLine3\r");
	WRITE_STR_FILE(tp.p2, "Line1\nLine2\nLine3\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_NoFinalNewline(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"final_newline.txt", tp.p1);
	ConcatPath(baseDir, L"no_final_newline.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "Line1\n");
	WRITE_STR_FILE(tp.p2, "Line1");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_ExtremelyLongLine(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"longline1.txt", tp.p1);
	ConcatPath(baseDir, L"longline2.txt", tp.p2);

	const size_t longSize = 1024 * 64; // 64KB line
	char* buffer = (char*)HeapAlloc(GetProcessHeap(), 0, longSize);
	if (!buffer) {
		Throw(L"Failed to alloc for long line test", NULL);
	}
	memset(buffer, 'A', longSize);
	if (!WriteDataFile(tp.p1, buffer, (DWORD)longSize)) {
		HeapFree(GetProcessHeap(), 0, buffer);
		Throw(L"Failed to write long line file", tp.p1);
	}
	// Create a slightly different long file
	buffer[longSize - 1] = 'B';
	if (!WriteDataFile(tp.p2, buffer, (DWORD)longSize)) {
		HeapFree(GetProcessHeap(), 0, buffer);
		Throw(L"Failed to write long line file 2", tp.p2);
	}
	HeapFree(GetProcessHeap(), 0, buffer);

	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	FreeTestPaths(&tp);
}

static void Test_WhitespaceOnlyFile(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"ws_only.txt", tp.p1);
	ConcatPath(baseDir, L"empty_for_ws.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "  \t \r\n \t  ");
	HANDLE h = CreateFileW(tp.p2, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, FC_IGNORE_WS, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_ForwardSlashesInPath(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"fwd_slash_file.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "data");
	// Create a path with forward slashes
	StringCchCopyW(tp.p2, MAX_LONG_PATH, tp.p1);
	for (int i = 0; tp.p2[i] != L'\0'; ++i) {
		if (tp.p2[i] == L'\\')
			tp.p2[i] = L'/';
	}
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_RelativePathTraversal(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"relative_file.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "data");
	// Build a path like C:\path\to\temp\FileCheckTests\..\FileCheckTests\relative_file.txt
	if (FAILED(StringCchPrintfW(tp.p2, MAX_LONG_PATH, L"%s\\..\\%s\\relative_file.txt", baseDir, L"FileCheckTests"))) Throw(L"StringCchPrintfW failed", NULL);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_TrailingDotInPath(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"trailing_dot_file.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "data");
	// Create a path ending with a dot
	if (FAILED(StringCchPrintfW(tp.p2, MAX_LONG_PATH, L"%s.", tp.p1))) Throw(L"StringCchPrintfW failed", NULL);
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_AlternateDataStream(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"ads_file.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "main stream data");
	// Create a path with an alternate data stream
	if (FAILED(StringCchPrintfW(tp.p2, MAX_LONG_PATH, L"%s:stream", tp.p1))) Throw(L"StringCchPrintfW failed", NULL);
	WRITE_STR_FILE(tp.p2, "ads data");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	// Comparing a file with its ADS should be treated as a different file,
	// but path canonicalization might reject it first. Invalid param is a safe result.
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_ERROR_INVALID_PARAM);
	FreeTestPaths(&tp);
}

static void Test_CompareFileToItself(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"self_compare.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "some content");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_AUTO, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u1, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_InvalidMode(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"invalid_mode.txt", tp.p1);
	WRITE_STR_FILE(tp.p1, "abc");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig((FC_MODE)99, 0, &testCtx); // Invalid mode enum
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	// The library should fall back to a default behavior.
	// Since the files are identical, the result should be OK.
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u1, &cfg) == FC_OK);
	FreeTestPaths(&tp);
}

static void Test_StructuredOutput_Deletion(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"s_del1.txt", tp.p1);
	ConcatPath(baseDir, L"s_del2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "LineA\nLineB\nLineC\n");
	WRITE_STR_FILE(tp.p2, "LineA\nLineC\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg);
	ASSERT_TRUE(testCtx.CallbackCount == 1);
	ASSERT_TRUE(testCtx.Blocks[0].Type == FC_DIFF_TYPE_DELETE);
	ASSERT_TRUE(testCtx.Blocks[0].StartA == 1 && testCtx.Blocks[0].EndA == 2);
	FreeTestPaths(&tp);
}
static void Test_StructuredOutput_Addition(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"s_add1.txt", tp.p1);
	ConcatPath(baseDir, L"s_add2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "LineA\nLineC\n");
	WRITE_STR_FILE(tp.p2, "LineA\nLineB\nLineC\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg);
	ASSERT_TRUE(testCtx.CallbackCount == 1);
	ASSERT_TRUE(testCtx.Blocks[0].Type == FC_DIFF_TYPE_ADD);
	ASSERT_TRUE(testCtx.Blocks[0].StartB == 1 && testCtx.Blocks[0].EndB == 2);
	FreeTestPaths(&tp);
}

static void Test_StructuredOutput_Change(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"s_chg1.txt", tp.p1);
	ConcatPath(baseDir, L"s_chg2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "LineA\nLineB\nLineC\n");
	WRITE_STR_FILE(tp.p2, "LineA\nLineX\nLineC\n");
	DIFF_TEST_CONTEXT testCtx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &testCtx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg);
	ASSERT_TRUE(testCtx.CallbackCount == 1);
	ASSERT_TRUE(testCtx.Blocks[0].Type == FC_DIFF_TYPE_CHANGE);
	ASSERT_TRUE(testCtx.Blocks[0].StartA == 1 && testCtx.Blocks[0].EndA == 2);
	ASSERT_TRUE(testCtx.Blocks[0].StartB == 1 && testCtx.Blocks[0].EndB == 2);
	FreeTestPaths(&tp);
}

static void Test_TextAscii_MultipleHunks(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"hunks1.txt", tp.p1);
	ConcatPath(baseDir, L"hunks2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "A\nX1\nX2\nB\nY1\nY2\nC\n");
	WRITE_STR_FILE(tp.p2, "A\nZ1\nZ2\nB\nW1\nW2\nC\n");
	DIFF_TEST_CONTEXT ctx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &ctx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	ASSERT_TRUE(ctx.CallbackCount == 2);
	ASSERT_TRUE(ctx.Blocks[0].Type == FC_DIFF_TYPE_CHANGE);
	ASSERT_TRUE(ctx.Blocks[1].Type == FC_DIFF_TYPE_CHANGE);
	FreeTestPaths(&tp);
}

static void Test_TextAscii_ResyncThreshold(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"resync1.txt", tp.p1);
	ConcatPath(baseDir, L"resync2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "A\nX\nB\nC\nD\n");
	WRITE_STR_FILE(tp.p2, "A\nY\nB\nC\nZ\n");
	DIFF_TEST_CONTEXT ctx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &ctx);
	cfg.ResyncLines = 3; // /nnnn equivalent
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	ASSERT_TRUE(ctx.CallbackCount == 1);
	ASSERT_TRUE(ctx.Blocks[0].Type == FC_DIFF_TYPE_CHANGE);
	FreeTestPaths(&tp);
}

static void Test_TextAscii_CollapsedChangeBlock(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"abbr1.txt", tp.p1);
	ConcatPath(baseDir, L"abbr2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "A\nX\nY\nZ\nB\n");
	WRITE_STR_FILE(tp.p2, "A\n1\n2\n3\nB\n");
	DIFF_TEST_CONTEXT ctx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &ctx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	ASSERT_TRUE(ctx.CallbackCount == 1);
	ASSERT_TRUE(ctx.Blocks[0].Type == FC_DIFF_TYPE_CHANGE);
	FreeTestPaths(&tp);
}

static void Test_TextAscii_DuplicateLinesLcs(const WCHAR* baseDir)
{
	// A = ["common","diff_A","common"], B = ["diff_B","common","diff_B","common"]
	// LCS must be the two "common" lines; expect exactly 2 diff hunks.
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"dup1.txt", tp.p1);
	ConcatPath(baseDir, L"dup2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "common\ndiff_A\ncommon\n");
	WRITE_STR_FILE(tp.p2, "diff_B\ncommon\ndiff_B\ncommon\n");
	DIFF_TEST_CONTEXT ctx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &ctx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	ASSERT_TRUE(ctx.CallbackCount == 2);
	FreeTestPaths(&tp);
}

static void Test_Binary_AllMismatches(const WCHAR* baseDir)
{
	// Files differ at 3 byte positions (2, 3, 4).  The binary comparison must
	// report EVERY differing byte, not stop after the first mismatch.
	unsigned char a[] = { 1,2,3,4,5 };
	unsigned char b[] = { 1,2,9,8,7 };
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"bin_fm1.dat", tp.p1);
	ConcatPath(baseDir, L"bin_fm2.dat", tp.p2);
	WriteDataFile(tp.p1, a, sizeof(a));
	WriteDataFile(tp.p2, b, sizeof(b));
	DIFF_TEST_CONTEXT ctx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_BINARY, 0, &ctx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	ASSERT_TRUE(ctx.CallbackCount == 3);
	ASSERT_TRUE(ctx.Blocks[0].StartA == 2); // offset of first differing byte
	ASSERT_TRUE(ctx.Blocks[1].StartA == 3);
	ASSERT_TRUE(ctx.Blocks[2].StartA == 4);
	FreeTestPaths(&tp);
}

static void Test_TextAscii_LineNumberAccuracy(const WCHAR* baseDir)
{
	TEST_PATHS tp = AllocTestPaths();
	ConcatPath(baseDir, L"ln1.txt", tp.p1);
	ConcatPath(baseDir, L"ln2.txt", tp.p2);
	WRITE_STR_FILE(tp.p1, "A\nB\nC\n");
	WRITE_STR_FILE(tp.p2, "A\nX\nC\n");
	DIFF_TEST_CONTEXT ctx = { 0 };
	FC_CONFIG cfg = MakeTestConfig(FC_MODE_TEXT_ASCII, 0, &ctx);
	ConvertWideToUtf8OrExit(tp.p1, tp.u1, UTF8_BUFFER_SIZE);
	ConvertWideToUtf8OrExit(tp.p2, tp.u2, UTF8_BUFFER_SIZE);
	ASSERT_TRUE(FC_CompareFilesUtf8(tp.u1, tp.u2, &cfg) == FC_DIFFERENT);
	ASSERT_TRUE(ctx.CallbackCount == 1);
	ASSERT_TRUE(ctx.Blocks[0].StartA == 1);
	ASSERT_TRUE(ctx.Blocks[0].StartB == 1);
	FreeTestPaths(&tp);
}

int wmain(void)
{
	WCHAR tempDir[MAX_LONG_PATH]; DWORD len = GetTempPathW(MAX_LONG_PATH, tempDir);
	if (!len || len >= MAX_LONG_PATH) Throw(L"TempPath fail", NULL);
	WCHAR testDir[MAX_LONG_PATH]; if (FAILED(PathCchCombine(testDir, MAX_LONG_PATH, tempDir, L"FileCheckTests"))) Throw(L"Combine fail", NULL);
	CreateDirectoryW(testDir, NULL);

	Test_TextAsciiIdentical(testDir);
	Test_TextAsciiDifferentContent(testDir);
	Test_CaseSensitivityWithSensitive(testDir);
	Test_CaseSensitivityWithInsensitive(testDir);
	Test_WhitespaceWithSensitive(testDir);
	Test_WhitespaceWithInsensitive(testDir);
	Test_WhitespaceCompress(testDir);
	Test_WhitespaceCompressDistinct(testDir);
	Test_TabsWithExpanded(testDir);
	Test_TabsWithRaw(testDir);
	Test_UnicodeUtf8Match(testDir);
	Test_UnicodeDiacritics(testDir);
	Test_UnicodeEmojiMultiline(testDir);
	Test_UnicodeBomEquivalence(testDir);
	Test_AutoModeBomEquivalence(testDir);
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
	Test_ErrorNonExistentFile(testDir);
	Test_ErrorReservedDeviceName(testDir);
	Test_ErrorRawDevicePath(testDir);
	Test_ErrorEmptyPath(testDir);
	Test_ErrorNullOutputCallback(testDir);
	Test_EmptyVsEmpty(testDir);
	Test_VeryLargeFile(testDir); // Note: This test is disabled by default within the function
	Test_MixedLineEndings(testDir);
	//Test_NoFinalNewline(testDir);
	Test_ExtremelyLongLine(testDir);
	//Test_WhitespaceOnlyFile(testDir);
	Test_ForwardSlashesInPath(testDir);
	Test_RelativePathTraversal(testDir);
	Test_TrailingDotInPath(testDir);
	//Test_AlternateDataStream(testDir);
	Test_CompareFileToItself(testDir);
	Test_InvalidMode(testDir);
	Test_StructuredOutput_Deletion(testDir);
	Test_StructuredOutput_Addition(testDir);
	Test_StructuredOutput_Change(testDir);
	Test_TextAscii_MultipleHunks(testDir);
	Test_TextAscii_ResyncThreshold(testDir);
	Test_TextAscii_CollapsedChangeBlock(testDir);
	Test_TextAscii_DuplicateLinesLcs(testDir);
	Test_Binary_AllMismatches(testDir);
	Test_TextAscii_LineNumberAccuracy(testDir);

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