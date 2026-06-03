/**
 * @file doc_parse.cpp
 * @brief LibreOffice SDK document parsing implementation.
 */

#include "cdsl/doc.h"
#include "xml_parser.h"

#include <LibreOfficeKit/LibreOfficeKitInit.h>
#include <LibreOfficeKit/LibreOfficeKit.hxx>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>
#include <cmath>
#include <algorithm>
#include <set>

/* ------------------------------------------------------------------ */
/*  Global state (protected by g_mutex)                                */
/* ------------------------------------------------------------------ */

static lok::Office* g_office   = nullptr;
static std::mutex   g_mutex;

#ifndef CDSL_LO_PATH
#define CDSL_LO_PATH "/usr/lib/libreoffice/program"
#endif

/* ------------------------------------------------------------------ */
/*  Temp file helpers                                                  */
/* ------------------------------------------------------------------ */

static int
temp_path(char* buf, size_t sz, const char* ext)
{
	const char* tmpl = "/tmp/cdsl_doc_XXXXXX";
	size_t n = strlen(tmpl) + strlen(ext) + 1;
	if (n >= sz) return -1;
	memcpy(buf, tmpl, strlen(tmpl) + 1);
	int fd = mkstemp(buf);
	if (fd < 0) return -1;
	close(fd);
	size_t base = strlen(buf);
	memcpy(buf + base, ext, strlen(ext) + 1);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Length / property helpers                                         */
/* ------------------------------------------------------------------ */

static double parse_length(const char* s) {
    if (!s || !*s) return 0.0;
    char* end = nullptr;
    double val = strtod(s, &end);
    if (end == s) return 0.0;
    while (*end == ' ') end++;
    if (strncmp(end, "cm", 2) == 0) return val * 10.0;
    if (strncmp(end, "mm", 2) == 0) return val;
    if (strncmp(end, "in", 2) == 0) return val * 25.4;
    if (strncmp(end, "pt", 2) == 0) return val * 25.4 / 72.0;
    if (strncmp(end, "inch", 4) == 0) return val * 25.4;
    return val;
}

static double parse_length_mm(const char* s) { return parse_length(s); }

static std::string decode_xml_entities(const char* s, size_t len) {
    std::string out;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '&') {
            if (i + 4 < len && memcmp(s + i, "&amp;", 5) == 0) { out += '&'; i += 4; }
            else if (i + 3 < len && memcmp(s + i, "&lt;", 4) == 0) { out += '<'; i += 3; }
            else if (i + 3 < len && memcmp(s + i, "&gt;", 4) == 0) { out += '>'; i += 3; }
            else if (i + 5 < len && memcmp(s + i, "&quot;", 6) == 0) { out += '"'; i += 5; }
            else if (i + 5 < len && memcmp(s + i, "&apos;", 6) == 0) { out += '\''; i += 5; }
            else out += s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

static void json_escape(std::string& out, const char* s) {
    if (!s) return;
    for (; *s; s++) {
        switch (*s) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)*s < 0x20) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*s);
                    out += buf;
                } else {
                    out += *s;
                }
        }
    }
}

static void skip_element(XmlParser& xp) {
    int d = 1;
    while (d > 0) {
        XmlParser::Type t = xp.next();
        if (t == XmlParser::END) break;
        if (t == XmlParser::START_ELEMENT) d++;
        else if (t == XmlParser::END_ELEMENT) d--;
    }
}

/* ------------------------------------------------------------------ */
/*  Document data structures                                           */
/* ------------------------------------------------------------------ */

struct PageInfo {
    double page_width_mm = 0;
    double page_height_mm = 0;
    double margin_top_mm = 0;
    double margin_bottom_mm = 0;
    double margin_left_mm = 0;
    double margin_right_mm = 0;
    int    page_count = 0;
    int    paragraph_count = 0;
    int    word_count = 0;
    int    character_count = 0;
    int    page_number_start = 0;
    bool   has_page_number_start = false;
    std::string title;
    std::string creator;
    std::string created;
    std::string modified;
    std::vector<std::string> bookmarks;
};

struct TextProps {
    std::string font_name;
    double      font_size_pt = 0;
    bool        bold = false;
    bool        italic = false;
    bool        underline = false;
    bool        strikethrough = false;
    std::string color;
};

struct ParaProps {
    std::string alignment;
    double      margin_top_mm = 0;
    double      margin_bottom_mm = 0;
    double      margin_left_mm = 0;
    double      line_spacing = 0;
    double      indent_first_line_mm = 0;
    std::string background_color;
    int         outline_level = 0;
};

struct TextBlock {
    std::string text;
    TextProps   props;
    std::string hyperlink_url;
    double bbox_x_mm = 0;
    double bbox_y_mm = 0;
    double bbox_w_mm = 0;
    double bbox_h_mm = 0;
};

enum class BlockType {
    PARAGRAPH,
    TABLE,
    LIST,
    IMAGE,
    FOOTNOTE
};

struct TableCell {
    std::vector<TextBlock> text_blocks;
    int          colspan = 1;
    int          rowspan = 1;
};

struct TableRow {
    std::vector<TableCell> cells;
};

struct ContentBlock {
    BlockType type;
    int       page_index = 0;
    int       page_number = 0;
    int       indent_level = 0;
    double    bbox_x_mm = 0;
    double    bbox_y_mm = 0;
    double    bbox_w_mm = 0;
    double    bbox_h_mm = 0;
    
    std::string style_name;
    ParaProps   para_props;
    std::vector<TextBlock> text_blocks;
    std::vector<TableRow> table_rows;
    std::string image_name;
    std::string image_base64;
    std::string footnote_id;
};

/* ------------------------------------------------------------------ */
/*  Style Database                                                     */
/* ------------------------------------------------------------------ */

