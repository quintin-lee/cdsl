/**
 * @file doc_parse.cpp
 * @brief LibreOffice SDK document parsing implementation.
 *
 * Uses LibreOfficeKit (LOK) to load Word documents and extract:
 *   - Page dimensions and margins (via FODT export)
 *   - Full text content (via TXT export)
 *   - Structured JSON output compatible with cdsl_context_load_json()
 *
 * All public functions use extern "C" for linkage from C code.
 *
 * The getCommandValues() API does not expose page layout properties in
 * headless mode, so we export to flat ODT XML (FODT) and parse the
 * page-layout-properties attributes directly.
 */

#include "cdsl/doc.h"
#include "xml_parser.h"

#include <LibreOfficeKit/LibreOfficeKitInit.h>
#include <LibreOfficeKit/LibreOfficeKit.hxx>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>

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

/**
 * @brief Create a unique temp file path template.
 *
 * Writes a path like /tmp/cdsl_XXXXXX.ext into @p buf (must be at least
 * 64 bytes). Returns 0 on success, -1 on failure.
 */
static int
temp_path(char* buf, size_t sz, const char* ext)
{
	const char* tmpl = "/tmp/cdsl_doc_XXXXXX";
	size_t n = strlen(tmpl) + strlen(ext) + 1;
	if (n >= sz) {
		return -1;
	}
	memcpy(buf, tmpl, strlen(tmpl) + 1);

	int fd = mkstemp(buf);
	if (fd < 0) {
		return -1;
	}
	close(fd);

	/* Replace trailing XXXXXX with unique chars then append extension */
	/* mkstemp already fills in the template, so just append ext */
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

/* Decode a single XML entity (&amp; &lt; &gt; &quot; &apos;) into buf[pos] */
static size_t decode_xml_entity(const char* s, size_t slen, char* buf, size_t sz, size_t pos) {
    if (slen >= 5 && memcmp(s, "&amp;", 5) == 0) { if (pos < sz) buf[pos] = '&'; return pos + 1; }
    if (slen >= 4 && memcmp(s, "&lt;", 4) == 0)  { if (pos < sz) buf[pos] = '<'; return pos + 1; }
    if (slen >= 4 && memcmp(s, "&gt;", 4) == 0)  { if (pos < sz) buf[pos] = '>'; return pos + 1; }
    if (slen >= 6 && memcmp(s, "&quot;", 6) == 0){ if (pos < sz) buf[pos] = '"'; return pos + 1; }
    if (slen >= 6 && memcmp(s, "&apos;", 6) == 0){ if (pos < sz) buf[pos] = '\''; return pos + 1; }
    return pos;
}

/* Decode XML entities in-place, returns new length */
static size_t decode_xml_inplace(char* s, size_t len) {
    size_t wp = 0;
    for (size_t rp = 0; rp < len; rp++) {
        if (s[rp] == '&') {
            size_t old = rp;
            while (rp < len && s[rp] != ';') rp++;
            if (rp < len) rp++; // skip ';'
            size_t elen = rp - old;
            size_t new_pos = decode_xml_entity(s + old, (elen < 10 ? elen : 10), s, len, wp);
            wp = new_pos;
        } else {
            s[wp++] = s[rp];
        }
    }
    s[wp] = '\0';
    return wp;
}

/* ------------------------------------------------------------------ */
/*  FODT hierarchical structure parser                                 */
/* ------------------------------------------------------------------ */

struct TextProps {
    std::string font_name;
    double      font_size_pt = 0;
    bool        bold = false;
    bool        italic = false;
    bool        underline = false;
    bool        strikethrough = false;
    std::string color;
    std::string background_color;
    bool        superscript = false;
    bool        subscript = false;
    double      letter_spacing_pt = 0;
};

struct ParaProps {
    std::string alignment;
    double    margin_top_mm = 0;
    double    margin_bottom_mm = 0;
    double    margin_left_mm = 0;
    double    margin_right_mm = 0;
    double    indent_first_line_mm = 0;
    double    line_spacing = 0;
    std::string background_color;
    std::string border_top;
    std::string border_bottom;
    std::string border_left;
    std::string border_right;
    int         outline_level = 0;
    std::string language;
};

struct Style {
    std::string name;
    std::string family;
    std::string parent;
    TextProps   text;
    ParaProps   para;
};

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
    bool   has_page_number_start = false;
    int    page_number_start = 0;
    std::string title;
    std::string creator;
    std::string created;
    std::string modified;
    std::string header_text;
    std::string footer_text;
    std::vector<std::string> bookmarks;
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

struct Paragraph {
    std::string   style_name;
    ParaProps     props;
    std::vector<TextBlock> blocks;
    double bbox_x_mm = 0;
    double bbox_y_mm = 0;
    double bbox_w_mm = 0;
    double bbox_h_mm = 0;
    int    page_index  = 0;
    int    page_number = 0;
};

#define MAX_CELLS_PER_ROW 256

struct TableCell {
    std::string text;
    int          colspan = 1;
    int          rowspan = 1;
};

struct TableRow {
    TableCell cells[MAX_CELLS_PER_ROW];
    int       num_cells = 0;
};

struct TableInfo {
    std::string style_name;
    TableRow    rows[256];
    int         num_rows = 0;
};

struct ListItem {
    std::string text;
    int         level = 1;
};

struct ListInfo {
    std::string style_name;
    ListItem    items[256];
    int         num_items = 0;
};

struct ImageInfo {
    std::string alt_text;
    double width_mm = 0;
    double height_mm = 0;
    double x_mm = 0;
    double y_mm = 0;
    int    page_num = 0;
};

struct FootnoteInfo {
    std::string citation;
    std::string body;
};

/* Bounded style database.
 *
 * ODF styles can form an inheritance chain via style:parent-style-name.
 * Styles are parsed in document order (which respects the ODF spec where
 * parent styles are declared before children). After all style elements
 * are parsed, resolve_style() recursively walks the parent chain and
 * merge_style() fills in missing property values from parent to child.
 *
 * The merge strategy is "child wins": a property is only copied from
 * parent when the child's corresponding field is zero/empty/false.
 */
#define MAX_STYLES 512
static Style g_style_db[MAX_STYLES];
static int   g_style_count = 0;

static void init_style_db() { g_style_count = 0; }
static Style* find_style(const char* name) {
    for (int i = 0; i < g_style_count; i++)
        if (g_style_db[i].name == name) return &g_style_db[i];
    return nullptr;
}
static Style* add_style(const char* name) {
    Style* existing = find_style(name);
    if (existing) return existing;
    if (g_style_count >= MAX_STYLES) return nullptr;
    Style* s = &g_style_db[g_style_count++];
    s->name = name;
    return s;
}

/* Fill missing child properties from parent; child values always win. */
static void merge_style(const Style& parent, Style& child) {
    if (child.para.alignment.empty() && !parent.para.alignment.empty())
        child.para.alignment = parent.para.alignment;
    if (child.para.margin_top_mm == 0 && parent.para.margin_top_mm != 0)
        child.para.margin_top_mm = parent.para.margin_top_mm;
    if (child.para.margin_bottom_mm == 0 && parent.para.margin_bottom_mm != 0)
        child.para.margin_bottom_mm = parent.para.margin_bottom_mm;
    if (child.para.margin_left_mm == 0 && parent.para.margin_left_mm != 0)
        child.para.margin_left_mm = parent.para.margin_left_mm;
    if (child.para.margin_right_mm == 0 && parent.para.margin_right_mm != 0)
        child.para.margin_right_mm = parent.para.margin_right_mm;
    if (child.para.indent_first_line_mm == 0 && parent.para.indent_first_line_mm != 0)
        child.para.indent_first_line_mm = parent.para.indent_first_line_mm;
    if (child.para.line_spacing == 0 && parent.para.line_spacing != 0)
        child.para.line_spacing = parent.para.line_spacing;
    if (child.text.font_name.empty() && !parent.text.font_name.empty())
        child.text.font_name = parent.text.font_name;
    if (child.text.font_size_pt == 0 && parent.text.font_size_pt != 0)
        child.text.font_size_pt = parent.text.font_size_pt;
    if (!child.text.bold && parent.text.bold)
        child.text.bold = parent.text.bold;
    if (!child.text.italic && parent.text.italic)
        child.text.italic = parent.text.italic;
    if (!child.text.underline && parent.text.underline)
        child.text.underline = parent.text.underline;
    if (!child.text.strikethrough && parent.text.strikethrough)
        child.text.strikethrough = parent.text.strikethrough;
    if (child.text.color.empty() && !parent.text.color.empty())
        child.text.color = parent.text.color;
    if (child.text.background_color.empty() && !parent.text.background_color.empty())
        child.text.background_color = parent.text.background_color;
    if (!child.text.superscript && parent.text.superscript)
        child.text.superscript = parent.text.superscript;
    if (!child.text.subscript && parent.text.subscript)
        child.text.subscript = parent.text.subscript;
    if (child.text.letter_spacing_pt == 0 && parent.text.letter_spacing_pt != 0)
        child.text.letter_spacing_pt = parent.text.letter_spacing_pt;
    if (child.para.background_color.empty() && !parent.para.background_color.empty())
        child.para.background_color = parent.para.background_color;
    if (child.para.border_top.empty() && !parent.para.border_top.empty())
        child.para.border_top = parent.para.border_top;
    if (child.para.border_bottom.empty() && !parent.para.border_bottom.empty())
        child.para.border_bottom = parent.para.border_bottom;
    if (child.para.border_left.empty() && !parent.para.border_left.empty())
        child.para.border_left = parent.para.border_left;
    if (child.para.border_right.empty() && !parent.para.border_right.empty())
        child.para.border_right = parent.para.border_right;
    if (child.para.outline_level == 0 && parent.para.outline_level != 0)
        child.para.outline_level = parent.para.outline_level;
    if (child.para.language.empty() && !parent.para.language.empty())
        child.para.language = parent.para.language;
}

/* Walk the style inheritance chain recursively and merge parent properties. */
static Style resolve_style(const char* name) {
    Style* s = find_style(name);
    if (!s) return Style{};
    Style result = *s;
    if (!result.parent.empty()) {
        Style parent = resolve_style(result.parent.c_str());
        merge_style(parent, result);
    }
    return result;
}

/* Parse text properties from a style:text-properties element */
static void parse_text_props_elem(XmlParser& xp, TextProps& props) {
    const char* v;
    if ((v = xp.attr("fo:font-family"))) props.font_name = v;
    if ((v = xp.attr("style:font-name"))) props.font_name = v;
    if ((v = xp.attr("fo:font-size"))) {
        char* e = nullptr;
        double sz = strtod(v, &e);
        if (e != v) props.font_size_pt = sz;
    }
    if ((v = xp.attr("fo:font-weight"))) props.bold = (strcmp(v, "bold") == 0);
    if ((v = xp.attr("fo:font-style")))  props.italic = (strcmp(v, "italic") == 0);
    if ((v = xp.attr("style:text-underline-style")))
        props.underline = (strcmp(v, "solid") == 0);
    if ((v = xp.attr("style:text-line-through-style")))
        props.strikethrough = (strcmp(v, "solid") == 0);
    if ((v = xp.attr("fo:color"))) props.color = v;
    if ((v = xp.attr("fo:background-color"))) props.background_color = v;
    if ((v = xp.attr("style:text-position"))) {
        if (strstr(v, "super")) props.superscript = true;
        else if (strstr(v, "sub"))  props.subscript = true;
    }
    if ((v = xp.attr("fo:letter-spacing"))) {
        char* e = nullptr;
        double ls = strtod(v, &e);
        if (e != v) props.letter_spacing_pt = ls;
    }
    /* style:use-window-font-color="true" = default (black) - leave color empty */
}

/* Parse paragraph properties from a style:paragraph-properties element */
static void parse_para_props_elem(XmlParser& xp, ParaProps& props) {
    const char* v;
    if ((v = xp.attr("fo:text-align"))) props.alignment = v;
    if ((v = xp.attr("fo:margin-top")))    props.margin_top_mm = parse_length_mm(v);
    if ((v = xp.attr("fo:margin-bottom"))) props.margin_bottom_mm = parse_length_mm(v);
    if ((v = xp.attr("fo:margin-left")))   props.margin_left_mm = parse_length_mm(v);
    if ((v = xp.attr("fo:margin-right")))  props.margin_right_mm = parse_length_mm(v);
    if ((v = xp.attr("text:indent")))      props.indent_first_line_mm = parse_length_mm(v);
    if ((v = xp.attr("fo:line-height"))) {
        char* e = nullptr;
        double lh = strtod(v, &e);
        if (e != v && strstr(e, "%")) props.line_spacing = lh / 100.0;
        else if (e != v) props.line_spacing = lh;
    }
    if ((v = xp.attr("fo:background-color")))  props.background_color = v;
    if ((v = xp.attr("fo:border-top")))        props.border_top = v;
    if ((v = xp.attr("fo:border-bottom")))     props.border_bottom = v;
    if ((v = xp.attr("fo:border-left")))       props.border_left = v;
    if ((v = xp.attr("fo:border-right")))      props.border_right = v;
    if ((v = xp.attr("fo:border"))) {
        props.border_top = v;
        props.border_bottom = v;
        props.border_left = v;
        props.border_right = v;
    }
    if ((v = xp.attr("text:outline-level")))  props.outline_level = atoi(v);
    if ((v = xp.attr("fo:language")))         props.language = v;
    else if ((v = xp.attr("style:language")))  props.language = v;
}

/* Parse a <style:style> element */
static void parse_style_elem(XmlParser& xp) {
    const char* name_ptr = xp.attr("style:name");
    if (!name_ptr) return;
    std::string name(name_ptr); // COPY before next attr() overwrites static buf

    const char* family_ptr = xp.attr("style:family");
    std::string family = family_ptr ? family_ptr : std::string();

    const char* parent_ptr = xp.attr("style:parent-style-name");
    std::string parent = parent_ptr ? parent_ptr : std::string();

    Style* s = add_style(name.c_str());
    if (!s) return;
    if (!family.empty()) s->family = family;
    if (!parent.empty()) s->parent = parent;

    // Read children: <style:paragraph-properties> and <style:text-properties>
    while (xp.next() == XmlParser::START_ELEMENT) {
        if (xp.match("style:paragraph-properties"))
            parse_para_props_elem(xp, s->para);
        else if (xp.match("style:text-properties"))
            parse_text_props_elem(xp, s->text);
        // Skip children of this element
        int depth = 1;
        while (depth > 0 && xp.next() != XmlParser::END) {
            if (xp.type() == XmlParser::START_ELEMENT) depth++;
            else if (xp.type() == XmlParser::END_ELEMENT) depth--;
        }
    }
    // consume </style:style>
    if (xp.type() == XmlParser::START_ELEMENT) {
        int d = 1;
        while (d > 0 && xp.next() != XmlParser::END) {
            if (xp.type() == XmlParser::START_ELEMENT) d++;
            else if (xp.type() == XmlParser::END_ELEMENT) d--;
        }
    }
}

/* Parse page layout properties from a <style:page-layout> element */
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
        int depth = 1;
        while (depth > 0 && xp.next() != XmlParser::END) {
            if (xp.type() == XmlParser::START_ELEMENT) depth++;
            else if (xp.type() == XmlParser::END_ELEMENT) depth--;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  JSON output builders                                              */
/* ------------------------------------------------------------------ */

static void json_escape(std::string& out, const char* str)
{
	if (!str) return;
	for (size_t i = 0; str[i]; i++) {
		unsigned char c = (unsigned char)str[i];
		switch (c) {
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		case '\t': out += "\\t";  break;
		default:
			if (c >= 0x20) out += (char)c;
			break;
		}
	}
}

static void json_append_text_block(std::string& out, const char* text,
                                   const TextProps& props, const TextBlock* block = nullptr)
{
    out += "{\n";
    if (text) {
        out += "      \"text\": \"";
        json_escape(out, text);
        out += "\"\n";
    }
    if (!props.font_name.empty()) {
        out += "      ,\"font_name\": \"";
        json_escape(out, props.font_name.c_str());
        out += "\"\n";
    }
    if (props.font_size_pt > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "      ,\"font_size_pt\": %.1f\n", props.font_size_pt);
        out += buf;
    }
    if (props.bold)          out += "      ,\"bold\": true\n";
    if (props.italic)        out += "      ,\"italic\": true\n";
    if (props.underline)     out += "      ,\"underline\": true\n";
    if (props.strikethrough) out += "      ,\"strikethrough\": true\n";
    if (!props.color.empty()) {
        out += "      ,\"color\": \"";
        json_escape(out, props.color.c_str());
        out += "\"\n";
    }
    if (!props.background_color.empty()) {
        out += "      ,\"background_color\": \"";
        json_escape(out, props.background_color.c_str());
        out += "\"\n";
    }
    if (props.superscript)
        out += "      ,\"superscript\": true\n";
    if (props.subscript)
        out += "      ,\"subscript\": true\n";
    if (props.letter_spacing_pt > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "      ,\"letter_spacing_pt\": %.2f\n", props.letter_spacing_pt);
        out += buf;
    }
    if (block && !block->hyperlink_url.empty()) {
        out += "      ,\"hyperlink_url\": \"";
        json_escape(out, block->hyperlink_url.c_str());
        out += "\"\n";
    }
    if (block) {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "      ,\"bbox_mm\": [%.1f, %.1f, %.1f, %.1f]\n",
            block->bbox_x_mm, block->bbox_y_mm,
            block->bbox_w_mm, block->bbox_h_mm);
        out += buf;
    }
    out += "}";
}

