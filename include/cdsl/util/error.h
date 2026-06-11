/**
 * @file cdsl/util/error.h
 * @brief Structured error reporting for DSL parsing and verification.
 *
 * @defgroup cdsl_error Error Handling
 * @{
 */
#ifndef CDSL_UTIL_ERROR_H
#define CDSL_UTIL_ERROR_H

#include "cdsl/util/portability.h"

#include "cdsl/util/portability.h"
#include <stdbool.h>

/**
 * @brief Error category classification.
 */
typedef enum {
	CDSL_ERR_SYNTAX,   /**< Parse error (invalid syntax) */
	CDSL_ERR_TYPE,	   /**< Type mismatch error */
	CDSL_ERR_SEMANTIC, /**< Semantic error (unknown variable/action) */
	CDSL_ERR_RUNTIME,  /**< Runtime evaluation error */
	CDSL_ERR_WARNING   /**< Static analysis warning (non-fatal) */
} cdsl_error_kind_t;

/**
 * @brief Single error instance with location and hint.
 */
typedef struct cdsl_error {
	int line;		/**< Source line number (0 if unavailable) */
	int column;		/**< Source column number (0 if unavailable) */
	char* message;		/**< Error description */
	char* hint;		/**< Optional fix suggestion (may be NULL) */
	cdsl_error_kind_t kind; /**< Error category */
} cdsl_error_t;

/**
 * @brief Create a new error instance.
 * @param kind Error category
 * @param line Line number (0 if unknown)
 * @param column Column number (0 if unknown)
 * @param message Error message (duplicated internally)
 * @param hint Optional hint (duplicated internally, may be NULL)
 * @return Newly allocated error (must be freed with cdsl_error_free)
 */
CDSL_NODISCARD
cdsl_error_t* cdsl_error_create(
    cdsl_error_kind_t kind, int line, int column, const char* message, const char* hint);

/**
 * @brief Free an error instance.
 * @param err Error to free (NULL-safe)
 */
void cdsl_error_free(cdsl_error_t* err);

/**
 * @brief Print an error to stderr.
 * @param err Error to print
 */
void cdsl_error_print(const cdsl_error_t* err);

/**
 * @brief Collectable list of errors.
 */
typedef struct cdsl_error_list {
	cdsl_error_t** errors; /**< Array of error pointers */
	int count;	       /**< Number of errors */
	int capacity;	       /**< Allocated capacity */
} cdsl_error_list_t;

/**
 * @brief Create a new empty error list.
 * @return Newly allocated list (must be freed with cdsl_error_list_free)
 */
CDSL_NODISCARD
cdsl_error_list_t* cdsl_error_list_create(void);

/**
 * @brief Free an error list and all contained errors.
 * @param list List to free (NULL-safe)
 */
void cdsl_error_list_free(cdsl_error_list_t* list);

/**
 * @brief Add an error to the list (ownership transferred).
 * @param list Target list
 * @param err Error to add (NULL-safe)
 */
void cdsl_error_list_add(cdsl_error_list_t* list, cdsl_error_t* err);

/**
 * @brief Check if the list contains any errors.
 * @param list Source list
 * @return true if count > 0
 */
bool cdsl_error_list_has_errors(const cdsl_error_list_t* list);

/**
 * @brief Print all errors in the list to stderr.
 * @param list Source list
 */
void cdsl_error_list_print(const cdsl_error_list_t* list);

#endif
/** @} */
