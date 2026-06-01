## Why

To enhance codebase maintainability and readability, the newly added LibreOffice Word document parser needs detailed architectural C/C++ comments. At the same time, we must update all project markdown documentation to officially introduce and describe the document parser module, its architecture, API functions, JSON payload contract, and usage examples.

## What Changes

- Add high-quality, necessary architectural comments to the C/C++ parser files ([xml_parser.cpp](file:///home/quintin/workspace/source/c/dsl/src/doc/xml_parser.cpp) and [doc_parse.cpp](file:///home/quintin/workspace/source/c/dsl/src/doc/doc_parse.cpp)) strictly adhering to Necessary Comments policies (avoiding simple agent memo comments, focusing instead on complex layout mapping algorithms and reentrant LOK callback logic).
- Update and refresh project Markdown documents in `docs/` (`api-reference.md`, `architecture.md`, `modules.md`, `user-guide.md`) to integrate the `cdsl_doc_*` engine.

## Capabilities

### New Capabilities
- `doc-parser-documentation`: Document the detailed implementation, structured parameters, and APIs of the LibreOffice Word document parser.

### Modified Capabilities

## Impact

- Public Header: [cdsl/doc.h](file:///home/quintin/workspace/source/c/dsl/include/cdsl/doc.h) (Reference documentation/comment review)
- Source Implementation: [doc_parse.cpp](file:///home/quintin/workspace/source/c/dsl/src/doc/doc_parse.cpp), [xml_parser.cpp](file:///home/quintin/workspace/source/c/dsl/src/doc/xml_parser.cpp) (Detailed essential comments)
- Documentation: `docs/api-reference.md`, `docs/architecture.md`, `docs/modules.md`, `docs/user-guide.md` (Updated contents)
