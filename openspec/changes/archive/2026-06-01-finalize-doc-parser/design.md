## Context

`doc_parse.cpp` 已达 1550+ 行，多处文本提取逻辑重复（header/footer/footnote 均复制相同的 text:p 递归提取代码）。需在添加最后一批属性前先重构。

## Goals / Non-Goals

**Goals:**
- 提取书签名称和位置
- 提取制表位位置（mm）和类型（left/right/center/decimal）
- 提取标题大纲级别（1-10）
- 提取文档语言设置
- 重构：提取 `extract_paragraph_text(XmlParser&)` 公共函数

**Non-Goals:**
- 修改公共 API 或 JSON 顶层结构

## Decisions

- 书签以数组形式输出到文档级别
- 制表位作为段落属性输出
- 大纲级别映射为段落的 `outline_level` 字段

## Risks / Trade-offs

- 无重大风险 — 均为增量添加，不影响现有功能
