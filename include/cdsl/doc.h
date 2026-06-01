/**
 * @file cdsl/doc.h
 * @brief Word document parsing via LibreOffice SDK.
 *
 * Provides functions to extract text and structured properties from
 * Word documents (.docx) using the LibreOffice Kit (LOK) C API.
 * Extracted data is returned as JSON strings compatible with
 * cdsl_context_load_json().
 *
 * @section thread_safety Thread Safety
 *
 * | Function                  | Thread-safe | Notes                                   |
 * |---------------------------|-------------|-----------------------------------------|
 * | cdsl_doc_init()           | ✅ Yes      | Idempotent; double-checked locking      |
 * | cdsl_doc_shutdown()       | ✅ Yes      | Mutex-protected                         |
 * | cdsl_doc_extract_text()   | ✅ Yes      | LOK critical section serialized by mutex|
 * | cdsl_doc_extract_to_json()| ✅ Yes      | Same as extract_text                    |
 * | cdsl_doc_free_string()    | ✅ Yes      | Just free(); no global state            |
 *
 * Multiple threads can call extract functions concurrently on different
 * documents. LibreOfficeKit API calls (documentLoad, saveAs) are
 * serialized by an internal mutex; file I/O and JSON construction
 * run outside the critical section.
 *
 * @defgroup cdsl_doc Document Parser
 * @{
 */
#ifndef CDSL_DOC_H
#define CDSL_DOC_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the LibreOfficeKit runtime.
 *
 * Thread-safe and idempotent — may be called from multiple threads.
 * Only the first successful call spawns the headless LibreOffice process.
 *
 * @return 1 on success, 0 on failure
 */
int cdsl_doc_init(void);

/**
 * @brief Shutdown the LibreOfficeKit runtime and free all resources.
 *
 * Thread-safe. If another thread is inside an extract function, shutdown
 * waits for it to complete before destroying the Office instance.
 */
void cdsl_doc_shutdown(void);

/**
 * @brief Extract document properties as a structured JSON string.
 *
 * Parses the document at @p path and returns hierarchical JSON with:
 *   - Page dimensions (mm)     → document.pages[0].width_mm, .height_mm
 *   - Page margins (mm)        → document.pages[0].margin_*_mm
 *   - Paragraph properties     → alignment, spacing_before_mm, spacing_after_mm,
 *                                line_spacing, indent_first_line_mm
 *   - Text properties          → font_name, font_size_pt, bold, italic,
 *                                underline, strikethrough, color
 *   - Full text content        → document.full_text
 *   - Document metadata        → document.metadata.page_count, paragraph_count,
 *                                word_count, character_count
 *
 * The returned JSON is compatible with cdsl_context_load_json().
 *
 * Thread-safe — multiple threads can call on different documents
 * concurrently (LibreOffice API calls are serialized internally).
 *
 * @param  path  Absolute path to the .docx file
 * @return JSON string (must be freed via cdsl_doc_free_string), or NULL on error
 */
char* cdsl_doc_extract_to_json(const char* path);

/**
 * @brief Extract plain text content from a document.
 *
 * Thread-safe — see cdsl_doc_extract_to_json() for details.
 *
 * @param  path  Absolute path to the .docx file
 * @return Plain text (must be freed via cdsl_doc_free_string), or NULL on error
 */
char* cdsl_doc_extract_text(const char* path);

/**
 * @brief Free a string returned by a cdsl_doc_* function.
 *
 * Thread-safe (no global state accessed).
 *
 * @param str  String to free (may be NULL)
 */
void cdsl_doc_free_string(char* str);

#ifdef __cplusplus
}
#endif

#endif
/** @} */
