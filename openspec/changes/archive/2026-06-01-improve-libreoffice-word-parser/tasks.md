## 1. Parser Infrastructure and XML Enhancements

- [x] 1.1 Extend custom XmlParser to support attribute retrieval for margins, font name, font size, bold, italic, underline, strikethrough, alignment, indent, line height, and text color.
- [x] 1.2 Implement hierarchical style sheet caching and parent-child inheritance resolution within Flat ODT parser.
- [x] 1.3 Implement complete metadata extraction from `<meta:document-statistic>` node (page count, paragraph count, word count, character count).

## 2. Structured Layout and Text Property Extraction

- [x] 2.1 Implement robust margin (`spacing_before_mm`, `spacing_after_mm`) extraction logic for paragraphs.
- [x] 2.2 Implement robust paragraph alignment and line spacing extraction, with support for absolute and percentage formats.
- [x] 2.3 Implement robust text block span parsing to extract font family name, size (in points), font weight, italics, underline, strikethrough, and hex color value.

## 3. Position and Bounding Box Estimation

- [x] 3.1 Expose reentrant `LOK_CALLBACK_INVALIDATE_VISIBLE_CURSOR` processing via thread-safe callbacks in `get_paragraph_positions()`.
- [x] 3.2 Implement precise paragraph bounding box (`bbox_mm`) computation in millimeters.
- [x] 3.3 Implement precise nested text blocks bounding box (`bbox_mm`) interpolation based on string lengths.

## 4. Output Composition and Verification

- [x] 4.1 Assemble enriched metadata and layout structure into the standard JSON response inside `cdsl_doc_extract_to_json()`.
- [x] 4.2 Update unit test suites to verify new JSON properties and statistics in `test_doc.c`.
