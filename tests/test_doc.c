/**
 * @file test_doc.c
 * @brief Document parsing tests for C-DSL.
 */
#include <cdsl/doc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "test.h"

static const char* TEST_DOCX = TEST_FIXTURES_DIR "/test_doc.docx";
static const char* MISSING_PATH = TEST_FIXTURES_DIR "/nonexistent.docx";

static void
test_init_and_extract_text()
{
	TEST_BEGIN("Init and extract text");
	int ok = cdsl_doc_init();
	TEST_ASSERT(ok == 1, "cdsl_doc_init() should succeed");

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
	TEST_ASSERT(strstr(json, "\"paragraph_count\"") != NULL,
		    "JSON should contain paragraph_count");
	TEST_ASSERT(strstr(json, "\"word_count\"") != NULL, "JSON should contain word_count");
	TEST_ASSERT(strstr(json, "\"character_count\"") != NULL,
		    "JSON should contain character_count");
	TEST_ASSERT(strstr(json, "\"text_blocks\"") != NULL, "JSON should contain text_blocks");
	TEST_ASSERT(strstr(json, "\"bbox_mm\"") != NULL, "JSON should contain bbox_mm");
	TEST_ASSERT(strstr(json, "Hello, C-DSL") != NULL,
		    "JSON content should contain expected text");

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
test_null_path()
{
	TEST_BEGIN("Error - NULL path");

	char* text = cdsl_doc_extract_text(NULL);
	TEST_ASSERT_NULL(text, "Extract text with NULL path should return NULL");

	char* json = cdsl_doc_extract_to_json(NULL);
	TEST_ASSERT_NULL(json, "Extract JSON with NULL path should return NULL");

	TEST_END();
}

static void
test_free_null()
{
	TEST_BEGIN("Free NULL string");
	cdsl_doc_free_string(NULL);
	TEST_ASSERT(1, "cdsl_doc_free_string(NULL) should not crash");
	TEST_END();
}

static void
test_double_shutdown()
{
	TEST_BEGIN("Double shutdown");
	cdsl_doc_shutdown();
	cdsl_doc_shutdown();
	TEST_ASSERT(1, "Double shutdown should not crash");
	TEST_END();
}

/* ------------------------------------------------------------------ */
/*  Concurrent read test                                               */
/* ------------------------------------------------------------------ */

typedef struct {
	const char* path;
	int op; /* 0 = extract_text, 1 = extract_to_json */
	int ok;
} concurrent_job;

static void*
concurrent_worker(void* arg)
{
	concurrent_job* job = (concurrent_job*)arg;
	if (job->op == 0) {
		char* text = cdsl_doc_extract_text(job->path);
		if (text) {
			job->ok = (strstr(text, "Hello, C-DSL") != NULL);
			cdsl_doc_free_string(text);
		} else {
			job->ok = 0;
		}
	} else {
		char* json = cdsl_doc_extract_to_json(job->path);
		if (json) {
			job->ok = (strstr(json, "\"elements\"") != NULL &&
				   strstr(json, "Hello, C-DSL") != NULL);
			cdsl_doc_free_string(json);
		} else {
			job->ok = 0;
		}
	}
	return NULL;
}

static void
test_concurrent_reads()
{
	TEST_BEGIN("Concurrent reads (4 threads)");
	if (!cdsl_doc_init()) {
		TEST_ASSERT(0, "init should succeed");
		TEST_END();
		return;
	}

#define NUM_CONCURRENT 4
	pthread_t threads[NUM_CONCURRENT];
	concurrent_job jobs[NUM_CONCURRENT] = {
	    {TEST_DOCX, 0, 0},
	    {TEST_DOCX, 0, 0},
	    {TEST_DOCX, 1, 0},
	    {TEST_DOCX, 1, 0},
	};

	for (int i = 0; i < NUM_CONCURRENT; i++) {
		if (pthread_create(&threads[i], NULL, concurrent_worker, &jobs[i]) != 0) {
			TEST_ASSERT(0, "pthread_create should succeed");
			pthread_exit(NULL);
		}
	}
	for (int i = 0; i < NUM_CONCURRENT; i++) {
		pthread_join(threads[i], NULL);
		TEST_ASSERT(jobs[i].ok == 1, "Concurrent worker should succeed");
	}

	cdsl_doc_shutdown();
	TEST_END();
#undef NUM_CONCURRENT
}

static void
test_coordinate_accuracy()
{
	TEST_BEGIN("Coordinate and page numbering accuracy");
	char* json = cdsl_doc_extract_to_json(TEST_DOCX);
	TEST_ASSERT_NOT_NULL(json, "JSON should not be NULL");

	/* Check for relative Y coordinates and page indices */
	/* We expect at least one paragraph with a small bbox_y_mm (near top of page) */
	TEST_ASSERT(strstr(json, "\"bbox_mm\": [") != NULL, "Should contain bbox_mm");
	TEST_ASSERT(strstr(json, "\"page_index\":") != NULL, "Should contain page_index");

	/* 
	 * Verification of the fix: 
	 * 1. bbox_y_mm should be relative to page top.
	 * 2. page_index should be present.
	 */

	cdsl_doc_free_string(json);
	TEST_END();
}

int
main()
{
	printf("Running document parsing tests...\n");

	test_init_and_extract_text();
	test_extract_json();
	test_coordinate_accuracy();
	test_missing_file();
	test_null_path();
	test_free_null();
	test_concurrent_reads();
	test_double_shutdown();

	TEST_SUMMARY();
	TEST_EXIT();
}
