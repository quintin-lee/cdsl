## ADDED Requirements

### Requirement: Source code comments for complex parser logic
The codebase SHALL include inline comments explaining non-trivial algorithm paths in `xml_parser.cpp` and `doc_parse.cpp`.

#### Scenario: Comments present in XML attribute parser
- **WHEN** a reviewer reads `XmlParser::attr()`
- **THEN** the code SHALL explain the buffer pooling strategy

#### Scenario: Comments present in style inheritance resolver
- **WHEN** a reviewer reads `resolve_style()` and `merge_style()`
- **THEN** the code SHALL explain the parent-child property merge semantics

#### Scenario: Comments present in LOK cursor callback loop
- **WHEN** a reviewer reads `get_paragraph_positions()`
- **THEN** the code SHALL explain UNO command mapping and bbox interpolation

### Requirement: Markdown documentation for doc parser module
The project documentation SHALL document the `cdsl_doc_*` API in `docs/api-reference.md`, `docs/architecture.md`, `docs/modules.md`, and `docs/user-guide.md`.

#### Scenario: API reference updated
- **WHEN** a user reads `docs/api-reference.md`
- **THEN** the document SHALL include `cdsl_doc_init`, `cdsl_doc_shutdown`, `cdsl_doc_extract_text`, `cdsl_doc_extract_to_json`, and `cdsl_doc_free_string`

#### Scenario: Architecture diagram updated
- **WHEN** a user reads `docs/architecture.md`
- **THEN** the document SHALL reference the document parser subsystem

#### Scenario: Modules documentation updated
- **WHEN** a user reads `docs/modules.md`
- **THEN** the document SHALL describe `src/doc/` and its purpose

#### Scenario: User guide includes document parsing example
- **WHEN** a user reads `docs/user-guide.md`
- **THEN** the document SHALL contain a working code snippet for `cdsl_doc_extract_to_json`