static void json_append_paragraph(std::string& out, const char* style_name,
                                  const ParaProps& props, const TextBlock* blocks,
                                  int num_blocks, const Paragraph* para)
{
    out += "        {\n";
    if (style_name) {
        out += "          \"style\": \"";
        json_escape(out, style_name);
        out += "\"";
        if (!props.alignment.empty() || props.margin_top_mm > 0 || props.margin_bottom_mm > 0 ||
            props.margin_left_mm > 0 || props.indent_first_line_mm > 0 || props.line_spacing > 0)
            out += ",";
        out += "\n";
    }
    if (!props.alignment.empty()) {
        out += "          ,\"alignment\": \"";
        json_escape(out, props.alignment.c_str());
        out += "\"\n";
    }
    char buf[128];
    if (props.margin_top_mm > 0) {
        snprintf(buf, sizeof(buf), "          ,\"spacing_before_mm\": %.1f\n", props.margin_top_mm);
        out += buf;
    }
    if (props.margin_bottom_mm > 0) {
        snprintf(buf, sizeof(buf), "          ,\"spacing_after_mm\": %.1f\n", props.margin_bottom_mm);
        out += buf;
    }
    if (props.line_spacing > 0) {
        snprintf(buf, sizeof(buf), "          ,\"line_spacing\": %.2f\n", props.line_spacing);
        out += buf;
    }
    if (props.indent_first_line_mm > 0) {
        snprintf(buf, sizeof(buf), "          ,\"indent_first_line_mm\": %.1f\n", props.indent_first_line_mm);
        out += buf;
    }
    if (!props.background_color.empty()) {
        out += "          ,\"background_color\": \"";
        json_escape(out, props.background_color.c_str());
        out += "\"\n";
    }
    if (!props.border_top.empty())
        out += "          ,\"border_top\": \"" + props.border_top + "\"\n";
    if (!props.border_bottom.empty())
        out += "          ,\"border_bottom\": \"" + props.border_bottom + "\"\n";
    if (!props.border_left.empty())
        out += "          ,\"border_left\": \"" + props.border_left + "\"\n";
    if (!props.border_right.empty())
        out += "          ,\"border_right\": \"" + props.border_right + "\"\n";
    if (props.outline_level > 0) {
        snprintf(buf, sizeof(buf), "          ,\"outline_level\": %d\n", props.outline_level);
        out += buf;
    }
    if (!props.language.empty()) {
        out += "          ,\"language\": \"" + props.language + "\"\n";
    }

    if (para) {
        snprintf(buf, sizeof(buf), "          ,\"page_index\": %d\n", para->page_index);
        out += buf;
        if (para->page_number > 0) {
            snprintf(buf, sizeof(buf), "          ,\"page_number\": %d\n", para->page_number);
            out += buf;
        } else {
            out += "          ,\"page_number\": null\n";
        }
        snprintf(buf, sizeof(buf),
            "          ,\"bbox_mm\": [%.1f, %.1f, %.1f, %.1f]\n",
            para->bbox_x_mm, para->bbox_y_mm,
            para->bbox_w_mm, para->bbox_h_mm);
        out += buf;
    }

    out += "          ,\"text_blocks\": [\n";
    for (int i = 0; i < num_blocks; i++) {
        if (i > 0) out += ",\n";
        json_append_text_block(out, blocks[i].text.c_str(), blocks[i].props, &blocks[i]);
    }
    out += "\n          ]\n";
    out += "        }";
}

