/**
 * @file cdsl_error.c
 * @brief Structured error reporting implementation.
 *
 * Provides typed error instances with source location, human-readable
 * messages, optional fix hints, and collectable error lists. Used by
 * the verification engine (cdsl_verify_rule_detailed) and parser for
 * comprehensive error reporting.
 *
 * Error categories include syntax, type, semantic, and runtime errors.
 * Error lists use a growable array pattern for efficient collection.
 */

#include "cdsl_error.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief Create a new structured error.
 *
 * All fields except hint are required. The message and hint strings
 * are copied internally.
 *
 * @param kind    Error classification
 * @param line    Source line number (0 if unknown)
 * @param column  Source column number (0 if unknown)
 * @param message Human-readable error description
 * @param hint    Optional suggestion for fixing the error
 * @return New error instance (must be freed with cdsl_error_free)
 */
cdsl_error_t*
cdsl_error_create(
    cdsl_error_kind_t kind, int line, int column, const char* message, const char* hint)
{
	cdsl_error_t* e = calloc(1, sizeof(*e));
	e->kind = kind;
	e->line = line;
	e->column = column;
	e->message = message ? strdup(message) : strdup("");
	e->hint = hint ? strdup(hint) : NULL;
	return e;
}

/**
 * @brief Free a single error instance.
 *
 * @param err Error to free (NULL-safe)
 */
void
cdsl_error_free(cdsl_error_t* err)
{
	if (!err) {
		return;
	}
	free(err->message);
	free(err->hint);
	free(err);
}

/**
 * @brief Convert error kind to a printable string (internal).
 *
 * @param kind Error kind value
 * @return Static string label
 */
static const char*
kind_str(cdsl_error_kind_t kind)
{
	switch (kind) {
	case CDSL_ERR_SYNTAX:
		return "SYNTAX";
	case CDSL_ERR_TYPE:
		return "TYPE";
	case CDSL_ERR_SEMANTIC:
		return "SEMANTIC";
	case CDSL_ERR_RUNTIME:
		return "RUNTIME";
	default:
		return "UNKNOWN";
	}
}

/**
 * @brief Print a formatted error to stderr.
 *
 * Output format: [KIND] line L, col C: message
 *                hint: suggestion
 *
 * @param err Error to print (NULL-safe)
 */
void
cdsl_error_print(const cdsl_error_t* err)
{
	if (!err) {
		return;
	}
	fprintf(stderr,
		"[%s] line %d, col %d: %s",
		kind_str(err->kind),
		err->line,
		err->column,
		err->message);
	if (err->hint) {
		fprintf(stderr, "\n  hint: %s", err->hint);
	}
	fprintf(stderr, "\n");
}

/**
 * @brief Create an empty error list.
 *
 * Pre-allocates space for 16 errors; grows as needed.
 *
 * @return New error list (must be freed with cdsl_error_list_free)
 */
cdsl_error_list_t*
cdsl_error_list_create(void)
{
	cdsl_error_list_t* list = calloc(1, sizeof(*list));
	list->capacity = 16;
	list->errors = malloc(sizeof(cdsl_error_t*) * list->capacity);
	return list;
}

/**
 * @brief Free an error list and all contained errors.
 *
 * @param list List to free (NULL-safe)
 */
void
cdsl_error_list_free(cdsl_error_list_t* list)
{
	if (!list) {
		return;
	}
	for (int i = 0; i < list->count; i++) {
		cdsl_error_free(list->errors[i]);
	}
	free(list->errors);
	free(list);
}

/**
 * @brief Add an error to the list.
 *
 * The list takes ownership of the error pointer.
 *
 * @param list Target error list
 * @param err  Error to add (NULL-safe, no-op if NULL)
 */
void
cdsl_error_list_add(cdsl_error_list_t* list, cdsl_error_t* err)
{
	if (!list || !err) {
		return;
	}
	if (list->count >= list->capacity) {
		list->capacity *= 2;
		list->errors = realloc(list->errors, sizeof(cdsl_error_t*) * list->capacity);
	}
	list->errors[list->count++] = err;
}

/**
 * @brief Check if an error list contains any errors.
 *
 * @param list Error list to check
 * @return 1 if the list contains errors, 0 otherwise
 */
int
cdsl_error_list_has_errors(const cdsl_error_list_t* list)
{
	return list && list->count > 0;
}

/**
 * @brief Print all errors in a list.
 *
 * Each error is printed via cdsl_error_print().
 *
 * @param list Error list to print (no-op if NULL or empty)
 */
void
cdsl_error_list_print(const cdsl_error_list_t* list)
{
	if (!list || list->count == 0) {
		return;
	}
	for (int i = 0; i < list->count; i++) {
		cdsl_error_print(list->errors[i]);
	}
}
