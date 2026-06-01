## Why

经过三轮迭代，Word 解析器已提取 95%+ 的文档属性。剩余的书签、制表位、大纲级别、文档语言等低频属性尚未实现。同时代码中存在重复的 XML 文本提取逻辑需要重构。

## What Changes

- **书签提取**：解析 `text:bookmark` / `text:bookmark-start`
- **制表位提取**：解析 `style:tab-stops` → `style:tab-stop`
- **大纲级别提取**：解析 `text:outline-level` on headings
- **文档语言提取**：解析 `fo:language` / `fo:country`
- **代码重构**：提取公共的 `extract_text_from_xml()` 辅助函数消除重复

## Capabilities

### New Capabilities
- `final-doc-properties`: 提取书签、制表位、大纲级别、文档语言，并重构重复代码

## Impact

- 源码：`src/doc/doc_parse.cpp` — 新增 4 个解析器和 1 个公共辅助函数