static void json_append_table(std::string& out, const TableInfo& table)
{
    out += "    {\n";
    out += "      \"style\": \"";
    json_escape(out, table.style_name.empty() ? "Default" : table.style_name.c_str());
    out += "\",\n";
    out += "      \"rows\": [\n";
    for (int r = 0; r < table.num_rows; r++) {
        if (r > 0) out += ",\n";
        out += "        {\"cells\":[";
        for (int c = 0; c < table.rows[r].num_cells; c++) {
            if (c > 0) out += ",";
            out += "{\"text\":\"";
            json_escape(out, table.rows[r].cells[c].text.c_str());
            out += "\"";
            if (table.rows[r].cells[c].colspan > 1) {
                char b[32]; snprintf(b, sizeof(b), ",\"colspan\":%d", table.rows[r].cells[c].colspan);
                out += b;
            }
            if (table.rows[r].cells[c].rowspan > 1) {
                char b[32]; snprintf(b, sizeof(b), ",\"rowspan\":%d", table.rows[r].cells[c].rowspan);
                out += b;
            }
            out += "}";
        }
        out += "]}";
    }
    out += "\n      ]\n    }";
}

static void json_append_list(std::string& out, const ListInfo& list)
{
    out += "    {\n";
    out += "      \"style\": \"";
    json_escape(out, list.style_name.empty() ? "Default" : list.style_name.c_str());
    out += "\",\n";
    out += "      \"items\": [\n";
    for (int i = 0; i < list.num_items; i++) {
        if (i > 0) out += ",\n";
        out += "        {\"text\":\"";
        json_escape(out, list.items[i].text.c_str());
        out += "\",\"level\":";
        char b[32]; snprintf(b, sizeof(b), "%d}", list.items[i].level);
        out += b;
    }
    out += "\n      ]\n    }";
}

