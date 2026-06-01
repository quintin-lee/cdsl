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
};

struct ParaProps {
    std::string alignment;
    double    margin_top_mm = 0;
    double    margin_bottom_mm = 0;
    double    margin_left_mm = 0;
    double    margin_right_mm = 0;
    double    indent_first_line_mm = 0;
    double    line_spacing = 0; /* 0 = not set */
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
};

struct TextBlock {
    std::string text;
    TextProps   props;
    double bbox_x_mm = 0;
    double bbox_y_mm = 0;
    double bbox_w_mm = 0;
    double bbox_h_mm = 0;
};

struct Paragraph {
    std::string   style_name;
    ParaProps     props;
    TextBlock     blocks[256];
    int           num_blocks = 0;
    double bbox_x_mm = 0;
    double bbox_y_mm = 0;
    double bbox_w_mm = 0;
    double bbox_h_mm = 0;
    int    page_num = 0;
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
        }
        int depth = 1;
        while (depth > 0 && xp.next() != XmlParser::END) {
            if (xp.type() == XmlParser::START_ELEMENT) depth++;
            else if (xp.type() == XmlParser::END_ELEMENT) depth--;
        }
    }
}

/* Forward decl for existing JSON escape helper */
static size_t json_escape_append(char* buf, size_t sz, size_t pos, const char* str);

/* ------------------------------------------------------------------ */
/*  JSON output builders                                              */
/* ------------------------------------------------------------------ */

static size_t json_append_text_block(char* buf, size_t sz, size_t pos,
                                      const char* text, const TextProps& props,
                                      const TextBlock* block = nullptr)
{
    pos += snprintf(buf + pos, sz - pos, "{\n");
    if (text) {
        pos += snprintf(buf + pos, sz - pos, "      \"text\": \"");
        pos = json_escape_append(buf, sz, pos, text);
        if (pos < sz - 2) { buf[pos++] = '"'; buf[pos++] = '\n'; }
    }
    if (!props.font_name.empty()) {
        pos += snprintf(buf + pos, sz - pos, "      ,\"font_name\": \"");
        pos = json_escape_append(buf, sz, pos, props.font_name.c_str());
        if (pos < sz - 2) { buf[pos++] = '"'; buf[pos++] = '\n'; }
    }
    if (props.font_size_pt > 0) {
        pos += snprintf(buf + pos, sz - pos, "      ,\"font_size_pt\": %.1f\n", props.font_size_pt);
    }
    if (props.bold)
        pos += snprintf(buf + pos, sz - pos, "      ,\"bold\": true\n");
    if (props.italic)
        pos += snprintf(buf + pos, sz - pos, "      ,\"italic\": true\n");
    if (props.underline)
        pos += snprintf(buf + pos, sz - pos, "      ,\"underline\": true\n");
    if (props.strikethrough)
        pos += snprintf(buf + pos, sz - pos, "      ,\"strikethrough\": true\n");
    if (!props.color.empty()) {
        pos += snprintf(buf + pos, sz - pos, "      ,\"color\": \"%s\"\n", props.color.c_str());
    }
    if (block) {
        pos += snprintf(buf + pos, sz - pos,
            "      ,\"bbox_mm\": [%.1f, %.1f, %.1f, %.1f]\n",
            block->bbox_x_mm, block->bbox_y_mm,
            block->bbox_w_mm, block->bbox_h_mm);
    }
    if (pos + 1 < sz) buf[pos++] = '}';
    return pos;
}

