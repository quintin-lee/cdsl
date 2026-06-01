## ADDED Requirements

### Requirement: Table structure extraction
The parser SHALL extract `text:table` elements including rows, cells, and cell text content with column span and row span attributes.

#### Scenario: Extract simple table
- **WHEN** a document contains a table with 2 rows and 3 columns
- **THEN** the JSON SHALL include a `tables` array with row/cell hierarchy and cell text content

#### Scenario: Table with merged cells
- **WHEN** a cell has `table:number-columns-spanned` or `table:number-rows-spanned`
- **THEN** the JSON SHALL include colspan and rowspan values

### Requirement: Table style extraction
The parser SHALL extract table style properties including border width, background color, and cell padding.

#### Scenario: Table with borders
- **WHEN** a table has `fo:border` attributes
- **THEN** the JSON SHALL include border properties for the table and each cell
