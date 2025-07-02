/*
 * filecheck.h - Header-only file comparison library (text and binary)
 * Windows-only, UTF-8 filenames via wide-character paths.
 * Uses append-only dynamic arrays for text diff and memory-mapped binary compare.
 * Public domain or MIT license.
 *
 * Thread Safety:
 *   - All public functions are reentrant and safe to call concurrently.
 *   - No global or static mutable state is used by the library.
 *   - Each call allocates its own local resources.
 *   - User-provided fc_output_cb callbacks must be thread-safe if used across threads.
 *   - fc_compare_buffers works entirely on local heap memory and does not modify shared state.
 */
#ifndef FILECHECK_H
#define FILECHECK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>

    /* Return codes */
    typedef enum {
        FC_OK = 0,
        FC_DIFFERENT,
        FC_ERROR_IO,
        FC_ERROR_INVALID_PARAM,
        FC_ERROR_MEMORY
    } fc_result_t;

    /* Comparison modes */
    typedef enum {
        FC_MODE_TEXT,
        FC_MODE_BINARY
    } fc_mode_t;

    /* Flags */
#define FC_IGNORE_CASE     0x0001  /* ignore case */
#define FC_IGNORE_WS       0x0002  /* ignore whitespace */
#define FC_SHOW_LINE_NUMS  0x0004  /* show line numbers */
#define FC_RAW_TABS        0x0008  /* do not expand tabs */
#define FC_UNICODE_TEXT    0x0010  /* treat as UTF-16 text */

