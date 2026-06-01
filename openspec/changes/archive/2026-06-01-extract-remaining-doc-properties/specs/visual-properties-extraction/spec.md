## ADDED Requirements

### Requirement: Text background color extraction
The parser SHALL extract `fo:background-color` from `style:text-properties` and inline text spans.

#### Scenario: Extract text highlight
- **WHEN** text has a background color attribute
- **THEN** the text_block JSON SHALL include `background_color` field

### Requirement: Paragraph border extraction
The parser SHALL extract `fo:border` and directional border attributes from paragraph properties.

#### Scenario: Extract paragraph borders
- **WHEN** a paragraph has border attributes
- **THEN** the paragraph JSON SHALL include `border_top`, `border_bottom`, `border_left`, `border_right` fields

### Requirement: Paragraph background extraction
The parser SHALL extract `fo:background-color` from paragraph properties.

#### Scenario: Extract paragraph shading
- **WHEN** a paragraph has background color
- **THEN** the paragraph JSON SHALL include `background_color` field

### Requirement: Superscript and subscript extraction
The parser SHALL extract `style:text-position` for super/sub script styling.

#### Scenario: Extract text position
- **WHEN** text has `style:text-position` attribute
- **THEN** text_block SHALL include `superscript` or `subscript` boolean fields

### Requirement: Letter spacing extraction
The parser SHALL extract `fo:letter-spacing` from text properties.

#### Scenario: Extract character spacing
- **WHEN** text has letter-spacing attribute
- **THEN** text_block SHALL include `letter_spacing_pt` field

### Requirement: Tab stop extraction
The parser SHALL extract `style:tab-stops` → `style:tab-stop` definitions from paragraph properties.

#### Scenario: Extract tab stops
- **WHEN** a paragraph style defines tab stops
- **THEN** the paragraph JSON SHALL include a `tab_stops` array with position_mm and type