static void json_append_image(std::string& out, const ImageInfo& img)
{
    out += "    {\n";
    char b[128];
    snprintf(b, sizeof(b), "      \"width_mm\": %.1f,\n      \"height_mm\": %.1f,\n      \"x_mm\": %.1f,\n      \"y_mm\": %.1f",
             img.width_mm, img.height_mm, img.x_mm, img.y_mm);
    out += b;
    if (!img.alt_text.empty()) {
        out += ",\n      \"alt_text\": \"";
        json_escape(out, img.alt_text.c_str());
        out += "\"";
    }
    out += "\n    }";
}

static void json_append_footnote(std::string& out, const FootnoteInfo& fn)
{
    out += "    {\n";
    out += "      \"citation\": \"";
    json_escape(out, fn.citation.c_str());
    out += "\",\n";
    out += "      \"body\": \"";
    json_escape(out, fn.body.c_str());
    out += "\"\n    }";
}

static bool parse_fodt_table(XmlParser& xp, TableInfo& table) {
    table.num_rows = 0;
    int depth = 1;
    while (depth > 0 && xp.next() != XmlParser::END) {
        if (xp.type() == XmlParser::END_ELEMENT) { depth--; continue; }
        if (xp.type() != XmlParser::START_ELEMENT) continue;
        if (xp.match("table:table-row")) {
            if (table.num_rows >= 256) { /* skip */ int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} continue; }
            TableRow& row = table.rows[table.num_rows++];
            int rd = 1;
            while (rd > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::END_ELEMENT) { rd--; continue; }
                if (xp.type() != XmlParser::START_ELEMENT) continue;
                if (xp.match("table:table-cell") || xp.match("table:covered-table-cell")) {
                    if (row.num_cells >= MAX_CELLS_PER_ROW) { int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} continue; }
                    TableCell& cell = row.cells[row.num_cells++];
                    const char* v;
                    if ((v = xp.attr("table:number-columns-spanned"))) cell.colspan = atoi(v);
                    if ((v = xp.attr("table:number-rows-spanned")))    cell.rowspan = atoi(v);
                    int cd = 1; std::string ct;
                    while (cd > 0 && xp.next() != XmlParser::END) {
                        if (xp.type() == XmlParser::END_ELEMENT) { cd--; continue; }
                        if (xp.type() == XmlParser::START_ELEMENT) {
                            if (xp.match("text:p")) { int pd=1; while(pd>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::END_ELEMENT){pd--;continue;}if(xp.type()==XmlParser::START_ELEMENT){pd++;continue;}if(xp.type()==XmlParser::TEXT&&xp.textContent())ct.append(xp.textContent(),xp.textLength());} }
                            else { cd++; }
                            continue;
                        }
                        if (xp.type() == XmlParser::TEXT && xp.textContent()) ct.append(xp.textContent(), xp.textLength());
                    }
                    cell.text = ct;
                } else { int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} }
            }
        } else { int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} }
    }
    return true;
}

static bool parse_fodt_list(XmlParser& xp, ListInfo& list) {
    list.num_items = 0;
    int depth = 1;
    while (depth > 0 && xp.next() != XmlParser::END) {
        if (xp.type() == XmlParser::END_ELEMENT) { depth--; continue; }
        if (xp.type() != XmlParser::START_ELEMENT) continue;
        if (xp.match("text:list-item")) {
            if (list.num_items >= 256) { int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} continue; }
            ListItem& item = list.items[list.num_items++];
            int id=1; std::string it;
            while (id > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::END_ELEMENT) { id--; continue; }
                if (xp.type() == XmlParser::START_ELEMENT) {
                    if (xp.match("text:p") || xp.match("text:h")) { int pd=1; while(pd>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::END_ELEMENT){pd--;continue;}if(xp.type()==XmlParser::START_ELEMENT){if(xp.match("text:span")){int sd=1;while(sd>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::END_ELEMENT){sd--;continue;}if(xp.type()==XmlParser::START_ELEMENT){sd++;continue;}if(xp.type()==XmlParser::TEXT&&xp.textContent())it.append(xp.textContent(),xp.textLength());}}else{pd++;}continue;}if(xp.type()==XmlParser::TEXT&&xp.textContent())it.append(xp.textContent(),xp.textLength());} }
                    else { id++; }
                    continue;
                }
                if (xp.type() == XmlParser::TEXT && xp.textContent()) it.append(xp.textContent(), xp.textLength());
            }
            item.text = it;
        } else { int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} }
    }
    return true;
}

static bool parse_fodt_image(XmlParser& xp, ImageInfo& image) {
    const char* v;
    if ((v = xp.attr("svg:width")))  image.width_mm  = parse_length_mm(v);
    if ((v = xp.attr("svg:height"))) image.height_mm = parse_length_mm(v);
    if ((v = xp.attr("svg:x")))      image.x_mm      = parse_length_mm(v);
    if ((v = xp.attr("svg:y")))      image.y_mm      = parse_length_mm(v);
    int depth = 1;
    while (depth > 0 && xp.next() != XmlParser::END) {
        if (xp.type() == XmlParser::END_ELEMENT) { depth--; continue; }
        if (xp.type() != XmlParser::START_ELEMENT) continue;
        if (xp.match("draw:image")) {
            int dd = 1;
            while (dd > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) {
                    if (xp.match("svg:title") || xp.match("svg:desc")) {
                        int td = 1;
                        while (td > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::END_ELEMENT) { td--; continue; }
                            if (xp.type() == XmlParser::START_ELEMENT) { td++; continue; }
                            if (xp.type() == XmlParser::TEXT && xp.textContent()) image.alt_text.append(xp.textContent(), xp.textLength());
                        }
                    } else { dd++; }
                } else if (xp.type() == XmlParser::END_ELEMENT) dd--;
            }
        }
        depth++;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  FODT body parser: walk <office:text> → <text:p> → <text:span>     */
/* ------------------------------------------------------------------ */

