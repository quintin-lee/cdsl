## Context

当前 `parse_fodt_body` 只处理 `text:p`/`text:h` 段落及其内部的 `text:span`/`text:a` 元素。页眉页脚位于 `office:master-styles`（当前被跳过），脚注位于 `text:note`（被跳过），视觉效果属性在 style 解析中部分缺失。

## Goals / Non-Goals

**Goals:**
- 提取页眉/页脚段落文本
- 提取脚注/尾注引用和正文
- 提取文字背景色（高亮）、段落边框、段落底纹
- 提取上标/下标、字符间距、制表位

**Non-Goals:**
- 批注/修订追踪、OLE 嵌入对象、域代码渲染

## Decisions

- **页眉页脚**：在 `parse_fodt_document` 中解析 `office:master-styles` → `style:master-page` → `style:header`/`style:footer`，提取内部 `text:p` 段落
- **脚注**：在 `parse_fodt_body` 中识别 `text:note`，提取 `text:note-citation` 和 `text:note-body`→`text:p`
- **视觉效果**：扩展 `TextProps` 添加 `background_color`、`superscript`、`subscript`、`letter_spacing_pt`；扩展 `ParaProps` 添加 `background_color`、`border_*`、`tab_stops`

## Risks / Trade-offs

- [Risk] `office:master-styles` 中可能有多个 master-page，需要按 `style:page-layout-name` 关联
  → 简化处理：提取第一个 master-page 的 header/footer，应用于所有页面