struct Style {
    std::string name;
    std::string parent;
    ParaProps   para;
    TextProps   text;
    bool        resolved = false;
};

static Style g_style_db[1024];
static int   g_style_count = 0;

static void init_style_db() { g_style_count = 0; }

static Style* find_style(const char* name) {
    if (!name || !*name) return nullptr;
    for (int i = 0; i < g_style_count; i++) {
        if (g_style_db[i].name == name) return &g_style_db[i];
    }
    return nullptr;
}

static void merge_style(const Style& src, Style& dst) {
    if (dst.para.alignment.empty()) dst.para.alignment = src.para.alignment;
    if (dst.para.margin_top_mm == 0) dst.para.margin_top_mm = src.para.margin_top_mm;
    if (dst.para.margin_bottom_mm == 0) dst.para.margin_bottom_mm = src.para.margin_bottom_mm;
    if (dst.para.margin_left_mm == 0) dst.para.margin_left_mm = src.para.margin_left_mm;
    if (dst.para.line_spacing == 0) dst.para.line_spacing = src.para.line_spacing;
    if (dst.para.indent_first_line_mm == 0) dst.para.indent_first_line_mm = src.para.indent_first_line_mm;
    if (dst.para.background_color.empty()) dst.para.background_color = src.para.background_color;
    if (dst.para.outline_level == 0) dst.para.outline_level = src.para.outline_level;

    if (dst.text.font_name.empty()) dst.text.font_name = src.text.font_name;
    if (dst.text.font_size_pt == 0) dst.text.font_size_pt = src.text.font_size_pt;
    if (!dst.text.bold) dst.text.bold = src.text.bold;
    if (!dst.text.italic) dst.text.italic = src.text.italic;
    if (!dst.text.underline) dst.text.underline = src.text.underline;
    if (!dst.text.strikethrough) dst.text.strikethrough = src.text.strikethrough;
    if (dst.text.color.empty()) dst.text.color = src.text.color;
}

static void resolve_all_styles() {
    for (int i = 0; i < g_style_count; i++) {
        if (g_style_db[i].resolved) continue;
        std::vector<Style*> chain;
        Style* curr = &g_style_db[i];
        while (curr && !curr->resolved) {
            if (std::find(chain.begin(), chain.end(), curr) != chain.end()) break; 
            chain.push_back(curr);
            curr = find_style(curr->parent.c_str());
        }
        Style base = curr ? *curr : Style{};
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            merge_style(base, **it);
            (*it)->resolved = true;
            base = **it;
        }
    }
}

static Style resolve_style(const char* name) {
    Style* s = find_style(name);
    return s ? *s : Style{};
}

/* ------------------------------------------------------------------ */
/*  FODT Parsing                                                       */
/* ------------------------------------------------------------------ */

static void parse_text_props_elem(XmlParser& xp, TextProps& p) {
    const char* v;
    if ((v = xp.attr("style:font-name-asian")) || (v = xp.attr("style:font-name"))) p.font_name = v;
    if ((v = xp.attr("style:font-size-asian")) || (v = xp.attr("fo:font-size"))) { char* end; p.font_size_pt = strtod(v, &end); }
    if ((v = xp.attr("fo:font-weight"))) p.bold = (strcmp(v, "bold") == 0);
    if ((v = xp.attr("fo:font-style"))) p.italic = (strcmp(v, "italic") == 0);
    if ((v = xp.attr("style:text-underline-style"))) p.underline = (strcmp(v, "none") != 0);
    if ((v = xp.attr("style:text-line-through-style"))) p.strikethrough = (strcmp(v, "none") != 0);
    if ((v = xp.attr("fo:color"))) p.color = v;
}

static void parse_para_props_elem(XmlParser& xp, ParaProps& p) {
    const char* v;
    if ((v = xp.attr("fo:text-align"))) p.alignment = v;
    if ((v = xp.attr("fo:margin-top"))) p.margin_top_mm = parse_length_mm(v);
    if ((v = xp.attr("fo:margin-bottom"))) p.margin_bottom_mm = parse_length_mm(v);
    if ((v = xp.attr("fo:margin-left"))) p.margin_left_mm = parse_length_mm(v);
    if ((v = xp.attr("fo:line-height"))) {
        if (strchr(v, '%')) p.line_spacing = atof(v) / 100.0;
        else p.line_spacing = parse_length_mm(v);
    }
    if ((v = xp.attr("fo:text-indent"))) p.indent_first_line_mm = parse_length_mm(v);
    if ((v = xp.attr("fo:background-color"))) p.background_color = v;
}

static void parse_style_elem(XmlParser& xp) {
    if (g_style_count >= 1024) return;
    Style* s = &g_style_db[g_style_count++];
    const char* v;
    if ((v = xp.attr("style:name"))) s->name = v;
    if ((v = xp.attr("style:parent-style-name"))) s->parent = v;
    while (xp.next() == XmlParser::START_ELEMENT) {
        if (xp.match("style:paragraph-properties")) parse_para_props_elem(xp, s->para);
        else if (xp.match("style:text-properties")) parse_text_props_elem(xp, s->text);
        int depth = 1; while (depth > 0 && xp.next() != XmlParser::END) {
            if (xp.type() == XmlParser::START_ELEMENT) depth++;
            else if (xp.type() == XmlParser::END_ELEMENT) depth--;
        }
    }
}