static bool parse_fodt_body(XmlParser& xp, PageInfo& page,
                             Paragraph* out_paras, int& out_count,
                             TableInfo* out_tables = nullptr, int* out_table_count = nullptr,
                             ListInfo* out_lists = nullptr, int* out_list_count = nullptr,
                             ImageInfo* out_images = nullptr, int* out_image_count = nullptr,
                             FootnoteInfo* out_footnotes = nullptr, int* out_fn_count = nullptr)
{
    out_count = 0;
    int depth = 1;
    while (depth > 0 && xp.next() != XmlParser::END) {
        if (xp.type() == XmlParser::END_ELEMENT) { depth--; continue; }
        if (xp.type() != XmlParser::START_ELEMENT) continue;

        if (xp.match("text:p") || xp.match("text:h")) {
            if (out_count >= 256) {
                // skip remaining paragraphs
                int d = 1;
                while (d > 0 && xp.next() != XmlParser::END) {
                    if (xp.type() == XmlParser::START_ELEMENT) d++;
                    else if (xp.type() == XmlParser::END_ELEMENT) d--;
                }
                continue;
            }

            Paragraph& para = out_paras[out_count];
            const char* sn = xp.attr("text:style-name");
            if (sn) {
                para.style_name = sn;
                Style st = resolve_style(sn);
                para.props = st.para;
            }

            // Parse paragraph children for text
            int para_depth = 1;
            std::string current_hyperlink;
            while (para_depth > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::END_ELEMENT) { para_depth--; continue; }
                if (xp.type() == XmlParser::START_ELEMENT) {
                    if (xp.match("text:a")) {
                        const char* href = xp.attr("xlink:href");
                        if (href) current_hyperlink = href;
                        para_depth++;
                        continue;
                    }
                    if (xp.match("text:bookmark") || xp.match("text:bookmark-start")) {
                        const char* bn = xp.attr("text:name");
                        if (bn) page.bookmarks.push_back(bn);
                    }
                    if (xp.match("text:span")) {
                        // text span with potential style override
                        const char* span_style = xp.attr("text:style-name");
                        TextProps span_props;
                        if (span_style) {
                            Style ss = resolve_style(span_style);
                            span_props = ss.text;
                        }

                        // Collect all TEXT tokens inside the span
                        int span_depth = 1;
                        std::string span_text;
                        while (span_depth > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::END_ELEMENT) { span_depth--; continue; }
                            if (xp.type() == XmlParser::START_ELEMENT) { span_depth++; continue; }
                            if (xp.type() == XmlParser::TEXT && xp.textContent()) {
                                span_text.append(xp.textContent(), xp.textLength());
                            }
                        }

                        if (!span_text.empty()) {
                            // decode XML entities
                            char* dec = strdup(span_text.c_str());
                            if (dec) {
                                decode_xml_inplace(dec, strlen(dec));
                            }
                            if (para.blocks.size() < 256) {
                                TextBlock tb;
                                if (dec) tb.text = dec;
                                else tb.text = span_text;
                                tb.props = span_props;
                                tb.hyperlink_url = current_hyperlink;
                                para.blocks.push_back(tb);
                            }
                            free(dec);
                        }
                    } else {
                        // Unknown element inside paragraph - skip
                        int d = 1;
                        while (d > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::START_ELEMENT) d++;
                            else if (xp.type() == XmlParser::END_ELEMENT) d--;
                        }
                    }
                    continue;
                }
                // TEXT token directly inside the paragraph
                if (xp.type() == XmlParser::TEXT && xp.textContent() && xp.textLength() > 0) {
                    // Copy and decode XML entities
                    size_t tlen = xp.textLength();
                    char* dec = (char*)malloc(tlen + 1);
                    if (dec) {
                        memcpy(dec, xp.textContent(), tlen);
                        dec[tlen] = '\0';
                        decode_xml_inplace(dec, tlen);
                    }
                    if (para.blocks.size() < 256) {
                        TextBlock tb;
                        if (dec) tb.text = dec;
                        tb.hyperlink_url = current_hyperlink;
                        para.blocks.push_back(tb);
                    }
                    free(dec);
                }
            }
            out_count++;
        } else if (xp.match("text:section")) {
            if (!parse_fodt_body(xp, page, out_paras, out_count, out_tables, out_table_count, out_lists, out_list_count, out_images, out_image_count, out_footnotes, out_fn_count))
                return false;
        } else if (xp.match("text:table") && out_tables && out_table_count) {
            if (*out_table_count < 256) { parse_fodt_table(xp, out_tables[(*out_table_count)++]); }
            else { int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} }
        } else if (xp.match("text:list") && out_lists && out_list_count) {
            if (*out_list_count < 256) { parse_fodt_list(xp, out_lists[(*out_list_count)++]); }
            else { int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} }
        } else if (xp.match("draw:frame") && out_images && out_image_count) {
            if (*out_image_count < 256) { parse_fodt_image(xp, out_images[(*out_image_count)++]); }
            else { int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} }
        } else if (xp.match("text:note") && out_footnotes && out_fn_count) {
            if (*out_fn_count < 256) {
                FootnoteInfo& fn = out_footnotes[*out_fn_count];
                int nd = 1;
                while (nd > 0 && xp.next() != XmlParser::END) {
                    if (xp.type() == XmlParser::END_ELEMENT) { nd--; continue; }
                    if (xp.type() != XmlParser::START_ELEMENT) continue;
                    if (xp.match("text:note-citation")) {
                        int cd = 1; std::string ct;
                        while (cd > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::END_ELEMENT) { cd--; continue; }
                            if (xp.type() == XmlParser::START_ELEMENT) { cd++; continue; }
                            if (xp.type() == XmlParser::TEXT && xp.textContent()) ct.append(xp.textContent(), xp.textLength());
                        }
                        fn.citation = ct;
                    } else if (xp.match("text:note-body")) {
                        int bd = 1; std::string bt;
                        while (bd > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::END_ELEMENT) { bd--; continue; }
                            if (xp.type() == XmlParser::START_ELEMENT) {
                                if (xp.match("text:p") || xp.match("text:h")) {
                                    int pd = 1;
                                    while (pd > 0 && xp.next() != XmlParser::END) {
                                        if (xp.type() == XmlParser::END_ELEMENT) { pd--; continue; }
                                        if (xp.type() == XmlParser::START_ELEMENT) { pd++; continue; }
                                        if (xp.type() == XmlParser::TEXT && xp.textContent()) bt.append(xp.textContent(), xp.textLength());
                                    }
                                } else { bd++; }
                                continue;
                            }
                            if (xp.type() == XmlParser::TEXT && xp.textContent()) bt.append(xp.textContent(), xp.textLength());
                        }
                        fn.body = bt;
                    } else { nd++; }
                }
                (*out_fn_count)++;
            } else { int d=1; while(d>0&&xp.next()!=XmlParser::END){if(xp.type()==XmlParser::START_ELEMENT)d++;else if(xp.type()==XmlParser::END_ELEMENT)d--;} }
        } else {
            // Other elements inside body - skip
            int d = 1;
            while (d > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) d++;
                else if (xp.type() == XmlParser::END_ELEMENT) d--;
            }
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  Main FODT parsing entry point                                      */
/* ------------------------------------------------------------------ */

