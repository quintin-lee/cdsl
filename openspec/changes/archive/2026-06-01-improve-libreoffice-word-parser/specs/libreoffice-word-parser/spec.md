## ADDED Requirements

### Requirement: Paragraph margin extraction
The Word document parser SHALL extract paragraph-level margin properties from FODT and map them into the output JSON array elements as `spacing_before_mm` and `spacing_after_mm`.

#### Scenario: Extract spacing before and after
- **WHEN** a document paragraph style contains fo:margin-top and fo:margin-bottom attributes
- **THEN** the JSON output for the paragraph SHALL contain spacing_before_mm and spacing_after_mm as numeric floating-point values in millimeters

### Requirement: Paragraph alignment and line height extraction
The Word document parser SHALL extract paragraph alignment and line spacing/height properties, including percentage-based formats.

#### Scenario: Extract paragraph alignment and line spacing
- **WHEN** a paragraph has fo:text-align or fo:line-height attributes
- **THEN** the JSON output SHALL populate alignment and line_spacing accordingly

### Requirement: Text span and font parameter extraction
The Word document parser SHALL parse inner `text:span` nodes inside paragraphs, extracting font family, size, weights, styles (italic, bold), decoration (underline, strikethrough), and hex/string colors.

#### Scenario: Extract font name and size and styling
- **WHEN** a text span has font-family, font-size, font-weight, font-style, text-underline-style, or fo:color attributes
- **THEN** the text block object inside the paragraph's text_blocks array SHALL populate font_name, font_size_pt, bold, italic, underline, strikethrough, and color fields

### Requirement: Complete document statistics
The Word document parser SHALL parse the `<meta:document-statistic>` node inside `office:meta` to extract the overall page count, paragraph count, word count, and character count of the document.

#### Scenario: Extract metadata statistics
- **WHEN** the FODT document is loaded
- **THEN** the output JSON SHALL contain a `metadata` object with page_count, paragraph_count, word_count, and character_count as integers

### Requirement: Reentrant positional estimation
The Word document parser SHALL estimate physical paragraph and text block bounding boxes (`bbox_mm` array as `[x, y, w, h]`) and physical page numbers using LOK cursors via `LOK_CALLBACK_INVALIDATE_VISIBLE_CURSOR` in a thread-safe, reentrant manner.

#### Scenario: Positional estimation via reentrant callback
- **WHEN** get_paragraph_positions() registers a cursor callback and moves the cursor paragraph by paragraph
- **THEN** each paragraph's `bbox_mm` and its text blocks' `bbox_mm` SHALL be populated with accurate coordinates in millimeters
