## ADDED Requirements

### Requirement: Image element extraction
The parser SHALL extract `draw:frame` and `draw:image` elements with dimensions and alternative text.

#### Scenario: Extract inline image
- **WHEN** a document contains an image inside a paragraph
- **THEN** the JSON SHALL include the image in an `images` array with width_mm, height_mm, and alt_text

#### Scenario: Extract anchored image
- **WHEN** an image is anchored to a page position
- **THEN** the JSON SHALL include position (x_mm, y_mm) and the containing page number
