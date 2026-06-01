#include "xml_parser.h"
#include <cstring>
#include <cstdlib>

XmlParser::XmlParser(const char* data, size_t len)
    : pos_(data), end_(data + len), cur_type_(END),
      cur_tag_(nullptr), cur_tag_len_(0),
      cur_text_(nullptr), cur_text_len_(0),
      pending_tag_(nullptr), pending_tag_len_(0) {}

void XmlParser::skipWhitespace() {
    while (pos_ < end_) {
        unsigned char c = *pos_;
        if (c > 0x20 || (c != ' ' && c != '\t' && c != '\n' && c != '\r'))
            break;
        pos_++;
    }
}

void XmlParser::advanceTag() {
    for (;;) {
        const char* start = pos_ + 1;

        if (*start == '?' || *start == '!') {
            if (start[0] == '!' && end_ - start >= 3 && start[1] == '-' && start[2] == '-') {
                const char* p = start + 3;
                while (p + 2 < end_ && !(p[0] == '-' && p[1] == '-' && p[2] == '>'))
                    p++;
                if (p + 2 < end_) p += 3;
                pos_ = p;
            } else {
                const char* p = start;
                while (p < end_ && *p != '>') p++;
                if (p < end_) p++;
                pos_ = p;
            }
            skipWhitespace();
            if (pos_ >= end_) { cur_type_ = END; return; }
            if (*pos_ == '<') continue; // keep scanning — skip PI/comment
            cur_text_ = pos_;
            while (pos_ < end_ && *pos_ != '<') pos_++;
            cur_text_len_ = (size_t)(pos_ - cur_text_);
            if (cur_text_len_ == 0) {
                if (pos_ >= end_) { cur_type_ = END; return; }
                continue;
            }
            cur_type_ = TEXT;
            return;
        }

        bool is_end = (*start == '/');
        if (is_end) start++;

        const char* name_start = start;
        const char* name_end = name_start;
        while (name_end < end_ && *name_end != '>' && *name_end != '/'
               && *name_end != ' ' && *name_end != '\t'
               && *name_end != '\n' && *name_end != '\r')
            name_end++;

        cur_tag_ = name_start;
        cur_tag_len_ = (size_t)(name_end - name_start);

        const char* scan = name_end;
        while (scan < end_ && *scan != '>') {
            if (is_end) break;
            scan++;
        }
        bool self_closing = false;
        const char* end_tag = scan;
        if (end_tag > start && *(end_tag - 1) == '/')
            self_closing = true;

        pos_ = end_tag + 1;

        cur_type_ = is_end ? END_ELEMENT : START_ELEMENT;
        if (self_closing) {
            pending_tag_ = cur_tag_;
            pending_tag_len_ = cur_tag_len_;
        } else {
            pending_tag_ = nullptr;
        }
        return;
    }
}

void XmlParser::advanceText() {
    cur_text_ = pos_;
    while (pos_ < end_ && *pos_ != '<')
        pos_++;
    cur_text_len_ = (size_t)(pos_ - cur_text_);
    if (cur_text_len_ == 0) {
        cur_type_ = next(); // skip empty text, recurse
        return;
    }
    cur_type_ = TEXT;
}

XmlParser::Type XmlParser::next() {
    if (pending_tag_) {
        cur_tag_ = pending_tag_;
        cur_tag_len_ = pending_tag_len_;
        pending_tag_ = nullptr;
        cur_type_ = END_ELEMENT;
        return END_ELEMENT;
    }
    skipWhitespace();
    if (pos_ >= end_) {
        cur_type_ = END;
        return END;
    }
    if (*pos_ == '<') {
        advanceTag();
        return cur_type_;
    }
    advanceText();
    return cur_type_;
}

bool XmlParser::match(const char* qname) const {
    size_t n = strlen(qname);
    return n == cur_tag_len_ && memcmp(cur_tag_, qname, n) == 0;
}

// Find attribute value by local name (e.g. "text:style-name" -> find `text:style-name="..."`)
const char* XmlParser::attr(const char* local) const {
    // Search from cur_tag_ + cur_tag_len_ to the next '>'
    const char* search = cur_tag_ + cur_tag_len_;
    size_t local_len = strlen(local);
    while (search < end_ && *search != '>' && *search != '/') {
        // Skip whitespace
        while (search < end_ && (*search == ' ' || *search == '\t' || *search == '\n' || *search == '\r'))
            search++;
        if (search >= end_ || *search == '>' || *search == '/')
            break;

        // Check if this attribute name matches
        if ((size_t)(end_ - search) > local_len + 2 &&
            memcmp(search, local, local_len) == 0 &&
            search[local_len] == '=') {
            // Found: skip ="
            const char* val = search + local_len + 1;
            if (*val == '"' || *val == '\'') {
                char quote = *val;
                val++;
                const char* val_end = val;
                while (val_end < end_ && *val_end != quote)
                    val_end++;
                // We can't return a dynamically allocated string from attr(),
                // so we use thread-local or static storage.
                // For FODT parsing this is fine since we consume one attr at a time.
                static char buf[4096];
                size_t vlen = (size_t)(val_end - val);
                if (vlen >= sizeof(buf)) vlen = sizeof(buf) - 1;
                memcpy(buf, val, vlen);
                buf[vlen] = '\0';
                // Decode common XML entities in the copy
                // We'll do entity decoding in the property parsing functions
                return buf;
            }
            return nullptr;
        }
        // Skip to next attribute
        search++;
        while (search < end_ && *search != '=' && *search != ' ' && *search != '\t' && *search != '\n' && *search != '\r' && *search != '>' && *search != '/')
            search++;
        if (search < end_ && *search == '=') {
            search++; // skip =
            if (search < end_ && (*search == '"' || *search == '\'')) {
                char quote = *search;
                search++;
                while (search < end_ && *search != quote)
                    search++;
                if (search < end_)
                    search++; // skip closing quote
            }
        }
    }
    return nullptr;
}
