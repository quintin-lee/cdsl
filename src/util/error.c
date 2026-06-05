/**
 * @file src/util/error.c
 * @brief Structured error reporting implementation.
 *
 * @ingroup cdsl_error
 * @defgroup cdsl_error_impl Error implementation
 * @{
 */

#include "cdsl/util/error.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief Convert error kind to a printable string.
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
 * @brief Create a new structured error.
 *
 * The message and hint strings are copied internally.
 */
cdsl_error_t*
cdsl_error_create(
    cdsl_error_kind_t kind, int line, int column, const char* message, const char* hint)
{
	cdsl_error_t* e = calloc(1, sizeof(*e));
	if (!e) {
		return NULL;
	}
	e->kind = kind;
	e->line = line;
	e->column = column;
	e->message = message ? strdup(message) : strdup("");
	if (!e->message) {
		free(e);
		return NULL;
	}
	e->hint = hint ? strdup(hint) : NULL;
	return e;
}

/**
 * @brief Free a single error instance.
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
 * @brief Print a formatted error to stderr.
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
 */
cdsl_error_list_t*
cdsl_error_list_create(void)
{
	cdsl_error_list_t* list = calloc(1, sizeof(*list));
	if (!list) {
		return NULL;
	}
	list->capacity = 16;
	list->errors = (cdsl_error_t**)malloc(sizeof(cdsl_error_t*) * list->capacity);
	if (!list->errors) {
		free(list);
		return NULL;
	}
	return list;
}

/**
 * @brief Free an error list and all contained errors.
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
	free((void*)list->errors);
	free(list);
}

/**
 * @brief Add an error to the list (ownership transferred).
 */
void
cdsl_error_list_add(cdsl_error_list_t* list, cdsl_error_t* err)
{
	if (!list || !err) {
		return;
	}
	if (list->count >= list->capacity) {
		int new_cap = list->capacity * 2;
		cdsl_error_t** new_errors =
		    (cdsl_error_t**)realloc((void*)list->errors, sizeof(cdsl_error_t*) * new_cap);
		if (!new_errors) {
			return;
		}
		list->errors = new_errors;
		list->capacity = new_cap;
	}
	list->errors[list->count++] = err;
}

/**
 * @brief Check if an error list contains any errors.
 */
bool
cdsl_error_list_has_errors(const cdsl_error_list_t* list)
{
	return list && list->count > 0;
}

/**
 * @brief Print all errors in a list.
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
/** @} */
