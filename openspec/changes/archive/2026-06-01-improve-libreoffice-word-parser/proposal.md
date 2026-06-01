## Why

Currently, the C-DSL LibreOffice document parser has limited metadata extraction and parsing capabilities for Word documents (.docx). To better evaluate comprehensive document quality rules, we need to extract more fine-grained structured properties (such as paragraph margins, line heights, font parameters, page count, paragraph count, word count, character count, and alignment) into a structured JSON payload that the C-DSL VM can evaluate via rules.

## What Changes

- Enhance the FODT XML parser to capture detailed paragraph-level properties (alignment, spacing before/after, line spacing, first-line indent).
- Enhance the FODT XML parser to capture detailed text/span-level properties (font name, font size, bold, italic, underline, strikethrough, color).
- Expose complete document statistics (page count, paragraph count, word count, character count) under a `metadata` object in the output JSON.
- Enhance positional rendering/estimation by querying LibreOffice's `LOK_CALLBACK_INVALIDATE_VISIBLE_CURSOR` via reentrant callbacks to estimate physical page numbers and bounding boxes for paragraphs and text blocks, in a robust, multi-threaded safe manner.

## Capabilities

### New Capabilities
- `libreoffice-word-parser`: Enhance LibreOffice SDK integration with comprehensive layout, text styling, document metrics extraction, and positional estimation.

### Modified Capabilities

## Impact

- Public Header: [cdsl/doc.h](file:///home/quintin/workspace/source/c/dsl/include/cdsl/doc.h) - no changes to signature, but behavior/JSON format is highly enriched.
- Source Files:
  - [doc_parse.cpp](file:///home/quintin/workspace/source/c/dsl/src/doc/doc_parse.cpp): XML parser integration, LOK cursor callback handling, JSON output composition.
  - [xml_parser.cpp](file:///home/quintin/workspace/source/c/dsl/src/doc/xml_parser.cpp) & [xml_parser.h](file:///home/quintin/workspace/source/c/dsl/src/doc/xml_parser.h): Custom lightweight SAX-like parser supporting attribute fetching.
- Tests: [test_doc.c](file:///home/quintin/workspace/source/c/dsl/tests/test_doc.c) will be updated or added to fully verify the new structured fields and statistics.
