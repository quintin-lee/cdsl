## ADDED Requirements

### Requirement: Header extraction
The parser SHALL extract header content from `office:master-styles` → `style:master-page` → `style:header` nodes.

#### Scenario: Extract header text
- **WHEN** a document has header text in its master page style
- **THEN** each page object SHALL include a `header` field with paragraph text_blocks

### Requirement: Footer extraction
The parser SHALL extract footer content from `style:footer` nodes.

#### Scenario: Extract footer text
- **WHEN** a document has footer content with page numbers
- **THEN** each page object SHALL include a `footer` field with paragraph content
