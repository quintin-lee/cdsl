RULE official_doc_format_review {
    META {
        description = "党政机关公文格式审查 (GB/T 9704-2012)"
        pass_threshold = "85"
        partial_threshold = "60"
    }

    // ========================================================================
    // 5. 版面
    // ========================================================================

    METRIC page_size {
        META {
            description = "用纸幅面尺寸检查"
            weight = "6"
            is_critical = "true"
        }
        CASE doc.page_width == 210 && doc.page_height == 297 THEN score(6)
        DEFAULT fail_metric(0, "用纸尺寸错误 | 请使用A4型纸(210mm×297mm)")
    }

    METRIC margin_top {
        META {
            description = "天头(上白边)检查"
            weight = "6"
            is_critical = "true"
        }
        CASE doc.margin_top >= 36 && doc.margin_top <= 38 THEN score(6)
        DEFAULT fail_metric(0, "天头尺寸错误 | 天头（上白边）应为37mm±1mm")
    }

    METRIC margin_left {
        META {
            description = "订口(左白边)检查"
            weight = "6"
            is_critical = "true"
        }
        CASE doc.margin_left >= 27 && doc.margin_left <= 29 THEN score(6)
        DEFAULT fail_metric(0, "订口尺寸错误 | 订口（左白边）应为28mm±1mm")
    }

    METRIC lines_per_page {
        META {
            description = "每面行数检查"
            weight = "4"
        }
        CASE doc.lines_per_page == 22 THEN score(4)
        DEFAULT fail_metric(0, "每面行数错误 | 一般每面排22行")
    }

    METRIC chars_per_line {
        META {
            description = "每行字数检查"
            weight = "4"
        }
        CASE doc.chars_per_line == 28 THEN score(4)
        DEFAULT fail_metric(0, "每行字数错误 | 每行排28个字")
    }

    // ========================================================================
    // 7.2 版头
    // ========================================================================

    METRIC hdr_copy_number_font {
        META {
            description = "份号字体检查"
            weight = "5"
        }
        CASE header.copy_number_font == "仿宋" THEN score(5)
        DEFAULT fail_metric(0, "份号 字体为「header.copy_number_font」，不符合规范要求 | 份号应用 仿宋 字体")
    }

    METRIC hdr_secrecy_presence {
        META {
            description = "密级存在性检查"
            weight = "6"
            is_critical = "true"
        }
        CASE header.has_secrecy == true THEN score(6)
        DEFAULT fail_metric(0, "未能识别到「密级」，请检查该内容是否存在或格式是否正确 | 文档中缺失该要素，建议核实补充。规范参考：密级应用 黑体 字体")
    }

    METRIC hdr_secrecy_font {
        META {
            description = "密级字体检查"
            weight = "5"
        }
        CASE header.has_secrecy == false || header.secrecy_font == "黑体" THEN score(5)
        DEFAULT fail_metric(0, "密级 字体为「header.secrecy_font」，不符合规范要求 | 密级应用 黑体 字体")
    }

    METRIC hdr_urgency_presence {
        META {
            description = "紧急程度存在性检查"
            weight = "5"
        }
        CASE header.has_urgency == true THEN score(5)
        DEFAULT fail_metric(0, "未能识别到「紧急程度」，请检查该内容是否存在或格式是否正确 | 文档中缺失该要素，建议核实补充。规范参考：紧急程度应用 黑体 字体")
    }

    METRIC hdr_urgency_font {
        META {
            description = "紧急程度字体检查"
            weight = "5"
        }
        CASE header.has_urgency == false || header.urgency_font == "黑体" THEN score(5)
        DEFAULT fail_metric(0, "紧急程度 字体为「header.urgency_font」，不符合规范要求 | 紧急程度应用 黑体 字体")
    }

    METRIC hdr_org_logo_top {
        META {
            description = "发文机关标志上边缘检查"
            weight = "8"
            is_critical = "true"
        }
        CASE header.org_logo_top_mm >= 34.5 && header.org_logo_top_mm <= 35.5 THEN score(8)
        DEFAULT fail_metric(0, "发文机关标志 设置为「header.org_logo_top_mm毫米」，不符合版面要求 | 发文机关标志与版心上边缘应设置为 35毫米")
    }

    METRIC hdr_org_logo_font {
        META {
            description = "发文机关标志字体检查"
            weight = "5"
        }
        CASE header.org_logo_font == "小标宋" THEN score(5)
        DEFAULT fail_metric(0, "发文机关标志 字体为「header.org_logo_font」，不符合规范要求 | 发文机关标志推荐使用小标宋体字")
    }

    METRIC hdr_org_logo_color {
        META {
            description = "发文机关标志颜色检查"
            weight = "5"
        }
        CASE header.org_logo_color == "red" THEN score(5)
        DEFAULT fail_metric(0, "发文机关标志 颜色为「header.org_logo_color」，不符合规范要求 | 发文机关标志应为红色")
    }

    METRIC hdr_serial_number_font {
        META {
            description = "发文字号字体检查"
            weight = "5"
        }
        CASE header.serial_number_font == "仿宋" THEN score(5)
        DEFAULT fail_metric(0, "发文字号 字体为「header.serial_number_font」，不符合规范要求 | 发文字号应用 仿宋 字体")
    }

    METRIC hdr_serial_bracket {
        META {
            description = "发文字号年份括号检查"
            weight = "5"
        }
        CASE header.serial_bracket == "六角括号" THEN score(5)
        DEFAULT fail_metric(0, "发文字号 年份括号为「header.serial_bracket」，不符合规范要求 | 年份应使用六角括号〔〕括入")
    }

    METRIC hdr_serial_padding {
        META {
            description = "发文字号虚位检查"
            weight = "4"
        }
        CASE header.serial_has_zero_padding == false THEN score(4)
        DEFAULT fail_metric(0, "发文字号 编有虚位，不符合规范要求 | 发文顺序号不编虚位（即1不编为01）")
    }

    METRIC hdr_signer_presence {
        META {
            description = "签发人存在性检查"
            weight = "5"
        }
        CASE header.has_signer == true THEN score(5)
        DEFAULT fail_metric(0, "未能识别到「签发人」，请检查该内容是否存在或格式是否正确 | 文档中缺失该要素，建议核实补充。规范参考：签发人格式应符合 ^签发人：[^\\n\\r]+$")
    }

    METRIC hdr_signer_label_font {
        META {
            description = "签发人标签字体检查"
            weight = "4"
        }
        CASE header.has_signer == false || header.signer_label_font == "仿宋" THEN score(4)
        DEFAULT fail_metric(0, "「签发人」三字 字体为「header.signer_label_font」，不符合规范要求 | 签发人三字应用3号仿宋体字")
    }

    METRIC hdr_signer_name_font {
        META {
            description = "签发人姓名字体检查"
            weight = "4"
        }
        CASE header.has_signer == false || header.signer_name_font == "楷体" THEN score(4)
        DEFAULT fail_metric(0, "签发人姓名 字体为「header.signer_name_font」，不符合规范要求 | 签发人姓名应用3号楷体字")
    }

    METRIC hdr_separator_distance {
        META {
            description = "版头分割线距离检查"
            weight = "6"
        }
        CASE header.separator_distance_mm >= 3.5 && header.separator_distance_mm <= 4.5 THEN score(6)
        DEFAULT fail_metric(0, "版头分割线 设置为「header.separator_distance_mm毫米」，不符合版面要求 | 红线距离发文字号下边缘应为 4毫米")
    }

    METRIC hdr_separator_color {
        META {
            description = "版头分割线颜色检查"
            weight = "5"
        }
        CASE header.separator_color == "red" THEN score(5)
        DEFAULT fail_metric(0, "版头分割线 颜色为「header.separator_color」，不符合规范要求 | 版头分隔线应为红色")
    }

    // ========================================================================
    // 7.3 主体
    // ========================================================================

    METRIC body_title_font {
        META {
            description = "标题字体检查"
            weight = "6"
            is_critical = "true"
        }
        CASE body.title_font == "小标宋" THEN score(6)
        DEFAULT fail_metric(0, "标题 字体为「body.title_font」，不符合规范要求 | 标题一般用2号小标宋体字")
    }

    METRIC body_title_alignment {
        META {
            description = "标题排布检查"
            weight = "5"
        }
        CASE body.title_alignment == "center" THEN score(5)
        DEFAULT fail_metric(0, "标题 排布方式为「body.title_alignment」，不符合规范要求 | 标题应分一行或多行居中排布")
    }

    METRIC body_recipient_spacing {
        META {
            description = "主送机关上方间距检查"
            weight = "6"
        }
        CASE body.recipient_spacing_lines == 1 THEN score(6)
        DEFAULT fail_metric(0, "主送机关 设置不规范（当前检测值: body.recipient_spacing_lines） | 主送机关上方间距应设置为 1行")
    }

    METRIC body_recipient_indent {
        META {
            description = "主送机关缩进检查"
            weight = "5"
        }
        CASE body.recipient_indent == 0 THEN score(5)
        DEFAULT fail_metric(0, "主送机关 缩进为「body.recipient_indent」，不符合规范要求 | 主送机关应居左顶格")
    }

    METRIC body_font {
        META {
            description = "正文字体检查"
            weight = "6"
            is_critical = "true"
        }
        CASE body.content_font == "仿宋" && body.content_font_size == 3 THEN score(6)
        DEFAULT fail_metric(0, "正文 字体为「body.content_font」字号为「body.content_font_size」，不符合规范要求 | 正文一般用3号仿宋体字")
    }

    METRIC body_first_line_indent {
        META {
            description = "正文首行缩进检查"
            weight = "8"
            is_critical = "true"
        }
        CASE body.first_line_indent_chars == 2 THEN score(8)
        DEFAULT fail_metric(0, "正文 首行缩进为「body.first_line_indent_chars个字符」，请按要求调整 | 正文首行缩进应为 2字符（左空二字）")
    }

    METRIC body_level1_header {
        META {
            description = "一级标题字体检查"
            weight = "5"
        }
        CASE body.level1_font == "黑体" THEN score(5)
        DEFAULT fail_metric(0, "一级标题 字体为「body.level1_font」，不符合规范要求 | 文中结构层次第一层一般用黑体字")
    }

    METRIC body_level2_header {
        META {
            description = "二级标题字体检查"
            weight = "5"
        }
        CASE body.level2_font == "楷体" THEN score(5)
        DEFAULT fail_metric(0, "二级标题 字体为「body.level2_font」，不符合规范要求 | 文中结构层次第二层一般用楷体字")
    }

    METRIC body_attachment_indent {
        META {
            description = "附件说明缩进检查"
            weight = "5"
        }
        CASE body.attachment_indent_chars == 2 THEN score(5)
        DEFAULT fail_metric(0, "附件说明 首行缩进为「body.attachment_indent_chars个字符」，请按要求调整 | 附件说明左缩进应设置为 2字符（左空二字）")
    }

    METRIC body_attachment_format {
        META {
            description = "附件说明格式检查"
            weight = "4"
        }
        CASE body.attachment_format_ok == true THEN score(4)
        DEFAULT fail_metric(0, "附件说明 格式不规范 | 附件说明应以「附件：」开头，序号后不加标点")
    }

    // ========================================================================
    // 7.4 版记
    // ========================================================================

    METRIC rec_separator_width {
        META {
            description = "版记分割线宽度检查"
            weight = "6"
        }
        CASE rec.separator_width_mm >= 155.5 && rec.separator_width_mm <= 156.5 THEN score(6)
        DEFAULT fail_metric(0, "版记分割线 尺寸为「rec.separator_width_mm毫米」，不符合规格 | 版记末条分隔线宽度应与版心等宽（156毫米）")
    }

    METRIC rec_copy_recipient_font {
        META {
            description = "抄送机关字体检查"
            weight = "5"
        }
        CASE rec.has_copy_recipient == false || (rec.copy_recipient_font == "仿宋" && rec.copy_recipient_font_size == 4) THEN score(5)
        DEFAULT fail_metric(0, "抄送机关 字体为「rec.copy_recipient_font」，不符合规范要求 | 抄送机关一般用4号仿宋体字")
    }

    METRIC rec_issuer_font {
        META {
            description = "印发机关字体检查"
            weight = "5"
        }
        CASE rec.issuer_font == "仿宋" && rec.issuer_font_size == 4 THEN score(5)
        DEFAULT fail_metric(0, "印发机关 字体为「rec.issuer_font」，不符合规范要求 | 印发机关和印发日期一般用4号仿宋体字")
    }

    METRIC rec_issue_alignment {
        META {
            description = "印发信息对齐方式检查"
            weight = "5"
        }
        CASE rec.issue_alignment == "两端对齐" THEN score(5)
        DEFAULT fail_metric(0, "印发信息 对齐方式为「rec.issue_alignment」，不符合要求 | 印发信息对齐方式应为 两端对齐（印发机关左空一字，印发日期右空一字）")
    }

    // ========================================================================
    // 7.5 页码
    // ========================================================================

    METRIC page_number_font {
        META {
            description = "页码字体检查"
            weight = "5"
        }
        CASE page.number_font == "半角宋体" && page.number_font_size == 4 THEN score(5)
        DEFAULT fail_metric(0, "页码 字体为「page.number_font」，不符合规范要求 | 一般用4号半角宋体阿拉伯数字")
    }

    METRIC page_number_dash {
        META {
            description = "页码一字线检查"
            weight = "4"
        }
        CASE page.number_has_dashes == true THEN score(4)
        DEFAULT fail_metric(0, "页码 缺少一字线修饰 | 页码数字左右应各放一条一字线")
    }
}
