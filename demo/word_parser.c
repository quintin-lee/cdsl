/**
 * @file word_parser.c
 * @brief Word document parsing demo.
 *
 * Demonstrates how to use the cdsl_doc_* API to extract structured
 * information from Word documents (.doc, .docx) into JSON format.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include "cdsl/doc.h"

int
main(int argc, char* argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <word_file_path>\n", argv[0]);
		return 1;
	}

	char abs_path[PATH_MAX];
	if (!realpath(argv[1], abs_path)) {
		perror("realpath");
		return 1;
	}

	const char* file_path = abs_path;

	// Initialize the document parser runtime
	if (!cdsl_doc_init()) {
		fprintf(stderr, "Error: Failed to initialize document parser (LibreOfficeKit).\n");
		return 1;
	}

	printf("Parsing document: %s\n", file_path);

	// Extract document contents to a JSON string
	char* json_output = cdsl_doc_extract_to_json(file_path);

	if (json_output) {
		printf("Extraction successful. JSON output:\n\n");
		printf("%s\n", json_output);

		// Free the returned JSON string
		cdsl_doc_free_string(json_output);
	} else {
		fprintf(stderr, "Error: Failed to extract JSON from document: %s\n", file_path);
		cdsl_doc_shutdown();
		return 1;
	}

	// Shutdown the parser runtime
	cdsl_doc_shutdown();

	/*
	 * WORKAROUND: LibreOfficeKit is known to have issues with clean shutdown
	 * and can sometimes crash in background threads or atexit handlers.
	 * Using _exit(0) bypasses these handlers and ensures a clean termination
	 * for this demo CLI.
	 */
	_exit(0);
}
