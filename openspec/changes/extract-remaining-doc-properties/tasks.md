## 1. 页眉页脚

- [ ] 1.1 在 `parse_fodt_document` 中解析 `office:master-styles` → `style:header`/`style:footer`
- [ ] 1.2 提取页眉页脚内的 `text:p` 段落文本
- [ ] 1.3 JSON 序列化 header/footer 到 page 对象

## 2. 脚注尾注

- [ ] 2.1 在 `parse_fodt_body` 中识别 `text:note` 元素
- [ ] 2.2 提取 `text:note-citation` 引用标记和 `text:note-body` 正文
- [ ] 2.3 JSON 序列化到 `footnotes` / `endnotes` 数组

## 3. 视觉效果属性

- [ ] 3.1 扩展 `TextProps` 添加 `background_color`、`superscript`、`subscript`、`letter_spacing_pt`
- [ ] 3.2 扩展 `ParaProps` 添加 `background_color`、`border_*`、`tab_stops`
- [ ] 3.3 在 `parse_text_props_elem` 中提取背景色、上标下标、字符间距
- [ ] 3.4 在 `parse_para_props_elem` 中提取段落背景色、边框
- [ ] 3.5 JSON 序列化新增属性到 text_block 和 paragraph 输出

## 4. 测试

- [ ] 4.1 更新 `test_doc.c` 验证新增属性
- [ ] 4.2 编译通过并全部测试通过
