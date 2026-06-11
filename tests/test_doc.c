/**
 * @file test_doc.c
 * @brief Document parsing tests for C-DSL.
 */
#include <cdsl/doc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cdsl/util/threads.h"
#include <unistd.h>
#include "test.h"

static const char* TEST_DOCX = TEST_FIXTURES_DIR "/test_doc.docx";
static const char* MISSING_PATH = TEST_FIXTURES_DIR "/nonexistent.docx";

static void
test_extract_text()
{
	TEST_BEGIN("Extract text from valid docx");
	char* text = cdsl_doc_extract_text(TEST_DOCX);
	TEST_ASSERT_NOT_NULL(text, "cdsl_doc_extract_text() should return non-NULL");
	TEST_ASSERT(strlen(text) > 0, "Extracted text should not be empty");
	TEST_ASSERT(strstr(text, "Hello, C-DSL") != NULL, "Text should contain expected content");
	cdsl_doc_free_string(text);
	TEST_END();
}

static void
test_extract_json()
{
	TEST_BEGIN("Extract JSON from valid docx");
	char* json = cdsl_doc_extract_to_json(TEST_DOCX);
	TEST_ASSERT_NOT_NULL(json, "cdsl_doc_extract_to_json() should return non-NULL");
	TEST_ASSERT(strlen(json) > 0, "JSON should not be empty");
	TEST_ASSERT(strstr(json, "\"document\"") != NULL, "JSON should contain document root");
	TEST_ASSERT(strstr(json, "\"elements\"") != NULL, "JSON should contain elements");
	TEST_ASSERT(strstr(json, "\"page_size_mm\"") != NULL, "JSON should contain page_size_mm");
	TEST_ASSERT(strstr(json, "\"page_number\"") != NULL, "JSON should contain page_number");
	TEST_ASSERT(strstr(json, "\"metadata\"") != NULL, "JSON should contain metadata");
	TEST_ASSERT(strstr(json, "\"page_count\"") != NULL, "JSON should contain page_count");
	TEST_ASSERT(strstr(json, "\"text_blocks\"") != NULL, "JSON should contain text_blocks");
	TEST_ASSERT(strstr(json, "\"color\"") != NULL, "JSON should contain color attribute");
	cdsl_doc_free_string(json);
	TEST_END();
}

static void
test_missing_file()
{
	TEST_BEGIN("Error - missing file");
	char* text = cdsl_doc_extract_text(MISSING_PATH);
	TEST_ASSERT_NULL(text, "Extract text from missing file should return NULL");
	char* json = cdsl_doc_extract_to_json(MISSING_PATH);
	TEST_ASSERT_NULL(json, "Extract JSON from missing file should return NULL");
	TEST_END();
}

static void
test_coordinate_accuracy()
{
	TEST_BEGIN("Coordinate and page numbering accuracy");
	char* json = cdsl_doc_extract_to_json(TEST_DOCX);
	TEST_ASSERT_NOT_NULL(json, "JSON should not be NULL");
	TEST_ASSERT(strstr(json, "\"bbox_mm\": [") != NULL, "Should contain bbox_mm");
	TEST_ASSERT(strstr(json, "\"page_index\":") != NULL, "Should contain page_index");
	cdsl_doc_free_string(json);
	TEST_END();
}

int
main()
{
	printf("Running document parsing tests...\n");

	if (!cdsl_doc_init()) {
		fprintf(stderr, "Failed to initialize document parser\n");
		return 1;
	}

	test_extract_text();
	test_extract_json();
	test_coordinate_accuracy();
	test_missing_file();

	cdsl_doc_shutdown();

	TEST_SUMMARY();
	TEST_EXIT();
}
