## ADDED Requirements

### Requirement: Extended Dublin Core metadata extraction
The parser SHALL extract extended metadata from `office:document-meta` including title, creator, creation date, modification date, subject, and keywords.

#### Scenario: Extract document metadata
- **WHEN** a document has title, author, and creation date in its properties
- **THEN** the JSON `metadata` object SHALL include `title`, `creator`, `created`, `modified`, `subject`, and `keywords` fields
