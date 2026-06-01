## ADDED Requirements

### Requirement: Footnote and endnote extraction
The parser SHALL extract `text:note` elements, including the note citation marker and note body text.

#### Scenario: Extract footnotes
- **WHEN** a document contains footnotes with citation and body text
- **THEN** the JSON SHALL include a `footnotes` array at document level with note_id, citation, and body content
