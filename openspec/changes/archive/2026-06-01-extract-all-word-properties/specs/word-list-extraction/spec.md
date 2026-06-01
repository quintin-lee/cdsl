## ADDED Requirements

### Requirement: Ordered list extraction
The parser SHALL extract ordered `text:list` structures with nested levels and numbering styles.

#### Scenario: Single-level ordered list
- **WHEN** a document contains an ordered list with 3 items
- **THEN** the JSON SHALL include a `lists` array with list items and their numbering

#### Scenario: Nested mixed list
- **WHEN** a list contains both ordered and unordered sub-lists
- **THEN** the JSON SHALL preserve the nesting hierarchy and distinguish list types