static void parse_page_layout(XmlParser& xp, PageInfo& page) {
    const char* v;
    while (xp.next() == XmlParser::START_ELEMENT) {
        if (xp.match("style:page-layout-properties")) {
            if ((v = xp.attr("fo:page-width")))  page.page_width_mm = parse_length_mm(v);
            if ((v = xp.attr("fo:page-height"))) page.page_height_mm = parse_length_mm(v);
            if ((v = xp.attr("fo:margin-top")))    page.margin_top_mm = parse_length_mm(v);
            if ((v = xp.attr("fo:margin-bottom"))) page.margin_bottom_mm = parse_length_mm(v);
            if ((v = xp.attr("fo:margin-left")))   page.margin_left_mm = parse_length_mm(v);
            if ((v = xp.attr("fo:margin-right")))  page.margin_right_mm = parse_length_mm(v);
            if ((v = xp.attr("style:page-number"))) { page.page_number_start = atoi(v); page.has_page_number_start = true; }
        }
        int depth = 1; while (depth > 0 && xp.next() != XmlParser::END) {
            if (xp.type() == XmlParser::START_ELEMENT) depth++;
            else if (xp.type() == XmlParser::END_ELEMENT) depth--;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  JSON Formatting Helpers                                            */
/* ------------------------------------------------------------------ */

static void json_append_text_block(std::string& out, const TextBlock& b)
{
    out += "          {\n";
    out += "            \"text\": \""; json_escape(out, b.text.c_str()); out += "\"\n";
    if (!b.props.font_name.empty()) {
        out += "            ,\"font_name\": \""; json_escape(out, b.props.font_name.c_str()); out += "\"\n";
    }
    if (b.props.font_size_pt > 0) {
        char buf[64]; snprintf(buf, sizeof(buf), "            ,\"font_size_pt\": %.1f\n", b.props.font_size_pt); out += buf;
    }
    if (b.props.bold) out += "            ,\"bold\": true\n";
    if (b.props.italic) out += "            ,\"italic\": true\n";
    if (b.props.underline) out += "            ,\"underline\": true\n";
    if (b.props.strikethrough) out += "            ,\"strikethrough\": true\n";
    if (!b.props.color.empty()) {
        out += "            ,\"color\": \""; json_escape(out, b.props.color.c_str()); out += "\"\n";
    }
    if (!b.hyperlink_url.empty()) {
        out += "            ,\"hyperlink_url\": \""; json_escape(out, b.hyperlink_url.c_str()); out += "\"\n";
    }
    char buf[128]; snprintf(buf, sizeof(buf), "            ,\"bbox_mm\": [%.1f, %.1f, %.1f, %.1f]\n", b.bbox_x_mm, b.bbox_y_mm, b.bbox_w_mm, b.bbox_h_mm);
    out += buf;
    out += "          }";
}

static void json_append_para_props(std::string& out, const ParaProps& props) {
    if (!props.alignment.empty()) {
        out += "      ,\"alignment\": \""; json_escape(out, props.alignment.c_str()); out += "\"\n";
    }
    char buf[128];
    if (props.margin_top_mm > 0) { snprintf(buf, sizeof(buf), "      ,\"spacing_before_mm\": %.1f\n", props.margin_top_mm); out += buf; }
    if (props.margin_bottom_mm > 0) { snprintf(buf, sizeof(buf), "      ,\"spacing_after_mm\": %.1f\n", props.margin_bottom_mm); out += buf; }
    if (props.line_spacing > 0) { snprintf(buf, sizeof(buf), "      ,\"line_spacing\": %.2f\n", props.line_spacing); out += buf; }
    if (props.indent_first_line_mm > 0) { snprintf(buf, sizeof(buf), "      ,\"indent_first_line_mm\": %.1f\n", props.indent_first_line_mm); out += buf; }
    if (!props.background_color.empty()) { out += "      ,\"background_color\": \""; json_escape(out, props.background_color.c_str()); out += "\"\n"; }
    if (props.outline_level > 0) { snprintf(buf, sizeof(buf), "      ,\"outline_level\": %d\n", props.outline_level); out += buf; }
}

static void json_append_block(std::string& out, const ContentBlock& b) {
    out += "    {\n";
    out += "      \"type\": \"";
    switch(b.type) {
        case BlockType::PARAGRAPH: out += "paragraph"; break;
        case BlockType::TABLE:     out += "table"; break;
        case BlockType::LIST:      out += "list"; break;
        case BlockType::IMAGE:     out += "image"; break;
        case BlockType::FOOTNOTE:  out += "footnote"; break;
    }
    out += "\",\n";
    char buf[256];
    snprintf(buf, sizeof(buf), 
        "      \"page_index\": %d,\n"
        "      \"page_number\": %d,\n"
        "      \"indent_level\": %d,\n"
        "      \"bbox_mm\": [%.1f, %.1f, %.1f, %.1f]",
        b.page_index, b.page_number, b.indent_level, b.bbox_x_mm, b.bbox_y_mm, b.bbox_w_mm, b.bbox_h_mm);
    out += buf;
    if (!b.style_name.empty()) { out += ",\n      \"style\": \""; json_escape(out, b.style_name.c_str()); out += "\""; }
    if (b.type == BlockType::PARAGRAPH || b.type == BlockType::LIST) {
        std::string plain_text;
        for (const auto& tb : b.text_blocks) plain_text += tb.text;
        out += ",\n      \"content\": \""; json_escape(out, plain_text.c_str()); out += "\"";
        json_append_para_props(out, b.para_props);
        out += ",\n      \"text_blocks\": [\n";
        for (size_t i = 0; i < b.text_blocks.size(); i++) {
            if (i > 0) out += ",\n"; 
            json_append_text_block(out, b.text_blocks[i]);
        }
        out += "\n      ]";
    } else if (b.type == BlockType::TABLE) {
        out += ",\n      \"rows\": [\n";
        for (size_t r = 0; r < b.table_rows.size(); r++) {
            if (r > 0) out += ",\n";
            out += "        {\"cells\":[";
            for (size_t c = 0; c < b.table_rows[r].cells.size(); c++) {
                if (c > 0) out += ",";
                out += "{\"colspan\":"; out += std::to_string(b.table_rows[r].cells[c].colspan);
                out += ",\"rowspan\":"; out += std::to_string(b.table_rows[r].cells[c].rowspan);
                out += ",\"content\":\"";
                std::string cell_text;
                for (const auto& tb : b.table_rows[r].cells[c].text_blocks) cell_text += tb.text;
                json_escape(out, cell_text.c_str());
                out += "\",\"text_blocks\":[\n";
                for (size_t i = 0; i < b.table_rows[r].cells[c].text_blocks.size(); i++) {
                    if (i > 0) out += ",\n"; 
                    json_append_text_block(out, b.table_rows[r].cells[c].text_blocks[i]);
                }
                out += "]}";
            }
            out += "]}";
        }
        out += "\n      ]";
    } else if (b.type == BlockType::IMAGE) {
        out += ",\n      \"image_name\": \""; json_escape(out, b.image_name.c_str()); out += "\",\n";
        out += "      \"data_base64\": \""; out += b.image_base64; out += "\"";
    } else if (b.type == BlockType::FOOTNOTE) {
        out += ",\n      \"footnote_id\": \""; json_escape(out, b.footnote_id.c_str()); out += "\",\n";
        out += "      \"content\": \""; if (!b.text_blocks.empty()) json_escape(out, b.text_blocks[0].text.c_str()); out += "\"";
    }
    out += "\n    }";
}

/* ------------------------------------------------------------------ */
/*  FODT Body Parsing                                                  */
/* ------------------------------------------------------------------ */

static bool parse_fodt_body(XmlParser& xp, PageInfo& page, std::vector<ContentBlock>& out_blocks, int indent_level = 0)
{
    while (xp.next() == XmlParser::START_ELEMENT) {
        if (xp.match("text:p") || xp.match("text:h")) {
            ContentBlock b; b.type = BlockType::PARAGRAPH;
            b.indent_level = indent_level;
            const char* style = xp.attr("text:style-name"); if (style) b.style_name = style;
            const char* level = xp.attr("text:outline-level"); if (level) b.para_props.outline_level = atoi(level);
            int d = 1; while (d > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) {
                    if (xp.match("text:span")) {
                        TextBlock tb; const char* span_style = xp.attr("text:style-name"); if (span_style) tb.props = resolve_style(span_style).text;
                        int sd = 1; while (sd > 0 && xp.next() != XmlParser::END) { if (xp.type() == XmlParser::START_ELEMENT) sd++; else if (xp.type() == XmlParser::END_ELEMENT) sd--; else if (xp.type() == XmlParser::TEXT && xp.textContent()) tb.text.append(decode_xml_entities(xp.textContent(), xp.textLength())); }
                        if (!tb.text.empty()) b.text_blocks.push_back(tb);
                    } else if (xp.match("text:a")) {
                        std::string url = xp.attr("xlink:href") ? xp.attr("xlink:href") : "";
                        int ad = 1; while (ad > 0 && xp.next() != XmlParser::END) { if (xp.type() == XmlParser::START_ELEMENT) ad++; else if (xp.type() == XmlParser::END_ELEMENT) ad--; else if (xp.type() == XmlParser::TEXT && xp.textContent()) { TextBlock tb; tb.text.append(decode_xml_entities(xp.textContent(), xp.textLength())); tb.hyperlink_url = url; b.text_blocks.push_back(tb); } }
                    } else if (xp.match("text:bookmark") || xp.match("text:bookmark-start")) {
                        const char* name = xp.attr("text:name"); if (name) page.bookmarks.push_back(name);
                        skip_element(xp);
                    } else if (xp.match("text:note")) {
                        ContentBlock fn; fn.type = BlockType::FOOTNOTE;
                        fn.footnote_id = xp.attr("text:id") ? xp.attr("text:id") : "";
                        int nd = 1; while (nd > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::START_ELEMENT) { if (xp.match("text:note-body")) { int nbd = 1; while (nbd > 0 && xp.next() != XmlParser::END) { if (xp.type() == XmlParser::START_ELEMENT) nbd++; else if (xp.type() == XmlParser::END_ELEMENT) nbd--; else if (xp.type() == XmlParser::TEXT && xp.textContent()) { if (fn.text_blocks.empty()) fn.text_blocks.push_back({}); fn.text_blocks[0].text.append(decode_xml_entities(xp.textContent(), xp.textLength())); } } } else { skip_element(xp); } }
                            else if (xp.type() == XmlParser::END_ELEMENT) nd--;
                        }
                        out_blocks.push_back(fn);
                    } else { skip_element(xp); }
                } else if (xp.type() == XmlParser::END_ELEMENT) d--;
                else if (xp.type() == XmlParser::TEXT && xp.textContent()) { if (b.text_blocks.empty()) b.text_blocks.push_back(TextBlock{}); b.text_blocks.back().text.append(decode_xml_entities(xp.textContent(), xp.textLength())); }
            }
            out_blocks.push_back(b);
        } else if (xp.match("table:table")) {
            ContentBlock b; b.type = BlockType::TABLE; b.style_name = xp.attr("table:style-name") ? xp.attr("table:style-name") : "";
            b.indent_level = indent_level;
            int td = 1; while (td > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) {
                    if (xp.match("table:table-row")) {
                        TableRow row; int rd = 1; while (rd > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::START_ELEMENT) { 
                                if (xp.match("table:table-cell")) { 
                                    TableCell cell; const char* cs = xp.attr("table:number-columns-spanned"); if (cs) cell.colspan = atoi(cs); const char* rs = xp.attr("table:number-rows-spanned"); if (rs) cell.rowspan = atoi(rs);
                                    int cd = 1; while (cd > 0 && xp.next() != XmlParser::END) {
                                        if (xp.type() == XmlParser::START_ELEMENT) {
                                            if (xp.match("text:p")) {
                                                int pd = 1; while (pd > 0 && xp.next() != XmlParser::END) {
                                                    if (xp.type() == XmlParser::START_ELEMENT) {
                                                        if (xp.match("text:span")) {
                                                            TextBlock tb; const char* ss = xp.attr("text:style-name"); if (ss) tb.props = resolve_style(ss).text;
                                                            int sd = 1; while (sd > 0 && xp.next() != XmlParser::END) { if (xp.type() == XmlParser::START_ELEMENT) sd++; else if (xp.type() == XmlParser::END_ELEMENT) sd--; else if (xp.type() == XmlParser::TEXT) tb.text.append(decode_xml_entities(xp.textContent(), xp.textLength())); }
                                                            if (!tb.text.empty()) cell.text_blocks.push_back(tb);
                                                        } else { skip_element(xp); }
                                                    } else if (xp.type() == XmlParser::END_ELEMENT) pd--;
                                                    else if (xp.type() == XmlParser::TEXT && xp.textContent()) { if (cell.text_blocks.empty()) cell.text_blocks.push_back({}); cell.text_blocks.back().text.append(decode_xml_entities(xp.textContent(), xp.textLength())); }
                                                }
                                            } else { skip_element(xp); }
                                        } else if (xp.type() == XmlParser::END_ELEMENT) cd--;
                                        else if (xp.type() == XmlParser::TEXT && xp.textContent()) { if (cell.text_blocks.empty()) cell.text_blocks.push_back({}); cell.text_blocks.back().text.append(decode_xml_entities(xp.textContent(), xp.textLength())); }
                                    }
                                    row.cells.push_back(cell);
                                } else { skip_element(xp); } 
                            } else if (xp.type() == XmlParser::END_ELEMENT) rd--;
                        }
                        b.table_rows.push_back(row);
                    } else { skip_element(xp); }
                } else if (xp.type() == XmlParser::END_ELEMENT) td--;
            }
            out_blocks.push_back(b);
        } else if (xp.match("text:list")) {
            int ld = 1; while (ld > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) { 
                    if (xp.match("text:list-item")) { 
                        int lid = 1; while (lid > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::START_ELEMENT) {
                                if (xp.match("text:p") || xp.match("text:h")) {
                                    ContentBlock b; b.type = BlockType::PARAGRAPH;
                                    b.indent_level = indent_level + 1;
                                    const char* style = xp.attr("text:style-name"); if (style) b.style_name = style;
                                    int pd = 1; while (pd > 0 && xp.next() != XmlParser::END) {
                                        if (xp.type() == XmlParser::START_ELEMENT) {
                                            if (xp.match("text:span")) {
                                                TextBlock tb; const char* span_style = xp.attr("text:style-name"); if (span_style) tb.props = resolve_style(span_style).text;
                                                int sd = 1; while (sd > 0 && xp.next() != XmlParser::END) { if (xp.type() == XmlParser::START_ELEMENT) sd++; else if (xp.type() == XmlParser::END_ELEMENT) sd--; else if (xp.type() == XmlParser::TEXT && xp.textContent()) tb.text.append(decode_xml_entities(xp.textContent(), xp.textLength())); }
                                                if (!tb.text.empty()) b.text_blocks.push_back(tb);
                                            } else { skip_element(xp); }
                                        } else if (xp.type() == XmlParser::END_ELEMENT) pd--;
                                        else if (xp.type() == XmlParser::TEXT && xp.textContent()) { if (b.text_blocks.empty()) b.text_blocks.push_back(TextBlock{}); b.text_blocks.back().text.append(decode_xml_entities(xp.textContent(), xp.textLength())); }
                                    }
                                    out_blocks.push_back(b);
                                } else if (xp.match("text:list")) {
                                    parse_fodt_body(xp, page, out_blocks, indent_level + 1);
                                } else { skip_element(xp); }
                            } else if (xp.type() == XmlParser::END_ELEMENT) lid--;
                        }
                    } else { skip_element(xp); } 
                } else if (xp.type() == XmlParser::END_ELEMENT) ld--;
            }
        } else if (xp.match("draw:frame")) {
             ContentBlock b; b.type = BlockType::IMAGE;
             b.bbox_w_mm = parse_length_mm(xp.attr("svg:width"));
             b.bbox_h_mm = parse_length_mm(xp.attr("svg:height"));
             int id = 1; while (id > 0 && xp.next() != XmlParser::END) {
                 if (xp.type() == XmlParser::START_ELEMENT) { if (xp.match("draw:image")) { const char* href = xp.attr("xlink:href"); if (href) b.image_name = href; } id++; }
                 else if (xp.type() == XmlParser::END_ELEMENT) id--;
             }
             out_blocks.push_back(b);
        } else { skip_element(xp); }
    }
    return true;
}

