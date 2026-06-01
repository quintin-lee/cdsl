## Context

当前的 `parse_fodt_body()` 只处理 `text:p` / `text:h` 段落和 `text:span` 文本块，其余 FODT 元素通过 `else` 分支直接 `skip`。需要扩展解析器以递归方式遍历所有文档结构元素。

## Goals / Non-Goals

**Goals:**
- 提取表格（Table → Row → Cell 三级层次）
- 提取列表（有序/无序，嵌套层级）
- 提取图片（draw:frame → draw:image，尺寸和位置）
- 提取超链接（text:a，URL 和显示文本）
- 提取页眉/页脚内容
- 提取脚注/尾注引用和正文
- 提取扩展文档元数据

**Non-Goals:**
- 提取 OLE 嵌入对象、ActiveX 控件
- 解析修订/批注（tracked changes）
- PDF 输出支持
- 修改公共 API 签名

## Decisions

- **数据结构设计**：扩展现有 `PageInfo` 结构体，添加 `Table`/`ListGroup`/`Image`/`Footnote` 等结构体，保持栈内分配（最大 512 个元素）
- **JSON 结构**：在现有 JSON 中新增顶层 `tables`、`lists`、`images`、`footnotes` 数组，不影响现有 `paragraphs` 结构
- **超链接处理**：在 `text:span` 内识别 `text:a` 父元素，将 URL 作为 span 的 `hyperlink_url` 属性
- **元数据提取**：在现有 `office:meta` 解析中扩展 `office:document-meta` 标签解析

## Risks / Trade-offs

- [Risk] 复杂嵌套结构（表格内嵌列表、图片等）增加代码复杂度
  → 按功能增量实现，每个结构解析函数独立，避免交叉依赖
- [Risk] 大幅增加 JSON 输出大小
  → 保持向后兼容，旧字段结构不变，新字段以数组形式追加
