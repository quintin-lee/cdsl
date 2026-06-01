## ADDED Requirements

### Requirement: Header and footer content extraction
The parser SHALL extract header and footer sections from `style:header-footer` blocks for each page style.

#### Scenario: Extract header text
- **WHEN** a document has a header with text content
- **THEN** the page object in JSON SHALL include a `header` field with text and paragraph details

#### Scenario: Extract footer text
- **WHEN** a document has a footer with page numbers
- **THEN** the page object SHALL include a `footer` field with paragraph content
