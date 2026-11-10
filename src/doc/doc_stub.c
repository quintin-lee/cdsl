/**
 * @file src/doc/doc_stub.c
 * @brief No-op implementations of the document parsing API for builds that
 *        do not include the LibreOffice SDK (CDSL_USE_LIBREOFFICE not set).
 *
 * The real implementation lives in doc_parse.cpp and is only compiled when the
 * SDK is present.  Without this stub, test_doc (and any consumer that links
 * the full library) would fail at link time on the five cdsl_doc_* symbols.
 *
 * The stub keeps the *interface* testable: cdsl_doc_init() reports the
 * missing backend, extract functions return NULL, and callers can branch on
 * init's result to skip gracefully instead of crashing.
 */

#include "cdsl/doc.h"
#include <stdio.h>
#include <stdlib.h>

int
cdsl_doc_init(void)
{
	/* No LibreOffice SDK in this build; the document backend is unavailable. */
	fprintf(stderr,
		"[cdsl_doc] document parsing disabled in this build "
		"(LibreOffice SDK not found at configure time)\n");
	return 0;
}

void
cdsl_doc_shutdown(void)
{
	/* Nothing to tear down in the stub. */
}

char*
cdsl_doc_extract_to_json(const char* path)
{
	(void)path;
	return NULL;
}

char*
cdsl_doc_extract_text(const char* path)
{
	(void)path;
	return NULL;
}

void
cdsl_doc_free_string(char* s)
{
	free(s);
}
