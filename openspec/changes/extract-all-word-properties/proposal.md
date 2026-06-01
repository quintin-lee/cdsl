## Why

当前 `cdsl_doc_extract_to_json()` 仅提取正文段落和基本文本样式，大量 Word 文档结构信息（表格、列表、图片、超链接、页眉页脚、脚注等）被完全跳过。要实现完整的文档质量审计规则评估，必须提取所有结构化文档属性。

## What Changes

- **表格提取**：解析 `text:table` → `table:table-row` → `table:table-cell`，提取单元格文本和表格样式
- **列表提取**：识别 `text:list` / `text:list-item`，提取列表层级和编号样式
- **图片提取**：解析 `draw:frame` / `draw:image`，提取图片位置、尺寸和替代文本
- **超链接提取**：解析 `text:a`，提取链接 URL 和显示文本
- **页眉页脚提取**：从 `style:header-footer` 解析页眉页脚内容
- **脚注/尾注提取**：解析 `text:note`，提取脚注引用和内容
- **文档元属性扩展**：提取作者、创建时间、标题等 Dublin Core 元数据
- **多节布局支持**：支持不同 section 的独立页面布局
- **JSON 输出兼容**：扩展现有 JSON 结构，所有新字段向后兼容

## Capabilities

### New Capabilities
- `word-table-extraction`: 提取表格结构、单元格内容和样式
- `word-list-extraction`: 提取有序/无序列表层级结构
- `word-image-extraction`: 提取图片位置、尺寸和替代文本
- `word-hyperlink-extraction`: 提取超链接 URL 和显示文本
- `word-header-footer-extraction`: 提取页眉页脚内容
- `word-footnote-extraction`: 提取脚注/尾注引用和内容
- `word-metadata-extraction`: 提取扩展文档元数据（作者、时间等）

## Impact

- 源码：`src/doc/doc_parse.cpp` — 大幅扩展 FODT 解析逻辑
- 公共头文件：`include/cdsl/doc.h` — 无 API 变更，但 JSON 输出结构扩展
- 测试：`tests/test_doc.c` — 新增结构化验证测试
