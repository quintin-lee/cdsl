## ADDED Requirements

### Requirement: Hyperlink URL and text extraction
The parser SHALL extract `text:a` elements with their `xlink:href` target and display text content.

#### Scenario: Extract hyperlink
- **WHEN** a document contains a hyperlink with URL and display text
- **THEN** the `text_blocks` array SHALL include `hyperlink_url` field for spans within hyperlinks