static size_t json_append_paragraph(char* buf, size_t sz, size_t pos,
                                      const char* style_name, const ParaProps& props,
                                      const TextBlock* blocks, int num_blocks,
                                      const Paragraph* para)
{
    pos += snprintf(buf + pos, sz - pos, "        {\n");
    if (style_name) {
        pos += snprintf(buf + pos, sz - pos, "          \"style\": \"%s\"", style_name);
        if (!props.alignment.empty() || props.margin_top_mm > 0 || props.margin_bottom_mm > 0 ||
            props.margin_left_mm > 0 || props.indent_first_line_mm > 0 || props.line_spacing > 0)
            buf[pos++] = ',';
        buf[pos++] = '\n';
    }
    if (!props.alignment.empty())
        pos += snprintf(buf + pos, sz - pos, "          ,\"alignment\": \"%s\"\n", props.alignment.c_str());
    if (props.margin_top_mm > 0)
        pos += snprintf(buf + pos, sz - pos, "          ,\"spacing_before_mm\": %.1f\n", props.margin_top_mm);
    if (props.margin_bottom_mm > 0)
        pos += snprintf(buf + pos, sz - pos, "          ,\"spacing_after_mm\": %.1f\n", props.margin_bottom_mm);
    if (props.line_spacing > 0)
        pos += snprintf(buf + pos, sz - pos, "          ,\"line_spacing\": %.2f\n", props.line_spacing);
    if (props.indent_first_line_mm > 0)
        pos += snprintf(buf + pos, sz - pos, "          ,\"indent_first_line_mm\": %.1f\n", props.indent_first_line_mm);

    if (para) {
        pos += snprintf(buf + pos, sz - pos,
            "          ,\"bbox_mm\": [%.1f, %.1f, %.1f, %.1f]\n",
            para->bbox_x_mm, para->bbox_y_mm,
            para->bbox_w_mm, para->bbox_h_mm);
    }

    pos += snprintf(buf + pos, sz - pos, "          ,\"text_blocks\": [\n");
    for (int i = 0; i < num_blocks; i++) {
        if (i > 0) { if (pos < sz - 1) buf[pos++] = ','; buf[pos++] = '\n'; }
        pos = json_append_text_block(buf, sz, pos,
                                      blocks[i].text.c_str(), blocks[i].props,
                                      &blocks[i]);
    }
    pos += snprintf(buf + pos, sz - pos, "\n          ]\n");
    if (pos + 1 < sz) buf[pos++] = '}';
    return pos;
}

/* ------------------------------------------------------------------ */
/*  FODT body parser: walk <office:text> → <text:p> → <text:span>     */
/* ------------------------------------------------------------------ */

