## 1. 表格提取

- [x] 1.1 定义 Table/Row/Cell 数据结构体和 FODT 解析函数 parse_fodt_table()
- [x] 1.2 提取单元格文本、colspan、rowspan 属性
- [x] 1.3 JSON 序列化表格数据到 `tables` 数组

## 2. 列表提取

- [x] 2.1 解析 text:list / text:list-item 嵌套结构，识别有序/无序类型
- [x] 2.2 提取列表层级深度和编号样式信息
- [x] 2.3 JSON 序列化列表数据到 `lists` 数组

## 3. 超链接提取

- [x] 3.1 识别 text:a 元素，提取 xlink:href URL
- [x] 3.2 将超链接 URL 关联到对应 text_block

## 4. 图片提取

- [x] 4.1 解析 draw:frame / draw:image 元素
- [x] 4.2 提取图片尺寸（svg:width/svg:height）和替代文本
- [x] 4.3 JSON 序列化图片数据到 `images` 数组
- [x] 5.1 解析 style:header-footer 样式节点
- [x] 5.2 提取页眉页脚段落内容
- [x] 5.3 关联页眉页脚到对应 page 对象
- [x] 6.1 解析 text:note 元素
- [x] 6.2 提取脚注引用标记和正文内容
- [x] 6.3 JSON 序列化脚注到 `footnotes` 数组
- [x] 7.1 解析 office:document-meta 中的 dc:title/dc:creator 等元素
- [x] 7.2 将扩展元数据追加到 JSON `metadata` 对象

## 8. 测试和文档

- [x] 8.1 创建包含表格、列表、图片、超链接的测试 .docx 文件
- [x] 8.2 更新 test_doc.c 添加结构化验证测试
- [x] 8.3 更新 docs/ 中的 JSON 输出结构文档
