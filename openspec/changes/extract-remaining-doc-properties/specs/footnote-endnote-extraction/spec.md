## ADDED Requirements

### Requirement: Footnote extraction
The parser SHALL extract `text:note` elements with their citation markers and body content.

#### Scenario: Extract footnotes
- **WHEN** a document contains footnote references
- **THEN** the JSON SHALL include a `footnotes` array at document level with `citation` and `body` text

### Requirement: Endnote extraction
The parser SHALL extract endnotes (`text:note` with `text:note-class="endnote"`).

#### Scenario: Extract endnotes
- **WHEN** a document contains endnote references
- **THEN** the JSON SHALL include an `endnotes` array with citation and body text
