/**
 * @file cdsl_error.h
 * @brief Structured error reporting for DSL parsing and verification.
 *
 * @defgroup errors Error Handling
 * @{
 */

#ifndef CDSL_ERROR_H
#define CDSL_ERROR_H

/**
 * @brief Error category classification.
 */
typedef enum {
	CDSL_ERR_SYNTAX,   /**< Parse error (invalid syntax) */
	CDSL_ERR_TYPE,	   /**< Type mismatch error */
	CDSL_ERR_SEMANTIC, /**< Semantic error (unknown variable/action) */
	CDSL_ERR_RUNTIME   /**< Runtime evaluation error */
} cdsl_error_kind_t;

/**
 * @brief Single error instance with location and hint.
 *
 * Provides structured error information including line/column numbers,
 * a human-readable message, and an optional fix hint.
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
 *
 * Growable array of error pointers. Used by cdsl_verify_rule_detailed()
 * to collect all errors instead of stopping at the first one.
 */
typedef struct cdsl_error_list {
	cdsl_error_t** errors; /**< Array of error pointers */
	int count;	       /**< Number of errors */
	int capacity;	       /**< Allocated capacity */
} cdsl_error_list_t;

cdsl_error_list_t* cdsl_error_list_create(void);
void cdsl_error_list_free(cdsl_error_list_t* list);
void cdsl_error_list_add(cdsl_error_list_t* list, cdsl_error_t* err);
int cdsl_error_list_has_errors(const cdsl_error_list_t* list);
void cdsl_error_list_print(const cdsl_error_list_t* list);

#endif
/** @} */
