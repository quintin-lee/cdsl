/**
 * @file src/ai/internal.h
 * @brief Internal helpers for the AI bridge module.
 */

#ifndef CDSL_AI_INTERNAL_H
#define CDSL_AI_INTERNAL_H

/**
 * @brief Escape single quotes in @p s for POSIX single-quoted shell arguments.
 *
 * Replaces each @c ' with @c '"'"' so the returned string is safe when
 * wrapped in outer single quotes (@c '<escaped>').
 *
 * @param s Input string (NULL returns NULL)
 * @return Heap-allocated escaped string (caller frees), or NULL on error
 */
char* cdsl_ai_escape_shell_sq_internal(const char* s);

#endif
