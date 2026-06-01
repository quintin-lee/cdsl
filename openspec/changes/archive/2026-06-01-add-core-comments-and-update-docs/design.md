## Context

The project currently has complete API and architecture markdown documentation for the core C-DSL engine (ast, vm, schema, ai, etc.). However, the recently added `cdsl_doc_*` document parser module lacks corresponding documentation entries.

This change purely targets documentation and in-code comments, with no functional code changes.

## Goals / Non-Goals

**Goals:**
- Add necessary algorithmic comments to complex parser code in `xml_parser.cpp` and `doc_parse.cpp`.
- Update `docs/api-reference.md` to document the `cdsl_doc_*` API functions, their signatures, thread safety, and usage.
- Update `docs/architecture.md` to include the LibreOffice document parser as a new subsystem.
- Update `docs/modules.md` to describe the `src/doc/` module.
- Update `docs/user-guide.md` with a practical document parsing example.

**Non-Goals:**
- Changing any function signatures or production logic.
- Adding new market documentation like README.md or license files.

## Decisions

- **Comment style**: Use C-style `/* */` for structural section separators, inline `//` only for non-trivial algorithmic logic (LOK callback dispatch, JSON assembly order, style inheritance).
- **Documentation format**: Follow existing docs convention (pairs of heading + markdown table/example). No new sections added.

## Risks / Trade-offs

- [Risk] Over-commenting may introduce stale documentation.
  - Mitigation: Only add comments for permanent structural invariants; no transient "notes to self".