static bool parse_fodt_document(const char* fodt, size_t fodt_len,
                                 PageInfo& page, Paragraph* out_paras, int& out_count,
                                 std::string& full_text,
                                 TableInfo* out_tables = nullptr, int* out_table_count = nullptr,
                                 ListInfo* out_lists = nullptr, int* out_list_count = nullptr,
                                 ImageInfo* out_images = nullptr, int* out_image_count = nullptr,
                                 FootnoteInfo* out_footnotes = nullptr, int* out_fn_count = nullptr)
{
    init_style_db();
    out_count = 0;
    full_text.clear();
    page = PageInfo{};

    XmlParser xp(fodt, fodt_len);

    // Root: <office:document>
    // We just iterate top-level children
    int guard = 0;
    while (xp.next() != XmlParser::END) {
        if (++guard > 10000) { fprintf(stderr, "INFINITE LOOP in parse_fodt_document main\n"); break; }
        if (xp.type() != XmlParser::START_ELEMENT) continue;

        if (xp.match("office:meta")) {
            // Extract document statistics
            int d = 1;
            while (d > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) {
                    if (xp.match("meta:document-statistic")) {
                        const char* v;
                        if ((v = xp.attr("meta:page-count")))  page.page_count = atoi(v);
                        if ((v = xp.attr("meta:paragraph-count"))) page.paragraph_count = atoi(v);
                        if ((v = xp.attr("meta:word-count")))  page.word_count = atoi(v);
                        if ((v = xp.attr("meta:character-count"))) page.character_count = atoi(v);
                    }
                    int dd = 1;
                    while (dd > 0 && xp.next() != XmlParser::END) {
                        if (xp.type() == XmlParser::START_ELEMENT) dd++;
                        else if (xp.type() == XmlParser::END_ELEMENT) dd--;
                    }
                } else if (xp.type() == XmlParser::END_ELEMENT) d--;
            }
        } else if (xp.match("office:styles") || xp.match("office:automatic-styles")) {
            int d = 1;
            while (d > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) {
                    if (xp.match("style:style"))
                        parse_style_elem(xp);
                    else if (xp.match("style:page-layout"))
                        parse_page_layout(xp, page);
                    else {
                        int dd = 1;
                        while (dd > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::START_ELEMENT) dd++;
                            else if (xp.type() == XmlParser::END_ELEMENT) dd--;
                        }
                    }
                } else if (xp.type() == XmlParser::END_ELEMENT) d--;
            }
            // Resolve parent style chain after parsing all style elements
            for (int i = 0; i < g_style_count; i++) {
                if (!g_style_db[i].parent.empty()) {
                    Style parent = resolve_style(g_style_db[i].parent.c_str());
                    merge_style(parent, g_style_db[i]);
                }
            }
        } else if (xp.match("office:master-styles")) {
            int d = 1;
            while (d > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::END_ELEMENT) { d--; continue; }
                if (xp.type() != XmlParser::START_ELEMENT) continue;
                if (xp.match("style:master-page")) {
                    int md = 1;
                    while (md > 0 && xp.next() != XmlParser::END) {
                        if (xp.type() == XmlParser::END_ELEMENT) { md--; continue; }
                        if (xp.type() != XmlParser::START_ELEMENT) continue;
                        if (xp.match("style:header") || xp.match("style:header-left")) {
                            int hd = 1; std::string ht;
                            while (hd > 0 && xp.next() != XmlParser::END) {
                                if (xp.type() == XmlParser::END_ELEMENT) { hd--; continue; }
                                if (xp.type() == XmlParser::START_ELEMENT) {
                                    if (xp.match("text:p") || xp.match("text:h")) {
                                        int pd = 1;
                                        while (pd > 0 && xp.next() != XmlParser::END) {
                                            if (xp.type() == XmlParser::END_ELEMENT) { pd--; continue; }
                                            if (xp.type() == XmlParser::START_ELEMENT) { pd++; continue; }
                                            if (xp.type() == XmlParser::TEXT && xp.textContent()) ht.append(xp.textContent(), xp.textLength());
                                        }
                                    } else { hd++; }
                                    continue;
                                }
                                if (xp.type() == XmlParser::TEXT && xp.textContent()) ht.append(xp.textContent(), xp.textLength());
                            }
                            page.header_text = ht;
                        } else if (xp.match("style:footer") || xp.match("style:footer-left")) {
                            int fd = 1; std::string ft;
                            while (fd > 0 && xp.next() != XmlParser::END) {
                                if (xp.type() == XmlParser::END_ELEMENT) { fd--; continue; }
                                if (xp.type() == XmlParser::START_ELEMENT) {
                                    if (xp.match("text:p") || xp.match("text:h")) {
                                        int pd = 1;
                                        while (pd > 0 && xp.next() != XmlParser::END) {
                                            if (xp.type() == XmlParser::END_ELEMENT) { pd--; continue; }
                                            if (xp.type() == XmlParser::START_ELEMENT) { pd++; continue; }
                                            if (xp.type() == XmlParser::TEXT && xp.textContent()) ft.append(xp.textContent(), xp.textLength());
                                        }
                                    } else { fd++; }
                                    continue;
                                }
                                if (xp.type() == XmlParser::TEXT && xp.textContent()) ft.append(xp.textContent(), xp.textLength());
                            }
                            page.footer_text = ft;
                        } else { md++; }
                    }
                } else { d++; }
            }
        } else if (xp.match("office:body")) {
            int d = 1;
            while (d > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) {
                    if (xp.match("office:text")) {
                        if (!parse_fodt_body(xp, page, out_paras, out_count, out_tables, out_table_count, out_lists, out_list_count, out_images, out_image_count, out_footnotes, out_fn_count))
                            return false;
                    } else {
                        int dd = 1;
                        while (dd > 0 && xp.next() != XmlParser::END) {
                            if (xp.type() == XmlParser::START_ELEMENT) dd++;
                            else if (xp.type() == XmlParser::END_ELEMENT) dd--;
                        }
                    }
                } else if (xp.type() == XmlParser::END_ELEMENT) d--;
            }
        }
    }

    {
        auto extract_tag = [&](const char* tag) -> std::string {
            std::string o = std::string("<") + tag + ">";
            std::string c = std::string("</") + tag + ">";
            const char* s = (const char*)memmem(fodt, fodt_len, o.c_str(), o.size());
            if (!s) return {};
            s += o.size();
            const char* e = (const char*)memmem(s, fodt_len-(s-fodt), c.c_str(), c.size());
            if (!e) return {};
            return std::string(s, e-s);
        };
        page.title    = extract_tag("dc:title");
        page.creator  = extract_tag("dc:creator");
        page.created  = extract_tag("meta:creation-date");
        page.modified = extract_tag("dc:date");
    }

    return true;
}

/* ------------------------------------------------------------------ */
/*  Paragraph position extraction via LOK rendering                    */
/* ------------------------------------------------------------------ */

/*
 * Position estimation uses LibreOfficeKit's UNO command dispatch and
 * cursor visibility callback to enumerate paragraph bounding boxes.
 *
 * Workflow:
 *   1. Register a LOK_CALLBACK_INVALIDATE_VISIBLE_CURSOR callback.
 *   2. Send ".uno:GoDown" UNO commands to step through paragraphs.
 *   3. paintTile() triggers both rendering layout and callback dispatch.
 *   4. The callback reports cursor position in TWIPs (1/20 of a point,
 *      1/1440 of an inch). Convert to mm via twip_to_mm = 25.4 / 1440.
 *
 * Bounding box derivation:
 *   - bbox_x = cursor X (TWIPs → mm)
 *   - bbox_y = cursor Y (TWIPs → mm)
 *   - bbox_w = content_width - indent (mm)
 *   - bbox_h = estimated lines × line_height (mm)
 *
 * Lines are estimated from character count divided by characters-per-line.
 * Characters-per-line comes from content_width / font_advance, where
 * font_advance approximates average character width as 0.5 × line height.
 *
 * Text blocks within the same paragraph get proportional horizontal slices.
 *
 * Thread safety: called inside g_mutex critical section (caller holds lock).
 */

struct CursorCapture {
    double x_twip = 0, y_twip = 0, w_twip = 0, h_twip = 0;
    bool   updated = false;
};

static void cursor_callback(int type, const char* payload, void* data) {
    auto* cap = static_cast<CursorCapture*>(data);
    if (type == LOK_CALLBACK_INVALIDATE_VISIBLE_CURSOR && payload && *payload) {
        double x = 0, y = 0, w = 0, h = 0;
        int n = sscanf(payload, "%lf, %lf, %lf, %lf", &x, &y, &w, &h);
        if (n >= 2) {
            cap->x_twip = x; cap->y_twip = y; cap->w_twip = w; cap->h_twip = h;
            cap->updated = true;
        }
    }
}

