/*
 * FilecheckApp.c - CLI application using filecheck.h
 * Provides feature-compatible interface to fc.exe using header-only library.
 */
#include "filecheck.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void default_output(void* user_data, const char* message, int line1, int line2) {
	if (line1 >= 0 && line2 >= 0) {
		printf("%s (Line %d vs %d)\n", message, line1, line2);
	}
	else {
		printf("%s\n", message);
	}
}

static const char* get_basename(const char* path) {
	const char* base = strrchr(path, '\\');
	return base ? base + 1 : path;
}

static void print_usage(const char* prog) {
	printf("Usage: %s [options] file1 file2\n", prog);
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

int main(int argc, char* argv[]) {
	if (argc < 3) {
		const char* progname = get_basename(argv[0]);
		print_usage(progname);
		return -1; /* syntax error */
	}

	fc_config_t cfg = {0}; /* Initialize all fields to zero */
	/* set defaults */
	cfg.mode = FC_MODE_TEXT;
	cfg.flags = 0;
	cfg.resync_lines = 2;
	cfg.buffer_lines = 100;
	cfg.output = default_output;
	cfg.user_data = NULL;

	int i = 1;
	for (; i < argc - 2; ++i) {
		char* opt = argv[i];
		if (opt[0] == '/' || opt[0] == '-') {
			if (isdigit((unsigned char)opt[1])) {
				cfg.resync_lines = atoi(opt + 1);
			}
			else if (strncmp(opt + 1, "LB", 2) == 0 && isdigit((unsigned char)opt[3 - 1])) {
				cfg.buffer_lines = atoi(opt + 3);
			}
			else {
				switch (toupper((unsigned char)opt[1])) {
				case 'B': cfg.mode = FC_MODE_BINARY; break;
				case 'C': cfg.flags |= FC_IGNORE_CASE; break;
				case 'W': cfg.flags |= FC_IGNORE_WS; break;
				case 'L': cfg.mode = FC_MODE_TEXT; break;
				case 'N': cfg.flags |= FC_SHOW_LINE_NUMS; break;
				case 'T': cfg.flags |= FC_RAW_TABS; break;
				case 'U': cfg.flags |= FC_UNICODE_TEXT; break;
				default:
					printf("Invalid option: %s\n", opt);
					return -1;
				}
			}
		}
		else {
			printf("Invalid argument: %s\n", opt);
			return -1;
		}
	}
	const char* file1 = argv[argc - 2];
	const char* file2 = argv[argc - 1];

	fc_result_t res = fc_compare_files(file1, file2, &cfg);
	if (res == FC_OK) {
		/* identical */
		return 0;
	}
	else if (res == FC_DIFFERENT) {
		/* differences found */
		return 1;
	}
	else if (res == FC_ERROR_IO || res == FC_ERROR_MEMORY) {
		fprintf(stderr, "Error during comparison: %d\n", res);
		return 2;
	}
	else {
		/* invalid param */
		return -1;
	}
}
