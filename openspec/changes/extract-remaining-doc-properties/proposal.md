## Why

当前已提取正文文本、表格、列表、图片和超链接，但页眉页脚、脚注尾注、文字和段落视觉效果属性仍未提取，导致 Word 文档约 15% 的属性信息丢失，影响完整文档质量审计规则评估。

## What Changes

- **页眉页脚提取**：解析 `office:master-styles` → `style:header` / `style:footer`
- **脚注尾注提取**：解析 `text:note` → `text:note-citation` + `text:note-body`
- **文字高亮/背景色**：解析 `style:text-properties` → `fo:background-color`
- **段落边框和底纹**：解析 `style:paragraph-properties` → `fo:border*` + `fo:background-color`
- **制表位**：解析 `style:tab-stops` → `style:tab-stop`
- **上标/下标**：解析 `style:text-position`
- **字符间距**：解析 `fo:letter-spacing`
- **书签**：解析 `text:bookmark` / `text:bookmark-start`

## Capabilities

### New Capabilities
- `header-footer-extraction`: 提取页眉页脚内容
- `footnote-endnote-extraction`: 提取脚注尾注引用和正文
- `visual-properties-extraction`: 提取高亮、背景色、边框、底纹、上标下标、字符间距

## Impact

- 源码：`src/doc/doc_parse.cpp` — 扩展 `TextProps`、`ParaProps` 结构体和解析逻辑
- 公共头文件：`include/cdsl/doc.h` — 无 API 变更