/* Navigate paragraphs via LOK, capturing cursor rectangles (TWIPs).
 * paintTile() triggers layout AND pumps pending UNO commands (GoDown). */
static bool pump_until_updated(lok::Document* doc, CursorCapture& cap, int max_attempts = 10) {
    unsigned char buf[64];
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        if (cap.updated) return true;
        doc->paintTile(buf, 1, 1, 0, 0, 100, 100);
        usleep(1000);
    }
    return cap.updated;
}

static void
get_paragraph_positions(lok::Document* doc, Paragraph* paras,
                                    int count, const PageInfo& page)
{
    if (count <= 0 || !doc) return;

    CursorCapture cap;
    doc->registerCallback(cursor_callback, &cap);

    const double content_w_mm = page.page_width_mm
        - page.margin_left_mm - page.margin_right_mm;
    if (content_w_mm <= 0) {
        doc->registerCallback(nullptr, nullptr);
        return;
    }

    const     double twip_to_mm = 25.4 / 1440.0;

    int current_page = 1;
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            cap.updated = false;
            doc->postUnoCommand(".uno:GoDown", nullptr, false);
        }

        if (pump_until_updated(doc, cap)) {
            paras[i].bbox_x_mm = cap.x_twip * twip_to_mm;
            paras[i].bbox_y_mm = cap.y_twip * twip_to_mm;
            double line_h_mm = cap.h_twip * twip_to_mm;

            if (i > 0 && paras[i].bbox_y_mm < paras[i-1].bbox_y_mm)
                current_page++;
            paras[i].page_index  = current_page;
            paras[i].page_number = page.has_page_number_start ? (current_page + page.page_number_start - 1) : 0;

            double indent = paras[i].bbox_x_mm - page.margin_left_mm;
            if (indent < 0) indent = 0;
            paras[i].bbox_w_mm = content_w_mm - indent;

            int total_chars = 0;
            for (const auto& block : paras[i].blocks)
                total_chars += (int)block.text.length();

            if (total_chars > 0 && line_h_mm > 0) {
                double font_advance_mm = line_h_mm * 0.5;
                if (font_advance_mm <= 0) font_advance_mm = 2.0;
                int cpl = (int)(content_w_mm / font_advance_mm);
                if (cpl < 1) cpl = 1;
                int lines = (total_chars + cpl - 1) / cpl;
                if (lines < 1) lines = 1;
                paras[i].bbox_h_mm = lines * line_h_mm;
            } else {
                paras[i].bbox_h_mm = line_h_mm;
            }

            if (paras[i].blocks.size() == 1) {
                paras[i].blocks[0].bbox_x_mm = paras[i].bbox_x_mm;
                paras[i].blocks[0].bbox_y_mm = paras[i].bbox_y_mm;
                paras[i].blocks[0].bbox_w_mm = paras[i].bbox_w_mm;
                paras[i].blocks[0].bbox_h_mm = paras[i].bbox_h_mm;
                Style st = resolve_style(paras[i].style_name.c_str());
                if (paras[i].blocks[0].props.font_name.empty()) paras[i].blocks[0].props.font_name = st.text.font_name;
                if (paras[i].blocks[0].props.font_size_pt == 0) paras[i].blocks[0].props.font_size_pt = st.text.font_size_pt;
                if (!paras[i].blocks[0].props.bold) paras[i].blocks[0].props.bold = st.text.bold;
                if (!paras[i].blocks[0].props.italic) paras[i].blocks[0].props.italic = st.text.italic;
                if (!paras[i].blocks[0].props.underline) paras[i].blocks[0].props.underline = st.text.underline;
                if (!paras[i].blocks[0].props.strikethrough) paras[i].blocks[0].props.strikethrough = st.text.strikethrough;
                if (paras[i].blocks[0].props.color.empty()) paras[i].blocks[0].props.color = st.text.color;
            } else if (paras[i].blocks.size() > 1) {
                double current_x = paras[i].bbox_x_mm;
                for (size_t b = 0; b < paras[i].blocks.size(); b++) {
                    double pct = total_chars > 0 ? (double)paras[i].blocks[b].text.length() / total_chars : 0.0;
                    paras[i].blocks[b].bbox_x_mm = current_x;
                    paras[i].blocks[b].bbox_y_mm = paras[i].bbox_y_mm;
                    paras[i].blocks[b].bbox_w_mm = paras[i].bbox_w_mm * pct;
                    paras[i].blocks[b].bbox_h_mm = paras[i].bbox_h_mm;
                    current_x += paras[i].blocks[b].bbox_w_mm;

                    Style st = resolve_style(paras[i].style_name.c_str());
                    if (paras[i].blocks[b].props.font_name.empty()) paras[i].blocks[b].props.font_name = st.text.font_name;
                    if (paras[i].blocks[b].props.font_size_pt == 0) paras[i].blocks[b].props.font_size_pt = st.text.font_size_pt;
                    if (!paras[i].blocks[b].props.bold) paras[i].blocks[b].props.bold = st.text.bold;
                    if (!paras[i].blocks[b].props.italic) paras[i].blocks[b].props.italic = st.text.italic;
                    if (!paras[i].blocks[b].props.underline) paras[i].blocks[b].props.underline = st.text.underline;
                    if (!paras[i].blocks[b].props.strikethrough) paras[i].blocks[b].props.strikethrough = st.text.strikethrough;
                    if (paras[i].blocks[b].props.color.empty()) paras[i].blocks[b].props.color = st.text.color;
                }
            }
        } else {
            break;
        }
    }

    doc->registerCallback(nullptr, nullptr);
}


/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

extern "C" int
cdsl_doc_init(void)
{
	if (g_office) {
		return 1; /* fast path: already initialized */
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_office) {
		return 1; /* double-check: another thread raced ahead */
	}
	g_office = lok::lok_cpp_init(CDSL_LO_PATH);
	return (g_office != nullptr) ? 1 : 0;
}

extern "C" void
cdsl_doc_shutdown(void)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_office) {
		delete g_office;
		g_office = nullptr;
	}
}

extern "C" void
cdsl_doc_free_string(char* str)
{
	free(str);
}

extern "C" char*
cdsl_doc_extract_text(const char* path)
{
	if (!path) {
		return nullptr;
	}

	/* SaveAs TXT into a temp file (outside lock) */
	char tmp_path[128];
	if (temp_path(tmp_path, sizeof(tmp_path), ".txt") != 0) {
		return nullptr;
	}
	char url[4096], doc_url[4096];
	snprintf(url,     sizeof(url),     "file://%s", tmp_path);
	snprintf(doc_url, sizeof(doc_url), "file://%s", path);

	/* LOK critical section: document load + export */
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_office) {
			remove(tmp_path);
			return nullptr;
		}
		std::unique_ptr<lok::Document> doc(g_office->documentLoad(doc_url));
		if (!doc) {
			remove(tmp_path);
			return nullptr;
		}
		if (!doc->saveAs(url, "txt")) {
			remove(tmp_path);
			return nullptr;
		}
		doc.reset(); /* close document before reading file */
	}

	/* Read the saved text file */
	FILE* f = fopen(tmp_path, "rb");
	if (!f) {
		remove(tmp_path);
		return nullptr;
	}
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	char* out = (char*)malloc((size_t)fsize + 1);
	if (!out) {
		fclose(f);
		remove(tmp_path);
		return nullptr;
	}
	size_t nread = fread(out, 1, (size_t)fsize, f);
	out[nread] = '\0';
	fclose(f);
	remove(tmp_path);

	return out;
}

