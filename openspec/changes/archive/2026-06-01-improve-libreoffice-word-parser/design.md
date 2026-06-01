## Context

The C-DSL LibreOffice document parser (`cdsl_doc_*` inside [cdsl/doc.h](file:///home/quintin/workspace/source/c/dsl/include/cdsl/doc.h)) parses Microsoft Word `.docx` documents by using LibreOfficeKit (LOK) to:
1. Export the document to a Flat XML ODT (`.fodt`) file.
2. Read the flat XML to extract text and basic metadata.
3. Launch a rendered LOK session to capture coordinates for layout and paragraph boundaries via reentrant callbacks.

Currently, the structural properties parsed from the Flat XML (via [doc_parse.cpp](file:///home/quintin/workspace/source/c/dsl/src/doc/doc_parse.cpp) and [xml_parser.cpp](file:///home/quintin/workspace/source/c/dsl/src/doc/xml_parser.cpp)) are limited and do not capture detailed formatting properties. We need to extend the custom XML parsing logic and LOK cursor-capturing algorithms to fully extract layout (margins, line-spacing, alignment), text formatting (font name, font size, colors, font styling), document statistics (page count, paragraph count, etc.), and estimate the exact coordinates of paragraph and text blocks.

## Goals / Non-Goals

**Goals:**
- Extract paragraph styling: alignment (`fo:text-align`), spacing before (`fo:margin-top`), spacing after (`fo:margin-bottom`), line height/spacing (`fo:line-height`), and first-line indentation (`text:indent`).
- Extract text styling: font name (`style:font-name` or `fo:font-family`), font size (`fo:font-size`), bold (`fo:font-weight`), italic (`fo:font-style`), underline (`style:text-underline-style`), strikethrough (`style:text-line-through-style`), and color (`fo:color`).
- Parse document-wide statistics from `<meta:document-statistic>` (pages, paragraphs, words, characters) and expose under a `metadata` node in the final JSON.
- Implement thread-safe paragraph positioning in `get_paragraph_positions()` by tracking LOK callbacks via thread-safe cursors.

**Non-Goals:**
- Supporting general ODT or other non-docx formats beyond the Flat ODT (`.fodt`) intermediate export format used internally.
- Re-architecting the entire LOK model; we must keep the existing public API functions (`cdsl_doc_extract_to_json()` and `cdsl_doc_extract_text()`) backward-compatible.

## Decisions

- **Decision 1: custom C++ XML Parser extension vs. third-party XML library**
  - *Choice*: Extend the custom lightweight SAX-like `XmlParser` ([xml_parser.cpp](file:///home/quintin/workspace/source/c/dsl/src/doc/xml_parser.cpp)).
  - *Rationale*: C-DSL enforces a zero-external-dependencies policy, ensuring minimal build configuration complexity and absolute portable execution. The custom parser is fast and completely adequate for extracting flat XML attributes.
- **Decision 2: Bounded Paragraph array limits**
  - *Choice*: Retain the `256` maximum paragraphs array limit but ensure robust bounds check handling and graceful truncation to prevent overflows.
  - *Rationale*: Simpler memory management within standard C++ stack-allocated structs, aligned with current engine limits.

## Risks / Trade-offs

- [Risk] Large documents exceeding the `256` paragraphs threshold.
  - *Mitigation*: Ensure the FODT parser strictly checks array bounds and truncates further paragraph entries without crashing or overflowing memory.
- [Risk] Thread safety of global `g_office` instance during parallel document parses.
  - *Mitigation*: Ensure all LOK document-loading and document rendering activities are strictly serialized within the established global `g_mutex` critical section.