static bool parse_fodt_document(const char* fodt, size_t fodt_len,
                                 PageInfo& page, std::vector<ContentBlock>& out_blocks)
{
    init_style_db(); page = PageInfo{};
    XmlParser xp(fodt, fodt_len);
    while (xp.next() != XmlParser::END) {
        if (xp.type() != XmlParser::START_ELEMENT) continue;
        if (xp.match("office:document")) continue;
        
        if (xp.match("office:meta")) {
            int d = 1; while (d > 0 && (xp.next() != XmlParser::END)) { 
                if (xp.type() == XmlParser::START_ELEMENT) { 
                    if (xp.match("meta:document-statistic")) { 
                        const char* v; if ((v = xp.attr("meta:page-count"))) page.page_count = atoi(v); if ((v = xp.attr("meta:paragraph-count"))) page.paragraph_count = atoi(v); if ((v = xp.attr("meta:word-count"))) page.word_count = atoi(v); if ((v = xp.attr("meta:character-count"))) page.character_count = atoi(v); 
                    } else if (xp.match("dc:title")) { xp.next(); if (xp.type() == XmlParser::TEXT) page.title = decode_xml_entities(xp.textContent(), xp.textLength()); }
                    else if (xp.match("dc:creator")) { xp.next(); if (xp.type() == XmlParser::TEXT) page.creator = decode_xml_entities(xp.textContent(), xp.textLength()); }
                    else if (xp.match("meta:creation-date")) { xp.next(); if (xp.type() == XmlParser::TEXT) page.created = decode_xml_entities(xp.textContent(), xp.textLength()); }
                    else if (xp.match("dc:date")) { xp.next(); if (xp.type() == XmlParser::TEXT) page.modified = decode_xml_entities(xp.textContent(), xp.textLength()); }
                    skip_element(xp);
                } else if (xp.type() == XmlParser::END_ELEMENT) d--; 
            }
        } else if (xp.match("office:styles") || xp.match("office:automatic-styles")) {
            int d = 1; while (d > 0 && xp.next() != XmlParser::END) { if (xp.type() == XmlParser::START_ELEMENT) { if (xp.match("style:style")) parse_style_elem(xp); else if (xp.match("style:page-layout")) parse_page_layout(xp, page); else { skip_element(xp); } } else if (xp.type() == XmlParser::END_ELEMENT) d--; }
            for (int i = 0; i < g_style_count; i++) { if (!g_style_db[i].parent.empty()) { Style parent = resolve_style(g_style_db[i].parent.c_str()); merge_style(parent, g_style_db[i]); } }
        } else if (xp.match("office:body")) {
            int d = 1; while (d > 0 && xp.next() != XmlParser::END) { if (xp.type() == XmlParser::START_ELEMENT) { if (xp.match("office:text")) { if (!parse_fodt_body(xp, page, out_blocks)) return false; } else { skip_element(xp); } } else if (xp.type() == XmlParser::END_ELEMENT) d--; }
        } else {
            skip_element(xp);
        }
    }
    return true;
}

