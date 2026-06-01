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
};

struct Paragraph {
    std::string   style_name;
    ParaProps     props;
    TextBlock     blocks[256];
    int           num_blocks = 0;
};

/* Bounded style database */
#define MAX_STYLES 256
static Style g_style_db[MAX_STYLES];
static int   g_style_count = 0;

static void init_style_db() { g_style_count = 0; }
static Style* find_style(const char* name) {
    for (int i = 0; i < g_style_count; i++)
        if (g_style_db[i].name == name) return &g_style_db[i];
    return nullptr;
}
static Style* add_style(const char* name) {
    if (g_style_count >= MAX_STYLES) return nullptr;
    Style* s = &g_style_db[g_style_count++];
    s->name = name;
    return s;
}

/* Merge parent style properties into child (child values win) */
static void merge_style(const Style& parent, Style& child) {
    if (child.para.alignment.empty() && !parent.para.alignment.empty())
        child.para.alignment = parent.para.alignment;
    if (child.para.margin_top_mm == 0)    child.para.margin_top_mm = parent.para.margin_top_mm;
    if (child.para.margin_bottom_mm == 0) child.para.margin_bottom_mm = parent.para.margin_bottom_mm;
    if (child.para.margin_left_mm == 0)   child.para.margin_left_mm = parent.para.margin_left_mm;
    if (child.para.margin_right_mm == 0)  child.para.margin_right_mm = parent.para.margin_right_mm;
    if (child.para.indent_first_line_mm == 0) child.para.indent_first_line_mm = parent.para.indent_first_line_mm;
    if (child.para.line_spacing == 0)     child.para.line_spacing = parent.para.line_spacing;
    if (child.text.font_name.empty())     child.text.font_name = parent.text.font_name;
    if (child.text.font_size_pt == 0)     child.text.font_size_pt = parent.text.font_size_pt;
    if (!child.text.bold)                 child.text.bold = parent.text.bold;
    if (!child.text.italic)               child.text.italic = parent.text.italic;
    if (!child.text.underline)            child.text.underline = parent.text.underline;
    if (!child.text.strikethrough)        child.text.strikethrough = parent.text.strikethrough;
    if (child.text.color.empty())         child.text.color = parent.text.color;
}

/* Resolve a style name (with inheritance chain) */
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
                                      const char* text, const TextProps& props)
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
    if (pos + 1 < sz) buf[pos++] = '}';
    return pos;
}

static size_t json_append_paragraph(char* buf, size_t sz, size_t pos,
                                     const char* style_name, const ParaProps& props,
                                     const TextBlock* blocks, int num_blocks)
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

    pos += snprintf(buf + pos, sz - pos, "          ,\"text_blocks\": [\n");
    for (int i = 0; i < num_blocks; i++) {
        if (i > 0) { if (pos < sz - 1) buf[pos++] = ','; buf[pos++] = '\n'; }
        pos = json_append_text_block(buf, sz, pos,
                                      blocks[i].text.c_str(), blocks[i].props);
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
	/*  LOK critical section: load + FODT export                           */
	/* ------------------------------------------------------------------ */
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
		doc.reset();
	}

	/* ------------------------------------------------------------------ */
	/*  Read FODT into memory                                              */
	/* ------------------------------------------------------------------ */
	FILE* ff = fopen(fodt_path, "rb");
	if (!ff) {
		remove(fodt_path);
		return nullptr;
	}
	fseek(ff, 0, SEEK_END);
	long fodt_size = ftell(ff);
	fseek(ff, 0, SEEK_SET);

	char* fodt = (char*)malloc((size_t)fodt_size + 1);
	if (!fodt) {
		fclose(ff);
		remove(fodt_path);
		return nullptr;
	}
	size_t nf = fread(fodt, 1, (size_t)fodt_size, ff);
	fodt[nf] = '\0';
	fclose(ff);
	remove(fodt_path);

	/* ------------------------------------------------------------------ */
	/*  Parse FODT with XmlParser into hierarchical structure              */
	/* ------------------------------------------------------------------ */
	PageInfo   page;
	Paragraph  paragraphs[256];
	int        num_paras = 0;
	std::string full_text;

	if (!parse_fodt_document(fodt, (size_t)fodt_size, page, paragraphs, num_paras, full_text)) {
		free(fodt);
		return nullptr;
	}
	free(fodt);

	fprintf(stderr, "DEBUG: parsed %d paragraphs, page_width=%.0f, text='%.60s'\n",
		num_paras, page.page_width_mm,
		num_paras > 0 && paragraphs[0].num_blocks > 0 ? paragraphs[0].blocks[0].text.c_str() : "(empty)");

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
	    "    \"pages\": [\n"
	    "      {\n"
	    "        \"page_number\": 1,\n"
	    "        \"width_mm\": %.0f,\n"
	    "        \"height_mm\": %.0f,\n"
	    "        \"margin_top_mm\": %.0f,\n"
	    "        \"margin_bottom_mm\": %.0f,\n"
	    "        \"margin_left_mm\": %.0f,\n"
	    "        \"margin_right_mm\": %.0f,\n"
	    "        \"paragraphs\": [\n",
	    page.page_width_mm, page.page_height_mm,
	    page.margin_top_mm, page.margin_bottom_mm,
	    page.margin_left_mm, page.margin_right_mm);

	// Each paragraph
	for (int pi = 0; pi < num_paras; pi++) {
		if (pi > 0) {
                        if (pos < buf_sz - 2) { json_out[pos++] = ','; json_out[pos++] = '\n'; }
		}
		const Paragraph& para = paragraphs[pi];
		pos = json_append_paragraph(json_out, buf_sz, pos,
		                             para.style_name.empty() ? nullptr : para.style_name.c_str(),
		                             para.props,
		                             para.blocks, para.num_blocks);
	}

	// Close pages array and page object
	pos += snprintf(json_out + pos, buf_sz - pos,
	    "\n        ]\n"
	    "      }\n"
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