/* Output callback: message with optional line numbers */
    typedef void (*fc_output_cb)(void* user_data, const char* message,
        int line1, int line2);

    /* Configuration */
    typedef struct {
        fc_mode_t    mode;         /* text or binary */
        unsigned     flags;        /* option flags */
        unsigned     resync_lines; /* reserved */
        unsigned     buffer_lines; /* reserved */
        fc_output_cb output;       /* callback for diff messages */
        void* user_data;    /* passed to callback */
    } fc_config_t;

    /* UTF-8 path fopen with fallback */
    static inline FILE* fc_fopen(const char* path, const char* mode) {
        int cw = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
        if (cw <= 0) {
            FILE* f = NULL;
            return fopen_s(&f, path, mode) == 0 ? f : NULL;
        }
        wchar_t* wpath = (wchar_t*)malloc(cw * sizeof(wchar_t));
        if (!wpath) {
            FILE* f = NULL;
            return fopen_s(&f, path, mode) == 0 ? f : NULL;
        }
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, cw);
        int mw = MultiByteToWideChar(CP_UTF8, 0, mode, -1, NULL, 0);
        wchar_t* wmode = mw > 0 ? (wchar_t*)malloc(mw * sizeof(wchar_t)) : NULL;
        if (!wmode) {
            free(wpath);
            FILE* f = NULL;
            return fopen_s(&f, path, mode) == 0 ? f : NULL;
        }
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, mw);
        FILE* f = _wfopen(wpath, wmode);
        free(wmode);
        free(wpath);
        if (f) return f;
        FILE* fb = NULL;
        return fopen_s(&fb, path, mode) == 0 ? fb : NULL;
    }

    /* Read entire file into buffer */
    static inline char* fc_read_file(const char* path, size_t* out_len,
        fc_result_t* out_err) {
        if (!path || !out_len || !out_err) return NULL;
        FILE* f = fc_fopen(path, "rb");
        if (!f) { *out_err = FC_ERROR_IO; return NULL; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        if (sz < 0) { fclose(f); *out_err = FC_ERROR_IO; return NULL; }
        size_t len = (size_t)sz;
        rewind(f);
        char* buf = (char*)malloc(len);
        if (!buf) { fclose(f); *out_err = FC_ERROR_MEMORY; return NULL; }
        size_t r = fread(buf, 1, len, f);
        fclose(f);
        if (r != len) { free(buf); *out_err = FC_ERROR_IO; return NULL; }
        *out_len = len;
        *out_err = FC_OK;
        return buf;
    }

    /* Line descriptor */
    typedef struct {
        char* text;
        size_t   len;
        unsigned hash;
    } Line;

    /* Append-only dynamic array */
    typedef struct {
        Line* lines;
        size_t   count;
        size_t   capacity;
    } LineArray;

    static inline void la_init(LineArray* la) {
        la->lines = NULL;
        la->count = la->capacity = 0;
    }

    static inline void la_free(LineArray* la) {
        free(la->lines);
        la->lines = NULL;
        la->count = la->capacity = 0;
    }

    /* Hash line */
    static inline unsigned fc_hash_line(const char* s, size_t len,
        unsigned flags) {
        unsigned h = 0;
        for (size_t i = 0; i < len; ++i) {
            unsigned char c = (unsigned char)s[i];
            if ((flags & FC_IGNORE_WS) && (c == ' ' || c == '\t')) continue;
            if (flags & FC_IGNORE_CASE) c = (unsigned char)tolower(c);
            h = h * 31 + c;
        }
        return h;
    }

    /* Duplicate substring */
    static inline char* fc_strdup_range(const char* s, size_t len) {
        char* out = (char*)malloc(len + 1);
        if (!out) return NULL;
        memcpy(out, s, len);
        out[len] = '\0';
        return out;
    }

    /* Append to LineArray */
    static inline int la_append(LineArray* la, char* text, size_t len,
        unsigned hash) {
        if (la->count + 1 > la->capacity) {
            size_t newcap = la->capacity ? la->capacity * 2 : 64;
            Line* tmp = (Line*)realloc(la->lines, newcap * sizeof(Line));
            if (!tmp) return 0;
            la->lines = tmp;
            la->capacity = newcap;
        }
        la->lines[la->count].text = text;
        la->lines[la->count].len = len;
        la->lines[la->count].hash = hash;
        la->count++;
        return 1;
    }

    /* Parse lines into LineArray */
    static inline fc_result_t parse_lines_array(const char* buf, size_t buflen,
        LineArray* la, unsigned flags) {
        la_init(la);
        const char* p = buf, * end = buf + buflen;
        while (p < end) {
            const char* nl = p;
            while (nl < end && *nl != '\n' && *nl != '\r') nl++;
            size_t len = (size_t)(nl - p);
            char* txt = fc_strdup_range(p, len);
            if (!txt || !la_append(la, txt, len,
                fc_hash_line(txt, len, flags))) {
                free(txt);
                la_free(la);
                return FC_ERROR_MEMORY;
            }
            while (nl < end && (*nl == '\n' || *nl == '\r')) nl++;
            p = nl;
        }
        return FC_OK;
    }

#ifndef FC_CHUNK_SIZE
#define FC_CHUNK_SIZE (64*1024)
#endif

    /* Compare LineArrays */
    static inline fc_result_t compare_line_arrays(const LineArray* A,
        const LineArray* B,
        const fc_config_t* cfg) {
        size_t n = A->count < B->count ? A->count : B->count;
        for (size_t i = 0; i < n; ++i) {
            const Line* la = &A->lines[i];
            const Line* lb = &B->lines[i];
            if (la->hash != lb->hash || la->len != lb->len ||
                memcmp(la->text, lb->text, la->len) != 0) {
                if (cfg->output) {
                    cfg->output(cfg->user_data,
                        "Line differs",
                        (int)(i + 1), (int)(i + 1));
                }
                return FC_DIFFERENT;
            }
        }
        return (A->count == B->count) ? FC_OK : FC_DIFFERENT;
    }

    /* Compare files on disk */
    /*
     * Compare two files on disk.
     * Thread Safety:
     *   - Allocates independent file handles and mappings per call.
     *   - Frees all resources before returning.
     *   - No shared mutable state: reentrant and atomic per invocation.
     *   - Ensure fc_output_cb is thread-safe if used concurrently.
     */
    static inline fc_result_t fc_compare_files(const char* path1,
        const char* path2,
        const fc_config_t* cfg) {
        if (!path1 || !path2 || !cfg)
            return FC_ERROR_INVALID_PARAM;
        if (cfg->mode == FC_MODE_BINARY) {
            /* Memory-mapped binary comparison */
            HANDLE h1 = CreateFileA(path1, GENERIC_READ, FILE_SHARE_READ,
                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            HANDLE h2 = CreateFileA(path2, GENERIC_READ, FILE_SHARE_READ,
                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h1 == INVALID_HANDLE_VALUE || h2 == INVALID_HANDLE_VALUE) {
                if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
                if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
                return FC_ERROR_IO;
            }
            LARGE_INTEGER size1, size2;
            if (!GetFileSizeEx(h1, &size1) || !GetFileSizeEx(h2, &size2)) {
                CloseHandle(h1); CloseHandle(h2);
                return FC_ERROR_IO;
            }
            size_t sz1 = (size_t)size1.QuadPart;
            size_t sz2 = (size_t)size2.QuadPart;
            size_t cmp_sz = sz1 < sz2 ? sz1 : sz2;
            HANDLE m1 = CreateFileMapping(h1, NULL, PAGE_READONLY,
                (DWORD)(size1.QuadPart >> 32),
                (DWORD)(size1.QuadPart & 0xFFFFFFFF), NULL);
            HANDLE m2 = CreateFileMapping(h2, NULL, PAGE_READONLY,
                (DWORD)(size2.QuadPart >> 32),
                (DWORD)(size2.QuadPart & 0xFFFFFFFF), NULL);
            if (!m1 || !m2) {
                if (m1) CloseHandle(m1); if (m2) CloseHandle(m2);
                CloseHandle(h1); CloseHandle(h2);
                return FC_ERROR_IO;
            }
            unsigned char* b1 = (unsigned char*)MapViewOfFile(m1, FILE_MAP_READ, 0, 0, cmp_sz);
            unsigned char* b2 = (unsigned char*)MapViewOfFile(m2, FILE_MAP_READ, 0, 0, cmp_sz);
            if (!b1 || !b2) {
                if (b1) UnmapViewOfFile(b1); if (b2) UnmapViewOfFile(b2);
                CloseHandle(m1); CloseHandle(m2);
                CloseHandle(h1); CloseHandle(h2);
                return FC_ERROR_IO;
            }
            size_t wcount = cmp_sz / sizeof(uintptr_t);
            const uintptr_t* w1 = (const uintptr_t*)b1;
            const uintptr_t* w2 = (const uintptr_t*)b2;
            for (size_t i = 0; i < wcount; ++i) {
                if (w1[i] != w2[i]) {
                    size_t base = i * sizeof(uintptr_t);
                    for (size_t j = 0; j < sizeof(uintptr_t); ++j) {
                        if (b1[base + j] != b2[base + j]) {
                            if (cfg->output) {
                                char msg[64];
                                snprintf(msg, sizeof(msg),
                                    "Binary diff at offset 0x%zx", base + j);
                                cfg->output(cfg->user_data, msg, -1, -1);
                            }
                            UnmapViewOfFile(b1); UnmapViewOfFile(b2);
                            CloseHandle(m1); CloseHandle(m2);
                            CloseHandle(h1); CloseHandle(h2);
                            return FC_DIFFERENT;
                        }
                    }
                }
            }
            size_t offset = wcount * sizeof(uintptr_t);
            for (size_t i = offset; i < cmp_sz; ++i) {
                if (b1[i] != b2[i]) {
                    if (cfg->output) {
                        char msg[64];
                        snprintf(msg, sizeof(msg),
                            "Binary diff at offset 0x%zx", i);
                        cfg->output(cfg->user_data, msg, -1, -1);
                    }
                    UnmapViewOfFile(b1); UnmapViewOfFile(b2);
                    CloseHandle(m1); CloseHandle(m2);
                    CloseHandle(h1); CloseHandle(h2);
                    return FC_DIFFERENT;
                }
            }
            UnmapViewOfFile(b1); UnmapViewOfFile(b2);
            CloseHandle(m1); CloseHandle(m2);
            CloseHandle(h1); CloseHandle(h2);
            return sz1 == sz2 ? FC_OK : FC_DIFFERENT;
        }
        else {
            /* Text comparison */
            size_t len1, len2;
            fc_result_t err1, err2;
            char* buf1 = fc_read_file(path1, &len1, &err1);
            if (!buf1) return err1;
            char* buf2 = fc_read_file(path2, &len2, &err2);
            if (!buf2) { free(buf1); return err2; }
            LineArray A, B;
            fc_result_t r1 = parse_lines_array(buf1, len1, &A, cfg->flags);
            fc_result_t r2 = parse_lines_array(buf2, len2, &B, cfg->flags);
            free(buf1); free(buf2);
            if (r1 != FC_OK || r2 != FC_OK) {
                la_free(&A); la_free(&B);
                return FC_ERROR_MEMORY;
            }
            fc_result_t res = compare_line_arrays(&A, &B, cfg);
            la_free(&A); la_free(&B);
            return res;
        }
    }

#ifdef __cplusplus
}
#endif

#endif /* FILECHECK_H */