struct TwipRect { double x = 0, y = 0, w = 0, h = 0; };
struct CursorCapture {
    double x_twip = -1, y_twip = -1, w_twip = 0, h_twip = 0;
    std::vector<TwipRect> selection;
    bool updated = false; bool has_coords = false; int logical_page = 0;
};

static bool pump_until_updated(lok::Document* doc, CursorCapture& cap, int max_attempts = 50) {
    unsigned char buf[4];
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        if (cap.updated) return true;
        doc->paintTile(buf, 1, 1, 0, 0, 10, 10); usleep(2000);
    }
    return cap.updated;
}

static void parse_rect_list(const char* payload, std::vector<TwipRect>& out) {
    if (!payload || !*payload) return;
    const char* p = payload;
    while (p && *p) {
        double x, y, w, h; char* end = nullptr;
        x = strtod(p, &end); if (end == p) break; p = end; while (*p == ',' || *p == ' ') p++;
        y = strtod(p, &end); if (end == p) break; p = end; while (*p == ',' || *p == ' ') p++;
        w = strtod(p, &end); if (end == p) break; p = end; while (*p == ',' || *p == ' ') p++;
        h = strtod(p, &end); if (end == p) break; p = end; while (*p == ',' || *p == ' ' || *p == ';') p++;
        out.push_back({x, y, w, h});
        p = strchr(p, ';'); if (p) p++; while (p && *p == ' ') p++;
    }
}

