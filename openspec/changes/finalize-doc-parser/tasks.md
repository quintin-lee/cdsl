## 1. 代码重构

- [x] 1.1 提取公共的 `extract_paragraph_text()` 函数，消除 header/footer/footnote 中的重复文本提取逻辑
- [x] 1.2 简化 parse_fodt_body 中 text:note 处理的内联代码

## 2. 新属性提取

- [x] 2.1 扩展 `ParaProps` 添加 `outline_level` 和 `language` 字段
- [x] 2.2 提取 `text:outline-level` 和 `fo:language`/`fo:country`
- [x] 2.3 解析 `style:tab-stops` → `style:tab-stop` 并存储在 `ParaProps`
- [x] 2.4 解析 `text:bookmark` / `text:bookmark-start` 到文档级数组
- [x] 2.5 JSON 序列化新增属性

## 3. 测试

- [x] 3.1 编译通过并全部测试通过