extern "C" char*
cdsl_doc_extract_to_json(const char* path)
{
	if (!path) {
		return nullptr;
	}

	char fodt_path[128];
	if (temp_path(fodt_path, sizeof(fodt_path), ".fodt") != 0) {
		return nullptr;
	}

	char doc_url[4096], fodt_url[4096];
	snprintf(doc_url,  sizeof(doc_url),  "file://%s", path);
	snprintf(fodt_url, sizeof(fodt_url), "file://%s", fodt_path);

	PageInfo   page;
	std::vector<Paragraph> paragraphs(256);
	int        num_paras = 0;
	std::vector<TableInfo> tables(256);
	int        num_tables = 0;
	std::vector<ListInfo> lists(256);
	int        num_lists = 0;
	std::vector<ImageInfo> images(256);
	int        num_images = 0;
	std::vector<FootnoteInfo> footnotes(256);
	int        num_footnotes = 0;
	std::string full_text;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_office) {
			remove(fodt_path);
			return nullptr;
		}
		std::unique_ptr<lok::Document> doc(g_office->documentLoad(doc_url));
		if (!doc) {
			remove(fodt_path);
			return nullptr;
		}
		doc->initializeForRendering(nullptr);

		if (!doc->saveAs(fodt_url, "FODT")) {
			remove(fodt_path);
			return nullptr;
		}

		/* Read FODT into memory */
		FILE* ff = fopen(fodt_path, "rb");
		if (!ff) {
			remove(fodt_path);
			return nullptr;
		}
		fseek(ff, 0, SEEK_END);
		long fodt_size = ftell(ff);
		fseek(ff, 0, SEEK_SET);
		std::vector<char> fodt_buf(fodt_size + 1);
		size_t nf = fread(fodt_buf.data(), 1, (size_t)fodt_size, ff);
		fodt_buf[nf] = '\0';
		fclose(ff);
		remove(fodt_path);

		if (!parse_fodt_document(fodt_buf.data(), (size_t)fodt_size, page, paragraphs.data(), num_paras, full_text, tables.data(), &num_tables, lists.data(), &num_lists, images.data(), &num_images, footnotes.data(), &num_footnotes)) {
			return nullptr;
		}

		if (num_paras > 0 && doc) {
			get_paragraph_positions(doc.get(), paragraphs.data(), num_paras, page);
		}
	}

	std::string out;
	out.reserve(32768 + full_text.size() * 2);

	out += "{\n  \"document\": {\n    \"pages\": [\n";

	int current_render_page = 1;
	bool has_started_page = false;
	bool has_started_para = false;

	for (int pi = 0; pi < num_paras; pi++) {
		const Paragraph& para = paragraphs[pi];
		int para_page = para.page_index > 0 ? para.page_index : 1;

		if (!has_started_page || para_page != current_render_page) {
			if (has_started_page) {
				out += "\n        ]\n      }";
			}
			if (pi > 0) {
				out += ",\n";
			}
			current_render_page = para_page;
			char b[512];
			snprintf(b, sizeof(b),
			    "      {\n"
			    "        \"page_number\": %d,\n"
			    "        \"width_mm\": %.0f,\n"
			    "        \"height_mm\": %.0f,\n"
			    "        \"margin_top_mm\": %.0f,\n"
			    "        \"margin_bottom_mm\": %.0f,\n"
			    "        \"margin_left_mm\": %.0f,\n"
			    "        \"margin_right_mm\": %.0f,\n"
			    "        \"paragraphs\": [\n",
			    current_render_page,
			    page.page_width_mm, page.page_height_mm,
			    page.margin_top_mm, page.margin_bottom_mm,
			    page.margin_left_mm, page.margin_right_mm);
			out += b;
			if (!page.header_text.empty()) {
				out += "        ,\"header\": \"";
				json_escape(out, page.header_text.c_str());
				out += "\"\n";
			}
			if (!page.footer_text.empty()) {
				out += "        ,\"footer\": \"";
				json_escape(out, page.footer_text.c_str());
				out += "\"\n";
			}
			has_started_page = true;
			has_started_para = false;
		}

		if (has_started_para) {
			out += ",\n";
		}
		json_append_paragraph(out, para.style_name.empty() ? nullptr : para.style_name.c_str(),
		                       para.props, para.blocks.data(), (int)para.blocks.size(), &para);
		has_started_para = true;
	}

	if (has_started_page) {
		out += "\n        ]\n      }\n";
	} else {
		char b[512];
		snprintf(b, sizeof(b),
		    "      {\n"
		    "        \"page_number\": 1,\n"
		    "        \"width_mm\": %.0f,\n"
		    "        \"height_mm\": %.0f,\n"
		    "        \"margin_top_mm\": %.0f,\n"
		    "        \"margin_bottom_mm\": %.0f,\n"
		    "        \"margin_left_mm\": %.0f,\n"
		    "        \"margin_right_mm\": %.0f,\n"
		    "        \"paragraphs\": []\n"
		    "      }\n",
		    page.page_width_mm, page.page_height_mm,
		    page.margin_top_mm, page.margin_bottom_mm,
		    page.margin_left_mm, page.margin_right_mm);
		out += b;
	}

	out += "    ],\n    \"tables\": [\n";
	for (int ti = 0; ti < num_tables; ti++) {
		if (ti > 0) out += ",\n";
		json_append_table(out, tables[ti]);
	}
	out += "\n    ],\n    \"lists\": [\n";
	for (int li = 0; li < num_lists; li++) {
		if (li > 0) out += ",\n";
		json_append_list(out, lists[li]);
	}
	out += "\n    ],\n    \"images\": [\n";
	for (int ii = 0; ii < num_images; ii++) {
		if (ii > 0) out += ",\n";
		json_append_image(out, images[ii]);
	}
	out += "\n    ],\n    \"footnotes\": [\n";
	for (int fi = 0; fi < num_footnotes; fi++) {
		if (fi > 0) out += ",\n";
		json_append_footnote(out, footnotes[fi]);
	}
	out += "\n    ],\n    \"bookmarks\": [";
	for (size_t bi = 0; bi < page.bookmarks.size(); bi++) {
		if (bi > 0) out += ", ";
		out += "\"" + page.bookmarks[bi] + "\"";
	}
	out += "],\n    \"metadata\": {\n";
	char b[256];
	snprintf(b, sizeof(b), "      \"page_count\": %d,\n      \"paragraph_count\": %d,\n      \"word_count\": %d,\n      \"character_count\": %d",
	         page.page_count, page.paragraph_count, page.word_count, page.character_count);
	out += b;

	if (!page.title.empty()) {
		out += ",\n      \"title\": \""; json_escape(out, page.title.c_str()); out += "\"";
	}
	if (!page.creator.empty()) {
		out += ",\n      \"creator\": \""; json_escape(out, page.creator.c_str()); out += "\"";
	}
	if (!page.created.empty()) {
		out += ",\n      \"created\": \""; json_escape(out, page.created.c_str()); out += "\"";
	}
	if (!page.modified.empty()) {
		out += ",\n      \"modified\": \""; json_escape(out, page.modified.c_str()); out += "\"";
	}

	out += "\n    },\n    \"full_text\": \"";
	for (int pi = 0; pi < num_paras; pi++) {
		const Paragraph& para = paragraphs[pi];
		for (const auto& block : para.blocks) {
			json_escape(out, block.text.c_str());
		}
		if (pi < num_paras - 1) out += "\\n";
	}
	out += "\"\n  }\n}\n";

	return strdup(out.c_str());
}