static void cursor_callback(int type, const char* payload, void* data) {
    auto* cap = static_cast<CursorCapture*>(data);
    if (type == LOK_CALLBACK_INVALIDATE_VISIBLE_CURSOR && payload && *payload) {
        std::vector<TwipRect> rects; parse_rect_list(payload, rects);
        if (!rects.empty()) { cap->x_twip = rects[0].x; cap->y_twip = rects[0].y; cap->w_twip = rects[0].w; cap->h_twip = rects[0].h; cap->has_coords = true; cap->updated = true; }
    }
    if (type == LOK_CALLBACK_TEXT_SELECTION && payload && *payload) {
        cap->selection.clear(); parse_rect_list(payload, cap->selection); cap->updated = true;
    }
    if (type == LOK_CALLBACK_STATE_CHANGED && payload) {
        if (strncmp(payload, ".uno:StatePageNumber=", 21) == 0) {
            const char* val = payload + 21; int pnum = 0;
            while (*val && !isdigit(*val)) val++;
            if (*val && sscanf(val, "%d", &pnum) == 1) cap->logical_page = pnum;
        }
    }
}

static void get_block_positions(lok::Document* doc, std::vector<ContentBlock>& blocks) {
    if (blocks.empty() || !doc) return;
    CursorCapture cap; doc->registerCallback(cursor_callback, &cap);

    long doc_w = 0, doc_h = 0; doc->getDocumentSize(&doc_w, &doc_h);
    if (doc_h > 0) { unsigned char dummy[4]; doc->paintTile(dummy, 1, 1, 0, doc_h - 200, 200, 200); }
    
    char* rects_str = doc->getPartPageRectangles();
    std::vector<TwipRect> page_rects;
    if (rects_str) { parse_rect_list(rects_str, page_rects); free(rects_str); }

    doc->postUnoCommand(".uno:GoToStartOfDoc", nullptr, false);
    pump_until_updated(doc, cap, 50);

    const double twip_to_mm = 25.4 / 1440.0;
    
    for (auto& b : blocks) {
        cap.updated = false; cap.has_coords = false; cap.x_twip = -1; cap.y_twip = -1;
        cap.selection.clear();

        if (b.type == BlockType::PARAGRAPH || b.type == BlockType::LIST) {
            /* Force initial recognition for first paragraph */
            doc->postUnoCommand(".uno:GoToStartOfPara", nullptr, false);
            if (!pump_until_updated(doc, cap, 5)) {
                 /* If no update, move slightly then back to trigger recognition */
                 doc->postUnoCommand(".uno:GoRight", nullptr, false); pump_until_updated(doc, cap, 5);
                 doc->postUnoCommand(".uno:GoLeft", nullptr, false); pump_until_updated(doc, cap, 5);
            }
            
            cap.updated = false;
            doc->postUnoCommand(".uno:GoToEndOfParaSel", nullptr, false);
            if (pump_until_updated(doc, cap, 15) && !cap.selection.empty()) {
                TwipRect u = cap.selection[0];
                for (size_t s = 1; s < cap.selection.size(); s++) {
                    double ux1 = std::min(u.x, cap.selection[s].x), uy1 = std::min(u.y, cap.selection[s].y);
                    double ux2 = std::max(u.x + u.w, cap.selection[s].x + cap.selection[s].w);
                    double uy2 = std::max(u.y + u.h, cap.selection[s].y + cap.selection[s].h);
                    u = {ux1, uy1, ux2 - ux1, uy2 - uy1};
                }
                int phys_page = 1; double page_y = 0;
                for (size_t p = 0; p < page_rects.size(); p++) {
                    if (u.y + u.h / 2.0 >= page_rects[p].y && u.y + u.h / 2.0 < page_rects[p].y + page_rects[p].h) {
                        phys_page = (int)p + 1; page_y = page_rects[p].y; break;
                    }
                }
                b.page_index = phys_page; b.page_number = (cap.logical_page > 0) ? cap.logical_page : phys_page;
                b.bbox_x_mm = u.x * twip_to_mm; b.bbox_y_mm = std::max(0.0, (u.y - page_y) * twip_to_mm);
                b.bbox_w_mm = u.w * twip_to_mm; b.bbox_h_mm = u.h * twip_to_mm;
            } else if (cap.has_coords || (cap.x_twip != -1)) {
                int phys_page = 1; double page_y = 0;
                for (size_t p = 0; p < page_rects.size(); p++) {
                    if (cap.y_twip >= page_rects[p].y && cap.y_twip < page_rects[p].y + page_rects[p].h) {
                        phys_page = (int)p + 1; page_y = page_rects[p].y; break;
                    }
                }
                b.page_index = phys_page; b.page_number = (cap.logical_page > 0) ? cap.logical_page : phys_page;
                b.bbox_x_mm = cap.x_twip * twip_to_mm; b.bbox_y_mm = std::max(0.0, (cap.y_twip - page_y) * twip_to_mm);
                b.bbox_w_mm = 8000 * twip_to_mm; b.bbox_h_mm = cap.h_twip * twip_to_mm;
            }

            for (auto& tb : b.text_blocks) {
                double pct = 0; int total_len = 0; for(auto& t : b.text_blocks) total_len += t.text.length();
                pct = total_len > 0 ? (double)tb.text.length() / total_len : 0;
                tb.bbox_x_mm = b.bbox_x_mm; tb.bbox_y_mm = b.bbox_y_mm; tb.bbox_w_mm = b.bbox_w_mm * pct; tb.bbox_h_mm = b.bbox_h_mm;
                Style st = resolve_style(b.style_name.c_str());
                if (tb.props.font_name.empty()) tb.props.font_name = st.text.font_name;
                if (tb.props.font_size_pt == 0) tb.props.font_size_pt = st.text.font_size_pt;
            }
        } else if (b.type == BlockType::TABLE) {
            doc->postUnoCommand(".uno:SelectTable", nullptr, false);
            if (pump_until_updated(doc, cap, 15) && !cap.selection.empty()) {
                TwipRect u = cap.selection[0];
                for (size_t s = 1; s < cap.selection.size(); s++) {
                    double ux1 = std::min(u.x, cap.selection[s].x), uy1 = std::min(u.y, cap.selection[s].y);
                    double ux2 = std::max(u.x + u.w, cap.selection[s].x + cap.selection[s].w);
                    double uy2 = std::max(u.y + u.h, cap.selection[s].y + cap.selection[s].h);
                    u = {ux1, uy1, ux2 - ux1, uy2 - uy1};
                }
                int phys_page = 1; double page_y = 0;
                for (size_t p = 0; p < page_rects.size(); p++) {
                    if (u.y + u.h / 2.0 >= page_rects[p].y && u.y + u.h / 2.0 < page_rects[p].y + page_rects[p].h) {
                        phys_page = (int)p + 1; page_y = page_rects[p].y; break;
                    }
                }
                b.page_index = phys_page; b.page_number = (cap.logical_page > 0) ? cap.logical_page : phys_page;
                b.bbox_x_mm = u.x * twip_to_mm; b.bbox_y_mm = std::max(0.0, (u.y - page_y) * twip_to_mm);
                b.bbox_w_mm = u.w * twip_to_mm; b.bbox_h_mm = u.h * twip_to_mm;
            }
        } else {
            pump_until_updated(doc, cap, 5);
            if (cap.has_coords) {
                int phys_page = 1; double page_y = 0;
                for (size_t p = 0; p < page_rects.size(); p++) {
                    if (cap.y_twip >= page_rects[p].y && cap.y_twip < page_rects[p].y + page_rects[p].h) {
                        phys_page = (int)p + 1; page_y = page_rects[p].y; break;
                    }
                }
                b.page_index = phys_page; b.page_number = (cap.logical_page > 0) ? cap.logical_page : phys_page;
                b.bbox_x_mm = cap.x_twip * twip_to_mm; b.bbox_y_mm = std::max(0.0, (cap.y_twip - page_y) * twip_to_mm);
                if (b.bbox_w_mm == 0) b.bbox_w_mm = cap.w_twip * twip_to_mm;
                if (b.bbox_h_mm == 0) b.bbox_h_mm = cap.h_twip * twip_to_mm;
            }
        }
        cap.updated = false; doc->postUnoCommand(".uno:GoToEndOfPara", nullptr, false); pump_until_updated(doc, cap, 5);
        doc->postUnoCommand(".uno:GoRight", nullptr, false);
        if (!pump_until_updated(doc, cap, 10)) { doc->postUnoCommand(".uno:GoDown", nullptr, false); pump_until_updated(doc, cap, 5); }
    }
    doc->registerCallback(nullptr, nullptr);
}