static bool parse_fodt_body(XmlParser& xp, PageInfo& page,
                             Paragraph* out_paras, int& out_count)
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
            while (para_depth > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::END_ELEMENT) { para_depth--; continue; }
                if (xp.type() == XmlParser::START_ELEMENT) {
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
                            if (para.num_blocks < 256) {
                                TextBlock& tb = para.blocks[para.num_blocks++];
                                if (dec) tb.text = dec;
                                else tb.text = span_text;
                                tb.props = span_props;
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
                    if (para.num_blocks < 256) {
                        TextBlock& tb = para.blocks[para.num_blocks++];
                        if (dec) tb.text = dec;
                        free(dec);
                    }
                }
            }
            out_count++;
        } else if (xp.match("text:section")) {
            // section can contain more paragraphs - recurse
            if (!parse_fodt_body(xp, page, out_paras, out_count))
                return false;
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
                                 std::string& full_text)
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
            // skip
            int d = 1;
            while (d > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) d++;
                else if (xp.type() == XmlParser::END_ELEMENT) d--;
            }
        } else if (xp.match("office:body")) {
            int d = 1;
            while (d > 0 && xp.next() != XmlParser::END) {
                if (xp.type() == XmlParser::START_ELEMENT) {
                    if (xp.match("office:text")) {
                        if (!parse_fodt_body(xp, page, out_paras, out_count))
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

static void get_paragraph_positions(lok::Document* doc, Paragraph* paras,
                                    int count, const PageInfo& page)
{
    if (count <= 0 || !doc) return;

    CursorCapture cap;
    doc->registerCallback(cursor_callback, &cap);
    doc->initializeForRendering(nullptr);

    const double content_w_mm = page.page_width_mm
        - page.margin_left_mm - page.margin_right_mm;
    if (content_w_mm <= 0) {
        doc->registerCallback(nullptr, nullptr);
        return;
    }

    const double twip_to_mm = 25.4 / 1440.0;

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            cap.updated = false;
            doc->postUnoCommand(".uno:GoDown", nullptr, false);
        }

        if (pump_until_updated(doc, cap)) {
            paras[i].bbox_x_mm = cap.x_twip * twip_to_mm;
            paras[i].bbox_y_mm = cap.y_twip * twip_to_mm;
            double line_h_mm = cap.h_twip * twip_to_mm;

            double page_h_mm = page.page_height_mm > 0 ? page.page_height_mm : 297.0;
            int current_page_num = 1 + (int)(paras[i].bbox_y_mm / page_h_mm);
            paras[i].page_num = current_page_num;

            double indent = paras[i].bbox_x_mm - page.margin_left_mm;
            if (indent < 0) indent = 0;
            paras[i].bbox_w_mm = content_w_mm - indent;

            int total_chars = 0;
            for (int b = 0; b < paras[i].num_blocks; b++)
                total_chars += (int)paras[i].blocks[b].text.length();

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

            if (paras[i].num_blocks == 1) {
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
            } else if (paras[i].num_blocks > 1) {
                double current_x = paras[i].bbox_x_mm;
                for (int b = 0; b < paras[i].num_blocks; b++) {
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
        }
    }

    doc->registerCallback(nullptr, nullptr);
}

/* ------------------------------------------------------------------ */
/*  JSON helpers                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Append a JSON-escaped string to a buffer.
 *
 * Escapes ", \\, \n, \r, \t and strips other control characters.
 */
static size_t
json_escape_append(char* buf, size_t sz, size_t pos, const char* str)
{
	if (!str) {
		return pos;
	}
	for (size_t i = 0; str[i] && pos < sz - 2; i++) {
		unsigned char c = (unsigned char)str[i];
		switch (c) {
		case '"':
			if (pos + 2 < sz) {
				buf[pos++] = '\\';
				buf[pos++] = '"';
			}
			break;
		case '\\':
			if (pos + 2 < sz) {
				buf[pos++] = '\\';
				buf[pos++] = '\\';
			}
			break;
		case '\n':
			if (pos + 2 < sz) {
				buf[pos++] = '\\';
				buf[pos++] = 'n';
			}
			break;
		case '\r':
			if (pos + 2 < sz) {
				buf[pos++] = '\\';
				buf[pos++] = 'r';
			}
			break;
		case '\t':
			if (pos + 2 < sz) {
				buf[pos++] = '\\';
				buf[pos++] = 't';
			}
			break;
		default:
			if (c >= 0x20 && pos < sz - 1) {
				buf[pos++] = c;
			}
			break;
		}
	}
	return pos;
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
	delete g_office;
	g_office = nullptr;
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

	/* ------------------------------------------------------------------ */
	/*  Create temp file paths (outside lock)                              */
	/* ------------------------------------------------------------------ */
	char fodt_path[128];
	if (temp_path(fodt_path, sizeof(fodt_path), ".fodt") != 0) {
		return nullptr;
	}

	char doc_url[4096], fodt_url[4096];
	snprintf(doc_url,  sizeof(doc_url),  "file://%s", path);
	snprintf(fodt_url, sizeof(fodt_url), "file://%s", fodt_path);

	/* ------------------------------------------------------------------ */
	/*  LOK critical section: load → FODT export → parse → positions      */
	/* ------------------------------------------------------------------ */
	PageInfo   page;
	std::vector<Paragraph> paragraphs(256);
	int        num_paras = 0;
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
		if (!doc->saveAs(fodt_url, "FODT")) {
			remove(fodt_path);
			return nullptr;
		}

		/* Read FODT into memory (doc stays open for position extraction) */
		FILE* ff = fopen(fodt_path, "rb");
		if (!ff) {
			remove(fodt_path);
			return nullptr;
		}
		fseek(ff, 0, SEEK_END);
		long fodt_size = ftell(ff);
		fseek(ff, 0, SEEK_SET);
		char* fodt = (char*)malloc((size_t)fodt_size + 1);
		if (!fodt) { fclose(ff); remove(fodt_path); return nullptr; }
		size_t nf = fread(fodt, 1, (size_t)fodt_size, ff);
		fodt[nf] = '\0';
		fclose(ff);
		remove(fodt_path);

		if (!parse_fodt_document(fodt, (size_t)fodt_size, page, paragraphs.data(), num_paras, full_text)) {
			free(fodt);
			return nullptr;
		}
		free(fodt);

		/* Extract rendered paragraph positions from the open LOK document */
		if (num_paras > 0 && doc)
			get_paragraph_positions(doc.get(), paragraphs.data(), num_paras, page);
	}

	/* ------------------------------------------------------------------ */
	/*  Build output JSON                                                  */
	/* ------------------------------------------------------------------ */
	size_t buf_sz = 32768 + full_text.size() * 2;
	char* json_out = (char*)malloc(buf_sz);
	if (!json_out) return nullptr;

	size_t pos = 0;

	// Document root
	pos += snprintf(json_out + pos, buf_sz - pos,
	    "{\n"
	    "  \"document\": {\n"
	    "    \"pages\": [\n");

	// Dynamically group paragraphs by their physical page_num
	// If a paragraph doesn't have a computed page_num (0), fallback to page_num=1
	int current_render_page = 1;
	bool has_started_page = false;
	bool has_started_para = false;

	for (int pi = 0; pi < num_paras; pi++) {
		const Paragraph& para = paragraphs[pi];
		int para_page = para.page_num > 0 ? para.page_num : 1;

		if (!has_started_page || para_page != current_render_page) {
			if (has_started_page) {
				// Close previous page
				pos += snprintf(json_out + pos, buf_sz - pos, "\n        ]\n      }");
			}
			if (pi > 0) {
				pos += snprintf(json_out + pos, buf_sz - pos, ",\n");
			}
			current_render_page = para_page;
			pos += snprintf(json_out + pos, buf_sz - pos,
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
			has_started_page = true;
			has_started_para = false;
		}

		if (has_started_para) {
			if (pos < buf_sz - 2) { json_out[pos++] = ','; json_out[pos++] = '\n'; }
		}
		pos = json_append_paragraph(json_out, buf_sz, pos,
		                             para.style_name.empty() ? nullptr : para.style_name.c_str(),
		                             para.props,
		                             para.blocks, para.num_blocks,
		                             &para);
		has_started_para = true;
	}

	if (has_started_page) {
		pos += snprintf(json_out + pos, buf_sz - pos, "\n        ]\n      }\n");
	} else {
		// fallback: at least one empty page element if no paragraphs parsed
		pos += snprintf(json_out + pos, buf_sz - pos,
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
	}

	// Close pages array and page object
	pos += snprintf(json_out + pos, buf_sz - pos,
	    "    ],\n"
	    "    \"metadata\": {\n"
	    "      \"page_count\": %d,\n"
	    "      \"paragraph_count\": %d,\n"
	    "      \"word_count\": %d,\n"
	    "      \"character_count\": %d\n"
	    "    },\n"
	    "    \"full_text\": \"",
	    page.page_count, page.paragraph_count,
	    page.word_count, page.character_count);

	// Build full_text from paragraphs
	for (int pi = 0; pi < num_paras; pi++) {
		const Paragraph& para = paragraphs[pi];
		for (int bi = 0; bi < para.num_blocks; bi++) {
			pos = json_escape_append(json_out, buf_sz, pos, para.blocks[bi].text.c_str());
		}
		if (pi < num_paras - 1) {
			if (pos < buf_sz - 2) { json_out[pos++] = '\\'; json_out[pos++] = 'n'; }
		}
	}

	// Close JSON
	if (pos + 6 < buf_sz) {
		pos += snprintf(json_out + pos, buf_sz - pos, "\"\n  }\n}\n");
	}

	return json_out;
}