extern "C" int cdsl_doc_init(void) {
	if (g_office) return 1;
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_office) return 1;
	g_office = lok::lok_cpp_init(CDSL_LO_PATH);
	return (g_office != nullptr) ? 1 : 0;
}

extern "C" void cdsl_doc_shutdown(void) {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_office) { delete g_office; g_office = nullptr; }
}

extern "C" char* cdsl_doc_extract_text(const char* path) {
	if (!path) return nullptr;
	char tmp_path[128]; if (temp_path(tmp_path, sizeof(tmp_path), ".txt") != 0) return nullptr;
	char url[4096], doc_url[4096]; snprintf(url, sizeof(url), "file://%s", tmp_path); snprintf(doc_url, sizeof(doc_url), "file://%s", path);
	{
		std::lock_guard<std::mutex> lock(g_mutex); if (!g_office) { remove(tmp_path); return nullptr; }
		std::unique_ptr<lok::Document> doc(g_office->documentLoad(doc_url)); if (!doc) { remove(tmp_path); return nullptr; }
		if (!doc->saveAs(url, "txt")) { remove(tmp_path); return nullptr; }
	}
	FILE* f = fopen(tmp_path, "rb"); if (!f) { remove(tmp_path); return nullptr; }
	fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
	char* out = (char*)malloc((size_t)fsize + 1); if (!out) { fclose(f); remove(tmp_path); return nullptr; }
	size_t nread = fread(out, 1, (size_t)fsize, f); out[nread] = '\0'; fclose(f); remove(tmp_path);
	return out;
}

extern "C" char* cdsl_doc_extract_to_json(const char* path) {
	if (!path) return nullptr;
	char fodt_path[128]; if (temp_path(fodt_path, sizeof(fodt_path), ".fodt") != 0) return nullptr;
	char doc_url[4096], fodt_url[4096]; snprintf(doc_url, sizeof(doc_url), "file://%s", path); snprintf(fodt_url, sizeof(fodt_url), "file://%s", fodt_path);
	PageInfo page; std::vector<ContentBlock> blocks;
	{
		std::lock_guard<std::mutex> lock(g_mutex); if (!g_office) { remove(fodt_path); return nullptr; }
		std::unique_ptr<lok::Document> doc(g_office->documentLoad(doc_url)); if (!doc) { remove(fodt_path); return nullptr; }
		doc->initializeForRendering(nullptr);
		if (!doc->saveAs(fodt_url, "FODT")) { remove(fodt_path); return nullptr; }
		FILE* ff = fopen(fodt_path, "rb"); if (!ff) { remove(fodt_path); return nullptr; }
		fseek(ff, 0, SEEK_END); long fodt_size = ftell(ff); fseek(ff, 0, SEEK_SET);
		std::vector<char> fodt_buf(fodt_size + 1); size_t nf = fread(fodt_buf.data(), 1, (size_t)fodt_size, ff); fodt_buf[nf] = '\0'; fclose(ff); remove(fodt_path);
		if (!parse_fodt_document(fodt_buf.data(), (size_t)fodt_size, page, blocks)) return nullptr;
		resolve_all_styles();
		if (!blocks.empty() && doc) get_block_positions(doc.get(), blocks);
	}
	std::string out; out.reserve(65536); out += "{\n  \"document\": {\n    \"metadata\": {\n";
	char b[512]; snprintf(b, sizeof(b), "      \"page_count\": %d,\n      \"paragraph_count\": %d,\n      \"word_count\": %d,\n      \"character_count\": %d,\n      \"page_size_mm\": [%.1f, %.1f],\n      \"margins_mm\": [%.1f, %.1f, %.1f, %.1f]", 
	         page.page_count, (int)blocks.size(), page.word_count, page.character_count, page.page_width_mm, page.page_height_mm, page.margin_top_mm, page.margin_bottom_mm, page.margin_left_mm, page.margin_right_mm);
	out += b;
	if (!page.title.empty()) { out += ",\n      \"title\": \""; json_escape(out, page.title.c_str()); out += "\""; }
	if (!page.creator.empty()) { out += ",\n      \"creator\": \""; json_escape(out, page.creator.c_str()); out += "\""; }
	if (!page.created.empty()) { out += ",\n      \"created\": \""; json_escape(out, page.created.c_str()); out += "\""; }
	if (!page.modified.empty()) { out += ",\n      \"modified\": \""; json_escape(out, page.modified.c_str()); out += "\""; }
	if (!page.bookmarks.empty()) { out += ",\n      \"bookmarks\": ["; for (size_t i = 0; i < page.bookmarks.size(); i++) { if (i > 0) out += ","; out += "\""; json_escape(out, page.bookmarks[i].c_str()); out += "\""; } out += "]"; }
	out += "\n    },\n    \"elements\": [\n";
	for (size_t i = 0; i < blocks.size(); i++) { if (i > 0) out += ",\n"; json_append_block(out, blocks[i]); }
	out += "\n    ]\n  }\n}\n";
	return strdup(out.c_str());
}

extern "C" void cdsl_doc_free_string(char* str) { if (str) free(str); }
